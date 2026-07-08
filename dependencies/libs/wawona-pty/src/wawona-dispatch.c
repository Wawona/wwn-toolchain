/*
 * wawona-dispatch.c — in-process external-command dispatch for the App Store
 * compliant build (no fork/exec/posix_spawn).
 *
 * The zsh exec hook (patch-zsh-exec.py) calls wawona_dispatch_inprocess()
 * instead of forking an external binary. We resolve argv[0]'s basename against
 * an in-process *safe subset* and, if allowed, forward to the statically linked
 * uutils umbrella entry point wawona_coreutils_main(). That Rust entry wraps the
 * utility in catch_unwind so a panic cannot abort the host app.
 *
 * Exit-safety notes:
 *   - wawona_coreutils_main is declared weak: on platforms built without the
 *     `coreutils` Cargo feature (e.g. watchOS) the symbol is absent and we
 *     simply report WWN_DISPATCH_NOT_HANDLED, so libwwn-pty.a stays
 *     self-contained even though it is -force_load'd.
 *   - fastfetch_main is weak: absent when libfastfetch.a is not force-loaded.
     *     Keep in-process client names in sync with Wawona bundling (wwn-fastfetch).
     *   - wawona_nvim_main is weak: absent when libwawona-neovim.a is not force-loaded.
     *     Keep in-process editor names in sync with Wawona bundling (wwn-neovim).
 *   - waypipe_main is weak: absent when libwawona.a is built without waypipe-ssh.
 *     Uses in-process libssh2 for Wayland forwarding over SSH.
 *   - ssh_main / ssh_keygen_main / scp_main are weak: absent when
 *     libssh-inprocess.a (openssh built for iOS) is not force-loaded.
 *     ssh_main provides a full OpenSSH client (set SSH_ASKPASS_PASSWORD for
 *     password auth); ssh_keygen_main generates keys; scp_main copies files.
 *   - Utilities that call process::exit()/abort() internally would still take
 *     the app down; such utils are kept OUT of both this table and the Cargo
 *     feature subset. Keep the two lists in sync.
 */
#include "wwn_pty.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/*
 * Provided by the Rust uutils umbrella (see patch-coreutils-source.sh):
 *   #[no_mangle] pub extern "C" fn wawona_coreutils_main(c_int, *const *const c_char) -> c_int
 * Weak so the shim links on builds without the coreutils feature.
 */
extern int wawona_coreutils_main(int argc, const char *const *argv)
    __attribute__((weak));

/* Provided by wwn-fastfetch (libfastfetch.a, main renamed to fastfetch_main). */
extern int fastfetch_main(int argc, char *argv[])
    __attribute__((weak));

/* Provided by wwn-neovim (libwawona-neovim.a, main renamed to wawona_nvim_main). */
extern int wawona_nvim_main(int argc, char *argv[])
    __attribute__((weak));

/* Provided by libwawona.a (waypipe-ssh feature, Rust waypipe_main). */
extern int waypipe_main(int argc, char *argv[])
    __attribute__((weak));

/*
 * Provided by libssh-inprocess.a (openssh built for iOS, no fork/exec).
 * ssh_main: full OpenSSH client (connects via tcp, password via
 *   SSH_ASKPASS_PASSWORD env var).
 * ssh_keygen_main: key-generation tool (no network).
 * scp_main: secure copy client.
 * These are absent when the openssh static library is not linked, so
 * keep them weak.
 */
extern int ssh_main(int argc, char *argv[])
    __attribute__((weak));
extern int ssh_keygen_main(int argc, char *argv[])
    __attribute__((weak));
extern int scp_main(int argc, char *argv[])
    __attribute__((weak));

/*
 * Bundled Wayland demo clients from libweston-13.a.
 * These connect to the host compositor via WAYLAND_DISPLAY and open a window,
 * running in-process on the calling thread (foreground) until the window
 * closes.  All are weak so the dispatch shim links on builds where the weston
 * toytoolkit archive is absent (e.g. watchOS).
 */
extern int weston_simple_shm_main(int argc, char *argv[])
    __attribute__((weak));
extern int flower_main(int argc, char *argv[])
    __attribute__((weak));
extern int clickdot_main(int argc, char *argv[])
    __attribute__((weak));
extern int smoke_main(int argc, char *argv[])
    __attribute__((weak));
extern int eventdemo_main(int argc, char *argv[])
    __attribute__((weak));
extern int resizor_main(int argc, char *argv[])
    __attribute__((weak));
extern int cliptest_main(int argc, char *argv[])
    __attribute__((weak));
extern int transformed_main(int argc, char *argv[])
    __attribute__((weak));
extern int stacking_main(int argc, char *argv[])
    __attribute__((weak));
extern int dnd_main(int argc, char *argv[])
    __attribute__((weak));
extern int image_main(int argc, char *argv[])
    __attribute__((weak));
extern int scaler_main(int argc, char *argv[])
    __attribute__((weak));
