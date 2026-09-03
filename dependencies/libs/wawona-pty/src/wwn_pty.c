#if defined(__ANDROID__)
#define _POSIX_C_SOURCE 200809L
#endif

#include "wwn_pty.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <setjmp.h>
#include <spawn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <pthread.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && (TARGET_OS_IPHONE || TARGET_OS_TV || TARGET_OS_WATCH)
#include <sys/socket.h>
#include <pthread.h>
#include <dlfcn.h>
#include <sys/filio.h>
#include <time.h>

extern int wawona_zsh_main(int argc, char **argv);
#endif

extern char **environ;

static int g_app_log_fd = -1;
static pthread_once_t g_app_log_fd_once = PTHREAD_ONCE_INIT;

static void
app_log_fd_init(void)
{
	if (g_app_log_fd >= 0)
		return;
	g_app_log_fd = dup(STDERR_FILENO);
	if (g_app_log_fd < 0)
		g_app_log_fd = STDERR_FILENO;
}

#if defined(__APPLE__) && (TARGET_OS_IPHONE || TARGET_OS_TV || TARGET_OS_WATCH)
__attribute__((constructor(101)))
static void
wwn_pty_early_init(void)
{
	app_log_fd_init();
}
#endif

int
wwn_app_log_fd(void)
{
	pthread_once(&g_app_log_fd_once, app_log_fd_init);
	return g_app_log_fd;
}

static int
wwn_pty_log_enabled(void)
{
	static int enabled = -1;

	if (enabled < 0)
		enabled = getenv("WAWONA_PTY_QUIET") != NULL ? 0 : 1;
	return enabled;
}

