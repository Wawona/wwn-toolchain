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
     *     SSH uses in-process libssh2 (no openssh binary); set WAYPIPE_SSH_PASSWORD
     *     for password auth when invoking from a shell.
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

static int
wwn_is_nvim_name(const char *name)
{
	return name != NULL
	    && (strcmp(name, "nvim") == 0
	        || strcmp(name, "vi") == 0
	        || strcmp(name, "vim") == 0);
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
	if (strcmp(name, "waypipe") == 0 && waypipe_main != NULL)
		return 1;
	if (wwn_is_nvim_name(name) && wawona_nvim_main != NULL)
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

	if (strcmp(name, "waypipe") == 0 && waypipe_main != NULL) {
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

	if (!wwn_in_safe_subset(name))
		return WWN_DISPATCH_NOT_HANDLED;

	/* No coreutils linked in this build (e.g. watchOS): fall through. */
	if (wawona_coreutils_main == NULL)
		return WWN_DISPATCH_NOT_HANDLED;

	while (argv[argc] != NULL)
		argc++;

	/* The utility writes to the inherited stdout/stderr (the PTY slave). */
	rc = wawona_coreutils_main(argc, (const char *const *)argv);

	/* Flush so output orders correctly relative to the next zsh prompt. */
	fflush(stdout);
	fflush(stderr);

	/* Rust returns its own NOT_HANDLED sentinel when the util is unknown. */
	return rc;
}