extern int editor_main(int argc, char *argv[])
    __attribute__((weak));
extern int constraints_main(int argc, char *argv[])
    __attribute__((weak));

static void
wwn_dispatch_sync_terminal_size_env(void)
{
#if defined(__APPLE__) && (TARGET_OS_IPHONE || TARGET_OS_TV || TARGET_OS_WATCH)
	struct winsize ws;
	char buf[16];

	if (wwn_pty_tty_shim_get_winsize(&ws) != 0)
		return;
	if (ws.ws_col > 0) {
		snprintf(buf, sizeof buf, "%u", (unsigned)ws.ws_col);
		setenv("COLUMNS", buf, 1);
	}
	if (ws.ws_row > 0) {
		snprintf(buf, sizeof buf, "%u", (unsigned)ws.ws_row);
		setenv("LINES", buf, 1);
	}
#endif
}

static int
wwn_is_nvim_name(const char *name)
{
	return name != NULL
	    && (strcmp(name, "nvim") == 0
	        || strcmp(name, "vi") == 0
	        || strcmp(name, "vim") == 0);
}

static int
wwn_is_waypipe_name(const char *name)
{
	return name != NULL
	    && (strcmp(name, "waypipe") == 0
	        || strcmp(name, "waypipe-rs") == 0);
}

static int
wwn_is_ssh_name(const char *name)
{
	return name != NULL && strcmp(name, "ssh") == 0;
}

static int
wwn_is_ssh_keygen_name(const char *name)
{
	return name != NULL
	    && (strcmp(name, "ssh-keygen") == 0
	        || strcmp(name, "ssh_keygen") == 0);
}

static int
wwn_is_scp_name(const char *name)
{
	return name != NULL && strcmp(name, "scp") == 0;
}

/*
 * Table of bundled Wayland clients dispatchable from wwn-zsh.
 * Each entry maps a command name to a weak function pointer.  NULL fn means
 * the archive was not linked (e.g. watchOS build without weston toytoolkit).
 */
typedef int (*wwn_client_fn)(int, char *[]);

typedef struct {
	const char    *name;
	wwn_client_fn  fn;
} wwn_wayland_entry_t;

/*
 * Static initializer with weak symbols: the compiler stores NULL for any
 * absent weak symbol, so wwn_wayland_clients[i].fn is NULL when not linked.
 */
static const wwn_wayland_entry_t wwn_wayland_clients[] = {
	{ "weston-simple-shm", (wwn_client_fn)weston_simple_shm_main },
	{ "weston-flower",     (wwn_client_fn)flower_main     },
	{ "weston-clickdot",   (wwn_client_fn)clickdot_main   },
	{ "weston-smoke",      (wwn_client_fn)smoke_main      },
	{ "weston-eventdemo",  (wwn_client_fn)eventdemo_main  },
	{ "weston-resizor",    (wwn_client_fn)resizor_main    },
	{ "weston-cliptest",   (wwn_client_fn)cliptest_main   },
	{ "weston-transformed",(wwn_client_fn)transformed_main},
	{ "weston-stacking",   (wwn_client_fn)stacking_main   },
	{ "weston-dnd",        (wwn_client_fn)dnd_main        },
	{ "weston-image",      (wwn_client_fn)image_main      },
	{ "weston-scaler",     (wwn_client_fn)scaler_main     },
	{ "weston-editor",     (wwn_client_fn)editor_main     },
	{ "weston-constraints",(wwn_client_fn)constraints_main},
};

#define WWN_WAYLAND_CLIENTS_N \
	(sizeof(wwn_wayland_clients) / sizeof(wwn_wayland_clients[0]))

static wwn_client_fn
wwn_lookup_wayland_client(const char *name)
{
	size_t i;

	if (name == NULL)
		return NULL;
	for (i = 0; i < WWN_WAYLAND_CLIENTS_N; i++) {
		if (strcmp(name, wwn_wayland_clients[i].name) == 0)
			return wwn_wayland_clients[i].fn;
	}
	return NULL;
}

/*
 * In-process safe subset (v1). Mirrors the `coreutils` feature list in
 * Cargo.toml. Sandbox-meaningless or exit-prone utilities are intentionally
 * excluded. grep/find live in separate uutils projects (later phase).
 */
static const char *const wwn_safe_subset[] = {
	"ls",      "cat",     "cp",       "mv",       "rm",
	"mkdir",   "rmdir",   "ln",       "touch",    "echo",
	"pwd",     "head",    "tail",     "wc",       "sort",
	"cut",     "tr",      "seq",      "basename", "dirname",
	"stat",    "du",      "df",       "date",     "env",
	"printenv","uname",   "whoami",   "yes",      "tee",
	"nl",      "tac",     "fold",     "expand",   "unexpand",
	"truncate",
};