#define WWN_PTY_LOG(fmt, ...)                                                      \
	do {                                                                       \
		if (wwn_pty_log_enabled())                                         \
			dprintf(wwn_app_log_fd(), fmt, ##__VA_ARGS__);             \
	} while (0)

struct wwn_pty_session {
	int master_fd;
	pid_t pid;
};

static int
realpath_or_copy(const char *path, char *out, size_t out_len)
{
	if (realpath(path, out) != NULL)
		return 0;
	if (path[0] != '/')
		return -1;
	if (strlen(path) >= out_len)
		return -1;
	memcpy(out, path, strlen(path) + 1);
	return 0;
}

static int
shell_is_runnable(const char *canonical_shell)
{
	struct stat st;

#if defined(__APPLE__) && (TARGET_OS_IPHONE || TARGET_OS_TV || TARGET_OS_WATCH)
	/*
	 * iOS does not honor Unix execute bits on signed bundle Mach-O.
	 * Existence + regular file under the allowlist is sufficient; AMFI
	 * enforces code signature at posix_spawn time.
	 */
	if (stat(canonical_shell, &st) != 0)
		return 0;
	return S_ISREG(st.st_mode) && st.st_size > 0;
#else
	if (access(canonical_shell, X_OK) != 0)
		return 0;
	if (stat(canonical_shell, &st) != 0)
		return 0;
	return S_ISREG(st.st_mode);
#endif
}

static int
path_is_bundled_zsh_framework(const char *canonical_shell)
{
	const char *marker = "/Frameworks/zsh.framework/zsh";
	size_t mlen = strlen(marker);
	size_t clen = strlen(canonical_shell);

	if (clen < mlen)
		return 0;
	if (strcmp(canonical_shell + clen - mlen, marker) != 0)
		return 0;
	return shell_is_runnable(canonical_shell);
}

static int
ios_shell_path_allowed(const char *canonical_shell)
{
	if (getenv("WAWONA_ZSH_IN_PROCESS") != NULL)
		return 1;
	return path_is_bundled_zsh_framework(canonical_shell);
}

int
wwn_pty_is_allowed_shell_path(const char *shell_path)
{
	char canonical_shell[PATH_MAX];

	if (shell_path == NULL || shell_path[0] == '\0')
		return 0;

#if defined(__APPLE__) && (TARGET_OS_IPHONE || TARGET_OS_TV || TARGET_OS_WATCH)
	if (getenv("WAWONA_ZSH_IN_PROCESS") != NULL)
		return 1;
#endif

	if (realpath_or_copy(shell_path, canonical_shell, sizeof canonical_shell) != 0)
		return 0;

#if defined(__APPLE__) && (TARGET_OS_IPHONE || TARGET_OS_TV || TARGET_OS_WATCH)
	return ios_shell_path_allowed(canonical_shell);
#elif defined(__ANDROID__)
	if (strstr(canonical_shell, "libzsh_bin.so") != NULL)
		return shell_is_runnable(canonical_shell);
	{
		const char *roots[2];
		int i;

		roots[0] = getenv("WAWONA_BUNDLE_ROOTFS");
		roots[1] = getenv("WAWONA_ROOTFS");
		for (i = 0; i < 2; i++) {
			char canonical_root[PATH_MAX];
			size_t root_len;

			if (roots[i] == NULL || roots[i][0] == '\0')
				continue;
			if (realpath_or_copy(roots[i], canonical_root,
			                     sizeof canonical_root) != 0)
				continue;
			root_len = strlen(canonical_root);
			if (strncmp(canonical_shell, canonical_root, root_len) != 0)
				continue;
			if (canonical_shell[root_len] != '/')
				continue;
			if (strncmp(canonical_shell + root_len, "/usr/bin/", 9) != 0)
				continue;
			if (canonical_shell[root_len + 9] == '\0')
				continue;
			if (i == 1 && roots[0] != NULL && roots[0][0] != '\0')
				continue;
			return shell_is_runnable(canonical_shell);
		}
	}
	return 0;
#else
	{
		const char *roots[2];
		int i;

		roots[0] = getenv("WAWONA_BUNDLE_ROOTFS");
		roots[1] = getenv("WAWONA_ROOTFS");
		for (i = 0; i < 2; i++) {
			char canonical_root[PATH_MAX];
			size_t root_len;

			if (roots[i] == NULL || roots[i][0] == '\0')
				continue;
			if (realpath_or_copy(roots[i], canonical_root,
			                     sizeof canonical_root) != 0)
				continue;
			root_len = strlen(canonical_root);
			if (strncmp(canonical_shell, canonical_root, root_len) != 0)
				continue;
			if (canonical_shell[root_len] != '/')
				continue;
			if (strncmp(canonical_shell + root_len, "/usr/bin/", 9) != 0)
				continue;
			if (canonical_shell[root_len + 9] == '\0')
				continue;
			if (i == 1 && roots[0] != NULL && roots[0][0] != '\0')
				continue;
			return shell_is_runnable(canonical_shell);
		}
	}
	return 0;
#endif
}

#if defined(__APPLE__) && (TARGET_OS_IPHONE || TARGET_OS_TV || TARGET_OS_WATCH)
static int ios_pty_input_read = -1;
static int ios_pty_input_write = -1;
static int ios_terminal_master = -1;
static pthread_mutex_t ios_terminal_master_lock = PTHREAD_MUTEX_INITIALIZER;

#define WWN_IOS_MAX_PTYS 8
struct ios_pty_slot {
	int master;
	int slave;
	int input_read;
	int input_write;
	int live;
};
static struct ios_pty_slot ios_ptys[WWN_IOS_MAX_PTYS];

static struct ios_pty_slot *
ios_pty_slot_by_master(int master)
{
	int i;

	for (i = 0; i < WWN_IOS_MAX_PTYS; i++) {
		if (ios_ptys[i].live && ios_ptys[i].master == master)
			return &ios_ptys[i];
	}
	return NULL;
}

static struct ios_pty_slot *
ios_pty_slot_by_slave(int slave)
{
	int i;

	for (i = 0; i < WWN_IOS_MAX_PTYS; i++) {
		if (ios_ptys[i].live && ios_ptys[i].slave == slave)
			return &ios_ptys[i];
	}
	return NULL;
}

static void
ios_pty_focus_slot(struct ios_pty_slot *slot)
{
	if (slot == NULL)
		return;
	ios_pty_input_read = slot->input_read;
	ios_pty_input_write = slot->input_write;
}
/*
 * Process-global latch for the fake-TTY shim. setenv("WAWONA_PTY_FAKE_TTY")
 * is fragile: zsh rebuilds the global `environ` during startup, so a later
 * getenv() from an interposed isatty()/tcgetattr() can momentarily miss it and
 * make zsh think stdin is not a tty (→ non-interactive, no prompt). This flag
 * is set once when the socketpair fallback is armed and never cleared.
 */
static int ios_fake_tty_forced;

static int
open_pipe_fallback(int *master_fd, int *slave_fd, int pty_err)
{
	int fds[2];
	int input_pipe[2];
	int i;
	struct ios_pty_slot *slot = NULL;

	for (i = 0; i < WWN_IOS_MAX_PTYS; i++) {
		if (!ios_ptys[i].live) {
			slot = &ios_ptys[i];
			break;
		}
	}
	if (slot == NULL) {
		errno = EMFILE;
		return -1;
	}

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
		return -1;

	/*
	 * Shell stdout uses the socket slave; keyboard input uses a separate
	 * pipe. Dup2'ing the same socket endpoint to both stdin and stdout
	 * breaks zsh ZLE (typed bytes never reach the line editor). Stderr is
	 * wired to wwn_app_log_fd() in ios_zsh_thread so NSLog/os_log does not
	 * leak into weston-terminal.
	 *
	 * Each session keeps its own pair. Closing the previous pipe here used
	 * to make a second weston-terminal (or Foot + weston-terminal) steal
	 * the only keyboard pipe and latch one shell.
	 */
	if (pipe(input_pipe) != 0) {
		close(fds[0]);
		close(fds[1]);
		return -1;
	}

	slot->master = fds[0];
	slot->slave = fds[1];
	slot->input_read = input_pipe[0];
	slot->input_write = input_pipe[1];
	slot->live = 1;

	if (ios_pty_input_write < 0)
		ios_pty_focus_slot(slot);

	*master_fd = fds[0];
	*slave_fd = fds[1];
	ios_fake_tty_forced = 1;
	setenv("WAWONA_PTY_FAKE_TTY", "1", 1);
	WWN_PTY_LOG(
	        "wwn_pty: iOS sandbox blocks POSIX PTY (%s); using socketpair + input pipe + fake TTY shim for zsh (slot master=%d)\n",
	        strerror(pty_err), slot->master);
	return 0;
}

#endif

static int
open_posix_pty(int *master_fd, int *slave_fd, const struct winsize *ws)
{
	int master;
	char *slave_name;

	master = posix_openpt(O_RDWR | O_NOCTTY);
	if (master < 0)
		return -1;

	if (grantpt(master) != 0) {
		close(master);
		return -1;
	}
	if (unlockpt(master) != 0) {
		close(master);
		return -1;
	}

	slave_name = ptsname(master);
	if (slave_name == NULL) {
		close(master);
		return -1;
	}

	*slave_fd = open(slave_name, O_RDWR | O_NOCTTY);
	if (*slave_fd < 0) {
		close(master);
		return -1;
	}

	if (ws != NULL) {
		if (ioctl(master, TIOCSWINSZ, ws) != 0) {
			close(*slave_fd);
			close(master);
			return -1;
		}
	}

	*master_fd = master;
	return 0;
}

int
wwn_pty_open(int *master_fd, int *slave_fd, const struct winsize *ws)
{
#if defined(__APPLE__) && (TARGET_OS_IPHONE || TARGET_OS_TV || TARGET_OS_WATCH)
	int err;

	if (master_fd == NULL || slave_fd == NULL) {
		errno = EINVAL;
		return -1;
	}

	/*
	 * In-process zsh must use the socketpair+input-pipe fallback: stdin and
	 * stdout cannot share one fd (ZLE breaks), and the iOS soft keyboard must
	 * inject into the input pipe — never into the display master (writes there
	 * are read back by weston-terminal as faux shell output).
	 *
	 * Prefer that path whenever WAWONA_ZSH_IN_PROCESS is set. Otherwise try
	 * POSIX PTY, then fall back for any failure — iOS errno for
	 * posix_openpt is not always in the classic EPERM/ENODEV set, and a
	 * hard fail here used to make weston-terminal call exit() and take down
	 * the whole host process.
	 */
	if (getenv("WAWONA_ZSH_IN_PROCESS") != NULL) {
		if (open_pipe_fallback(master_fd, slave_fd, ENOTTY) == 0)
			return 0;
		errno = ENOTTY;
		return -1;
	}

	if (open_posix_pty(master_fd, slave_fd, ws) == 0)
		return 0;

	err = errno;
	if (open_pipe_fallback(master_fd, slave_fd, err) == 0)
		return 0;

	errno = err;
	return -1;
#else
	if (master_fd == NULL || slave_fd == NULL) {
		errno = EINVAL;
		return -1;
	}
	return open_posix_pty(master_fd, slave_fd, ws);
#endif
}

#if defined(__APPLE__) && (TARGET_OS_IPHONE || TARGET_OS_TV || TARGET_OS_WATCH)
/*
 * zsh's exit()/zexit must return control to ios_zsh_thread instead of
 * terminating the Wawona process (nested Weston was mid-pixman when zsh
 * failed keymap load and called exit → host SIGSEGV).
 */
static __thread jmp_buf wwn_zsh_exit_jmp;
static __thread int wwn_zsh_exit_armed;
static __thread int wwn_zsh_soft_exit_code;

__attribute__((__noreturn__)) void
wwn_zsh_soft_exit(int status)
{
	WWN_PTY_LOG("wwn_pty: soft-exit from in-process zsh status=%d (host stays up)\n",
	            status);
	if (wwn_zsh_exit_armed) {
		wwn_zsh_soft_exit_code = status;
		longjmp(wwn_zsh_exit_jmp, 1);
	}
	/* Called outside a shell session — still must not kill the host. */
	pthread_exit((void *)(intptr_t)status);
}

struct wwn_ios_shell_job {
	pid_t fake_pid;
	pthread_t thread;
	void *dylib;
	int slave_fd;
	int input_read_fd;
	int input_write_fd;
	int pace_read_fd;
	int nested;
	char *argv_storage[12];
	char *argv[12];
	volatile int running;
	int exit_code;
};

#define WWN_IOS_MAX_SHELL_JOBS 8
static struct wwn_ios_shell_job ios_shell_jobs[WWN_IOS_MAX_SHELL_JOBS];
static pthread_mutex_t ios_shell_jobs_lock = PTHREAD_MUTEX_INITIALIZER;
static pid_t ios_next_fake_pid = 48000;
static pthread_mutex_t ios_stdio_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * Interactive zsh_main is process-global (not re-entrant). The first shell
 * job runs zsh. Further PTY sessions get a nested dispatch loop on their
 * own socketpair so Foot + weston-terminal can both stay up.
 */
static pthread_t ios_shell_io_thread;
static int ios_shell_io_active;

static void
ios_close_fd(int *fdp)
{
	int fd;

	if (fdp == NULL)
		return;
	fd = *fdp;
	*fdp = -1;
	if (fd >= 0)
		close(fd);
}

void
wwn_pty_ios_allow_new_shell_session(void)
{
	int i;

	pthread_mutex_lock(&ios_shell_jobs_lock);
	for (i = 0; i < WWN_IOS_MAX_SHELL_JOBS; i++) {
		if (ios_shell_jobs[i].running) {
			pthread_mutex_unlock(&ios_shell_jobs_lock);
			WWN_PTY_LOG(
			        "wwn_pty: allow_new_shell_session skipped, shell still running\n");
			return;
		}
	}
	for (i = 0; i < WWN_IOS_MAX_SHELL_JOBS; i++)
		memset(&ios_shell_jobs[i], 0, sizeof ios_shell_jobs[i]);
	ios_shell_io_active = 0;
	pthread_mutex_unlock(&ios_shell_jobs_lock);
	WWN_PTY_LOG("wwn_pty: allow_new_shell_session, jobs cleared\n");
}

void
wwn_pty_ios_stop_shell_session(void)
{
	int i;
	int nthreads = 0;
	pthread_t threads[WWN_IOS_MAX_SHELL_JOBS];

	pthread_mutex_lock(&ios_shell_jobs_lock);
	for (i = 0; i < WWN_IOS_MAX_SHELL_JOBS; i++) {
		ios_close_fd(&ios_shell_jobs[i].pace_read_fd);
		ios_close_fd(&ios_shell_jobs[i].slave_fd);
		ios_close_fd(&ios_shell_jobs[i].input_read_fd);
		ios_close_fd(&ios_shell_jobs[i].input_write_fd);
		if (ios_shell_jobs[i].running && ios_shell_jobs[i].fake_pid != 0)
			threads[nthreads++] = ios_shell_jobs[i].thread;
	}
	if (ios_shell_io_active && nthreads == 0)
		threads[nthreads++] = ios_shell_io_thread;
	pthread_mutex_unlock(&ios_shell_jobs_lock);

	if (nthreads > 0 && ios_shell_io_active)
		(void)pthread_kill(ios_shell_io_thread, SIGINT);

	for (i = 0; i < nthreads; i++) {
		void *rv;

		pthread_join(threads[i], &rv);
	}

	for (i = 0; i < WWN_IOS_MAX_PTYS; i++) {
		if (!ios_ptys[i].live)
			continue;
		ios_close_fd(&ios_ptys[i].input_read);
		ios_close_fd(&ios_ptys[i].input_write);
		ios_ptys[i].live = 0;
		ios_ptys[i].master = -1;
		ios_ptys[i].slave = -1;
	}
	ios_pty_input_read = -1;
	ios_pty_input_write = -1;

	WWN_PTY_LOG("wwn_pty: stop_shell_session, joined %d shell thread(s)\n",
	            nthreads);
	wwn_pty_ios_allow_new_shell_session();
}

static struct wwn_ios_shell_job *
ios_shell_job_alloc(void)
{
	int i;
	int have_zsh = 0;

	pthread_mutex_lock(&ios_shell_jobs_lock);
	for (i = 0; i < WWN_IOS_MAX_SHELL_JOBS; i++) {
		if (ios_shell_jobs[i].running && !ios_shell_jobs[i].nested)
			have_zsh = 1;
	}
	for (i = 0; i < WWN_IOS_MAX_SHELL_JOBS; i++) {
		if (!ios_shell_jobs[i].running && ios_shell_jobs[i].fake_pid == 0) {
			memset(&ios_shell_jobs[i], 0, sizeof ios_shell_jobs[i]);
			ios_shell_jobs[i].fake_pid = ++ios_next_fake_pid;
			ios_shell_jobs[i].running = 1;
			ios_shell_jobs[i].nested = have_zsh;
			ios_shell_jobs[i].input_write_fd = -1;
			pthread_mutex_unlock(&ios_shell_jobs_lock);
			return &ios_shell_jobs[i];
		}
	}
	pthread_mutex_unlock(&ios_shell_jobs_lock);
	errno = EAGAIN;
	WWN_PTY_LOG("wwn_pty: no free shell job slot (max %d)\n",
	            WWN_IOS_MAX_SHELL_JOBS);
	return NULL;
}

static struct wwn_ios_shell_job *
ios_shell_job_find(pid_t fake_pid)
{
	int i;

	for (i = 0; i < WWN_IOS_MAX_SHELL_JOBS; i++) {
		if (ios_shell_jobs[i].fake_pid == fake_pid)
			return &ios_shell_jobs[i];
	}
	return NULL;
}

static int
ios_any_shell_running(void)
{
	int i;
	int running = 0;

	pthread_mutex_lock(&ios_shell_jobs_lock);
	for (i = 0; i < WWN_IOS_MAX_SHELL_JOBS; i++) {
		if (ios_shell_jobs[i].running) {
			running = 1;
			break;
		}
	}
	pthread_mutex_unlock(&ios_shell_jobs_lock);
	return running;
}

static int
ios_deliver_shell_tty_signal(unsigned char byte)
{
	int sig = 0;
	int i;
	pthread_t target = 0;
	int found = 0;
	int inject_fd;
	int pipe_only = 0;

	if (byte == 3)
		sig = SIGINT;
	else if (byte == 28)
		sig = SIGQUIT;
	else
		return 0;

	pthread_mutex_lock(&ios_terminal_master_lock);
	inject_fd = ios_pty_input_write;
	pthread_mutex_unlock(&ios_terminal_master_lock);

	pthread_mutex_lock(&ios_shell_jobs_lock);
	for (i = 0; i < WWN_IOS_MAX_SHELL_JOBS; i++) {
		if (!ios_shell_jobs[i].running)
			continue;
		if (inject_fd >= 0 && ios_shell_jobs[i].input_write_fd == inject_fd) {
			if (ios_shell_jobs[i].nested)
				pipe_only = 1;
			else {
				target = ios_shell_jobs[i].thread;
				found = 1;
			}
			break;
		}
	}
	if (!found && !pipe_only && ios_shell_io_active) {
		target = ios_shell_io_thread;
		found = 1;
	}
	pthread_mutex_unlock(&ios_shell_jobs_lock);

	if (pipe_only)
		return 0;

	if (!found)
		return 0;

	(void)pthread_kill(target, sig);
	WWN_PTY_LOG("wwn_pty: delivered signal %d for control byte 0x%02x\n", sig, byte);
	return 1;
}

static ssize_t
ios_terminal_inject_bytes(const unsigned char *bytes, size_t len)
{
	ssize_t total = 0;
	int fd;

	if (bytes == NULL || len == 0) {
		errno = EINVAL;
		return -1;
	}

	pthread_mutex_lock(&ios_terminal_master_lock);
	fd = ios_pty_input_write;
	pthread_mutex_unlock(&ios_terminal_master_lock);
	if (fd < 0) {
		errno = ENXIO;
		return -1;
	}

	for (size_t i = 0; i < len; i++) {
		unsigned char byte = bytes[i];

		if (byte == '\n')
			byte = '\r';

		if (ios_deliver_shell_tty_signal(byte)) {
			total++;
			continue;
		}

		pthread_mutex_lock(&ios_terminal_master_lock);
		fd = ios_pty_input_write;
		if (fd < 0) {
			pthread_mutex_unlock(&ios_terminal_master_lock);
			if (total > 0)
				return total;
			errno = ENXIO;
			return -1;
		}
		for (;;) {
			ssize_t n = write(fd, &byte, 1);

			if (n == 1) {
				total++;
				break;
			}
			if (n < 0 && errno == EINTR)
				continue;
			pthread_mutex_unlock(&ios_terminal_master_lock);
			if (total > 0)
				return total;
			return -1;
		}
		pthread_mutex_unlock(&ios_terminal_master_lock);
	}

	if (total > 0)
		WWN_PTY_LOG("wwn_pty: injected %zd bytes to shell stdin (fd=%d)\n", total,
		            fd);
	return total;
}

static void
ios_apply_envp(char *const envp[])
{
	int i;

	if (envp == NULL)
		return;
	for (i = 0; envp[i] != NULL; i++) {
		const char *eq = strchr(envp[i], '=');
		char *key;

		if (eq == NULL || eq == envp[i])
			continue;
		key = strndup(envp[i], (size_t)(eq - envp[i]));
		if (key == NULL)
			continue;
		setenv(key, eq + 1, 1);
		free(key);
	}
}

static void
ios_broadcast_sigwinch(void)
{
	int i;

	pthread_mutex_lock(&ios_shell_jobs_lock);
	for (i = 0; i < WWN_IOS_MAX_SHELL_JOBS; i++) {
		if (!ios_shell_jobs[i].running)
			continue;
		(void)pthread_kill(ios_shell_jobs[i].thread, SIGWINCH);
	}
	pthread_mutex_unlock(&ios_shell_jobs_lock);
}

static volatile sig_atomic_t ios_shell_init_done;
static volatile sig_atomic_t ios_shell_winch_pending;

static void
ios_defer_or_send_winch(void)
{
	if (!ios_shell_init_done) {
		ios_shell_winch_pending = 1;
		return;
	}
	ios_broadcast_sigwinch();
}

void
wwn_pty_ios_signal_shells(void)
{
	ios_defer_or_send_winch();
}

void
wwn_pty_ios_note_init_io(void)
{
	WWN_PTY_LOG(
	        "wwn_pty: init_io routed shell tty (SHTTY=stdin pipe, shout=stdout, USEZLE on)\n");
}

void
wwn_pty_ios_shell_init_done(void)
{
	sigset_t mask, pending;
	int sig;

	ios_shell_init_done = 1;
	WWN_PTY_LOG("wwn_pty: zsh init scripts finished\n");

	sigemptyset(&mask);
	sigaddset(&mask, SIGWINCH);
	(void)pthread_sigmask(SIG_UNBLOCK, &mask, NULL);

	/* Drop any SIGWINCH that arrived while init ran; winsize is already set
	 * via wwn_pty_tty_shim_set_winsize before the shell started. */
	while (sigpending(&pending) == 0 && sigismember(&pending, SIGWINCH)) {
		if (sigwait(&mask, &sig) != 0 || sig != SIGWINCH)
			break;
	}
	ios_shell_winch_pending = 0;
}

static void
ios_shell_sanitize_env(void)
{
	unsetenv("ZSH");
}

static void *
ios_zsh_thread(void *arg)
{
	struct wwn_ios_shell_job *job = arg;
	char tmp;
	ssize_t n;
	int in_fd;

	if (job->pace_read_fd >= 0) {
		int pace_fd;

		do {
			n = read(job->pace_read_fd, &tmp, 1);
		} while (n < 0 && errno == EINTR);
		pthread_mutex_lock(&ios_shell_jobs_lock);
		pace_fd = job->pace_read_fd;
		job->pace_read_fd = -1;
		pthread_mutex_unlock(&ios_shell_jobs_lock);
		if (pace_fd >= 0)
			close(pace_fd);
		WWN_PTY_LOG("wwn_pty: pace unblocked, starting in-process zsh\n");
	}

	ios_shell_io_thread = pthread_self();
	ios_shell_io_active = 1;

	{
		int shell_out_fd = job->slave_fd;

		if (job->input_read_fd >= 0)
			in_fd = dup2(job->input_read_fd, STDIN_FILENO);
		else if (shell_out_fd >= 0)
			in_fd = dup2(shell_out_fd, STDIN_FILENO);
		if (in_fd < 0)
			WWN_PTY_LOG("wwn_pty: dup2 shell stdin failed (%s)\n",
			        strerror(errno));
		if (job->input_read_fd >= 0 && job->input_read_fd > STDERR_FILENO)
			close(job->input_read_fd);
		job->input_read_fd = -1;

		if (shell_out_fd >= 0) {
			int app_log_fd = wwn_app_log_fd();

			if (dup2(shell_out_fd, STDOUT_FILENO) < 0)
				WWN_PTY_LOG("wwn_pty: dup2 shell stdout failed (%s)\n",
				        strerror(errno));
			if (app_log_fd >= 0 &&
			    dup2(app_log_fd, STDERR_FILENO) < 0)
				WWN_PTY_LOG("wwn_pty: dup2 shell stderr(app log) failed (%s)\n",
				        strerror(errno));
			if (shell_out_fd > STDERR_FILENO)
				close(shell_out_fd);
			job->slave_fd = -1;
		}
	}

	setvbuf(stdin, NULL, _IONBF, 0);
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);
	WWN_PTY_LOG(
	        "wwn_pty: shell stdio wired stdin=%d stdout=%d stderr=%d(app log)\n",
	        STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO);

	{
		const char *home = getenv("HOME");

		if (home && home[0])
			(void)chdir(home);
	}

	ios_shell_sanitize_env();

	ios_shell_init_done = 0;
	ios_shell_winch_pending = 0;
	{
		sigset_t mask;

		sigemptyset(&mask);
		sigaddset(&mask, SIGWINCH);
		(void)pthread_sigmask(SIG_BLOCK, &mask, NULL);
	}

	{
		int argc;

		for (argc = 0; job->argv[argc] != NULL && argc < 11; argc++)
			;
		WWN_PTY_LOG(
		        "wwn_pty: starting in-process zsh wawona_zsh_main argc=%d argv0=%s\n",
		        argc, (argc > 0 && job->argv[0]) ? job->argv[0] : "(none)");
		wwn_zsh_exit_armed = 1;
		wwn_zsh_soft_exit_code = 0;
		if (setjmp(wwn_zsh_exit_jmp) == 0)
			job->exit_code = wawona_zsh_main(argc, job->argv);
		else
			job->exit_code = wwn_zsh_soft_exit_code;
		wwn_zsh_exit_armed = 0;
		WWN_PTY_LOG("wwn_pty: zsh exited with status %d\n", job->exit_code);
	}

	ios_shell_io_active = 0;
	job->running = 0;
	return NULL;
}

