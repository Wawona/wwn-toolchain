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
#include <fcntl.h>
#include <unistd.h>
#include <stdatomic.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && (TARGET_OS_IPHONE || TARGET_OS_TV || TARGET_OS_WATCH)
/*
 * Set by wawona-mobile-spawn's worker thread so a re-entered
 * wawona_dispatch_inprocess runs the Wayland client synchronously instead of
 * spawning another detached worker (would recurse forever).
 */
_Thread_local int wwn_dispatch_async_worker;
#endif

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

/* Provided by wwn-phoon-rs (libphoon_rs.a, C ABI phoon_main). */
extern int phoon_main(int argc, char *argv[])
	__attribute__((weak));

/* Provided by wwn-wasm crates/wpm (libwpm.a). Weak so builds without wpm link. */
extern int wpm_main(int argc, char *argv[])
	__attribute__((weak));

/* Provided by wwn-neovim (libwawona-neovim.a, main renamed to wawona_nvim_main). */
extern int wawona_nvim_main(int argc, char *argv[])
    __attribute__((weak));

/* Provided by wwn-niri fuzzel port (libfuzzel.a, main renamed to fuzzel_main). */
extern int fuzzel_main(int argc, char *argv[])
    __attribute__((weak));

/* Provided by wwn-foot (libfoot.a, main renamed to foot_main). */
extern int foot_main(int argc, char *argv[])
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
extern int simple_egl_main(int argc, char *argv[])
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
extern int weston_terminal_main(int argc, char *argv[])
    __attribute__((weak));

/* Nested compositors: Wayland clients of the current WAYLAND_DISPLAY. */
extern int weston_compositor_main(int argc, char *argv[])
    __attribute__((weak));
extern int niri_main(void)
    __attribute__((weak));

/*
 * Provided by wwn-wasm (libwawona_wasm.a). Weak so watchOS and builds
 * without the runtime still link. Never add a strong stub — that would
 * satisfy -u and skip the real archive.
 */
extern int wawona_wasm_run(int argc, char *argv[])
    __attribute__((weak));
extern int wawona_wasm_can_run(const char *path)
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

static int
wwn_is_weston_compositor_name(const char *name)
{
	return name != NULL && strcmp(name, "weston") == 0;
}

static int
wwn_is_niri_name(const char *name)
{
	return name != NULL && strcmp(name, "niri") == 0;
}

static int
wwn_run_nested_weston(int argc, char **argv)
{
	int has_backend = 0;
	int has_socket = 0;
	int has_shell = 0;
	int i;
	int nargc;
	char **nargv;
	char sockarg[64];
	int rc;
	static atomic_uint seq;

	if (weston_compositor_main == NULL) {
		fprintf(stderr,
		        "wawona: weston is bundled but unavailable in this build.\n");
		fflush(stderr);
		return 127;
	}

	unsetenv("WWN_MODEB_TTY");
	for (i = 1; i < argc; i++) {
		if (strncmp(argv[i], "--backend", 9) == 0 ||
		    strcmp(argv[i], "-B") == 0)
			has_backend = 1;
		if (strncmp(argv[i], "--socket", 8) == 0)
			has_socket = 1;
		if (strncmp(argv[i], "--shell", 7) == 0)
			has_shell = 1;
	}

	nargc = argc;
	if (!has_backend)
		nargc++;
	if (!has_socket)
		nargc++;
	if (!has_shell)
		nargc++;
	nargv = calloc((size_t)nargc + 1, sizeof(*nargv));
	if (nargv == NULL)
		return 1;
	for (i = 0; i < argc; i++)
		nargv[i] = argv[i];
	nargc = argc;
	if (!has_backend)
		nargv[nargc++] = "--backend=wayland";
	if (!has_shell)
		nargv[nargc++] = "--shell=desktop-shell.so";
	if (!has_socket) {
		snprintf(sockarg, sizeof sockarg, "--socket=weston-inproc-%u",
		         (unsigned)atomic_fetch_add(&seq, 1u));
		nargv[nargc++] = sockarg;
	}
	nargv[nargc] = NULL;
	rc = weston_compositor_main(nargc, nargv);
	free(nargv);
	return rc;
}

