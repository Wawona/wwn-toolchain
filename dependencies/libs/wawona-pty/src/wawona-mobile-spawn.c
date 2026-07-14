/*
 * wawona-mobile-spawn.c — in-process posix_spawn for App Store–compliant Apple
 * mobile builds.  Compositors such as niri spawn helpers (fuzzel) via
 * posix_spawn/posix_spawnp; iOS cannot exec bundled binaries, so we intercept
 * spawn requests for tools the in-process dispatcher knows about and run them
 * on a pthread instead, returning a synthetic pid that waitpid can reap.
 */
#include "wwn_pty.h"

#if defined(__APPLE__) && (TARGET_OS_IPHONE || TARGET_OS_TV || TARGET_OS_WATCH)

#include <dlfcn.h>
#include <errno.h>
#include <pthread.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define WWN_MOBILE_SPAWN_MAX_JOBS 16
#define WWN_MOBILE_SPAWN_MAX_ARGV 32

struct wwn_mobile_spawn_job {
	pid_t fake_pid;
	pthread_t thread;
	volatile int running;
	int exit_code;
	char *argv_storage[WWN_MOBILE_SPAWN_MAX_ARGV];
	char *argv[WWN_MOBILE_SPAWN_MAX_ARGV];
};

static struct wwn_mobile_spawn_job mobile_spawn_jobs[WWN_MOBILE_SPAWN_MAX_JOBS];
static pthread_mutex_t mobile_spawn_lock = PTHREAD_MUTEX_INITIALIZER;
static pid_t mobile_next_fake_pid = 49000;

static void
wwn_mobile_apply_envp(char *const envp[])
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

static struct wwn_mobile_spawn_job *
wwn_mobile_spawn_job_alloc(void)
{
	int i;

	pthread_mutex_lock(&mobile_spawn_lock);
	for (i = 0; i < WWN_MOBILE_SPAWN_MAX_JOBS; i++) {
		if (!mobile_spawn_jobs[i].running &&
		    mobile_spawn_jobs[i].fake_pid == 0) {
			memset(&mobile_spawn_jobs[i], 0,
			       sizeof mobile_spawn_jobs[i]);
			mobile_spawn_jobs[i].fake_pid = ++mobile_next_fake_pid;
			mobile_spawn_jobs[i].running = 1;
			pthread_mutex_unlock(&mobile_spawn_lock);
			return &mobile_spawn_jobs[i];
		}
	}
	pthread_mutex_unlock(&mobile_spawn_lock);
	return NULL;
}

static struct wwn_mobile_spawn_job *
wwn_mobile_spawn_job_find(pid_t fake_pid)
{
	int i;

	for (i = 0; i < WWN_MOBILE_SPAWN_MAX_JOBS; i++) {
		if (mobile_spawn_jobs[i].fake_pid == fake_pid)
			return &mobile_spawn_jobs[i];
	}
	return NULL;
}

static void
wwn_mobile_spawn_job_cleanup(struct wwn_mobile_spawn_job *job)
{
	int i;

	for (i = 0; i < WWN_MOBILE_SPAWN_MAX_ARGV && job->argv_storage[i] != NULL;
	     i++) {
		free(job->argv_storage[i]);
		job->argv_storage[i] = NULL;
		job->argv[i] = NULL;
	}
	job->fake_pid = 0;
	job->running = 0;
}

static int
wwn_mobile_spawn_copy_argv(struct wwn_mobile_spawn_job *job,
                           char *const argv[])
{
	int i;

	for (i = 0; argv != NULL && argv[i] != NULL &&
	            i < WWN_MOBILE_SPAWN_MAX_ARGV - 1;
	     i++) {
		job->argv_storage[i] = strdup(argv[i]);
		if (job->argv_storage[i] == NULL)
			return -1;
		job->argv[i] = job->argv_storage[i];
	}
	job->argv[i] = NULL;
	return 0;
}

static void *
wwn_mobile_spawn_thread(void *arg)
{
	struct wwn_mobile_spawn_job *job = arg;

	job->exit_code = wawona_dispatch_inprocess(
	    job->argv[0], job->argv, NULL);
	if (job->exit_code == WWN_DISPATCH_NOT_HANDLED)
		job->exit_code = 127;
	fflush(stdout);
	fflush(stderr);
	pthread_mutex_lock(&mobile_spawn_lock);
	wwn_mobile_spawn_job_cleanup(job);
	pthread_mutex_unlock(&mobile_spawn_lock);
	return NULL;
}