static void
ios_nested_write_all(int fd, const char *s)
{
	size_t n;
	size_t off = 0;

	if (fd < 0 || s == NULL)
		return;
	n = strlen(s);
	while (off < n) {
		ssize_t w = write(fd, s + off, n - off);

		if (w <= 0)
			break;
		off += (size_t)w;
	}
}

static void *
ios_nested_shell_thread(void *arg)
{
	struct wwn_ios_shell_job *job = arg;
	char line[1024];
	size_t llen = 0;
	/* zsh_main is not re-entrant, so extra PTYs cannot run ZLE. Match the
	 * iOS .zshrc prompt (`PROMPT='%F{cyan}%~%f %# '`) instead of a product
	 * label. Home is the sandbox cwd for this fallback. */
	const char *prompt = "\033[36m~\033[0m % ";

	WWN_PTY_LOG("wwn_pty: nested in-process shell pid=%d (zsh stays on first PTY)\n",
	            (int)job->fake_pid);
	ios_nested_write_all(job->slave_fd, prompt);

	for (;;) {
		unsigned char b;
		ssize_t n;

		if (job->input_read_fd < 0)
			break;
		n = read(job->input_read_fd, &b, 1);
		if (n < 0 && errno == EINTR)
			continue;
		if (n <= 0)
			break;
		if (b == 3) {
			ios_nested_write_all(job->slave_fd, "^C\r\n");
			llen = 0;
			ios_nested_write_all(job->slave_fd, prompt);
			continue;
		}
		if (b == '\r' || b == '\n') {
			char *argv[32];
			int argc = 0;
			char *tok;
			char *save = NULL;
			int rc;
			char copy[1024];

			ios_nested_write_all(job->slave_fd, "\r\n");
			line[llen] = '\0';
			llen = 0;
			while (line[0] == ' ' || line[0] == '\t')
				memmove(line, line + 1, strlen(line));
			if (line[0] == '\0') {
				ios_nested_write_all(job->slave_fd, prompt);
				continue;
			}
			if (strcmp(line, "exit") == 0 || strcmp(line, "logout") == 0)
				break;

			memcpy(copy, line, strlen(line) + 1);
			tok = strtok_r(copy, " \t", &save);
			while (tok && argc < 31) {
				argv[argc++] = tok;
				tok = strtok_r(NULL, " \t", &save);
			}
			argv[argc] = NULL;
			if (argc == 0) {
				ios_nested_write_all(job->slave_fd, prompt);
				continue;
			}

			pthread_mutex_lock(&ios_stdio_lock);
			{
				int saved_in = dup(STDIN_FILENO);
				int saved_out = dup(STDOUT_FILENO);
				int saved_err = dup(STDERR_FILENO);
				int in_fd = dup(job->input_read_fd);
				int out_fd = dup(job->slave_fd);

				if (saved_in >= 0 && saved_out >= 0 && saved_err >= 0 &&
				    in_fd >= 0 && out_fd >= 0) {
					dup2(in_fd, STDIN_FILENO);
					dup2(out_fd, STDOUT_FILENO);
					dup2(out_fd, STDERR_FILENO);
					rc = wawona_dispatch_inprocess(argv[0], argv, NULL);
					dup2(saved_in, STDIN_FILENO);
					dup2(saved_out, STDOUT_FILENO);
					dup2(saved_err, STDERR_FILENO);
				} else {
					rc = WWN_DISPATCH_NOT_HANDLED;
				}
				if (in_fd >= 0)
					close(in_fd);
				if (out_fd >= 0)
					close(out_fd);
				if (saved_in >= 0)
					close(saved_in);
				if (saved_out >= 0)
					close(saved_out);
				if (saved_err >= 0)
					close(saved_err);
			}
			pthread_mutex_unlock(&ios_stdio_lock);

			if (rc == WWN_DISPATCH_NOT_HANDLED) {
				char msg[160];

				snprintf(msg, sizeof msg, "wawona: %s: not found\r\n",
				         argv[0]);
				ios_nested_write_all(job->slave_fd, msg);
			}
			ios_nested_write_all(job->slave_fd, prompt);
			continue;
		}
		if (b == 8 || b == 127) {
			if (llen > 0) {
				llen--;
				ios_nested_write_all(job->slave_fd, "\b \b");
			}
			continue;
		}
		if (b < 32)
			continue;
		if (llen + 1 < sizeof line) {
			line[llen++] = (char)b;
			(void)write(job->slave_fd, &b, 1);
		}
	}

	ios_close_fd(&job->pace_read_fd);
	ios_close_fd(&job->slave_fd);
	ios_close_fd(&job->input_read_fd);
	job->running = 0;
	return NULL;
}