static const char *
wwn_basename(const char *path)
{
	const char *base;

	if (path == NULL || path[0] == '\0')
		return NULL;
	base = strrchr(path, '/');
	return base != NULL ? base + 1 : path;
}

static int
wwn_in_safe_subset(const char *name)
{
	size_t i;
	size_t n = sizeof wwn_safe_subset / sizeof wwn_safe_subset[0];

	for (i = 0; i < n; i++) {
		if (strcmp(name, wwn_safe_subset[i]) == 0)
			return 1;
	}
	return 0;
}

static int
wwn_run_clear(void)
{
	fputs("\033[2J\033[H", stdout);
	fflush(stdout);
	return 0;
}

int
wawona_dispatch_can_handle(const char *argv0)
{
	const char *name = wwn_basename(argv0);

	if (name == NULL || name[0] == '\0')
		return 0;
	if (strcmp(name, "clear") == 0)
		return 1;
	if (strcmp(name, "fastfetch") == 0 && fastfetch_main != NULL)
		return 1;
	if (wwn_is_waypipe_name(name) && waypipe_main != NULL)
		return 1;
	if (wwn_is_nvim_name(name) && wawona_nvim_main != NULL)
		return 1;
	if (wwn_is_ssh_name(name) && ssh_main != NULL)
		return 1;
	if (wwn_is_ssh_keygen_name(name) && ssh_keygen_main != NULL)
		return 1;
	if (wwn_is_scp_name(name) && scp_main != NULL)
		return 1;
	if (wwn_lookup_wayland_client(name) != NULL)
		return 1;
	if (wawona_coreutils_main == NULL)
		return 0;
	return wwn_in_safe_subset(name);
}

int
wawona_dispatch_inprocess(const char *path, char *const argv[],
                          char *const envp[])
{
	const char *name;
	int argc = 0;
	int rc;

	(void)envp; /* in-process model shares environ; zsh applies assignments */

	if (argv == NULL || argv[0] == NULL)
		return WWN_DISPATCH_NOT_HANDLED;

	/* Prefer argv[0] for the utility name; fall back to the exec path. */
	name = wwn_basename(argv[0]);
	if (name == NULL || name[0] == '\0')
		name = wwn_basename(path);
	if (name == NULL || name[0] == '\0')
		return WWN_DISPATCH_NOT_HANDLED;

	if (strcmp(name, "clear") == 0)
		return wwn_run_clear();

	if (strcmp(name, "fastfetch") == 0 && fastfetch_main != NULL) {
		while (argv[argc] != NULL)
			argc++;
		rc = fastfetch_main(argc, argv);
		fflush(stdout);
		fflush(stderr);
		return rc;
	}

	if (wwn_is_waypipe_name(name) && waypipe_main != NULL) {
		while (argv[argc] != NULL)
			argc++;
		rc = waypipe_main(argc, argv);
		fflush(stdout);
		fflush(stderr);
		return rc;
	}

	if (wwn_is_nvim_name(name) && wawona_nvim_main != NULL) {
		while (argv[argc] != NULL)
			argc++;
		rc = wawona_nvim_main(argc, argv);
		fflush(stdout);
		fflush(stderr);
		return rc;
	}

	if (wwn_is_ssh_name(name) && ssh_main != NULL) {
		while (argv[argc] != NULL)
			argc++;
		rc = ssh_main(argc, argv);
		fflush(stdout);
		fflush(stderr);
		return rc;
	}

	if (wwn_is_ssh_keygen_name(name) && ssh_keygen_main != NULL) {
		while (argv[argc] != NULL)
			argc++;
		rc = ssh_keygen_main(argc, argv);
		fflush(stdout);
		fflush(stderr);
		return rc;
	}

	if (wwn_is_scp_name(name) && scp_main != NULL) {
		while (argv[argc] != NULL)
			argc++;
		rc = scp_main(argc, argv);
		fflush(stdout);
		fflush(stderr);
		return rc;
	}

	{
		wwn_client_fn wfn = wwn_lookup_wayland_client(name);

		if (wfn != NULL) {
			while (argv[argc] != NULL)
				argc++;
			rc = wfn(argc, argv);
			fflush(stdout);
			fflush(stderr);
			return rc;
		}
	}

	if (!wwn_in_safe_subset(name))
		return WWN_DISPATCH_NOT_HANDLED;

	/* No coreutils linked in this build (e.g. watchOS): fall through. */
	if (wawona_coreutils_main == NULL)
		return WWN_DISPATCH_NOT_HANDLED;

	while (argv[argc] != NULL)
		argc++;

	/* The utility writes to the inherited stdout/stderr (the PTY slave). */
	wwn_dispatch_sync_terminal_size_env();
	rc = wawona_coreutils_main(argc, (const char *const *)argv);

	/* Flush so output orders correctly relative to the next zsh prompt. */
	fflush(stdout);
	fflush(stderr);

	/* Rust returns its own NOT_HANDLED sentinel when the util is unknown. */
	return rc;
}
