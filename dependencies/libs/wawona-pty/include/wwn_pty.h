#ifndef WWN_PTY_H
#define WWN_PTY_H

#include <sys/types.h>
#include <stddef.h>
#include <termios.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WWN_PTY_API_VERSION 1

typedef struct wwn_pty_session wwn_pty_session;

/* Preserved stderr fd; captured before shell dup2() on fds 0–2. iOS in-process
 * zsh wires STDERR to this fd so NSLog/os_log stays out of weston-terminal. */
int wwn_app_log_fd(void);

#if defined(__APPLE__) && (TARGET_OS_IPHONE || TARGET_OS_TV || TARGET_OS_WATCH)
void wwn_pty_tty_shim_set_winsize(const struct winsize *ws);
void wwn_pty_ios_signal_shells(void);
void wwn_pty_ios_kick_shell_display(void);
void wwn_pty_ios_shell_init_done(void);
void wwn_pty_ios_note_init_io(void);
int wwn_pty_ios_waitpid(pid_t fake_pid, int *exit_status, int flags);
/* Direct PTY writes for iOS soft keyboard (bypasses wl_keyboard). */
void wwn_ios_terminal_set_master(int master_fd);
void wwn_ios_terminal_clear_master(int master_fd);
int wwn_ios_terminal_is_active(void);
ssize_t wwn_ios_terminal_inject(const void *buf, size_t len);

/*
 * In-process external-command dispatch (App Store compliant: no fork/exec).
 *
 * The zsh exec hook calls this before it would otherwise fork/exec an external
 * binary. If argv[0]'s basename is in the in-process safe subset and the
 * statically-linked uutils umbrella provides it, the utility runs in-process
 * and its exit code (>= 0) is returned. Otherwise WWN_DISPATCH_NOT_HANDLED is
 * returned and the caller falls through to its normal not-found handling.
 *
 * envp is advisory: the in-process model shares the host environ, and zsh
 * applies any `VAR=val cmd` assignments to environ before calling us.
 */
#define WWN_DISPATCH_NOT_HANDLED (-1)
int wawona_dispatch_inprocess(const char *path, char *const argv[],
                              char *const envp[]);
/*
 * Predicate used by the zsh exec hook at its fork-decision point: returns 1 if
 * argv0's basename is in the in-process safe subset AND the uutils umbrella is
 * linked in this build. Lets zsh choose the no-fork (builtin-like) path before
 * committing to a fork it must avoid on the sandbox.
 */
int wawona_dispatch_can_handle(const char *argv0);
#endif

int wwn_pty_open(int *master_fd, int *slave_fd, const struct winsize *ws);

int wwn_pty_is_allowed_shell_path(const char *shell_path);

pid_t wwn_pty_spawn_shell(const char *shell_path, char *const argv[],
                          int slave_fd, char *const envp[]);

pid_t wwn_pty_spawn_shell_paced(const char *shell_path, char *const argv[],
                                int slave_fd, int pace_read_fd,
                                char *const envp[]);

wwn_pty_session *wwn_pty_session_start(const char *shell_path,
                                       char *const argv[],
                                       char *const envp[],
                                       const struct winsize *ws);

ssize_t wwn_pty_read(int master_fd, void *buf, size_t len);
ssize_t wwn_pty_write(int master_fd, const void *buf, size_t len);

int wwn_pty_set_winsize(int master_fd, const struct winsize *ws);

int wwn_pty_reap(wwn_pty_session *session, int *exit_status);

void wwn_pty_session_destroy(wwn_pty_session *session);

#ifdef __cplusplus
}
#endif

#endif /* WWN_PTY_H */