static void
ios_pty_retarget_focus(void)
{
	int i;

	pthread_mutex_lock(&ios_terminal_master_lock);
	if (ios_pty_input_write < 0) {
		for (i = 0; i < WWN_IOS_MAX_PTYS; i++) {
			if (!ios_ptys[i].live || ios_ptys[i].input_write < 0)
				continue;
			ios_pty_focus_slot(&ios_ptys[i]);
			ios_terminal_master = ios_ptys[i].master;
			break;
		}
	}
	pthread_mutex_unlock(&ios_terminal_master_lock);
}

static void
ios_pty_release_slot(struct ios_pty_slot *slot)
{
	int wfd;

	if (slot == NULL || !slot->live)
		return;
	wfd = slot->input_write;
	ios_close_fd(&slot->input_read);
	ios_close_fd(&slot->input_write);
	slot->live = 0;
	slot->master = -1;
	slot->slave = -1;
	pthread_mutex_lock(&ios_terminal_master_lock);
	if (ios_pty_input_write == wfd) {
		ios_pty_input_write = -1;
		ios_pty_input_read = -1;
	}
	pthread_mutex_unlock(&ios_terminal_master_lock);
	ios_pty_retarget_focus();
}

static pid_t
ios_spawn_zsh_inprocess(const char *shell_path, char *const argv[], int slave_fd,
                        int pace_read_fd, char *const envp[])
{
	struct wwn_ios_shell_job *job;
	char *spawn_argv[3];
	int pace_fd;
	int shell_fd;
	int i;
	pthread_attr_t zsh_attr;
	pthread_attr_t *zsh_attrp = NULL;

	(void)argv;
	job = ios_shell_job_alloc();
	if (job == NULL)
		return -1;
	job->pace_read_fd = -1;
	job->slave_fd = -1;
	job->input_read_fd = -1;
	job->input_write_fd = -1;

	/*
	 * zsh is statically linked as wawona_zsh_main; never dlopen on iOS
	 * (banned, and unnecessary). The shell thread calls wawona_zsh_main
	 * directly. A second PTY (weston-terminal while Foot is up) uses the
	 * nested dispatch loop because zsh_main is not re-entrant.
	 */
	job->dylib = NULL;

	/*
	 * Launch as an interactive login shell ("-zsh -i"). The leading dash
	 * makes zsh run its full login+interactive init in order (so
	 * .zshenv/.zshrc/.zlogin all run); the explicit "-i" forces interactive
	 * mode regardless of isatty(). Without "-i", zsh decides interactivity
	 * from isatty(0)/isatty(2); on the socketpair fallback those fds are not
	 * real ttys, so any hiccup in the fake-TTY shim drops zsh into a
	 * non-interactive script reader (banner prints, but no prompt, no ZLE,
	 * no echo). "-i" makes the prompt + line editor unconditional.
	 */
	(void)shell_path;
	if (!job->nested) {
		spawn_argv[0] = (char *)"-zsh";
		spawn_argv[1] = (char *)"-i";
		spawn_argv[2] = NULL;

		for (i = 0; spawn_argv[i] != NULL && i < 11; i++) {
			job->argv_storage[i] = strdup(spawn_argv[i]);
			job->argv[i] = job->argv_storage[i];
		}
		job->argv[i] = NULL;
		WWN_PTY_LOG(
		        "wwn_pty: zsh spawn mode: in-process wawona_zsh_main (argv0=-zsh)\n");
	} else {
		WWN_PTY_LOG(
		        "wwn_pty: nested spawn (Foot zsh already running, pid=%d)\n",
		        (int)job->fake_pid);
	}

	/*
	 * pthread shares the fd table with terminal_run's caller. Parent closes
	 * pipes[0] (pace) and slave_fd immediately after spawn; dup so the shell
	 * thread keeps valid endpoints (fork() used to give the child its own table).
	 */
	pace_fd = pace_read_fd;
	shell_fd = slave_fd;
	if (pace_fd >= 0) {
		int fd = dup(pace_fd);

		if (fd < 0)
			goto spawn_fail;
		pace_fd = fd;
	}
	if (shell_fd >= 0) {
		int fd = dup(shell_fd);

		if (fd < 0) {
			if (pace_fd >= 0 && pace_fd != pace_read_fd)
				close(pace_fd);
			goto spawn_fail;
		}
		shell_fd = fd;
	}
	{
		struct ios_pty_slot *slot = ios_pty_slot_by_slave(slave_fd);

		if (slot != NULL && slot->input_read >= 0) {
			int old_read = slot->input_read;
			int fd = dup(old_read);

			if (fd < 0) {
				if (pace_fd >= 0 && pace_fd != pace_read_fd)
					close(pace_fd);
				if (shell_fd >= 0 && shell_fd != slave_fd)
					close(shell_fd);
				goto spawn_fail;
			}
			job->input_read_fd = fd;
			job->input_write_fd = slot->input_write;
			close(old_read);
			slot->input_read = -1;
			pthread_mutex_lock(&ios_terminal_master_lock);
			if (ios_pty_input_read == old_read)
				ios_pty_input_read = -1;
			pthread_mutex_unlock(&ios_terminal_master_lock);
		}
	}

	job->slave_fd = shell_fd;
	job->pace_read_fd = pace_fd;

	/*
	 * In-process zsh shares this address space. WWNRootfsManager and
	 * terminal.c already setenv() before spawn. Re-applying environ[] here
	 * used to mutate those strings and setenv() could reallocate environ,
	 * leaving dangling pointers (EXC_BAD_ACCESS in strchr).
	 */
	if (envp != NULL && getenv("WAWONA_ZSH_IN_PROCESS") == NULL)
		ios_apply_envp(envp);

	/*
	 * zsh's recursive descent parser and evaluator (par_list -> execlist ->
	 * execpline -> execcmd_exec -> doshfunc -> execode) normally run on the
	 * process main thread, which has a multi-megabyte stack. A secondary
	 * pthread defaults to only 512 KB on iOS, which overflows while parsing
	 * and executing large, deeply nested init scripts (e.g. compinit and the
	 * completion system), corrupting adjacent memory and aborting in malloc.
	 * Give the shell thread a generous stack so it behaves like a normal
	 * login shell.
	 */
	if (pthread_attr_init(&zsh_attr) == 0) {
		if (pthread_attr_setstacksize(&zsh_attr,
		                              (size_t)16 * 1024 * 1024) == 0)
			zsh_attrp = &zsh_attr;
	}
	i = pthread_create(&job->thread, zsh_attrp,
	                   job->nested ? ios_nested_shell_thread : ios_zsh_thread,
	                   job);
	if (zsh_attrp != NULL)
		pthread_attr_destroy(zsh_attrp);
	if (i != 0) {
spawn_fail:
		for (i = 0; job->argv_storage[i] != NULL && i < 11; i++)
			free(job->argv_storage[i]);
		if (job->pace_read_fd >= 0)
			close(job->pace_read_fd);
		if (job->slave_fd >= 0)
			close(job->slave_fd);
		if (job->input_read_fd >= 0)
			close(job->input_read_fd);
		if (job->dylib != NULL)
			dlclose(job->dylib);
		ios_pty_release_slot(ios_pty_slot_by_slave(slave_fd));
		job->fake_pid = 0;
		job->running = 0;
		return -1;
	}

	if (!job->nested) {
		ios_shell_io_thread = job->thread;
		ios_shell_io_active = 1;
	}

	/*
	 * Do NOT fire an asynchronous SIGWINCH at the freshly-started shell.
	 * zsh's SIGWINCH handler runs adjustwinsize() inline when signal
	 * queueing is not active, which sets shell parameters and reallocates;
	 * delivering it during zsh's init burst (e.g. compinit) re-enters malloc
	 * from a signal handler and corrupts the heap ("pointer being freed was
	 * not allocated"). With "-zsh -i" the interactive loop draws the prompt
	 * on its own, and resize_handler signals real size changes later, so the
	 * eager kick is both unnecessary and unsafe.
	 */
	WWN_PTY_LOG("wwn_pty: started in-process %s fake_pid=%d\n",
	            job->nested ? "nested shell" : "zsh", (int)job->fake_pid);
	return job->fake_pid;
}