static int
wwn_run_nested_niri(void)
{
	if (niri_main == NULL) {
		fprintf(stderr,
		        "wawona: niri is bundled but unavailable in this build.\n");
		fflush(stderr);
		return 127;
	}
	unsetenv("WWN_MODEB_TTY");
	setenv("NIRI_BACKEND", "nested", 1);
	return niri_main();
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
	{ "weston-simple-egl", (wwn_client_fn)simple_egl_main },
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
	{ "weston-terminal",   (wwn_client_fn)weston_terminal_main },
};

#define WWN_WAYLAND_CLIENTS_N \
	(sizeof(wwn_wayland_clients) / sizeof(wwn_wayland_clients[0]))

static const wwn_wayland_entry_t *
wwn_find_wayland_client(const char *name)
{
	size_t i;

	if (name == NULL)
		return NULL;
	for (i = 0; i < WWN_WAYLAND_CLIENTS_N; i++) {
		if (strcmp(name, wwn_wayland_clients[i].name) == 0)
			return &wwn_wayland_clients[i];
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

static int
wwn_is_help_name(const char *name)
{
	return name != NULL
	    && (strcmp(name, "help") == 0
	        || strcmp(name, "wawona") == 0);
}

static int
wwn_name_ends_with_wasm(const char *name)
{
	size_t n;

	if (name == NULL)
		return 0;
	n = strlen(name);
	return n >= 5 && strcmp(name + n - 5, ".wasm") == 0;
}

static int
wwn_file_is_wasm(const char *path)
{
	unsigned char mag[4];
	int fd;
	ssize_t n;

	if (path == NULL || path[0] == '\0')
		return 0;
	fd = open(path, O_RDONLY);
	if (fd < 0)
		return 0;
	n = read(fd, mag, 4);
	close(fd);
	return n == 4 && mag[0] == 0x00 && mag[1] == 'a'
	    && mag[2] == 's' && mag[3] == 'm';
}

static void
wwn_print_linked(const char *name, int linked)
{
	if (linked)
		fprintf(stdout, "  %-22s  bundled\n", name);
}

static int
wwn_run_help(int argc, char *const argv[])
{
	size_t i;
	size_t n;

	(void)argc;
	(void)argv;
	fprintf(stdout, "Wawona in-process shell — type a name; there is no fork/exec.\n");
	fprintf(stdout, "Milestone: Support WASI P1 P2 WASM!  https://github.com/Wawona/Wawona/milestone/2\n\n");

	fprintf(stdout, "Builtins (zsh):\n");
	fprintf(stdout, "  cd  export  alias  unalias  setopt  unsetopt  echo  print  pwd\n");
	fprintf(stdout, "  true  false  test  [  source  .  exit  return  jobs  fg  bg\n\n");

	fprintf(stdout, "Catalog command:\n");
	fprintf(stdout, "  help, wawona          this list\n");
	fprintf(stdout, "  clear                 reset the terminal\n");
	fprintf(stdout, "  apt                   optional App Store modules\n\n");

	fprintf(stdout, "uutils (in-process");
	if (wawona_coreutils_main == NULL)
		fprintf(stdout, ", not linked in this build");
	fprintf(stdout, "):\n  ");
	n = sizeof wwn_safe_subset / sizeof wwn_safe_subset[0];
	for (i = 0; i < n; i++) {
		fprintf(stdout, "%s%s", wwn_safe_subset[i],
		        (i + 1 == n) ? "\n" : " ");
	}
	fprintf(stdout, "\n");

	fprintf(stdout, "Bundled clients (linked in this binary):\n");
	wwn_print_linked("fastfetch", fastfetch_main != NULL);
	wwn_print_linked("phoon", phoon_main != NULL);
	wwn_print_linked("nvim / vi / vim", wawona_nvim_main != NULL);
	wwn_print_linked("waypipe", waypipe_main != NULL);
	wwn_print_linked("ssh", ssh_main != NULL);
	wwn_print_linked("ssh-keygen", ssh_keygen_main != NULL);
	wwn_print_linked("scp", scp_main != NULL);
	wwn_print_linked("fuzzel", fuzzel_main != NULL);
	wwn_print_linked("foot", foot_main != NULL);
	wwn_print_linked("weston", weston_compositor_main != NULL);
	wwn_print_linked("niri", niri_main != NULL);
	for (i = 0; i < WWN_WAYLAND_CLIENTS_N; i++) {
		if (wwn_wayland_clients[i].fn != NULL)
			wwn_print_linked(wwn_wayland_clients[i].name, 1);
	}
	fprintf(stdout, "\n");

	fprintf(stdout, "WASM / WASI (user .wasm documents; not Apple-signed):\n");
	if (wawona_wasm_run != NULL) {
		fprintf(stdout, "  wasm <file.wasm> [args]   run WASI P1 or P2\n");
		fprintf(stdout, "  wasm <package> [args]     run installed Runtime package\n");
		fprintf(stdout, "  ./file.wasm [args]        same, by magic \\\\0asm\n");
		fprintf(stdout, "  Drop .wasm into the Wawona Files / Documents folder.\n");
	} else {
		fprintf(stdout, "  coming — wwn-wasm is not linked in this build.\n");
		fprintf(stdout, "  See https://github.com/Wawona/Wawona/issues/146\n");
	}
	if (wpm_main != NULL) {
		fprintf(stdout, "\nRuntime packages (wpm — Mode A registry + local store):\n");
		fprintf(stdout, "  wpm install ./file.wasm   register a local .wasm\n");
		fprintf(stdout, "  wpm install <name>        from repo.wawona.io/wasm\n");
		fprintf(stdout, "  wpm list | search | remove <name>\n");
	}
	fprintf(stdout, "\nList names: ls $WAWONA_ROOTFS/usr/bin   or   ls ../usr/bin\n");
	fprintf(stdout, "Prefer a native port when we have one.\n");
	fflush(stdout);
	return 0;
}

static int
wwn_run_wasm(int argc, char *argv[])
{
	if (wawona_wasm_run == NULL) {
		fprintf(stdout,
		        "wawona: WASM runtime is not linked in this build (see `help`).\n");
		fflush(stdout);
		return 127;
	}
	return wawona_wasm_run(argc, argv);
}

int
wawona_dispatch_can_handle(const char *argv0)
{
	const char *name = wwn_basename(argv0);

	if (name == NULL || name[0] == '\0')
		return 0;
	if (wwn_is_help_name(name))
		return 1;
	if (strcmp(name, "clear") == 0)
		return 1;
	if (strcmp(name, "wasm") == 0)
		return 1;
	if (wwn_name_ends_with_wasm(name) || wwn_file_is_wasm(argv0))
		return 1;
	if (strcmp(name, "fastfetch") == 0 && fastfetch_main != NULL)
		return 1;
	if (strcmp(name, "phoon") == 0 && phoon_main != NULL)
		return 1;
	if (strcmp(name, "wpm") == 0 && wpm_main != NULL)
		return 1;
	if (strcmp(name, "fuzzel") == 0 && fuzzel_main != NULL)
		return 1;
	if (strcmp(name, "foot") == 0 && foot_main != NULL)
		return 1;
	if (wwn_is_weston_compositor_name(name) && weston_compositor_main != NULL)
		return 1;
	if (wwn_is_niri_name(name) && niri_main != NULL)
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
	if (wwn_find_wayland_client(name) != NULL)
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

#if !defined(__APPLE__) || !(TARGET_OS_IPHONE || TARGET_OS_TV || TARGET_OS_WATCH)
	(void)envp; /* in-process model shares environ; zsh applies assignments */
#endif

	if (argv == NULL || argv[0] == NULL)
		return WWN_DISPATCH_NOT_HANDLED;

	/* Prefer argv[0] for the utility name; fall back to the exec path. */
	name = wwn_basename(argv[0]);
	if (name == NULL || name[0] == '\0')
		name = wwn_basename(path);
	if (name == NULL || name[0] == '\0')
		return WWN_DISPATCH_NOT_HANDLED;

	if (wwn_is_help_name(name)) {
		while (argv[argc] != NULL)
			argc++;
		return wwn_run_help(argc, argv);
	}

	if (strcmp(name, "clear") == 0)
		return wwn_run_clear();

	if (strcmp(name, "wasm") == 0) {
		while (argv[argc] != NULL)
			argc++;
		return wwn_run_wasm(argc, argv);
	}

	if (wwn_name_ends_with_wasm(name) || wwn_file_is_wasm(path)
	    || wwn_file_is_wasm(argv[0])) {
		while (argv[argc] != NULL)
			argc++;
		return wwn_run_wasm(argc, argv);
	}

	if (strcmp(name, "fastfetch") == 0 && fastfetch_main != NULL) {
		while (argv[argc] != NULL)
			argc++;
		rc = fastfetch_main(argc, argv);
		fflush(stdout);
		fflush(stderr);
		return rc;
	}

	if (strcmp(name, "phoon") == 0 && phoon_main != NULL) {
		while (argv[argc] != NULL)
			argc++;
		rc = phoon_main(argc, argv);
		fflush(stdout);
		fflush(stderr);
		return rc;
	}

	if (strcmp(name, "wpm") == 0 && wpm_main != NULL) {
		while (argv[argc] != NULL)
			argc++;
		rc = wpm_main(argc, argv);
		fflush(stdout);
		fflush(stderr);
		return rc;
	}

	if (strcmp(name, "fuzzel") == 0 && fuzzel_main != NULL) {
		while (argv[argc] != NULL)
			argc++;
		rc = fuzzel_main(argc, argv);
		fflush(stdout);
		fflush(stderr);
		return rc;
	}

	if (strcmp(name, "foot") == 0 && foot_main != NULL) {
#if defined(__APPLE__) && (TARGET_OS_IPHONE || TARGET_OS_TV || TARGET_OS_WATCH)
		if (!wwn_dispatch_async_worker &&
		    wawona_dispatch_spawn_async(path, argv, envp) == 0) {
			fprintf(stderr, "wawona: started foot (detached)\n");
			fflush(stderr);
			return 0;
		}
#endif
		while (argv[argc] != NULL)
			argc++;
		rc = foot_main(argc, argv);
		fflush(stdout);
		fflush(stderr);
		return rc;
	}

	if (wwn_is_weston_compositor_name(name) &&
	    weston_compositor_main != NULL) {
#if defined(__APPLE__) && (TARGET_OS_IPHONE || TARGET_OS_TV || TARGET_OS_WATCH)
		if (!wwn_dispatch_async_worker &&
		    wawona_dispatch_spawn_async(path, argv, envp) == 0) {
			fprintf(stderr,
			        "wawona: started weston nested on "
			        "WAYLAND_DISPLAY=%s (detached)\n",
			        getenv("WAYLAND_DISPLAY") ?
			            getenv("WAYLAND_DISPLAY") : "(null)");
			fflush(stderr);
			return 0;
		}
#endif
		while (argv[argc] != NULL)
			argc++;
		rc = wwn_run_nested_weston(argc, argv);
		fflush(stdout);
		fflush(stderr);
		return rc;
	}

	if (wwn_is_niri_name(name) && niri_main != NULL) {
#if defined(__APPLE__) && (TARGET_OS_IPHONE || TARGET_OS_TV || TARGET_OS_WATCH)
		if (!wwn_dispatch_async_worker &&
		    wawona_dispatch_spawn_async(path, argv, envp) == 0) {
			fprintf(stderr,
			        "wawona: started niri nested on "
			        "WAYLAND_DISPLAY=%s (detached)\n",
			        getenv("WAYLAND_DISPLAY") ?
			            getenv("WAYLAND_DISPLAY") : "(null)");
			fflush(stderr);
			return 0;
		}
#endif
		rc = wwn_run_nested_niri();
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
		const wwn_wayland_entry_t *ent = wwn_find_wayland_client(name);

		if (ent != NULL) {
			wwn_client_fn wfn = ent->fn;

			if (wfn == NULL) {
				fprintf(stderr,
				        "wawona: '%s' is bundled but unavailable in this build.\n",
				        name);
				fflush(stderr);
				return 127;
			}
#if defined(__APPLE__) && (TARGET_OS_IPHONE || TARGET_OS_TV || TARGET_OS_WATCH)
			/*
			 * Detach Wayland toy clients from the zsh PTY thread so the
			 * shell stays responsive (#65). Skip when already on a
			 * spawn worker (re-entry from wawona_dispatch_spawn_async).
			 */
			if (!wwn_dispatch_async_worker &&
			    wawona_dispatch_spawn_async(path, argv, envp) == 0) {
				fprintf(stderr, "wawona: started %s (detached)\n",
				        name);
				fflush(stderr);
				return 0;
			}
#endif
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