static int
wwn_mobile_spawn_try_inprocess(pid_t *pid, const char *path,
                               char *const argv[], char *const envp[])
{
	struct wwn_mobile_spawn_job *job;
	const char *probe;
	int i;

	probe = (argv != NULL && argv[0] != NULL) ? argv[0] : path;
	if (probe == NULL || !wawona_dispatch_can_handle(probe))
		return 0;

	job = wwn_mobile_spawn_job_alloc();
	if (job == NULL) {
		errno = EAGAIN;
		return -1;
	}

	for (i = 0; argv != NULL && argv[i] != NULL &&
	            i < WWN_MOBILE_SPAWN_MAX_ARGV - 1;
	     i++) {
		job->argv_storage[i] = strdup(argv[i]);
		if (job->argv_storage[i] == NULL)
			goto fail;
		job->argv[i] = job->argv_storage[i];
	}
	job->argv[i] = NULL;

	wwn_mobile_apply_envp(envp);

	if (pthread_create(&job->thread, NULL, wwn_mobile_spawn_thread,
	                   job) != 0) {
fail:
		pthread_mutex_lock(&mobile_spawn_lock);
		wwn_mobile_spawn_job_cleanup(job);
		pthread_mutex_unlock(&mobile_spawn_lock);
		errno = EAGAIN;
		return -1;
	}

	*pid = job->fake_pid;
	fprintf(stderr,
	        "wawona-mobile-spawn: started in-process '%s' fake_pid=%d\n",
	        probe, (int)job->fake_pid);
	return 1;
}

/*
 * Start a bundled helper on a background pthread without blocking the caller.
 * Used by niri spawn_sync on Apple mobile so fuzzel's Wayland loop does not
 * freeze the compositor thread.  The job slot is reclaimed when the thread exits.
 */
int
wawona_dispatch_spawn_async(const char *path, char *const argv[],
                            char *const envp[])
{
	struct wwn_mobile_spawn_job *job;
	const char *probe;
	int i;

	probe = (argv != NULL && argv[0] != NULL) ? argv[0] : path;
	if (probe == NULL || !wawona_dispatch_can_handle(probe))
		return WWN_DISPATCH_NOT_HANDLED;

	job = wwn_mobile_spawn_job_alloc();
	if (job == NULL) {
		errno = EAGAIN;
		return -2;
	}

	if (wwn_mobile_spawn_copy_argv(job, argv) != 0) {
		pthread_mutex_lock(&mobile_spawn_lock);
		wwn_mobile_spawn_job_cleanup(job);
		pthread_mutex_unlock(&mobile_spawn_lock);
		errno = ENOMEM;
		return -2;
	}

	wwn_mobile_apply_envp(envp);

	if (pthread_create(&job->thread, NULL, wwn_mobile_spawn_thread,
	                   job) != 0) {
		pthread_mutex_lock(&mobile_spawn_lock);
		wwn_mobile_spawn_job_cleanup(job);
		pthread_mutex_unlock(&mobile_spawn_lock);
		errno = EAGAIN;
		return -2;
	}

	(void)pthread_detach(job->thread);
	{
		const char *wd = getenv("WAYLAND_DISPLAY");
		const char *nested = getenv("NIRI_NESTED_WAYLAND_DISPLAY");
		fprintf(stderr,
		        "wawona-mobile-spawn: started async '%s' "
		        "WAYLAND_DISPLAY=%s NIRI_NESTED=%s\n",
		        probe,
		        wd ? wd : "(null)",
		        nested ? nested : "(null)");
	}
	return 0;
}

static int (*real_posix_spawn)(pid_t *restrict, const char *restrict,
                               const posix_spawn_file_actions_t *restrict,
                               const posix_spawnattr_t *restrict,
                               char *const[], char *const[]) = NULL;

static int (*real_posix_spawnp)(pid_t *restrict, const char *restrict,
                                const posix_spawn_file_actions_t *restrict,
                                const posix_spawnattr_t *restrict,
                                char *const[], char *const[]) = NULL;

static pid_t (*real_waitpid)(pid_t, int *, int) = NULL;