int
wwn_pty_ios_waitpid(pid_t fake_pid, int *exit_status, int flags)
{
	struct wwn_ios_shell_job *job;
	void *rv;
	int i;

	job = ios_shell_job_find(fake_pid);
	if (job == NULL) {
		errno = ECHILD;
		return -1;
	}
	if (job->running) {
		int wfd;
		int si;

		/*
		 * In-process zsh has no async (SIGCHLD) exit notification, so a
		 * polling WNOHANG caller must see "still running" without
		 * blocking or tearing down stdin.
		 */
		if (flags & WNOHANG)
			return 0;
		/*
		 * Blocking reap (terminal closing): EOF this session's keyboard
		 * pipe so the shell leaves its read loop. Never close another
		 * session's inject fd (that used to kill Foot when weston-terminal
		 * exited).
		 */
		wfd = job->input_write_fd;
		if (wfd >= 0) {
			pthread_mutex_lock(&ios_terminal_master_lock);
			if (ios_pty_input_write == wfd) {
				ios_pty_input_write = -1;
				ios_pty_input_read = -1;
			}
			pthread_mutex_unlock(&ios_terminal_master_lock);
			for (si = 0; si < WWN_IOS_MAX_PTYS; si++) {
				if (ios_ptys[si].live && ios_ptys[si].input_write == wfd) {
					ios_ptys[si].input_write = -1;
					ios_close_fd(&ios_ptys[si].input_read);
					ios_ptys[si].live = 0;
					ios_ptys[si].master = -1;
					ios_ptys[si].slave = -1;
				}
			}
			close(wfd);
			job->input_write_fd = -1;
		}
		pthread_join(job->thread, &rv);
		ios_pty_retarget_focus();
	}
	if (exit_status != NULL)
		*exit_status = job->exit_code;
	for (i = 0; job->argv_storage[i] != NULL && i < 11; i++)
		free(job->argv_storage[i]);
	if (job->dylib != NULL)
		dlclose(job->dylib);
	if (job->input_write_fd >= 0) {
		int wfd = job->input_write_fd;
		int s;

		job->input_write_fd = -1;
		for (s = 0; s < WWN_IOS_MAX_PTYS; s++) {
			if (ios_ptys[s].live && ios_ptys[s].input_write == wfd)
				ios_pty_release_slot(&ios_ptys[s]);
		}
	}
	ios_close_fd(&job->input_read_fd);
	ios_close_fd(&job->slave_fd);
	ios_close_fd(&job->pace_read_fd);
	memset(job, 0, sizeof *job);
	ios_pty_retarget_focus();
	return fake_pid;
}
#endif

static pid_t
spawn_on_slave(const char *shell_path, char *const argv[], int slave_fd,
               int pace_read_fd, char *const envp[])
{
	posix_spawn_file_actions_t actions;
	posix_spawnattr_t attrs;
	pid_t pid;
	char pace_script[512];
	char *spawn_argv[8];
	int i;

	if (!wwn_pty_is_allowed_shell_path(shell_path)) {
		WWN_PTY_LOG("wwn_pty: shell path rejected: %s (in_process=%s)\n",
		        shell_path,
		        getenv("WAWONA_ZSH_IN_PROCESS") ?: "(unset)");
		errno = EPERM;
		return -1;
	}

#if defined(__APPLE__) && (TARGET_OS_IPHONE || TARGET_OS_TV || TARGET_OS_WATCH)
	return ios_spawn_zsh_inprocess(shell_path, argv, slave_fd, pace_read_fd, envp);
#elif defined(__ANDROID__)
	if (argv == NULL || argv[0] == NULL) {
		errno = EINVAL;
		return -1;
	}

	if (pace_read_fd >= 0) {
		snprintf(pace_script, sizeof pace_script,
		         "read -r _ <&3; exec %s", shell_path);
		spawn_argv[0] = (char *)shell_path;
		spawn_argv[1] = (char *)"-c";
		spawn_argv[2] = pace_script;
		spawn_argv[3] = NULL;
	} else {
		for (i = 0; argv[i] != NULL && i < 6; i++)
			spawn_argv[i] = argv[i];
		spawn_argv[i] = NULL;
	}

	if (envp == NULL)
		envp = environ;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0) {
		if (slave_fd >= 0) {
			dup2(slave_fd, STDIN_FILENO);
			dup2(slave_fd, STDOUT_FILENO);
			dup2(slave_fd, STDERR_FILENO);
			if (slave_fd > STDERR_FILENO)
				close(slave_fd);
		}
		if (pace_read_fd >= 0) {
			dup2(pace_read_fd, 3);
			if (pace_read_fd > 3)
				close(pace_read_fd);
		}
		execve(shell_path, spawn_argv, envp);
		_exit(127);
	}
	return pid;
#else
	if (argv == NULL || argv[0] == NULL) {
		errno = EINVAL;
		return -1;
	}

	if (posix_spawn_file_actions_init(&actions) != 0)
		return -1;
	if (posix_spawnattr_init(&attrs) != 0) {
		posix_spawn_file_actions_destroy(&actions);
		return -1;
	}

#if !defined(__APPLE__) || !(TARGET_OS_IPHONE || TARGET_OS_TV || TARGET_OS_WATCH)
	posix_spawnattr_setflags(&attrs, POSIX_SPAWN_SETPGROUP);
	if (posix_spawnattr_setpgroup(&attrs, 0) != 0) {
		posix_spawnattr_destroy(&attrs);
		posix_spawn_file_actions_destroy(&actions);
		return -1;
	}
#endif
	posix_spawn_file_actions_adddup2(&actions, slave_fd, STDIN_FILENO);
	posix_spawn_file_actions_adddup2(&actions, slave_fd, STDOUT_FILENO);
	posix_spawn_file_actions_adddup2(&actions, slave_fd, STDERR_FILENO);
	if (slave_fd > STDERR_FILENO)
		posix_spawn_file_actions_addclose(&actions, slave_fd);

	if (pace_read_fd >= 0) {
		posix_spawn_file_actions_adddup2(&actions, pace_read_fd, 3);
		if (pace_read_fd > 3)
			posix_spawn_file_actions_addclose(&actions, pace_read_fd);
	}

	if (pace_read_fd >= 0) {
		snprintf(pace_script, sizeof pace_script,
		         "read -r _ <&3; exec %s", shell_path);
		spawn_argv[0] = (char *)shell_path;
		spawn_argv[1] = (char *)"-c";
		spawn_argv[2] = pace_script;
		spawn_argv[3] = NULL;
	} else {
		for (i = 0; argv[i] != NULL && i < 6; i++)
			spawn_argv[i] = argv[i];
		spawn_argv[i] = NULL;
	}

	if (envp == NULL)
		envp = environ;

	if (posix_spawn(&pid, shell_path, &actions, &attrs, spawn_argv, envp) != 0) {
		WWN_PTY_LOG("wwn_pty: posix_spawn(%s) failed: %s\n",
		        shell_path, strerror(errno));
		posix_spawnattr_destroy(&attrs);
		posix_spawn_file_actions_destroy(&actions);
		return -1;
	}

	posix_spawnattr_destroy(&attrs);
	posix_spawn_file_actions_destroy(&actions);
	return pid;
#endif
}

pid_t
wwn_pty_spawn_shell(const char *shell_path, char *const argv[], int slave_fd,
                    char *const envp[])
{
	return spawn_on_slave(shell_path, argv, slave_fd, -1, envp);
}

pid_t
wwn_pty_spawn_shell_paced(const char *shell_path, char *const argv[],
                          int slave_fd, int pace_read_fd, char *const envp[])
{
	return spawn_on_slave(shell_path, argv, slave_fd, pace_read_fd, envp);
}