static void
wwn_mobile_spawn_resolve_real(void)
{
	if (real_posix_spawn == NULL)
		real_posix_spawn = (int (*)(pid_t *restrict,
		                            const char *restrict,
		                            const posix_spawn_file_actions_t
		                                *restrict,
		                            const posix_spawnattr_t *restrict,
		                            char *const[], char *const[]))dlsym(
		    RTLD_NEXT, "posix_spawn");
	if (real_posix_spawnp == NULL)
		real_posix_spawnp = (int (*)(pid_t *restrict,
		                             const char *restrict,
		                             const posix_spawn_file_actions_t
		                                 *restrict,
		                             const posix_spawnattr_t *restrict,
		                             char *const[], char *const[]))dlsym(
		    RTLD_NEXT, "posix_spawnp");
	if (real_waitpid == NULL)
		real_waitpid = (pid_t(*)(pid_t, int *, int))dlsym(RTLD_NEXT,
		                                                  "waitpid");
}

int
wwn_posix_spawn(pid_t *restrict pid, const char *restrict path,
                const posix_spawn_file_actions_t *restrict file_actions,
                const posix_spawnattr_t *restrict attrp,
                char *const argv[restrict], char *const envp[restrict])
{
	int handled;

	(void)file_actions;
	(void)attrp;
	wwn_mobile_spawn_resolve_real();
	handled = wwn_mobile_spawn_try_inprocess(pid, path, argv, envp);
	if (handled != 0)
		return handled < 0 ? -1 : 0;
	if (real_posix_spawn == NULL) {
		errno = ENOSYS;
		return -1;
	}
	return real_posix_spawn(pid, path, file_actions, attrp, argv, envp);
}

int
wwn_posix_spawnp(pid_t *restrict pid, const char *restrict file,
                 const posix_spawn_file_actions_t *restrict file_actions,
                 const posix_spawnattr_t *restrict attrp,
                 char *const argv[restrict], char *const envp[restrict])
{
	int handled;

	(void)file_actions;
	(void)attrp;
	wwn_mobile_spawn_resolve_real();
	handled = wwn_mobile_spawn_try_inprocess(pid, file, argv, envp);
	if (handled != 0)
		return handled < 0 ? -1 : 0;
	if (real_posix_spawnp == NULL) {
		errno = ENOSYS;
		return -1;
	}
	return real_posix_spawnp(pid, file, file_actions, attrp, argv, envp);
}

pid_t
wwn_waitpid(pid_t pid, int *status, int options)
{
	struct wwn_mobile_spawn_job *job;
	void *rv;

	wwn_mobile_spawn_resolve_real();
	job = wwn_mobile_spawn_job_find(pid);
	if (job != NULL) {
		if (job->running) {
			if (options & WNOHANG)
				return 0;
			pthread_join(job->thread, &rv);
		}
		if (status != NULL)
			*status = job->exit_code << 8;
		pthread_mutex_lock(&mobile_spawn_lock);
		wwn_mobile_spawn_job_cleanup(job);
		pthread_mutex_unlock(&mobile_spawn_lock);
		return pid;
	}
	if (real_waitpid == NULL) {
		errno = ENOSYS;
		return -1;
	}
	return real_waitpid(pid, status, options);
}

typedef struct {
	const void *replacement;
	const void *replacee;
} wwn_interpose_t;

__attribute__((used)) static const wwn_interpose_t wwn_interpose_posix_spawn
    __attribute__((section("__DATA,__interpose"))) = {
	.replacement = (const void *)(unsigned long)&wwn_posix_spawn,
	.replacee = (const void *)(unsigned long)&posix_spawn,
};

__attribute__((used)) static const wwn_interpose_t wwn_interpose_posix_spawnp
    __attribute__((section("__DATA,__interpose"))) = {
	.replacement = (const void *)(unsigned long)&wwn_posix_spawnp,
	.replacee = (const void *)(unsigned long)&posix_spawnp,
};

__attribute__((used)) static const wwn_interpose_t wwn_interpose_waitpid
    __attribute__((section("__DATA,__interpose"))) = {
	.replacement = (const void *)(unsigned long)&wwn_waitpid,
	.replacee = (const void *)(unsigned long)&waitpid,
};

#endif /* Apple mobile */