wwn_pty_session *
wwn_pty_session_start(const char *shell_path, char *const argv[],
                      char *const envp[], const struct winsize *ws)
{
	wwn_pty_session *session;
	int slave_fd = -1;

	session = calloc(1, sizeof *session);
	if (session == NULL)
		return NULL;

	if (wwn_pty_open(&session->master_fd, &slave_fd, ws) != 0) {
		free(session);
		return NULL;
	}

	session->pid = wwn_pty_spawn_shell(shell_path, argv, slave_fd, envp);
	close(slave_fd);
	if (session->pid < 0) {
		close(session->master_fd);
		free(session);
		return NULL;
	}

	return session;
}

int
wwn_pty_session_master_fd(const wwn_pty_session *session)
{
	if (session == NULL) {
		errno = EINVAL;
		return -1;
	}
	return session->master_fd;
}

pid_t
wwn_pty_session_pid(const wwn_pty_session *session)
{
	if (session == NULL) {
		errno = EINVAL;
		return -1;
	}
	return session->pid;
}

ssize_t
wwn_pty_read(int master_fd, void *buf, size_t len)
{
	return read(master_fd, buf, len);
}

ssize_t
wwn_pty_write(int master_fd, const void *buf, size_t len)
{
	return write(master_fd, buf, len);
}

int
wwn_pty_set_winsize(int master_fd, const struct winsize *ws)
{
	if (ws == NULL) {
		errno = EINVAL;
		return -1;
	}
#if defined(__APPLE__) && (TARGET_OS_IPHONE || TARGET_OS_TV || TARGET_OS_WATCH)
	wwn_pty_tty_shim_set_winsize(ws);
	ios_broadcast_sigwinch();
#endif
	if (ioctl(master_fd, TIOCSWINSZ, ws) == 0)
		return 0;
#if defined(__APPLE__) && (TARGET_OS_IPHONE || TARGET_OS_TV || TARGET_OS_WATCH)
	/* socketpair I/O fallback is not a TTY; ignore ENOTTY like macOS PTY shims. */
	if (errno == ENOTTY || errno == EINVAL || errno == ENODEV)
		return 0;
#endif
	return -1;
}

int
wwn_pty_reap(wwn_pty_session *session, int *exit_status)
{
	int status;

	if (session == NULL)
		return -1;

	if (session->pid > 0) {
		if (waitpid(session->pid, &status, 0) < 0)
			return -1;
		if (exit_status != NULL) {
			if (WIFEXITED(status))
				*exit_status = WEXITSTATUS(status);
			else
				*exit_status = -1;
		}
		session->pid = -1;
	}

	if (session->master_fd >= 0) {
		close(session->master_fd);
		session->master_fd = -1;
	}

	return 0;
}

void
wwn_pty_session_destroy(wwn_pty_session *session)
{
	if (session == NULL)
		return;
	if (session->pid > 0)
		kill(session->pid, SIGHUP);
	if (session->master_fd >= 0)
		close(session->master_fd);
	free(session);
}

#if defined(__APPLE__) && (TARGET_OS_IPHONE || TARGET_OS_TV || TARGET_OS_WATCH)
void
wwn_ios_terminal_set_master(int master_fd)
{
	struct ios_pty_slot *slot;

	pthread_mutex_lock(&ios_terminal_master_lock);
	ios_terminal_master = master_fd;
	slot = ios_pty_slot_by_master(master_fd);
	if (slot != NULL)
		ios_pty_focus_slot(slot);
	pthread_mutex_unlock(&ios_terminal_master_lock);
}

void
wwn_ios_terminal_clear_master(int master_fd)
{
	int i;

	pthread_mutex_lock(&ios_terminal_master_lock);
	if (ios_terminal_master == master_fd) {
		struct ios_pty_slot *slot = ios_pty_slot_by_master(master_fd);

		ios_terminal_master = -1;
		if (slot != NULL && ios_pty_input_write == slot->input_write) {
			ios_pty_input_write = -1;
			ios_pty_input_read = -1;
		}
		for (i = 0; i < WWN_IOS_MAX_PTYS; i++) {
			if (!ios_ptys[i].live || ios_ptys[i].input_write < 0)
				continue;
			if (ios_ptys[i].master == master_fd)
				continue;
			ios_pty_focus_slot(&ios_ptys[i]);
			ios_terminal_master = ios_ptys[i].master;
			break;
		}
	}
	pthread_mutex_unlock(&ios_terminal_master_lock);
}

int
wwn_ios_terminal_is_active(void)
{
	int active;

	pthread_mutex_lock(&ios_terminal_master_lock);
	active = ios_pty_input_write >= 0 || ios_terminal_master >= 0;
	pthread_mutex_unlock(&ios_terminal_master_lock);
	if (active)
		return 1;
	return ios_any_shell_running();
}

ssize_t
wwn_ios_terminal_inject(const void *buf, size_t len)
{
	const unsigned char *bytes = buf;
	unsigned char filtered[256];

	if (buf == NULL || len == 0) {
		errno = EINVAL;
		return -1;
	}

	if (len <= sizeof filtered) {
		memcpy(filtered, bytes, len);
		bytes = filtered;
	}

	return ios_terminal_inject_bytes(bytes, len);
}

void
wwn_pty_ios_kick_shell_display(void)
{
	ios_defer_or_send_winch();
}
#endif

#if defined(__APPLE__) && (TARGET_OS_IPHONE || TARGET_OS_TV || TARGET_OS_WATCH)
#include <stdarg.h>
#include <stdbool.h>

static struct winsize wwn_fake_winsize = { .ws_row = 24, .ws_col = 80 };
static struct termios wwn_fake_termios;
static bool wwn_fake_termios_init;

void
wwn_pty_tty_shim_set_winsize(const struct winsize *ws)
{
	if (ws != NULL)
		wwn_fake_winsize = *ws;
}

int
wwn_pty_tty_shim_get_winsize(struct winsize *ws)
{
	if (ws == NULL) {
		errno = EINVAL;
		return -1;
	}
	*ws = wwn_fake_winsize;
	return 0;
}

static int
wwn_fake_tty_active(void)
{
	return ios_fake_tty_forced || getenv("WAWONA_PTY_FAKE_TTY") != NULL;
}

static int
wwn_is_stdio_fd(int fd)
{
	return fd == STDIN_FILENO || fd == STDOUT_FILENO || fd == STDERR_FILENO;
}

static void
wwn_init_fake_termios(void)
{
	if (wwn_fake_termios_init)
		return;
	memset(&wwn_fake_termios, 0, sizeof wwn_fake_termios);
	/*
	 * Believable interactive cooked-mode defaults. zsh's ZLE calls
	 * tcgetattr() to snapshot this, then tcsetattr() to switch to raw mode
	 * (clearing ICANON/ECHO/ISIG) for line editing; wwn_tcsetattr stores
	 * whatever ZLE writes and wwn_tcgetattr replays it, so ZLE fully drives
	 * the fake TTY. Local echo here is irrelevant for real zsh (ZLE renders
	 * the line); it only matters that the flags round-trip faithfully.
	 */
	wwn_fake_termios.c_iflag = BRKINT | ICRNL | IXON | IXANY | IMAXBEL;
	wwn_fake_termios.c_oflag = OPOST | ONLCR;
	wwn_fake_termios.c_cflag = CS8 | CREAD | HUPCL;
	wwn_fake_termios.c_lflag =
	    ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOKE | ECHOCTL | IEXTEN;
	wwn_fake_termios.c_cc[VEOF] = 4;      /* ^D */
	wwn_fake_termios.c_cc[VEOL] = 0;
	wwn_fake_termios.c_cc[VERASE] = 0177; /* DEL */
	wwn_fake_termios.c_cc[VWERASE] = 23;  /* ^W */
	wwn_fake_termios.c_cc[VKILL] = 21;    /* ^U */
	wwn_fake_termios.c_cc[VREPRINT] = 18; /* ^R */
	wwn_fake_termios.c_cc[VINTR] = 3;     /* ^C */
	wwn_fake_termios.c_cc[VQUIT] = 28;    /* ^\ */
	wwn_fake_termios.c_cc[VSUSP] = 26;    /* ^Z */
	wwn_fake_termios.c_cc[VSTART] = 17;   /* ^Q */
	wwn_fake_termios.c_cc[VSTOP] = 19;    /* ^S */
	wwn_fake_termios.c_cc[VLNEXT] = 22;   /* ^V */
	wwn_fake_termios.c_cc[VDISCARD] = 15; /* ^O */
	wwn_fake_termios.c_cc[VMIN] = 1;
	wwn_fake_termios.c_cc[VTIME] = 0;
	cfsetispeed(&wwn_fake_termios, B38400);
	cfsetospeed(&wwn_fake_termios, B38400);
	wwn_fake_termios_init = true;
}

static ssize_t
wwn_read(int fd, void *buf, size_t count)
{
	ssize_t (*real_read)(int, void *, size_t);
	ssize_t n;

	real_read = (ssize_t (*)(int, void *, size_t))dlsym(RTLD_NEXT, "read");
	if (real_read == NULL) {
		static int wwn_read_interpose_warned;

		if (!wwn_read_interpose_warned) {
			WWN_PTY_LOG("wwn_pty: dlsym(RTLD_NEXT, read) failed: %s\n",
			        dlerror() != NULL ? dlerror() : "unknown");
			wwn_read_interpose_warned = 1;
		}
		errno = ENOSYS;
		return -1;
	}

	n = real_read(fd, buf, count);
	if (wwn_fake_tty_active() && ios_shell_io_active &&
	    pthread_equal(pthread_self(), ios_shell_io_thread) && n > 0 &&
	    (fd == STDIN_FILENO || fd == STDOUT_FILENO)) {
		unsigned char first = ((unsigned char *)buf)[0];

		WWN_PTY_LOG("wwn_pty: shell read %zd bytes from fd=%d first=0x%02x\n", n,
		            fd, first);
	}
	return n;
}

static ssize_t
wwn_write(int fd, const void *buf, size_t count)
{
	ssize_t (*real_write)(int, const void *, size_t);
	ssize_t ret;

	real_write = (ssize_t (*)(int, const void *, size_t))dlsym(RTLD_NEXT, "write");
	if (real_write == NULL) {
		errno = ENOSYS;
		return -1;
	}

	if (wwn_fake_tty_active() && (fd == STDOUT_FILENO || fd == STDERR_FILENO) && count > 0) {
		/*
		 * Always expand LF→CRLF for in-process shell stdout. zsh ZLE clears
		 * ONLCR via tcsetattr; without this, weston-terminal receives bare
		 * \\n and keeps the cursor column (stair-stepped/indented lines).
		 */
		int do_onlcr = (fd == STDOUT_FILENO && ios_shell_io_active);
		if (!do_onlcr && wwn_fake_termios_init &&
		    (wwn_fake_termios.c_oflag & OPOST) && (wwn_fake_termios.c_oflag & ONLCR))
			do_onlcr = 1;
		if (do_onlcr) {
			const char *p = (const char *)buf;
			size_t i, last = 0;
			ssize_t total = 0;
			for (i = 0; i < count; i++) {
				if (p[i] == '\n') {
					if (i > last) {
						ret = real_write(fd, p + last, i - last);
						if (ret < 0) return (total > 0 ? total : -1);
						total += ret;
					}
					ret = real_write(fd, "\r\n", 2);
					if (ret < 0) return (total > 0 ? total : -1);
					total += 1; /* report original \n byte count */
					last = i + 1;
				}
			}
			if (last < count) {
				ret = real_write(fd, p + last, count - last);
				if (ret < 0) return (total > 0 ? total : -1);
				total += ret;
			}
			return total;
		}
	}

	return real_write(fd, buf, count);
}

static int
wwn_isatty(int fd)
{
	if (wwn_fake_tty_active() && wwn_is_stdio_fd(fd))
		return 1;
	{
		int (*real)(int) = (int (*)(int))dlsym(RTLD_NEXT, "isatty");
		if (real != NULL)
			return real(fd);
	}
	errno = ENOTTY;
	return 0;
}

static int
wwn_tcgetattr(int fd, struct termios *termios_p)
{
	if (wwn_fake_tty_active() && wwn_is_stdio_fd(fd) && termios_p != NULL) {
		wwn_init_fake_termios();
		*termios_p = wwn_fake_termios;
		return 0;
	}
	{
		int (*real)(int, struct termios *) =
			(int (*)(int, struct termios *))dlsym(RTLD_NEXT, "tcgetattr");
		if (real != NULL)
			return real(fd, termios_p);
	}
	errno = ENOTTY;
	return -1;
}

static int
wwn_tcsetattr(int fd, int optional_actions, const struct termios *termios_p)
{
	(void)optional_actions;
	if (wwn_fake_tty_active() && wwn_is_stdio_fd(fd) && termios_p != NULL) {
		wwn_init_fake_termios();
		wwn_fake_termios = *termios_p;
		return 0;
	}
	{
		int (*real)(int, int, const struct termios *) =
			(int (*)(int, int, const struct termios *))dlsym(RTLD_NEXT,
			                                                     "tcsetattr");
		if (real != NULL)
			return real(fd, optional_actions, termios_p);
	}
	errno = ENOTTY;
	return -1;
}

static int
wwn_ioctl(int fd, unsigned long request, ...)
{
	void *arg;
	va_list ap;

	va_start(ap, request);
	arg = va_arg(ap, void *);
	va_end(ap);

	if (request == FIONREAD && arg != NULL) {
		int (*real)(int, unsigned long, ...);

		real = (int (*)(int, unsigned long, ...))dlsym(RTLD_NEXT, "ioctl");
		if (real != NULL)
			return real(fd, request, arg);
	}

	if (wwn_fake_tty_active() && wwn_is_stdio_fd(fd)) {
		if (request == TIOCGWINSZ && arg != NULL) {
			*(struct winsize *)arg = wwn_fake_winsize;
			return 0;
		}
		if (request == TIOCSWINSZ && arg != NULL) {
			wwn_fake_winsize = *(const struct winsize *)arg;
			return 0;
		}
		if (request == TIOCGPGRP && arg != NULL) {
			*(int *)arg = getpgrp();
			return 0;
		}
		if (request == TIOCSPGRP)
			return 0;
#ifdef TIOCGSID
		if (request == TIOCGSID && arg != NULL) {
			*(pid_t *)arg = getpid();
			return 0;
		}
#endif
	}

	{
		int (*real)(int, unsigned long, ...);
		real = (int (*)(int, unsigned long, ...))dlsym(RTLD_NEXT, "ioctl");
		if (real != NULL)
			return real(fd, request, arg);
	}
	errno = ENOTTY;
	return -1;
}

typedef struct {
	const void *replacement;
	const void *replacee;
} wwn_interpose_t;

__attribute__((used)) static const wwn_interpose_t wwn_interpose_read
    __attribute__((section("__DATA,__interpose"))) = {
	.replacement = (const void *)(unsigned long)&wwn_read,
	.replacee = (const void *)(unsigned long)&read,
};

__attribute__((used)) static const wwn_interpose_t wwn_interpose_write
    __attribute__((section("__DATA,__interpose"))) = {
	.replacement = (const void *)(unsigned long)&wwn_write,
	.replacee = (const void *)(unsigned long)&write,
};

__attribute__((used)) static const wwn_interpose_t wwn_interpose_isatty
    __attribute__((section("__DATA,__interpose"))) = {
	.replacement = (const void *)(unsigned long)&wwn_isatty,
	.replacee = (const void *)(unsigned long)&isatty,
};

__attribute__((used)) static const wwn_interpose_t wwn_interpose_tcgetattr
    __attribute__((section("__DATA,__interpose"))) = {
	.replacement = (const void *)(unsigned long)&wwn_tcgetattr,
	.replacee = (const void *)(unsigned long)&tcgetattr,
};

__attribute__((used)) static const wwn_interpose_t wwn_interpose_tcsetattr
    __attribute__((section("__DATA,__interpose"))) = {
	.replacement = (const void *)(unsigned long)&wwn_tcsetattr,
	.replacee = (const void *)(unsigned long)&tcsetattr,
};

__attribute__((used)) static const wwn_interpose_t wwn_interpose_ioctl
    __attribute__((section("__DATA,__interpose"))) = {
	.replacement = (const void *)(unsigned long)&wwn_ioctl,
	.replacee = (const void *)(unsigned long)&ioctl,
};
#endif /* Apple mobile */
