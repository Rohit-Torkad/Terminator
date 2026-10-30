/* pipeline.c - start a pipeline: N-1 pipes, N children in one process group,
 * dup2() wiring, redirections opened in the child, close-on-exec everywhere so
 * no stray pipe ends survive (needed for EOF delivery, including across tabs
 * that fork concurrently in thread mode). */
#include "pipeline.h"
#include "process_utils.h"
#include "signals.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

/* Child-side fatal error: report on the (already redirected or inherited) stderr. */
static void child_fail(const char *name, int err, int is_cmd, int code) {
    char msg[PATH_MAX + 128];
    int n = snprintf(msg, sizeof msg, "terminator: %s: %s\n", name,
                     (is_cmd && err == ENOENT) ? "command not found" : strerror(err));
    if (n > 0) { ssize_t w = write(2, msg, (size_t)n); (void)w; }
    _exit(code);
}

static void cleanup_failed(pid_t *pids, int started, pid_t pgid) {
    if (started) kill(-pgid, SIGKILL);
    for (int i = 0; i < started; i++) { int st; proc_wait_blocking(pids[i], &st); }
}

Job *pipeline_start(ShellState *sh, Stage *st, int n, const char *text, int bg, int give_stdin) {
    int inp[2] = { -1, -1 }, outp[2] = { -1, -1 }, errp[2] = { -1, -1 };
    int mid[MAX_STAGES][2];
    pid_t pids[MAX_STAGES];
    int made = 0, started = 0;
    pid_t pgid = 0;
    Job *job = NULL;
    char **envp = NULL;

    if (n < 1 || n > MAX_STAGES) { sh_err(sh, "pipeline too long (max %d commands)", MAX_STAGES); return NULL; }
    if (pipe_cloexec(outp) < 0 || pipe_cloexec(errp) < 0) goto syserr;
    if (give_stdin && !bg && pipe_cloexec(inp) < 0) goto syserr;
    for (; made < n - 1; made++) if (pipe_cloexec(mid[made]) < 0) goto syserr;

    envp = sh_build_env(sh);
    char cwd[PATH_MAX]; snprintf(cwd, sizeof cwd, "%s", sh->cwd);

    for (int i = 0; i < n; i++) {
        pid_t pid = fork();
        if (pid < 0) { sh_err(sh, "fork: %s", strerror(errno)); cleanup_failed(pids, started, pgid); goto fail; }
        if (pid == 0) {
            signals_reset_child();
            setpgid(0, pgid);                       /* pgid==0 for the first stage => new group */
            prctl(PR_SET_PDEATHSIG, SIGHUP);        /* don't outlive the tab engine */
            if (chdir(cwd) < 0) child_fail(cwd, errno, 0, 1);
            int fd;
            if (st[i].in_path) {                    /* input redirection failed => never exec */
                fd = open(st[i].in_path, O_RDONLY);
                if (fd < 0) child_fail(st[i].in_path, errno, 0, 1);
                dup2(fd, 0);
            } else if (i == 0) {
                if (inp[0] >= 0) dup2(inp[0], 0);
                else { fd = open("/dev/null", O_RDONLY); if (fd >= 0) dup2(fd, 0); }
            } else dup2(mid[i - 1][0], 0);
            if (st[i].out_path) {
                fd = open(st[i].out_path, O_WRONLY | O_CREAT | (st[i].append ? O_APPEND : O_TRUNC), 0644);
                if (fd < 0) child_fail(st[i].out_path, errno, 0, 1);
                dup2(fd, 1);
            } else if (i < n - 1) dup2(mid[i][1], 1);
            else dup2(outp[1], 1);
            dup2(errp[1], 2);
            if (st[i].producer) {                   /* replay output of an in-engine builtin/script */
                if (st[i].prod_len) { ssize_t w = write_all(1, st[i].prod_buf, st[i].prod_len); (void)w; }
                _exit(st[i].prod_status & 0xff);
            }
            environ = envp;                         /* child-only: tab's exported variables and PATH */
            execvp(st[i].argv[0], st[i].argv);
            child_fail(st[i].argv[0], errno, 1, errno == ENOENT ? 127 : 126);
        }
        if (i == 0) pgid = pid;
        setpgid(pid, pgid);                         /* both sides, avoids the race */
        pids[started++] = pid;
    }

    for (int i = 0; i < n - 1; i++) { close(mid[i][0]); close(mid[i][1]); }
    close(outp[1]); close(errp[1]);
    if (inp[0] >= 0) close(inp[0]);
    free_env(envp);
    job = job_add(sh, pgid, pids, n, text, bg, outp[0], errp[0], inp[1]);
    if (!job) {
        sh_err(sh, "too many jobs");
        kill(-pgid, SIGKILL);
        for (int i = 0; i < n; i++) { int s2; proc_wait_blocking(pids[i], &s2); }
        close(outp[0]); close(errp[0]); if (inp[1] >= 0) close(inp[1]);
    }
    return job;

syserr:
    sh_err(sh, "pipe: %s", strerror(errno));
fail:
    for (int i = 0; i < made; i++) { close(mid[i][0]); close(mid[i][1]); }
    close_fd(&outp[0]); close_fd(&outp[1]); close_fd(&errp[0]); close_fd(&errp[1]);
    close_fd(&inp[0]); close_fd(&inp[1]);
    free_env(envp);
    return NULL;
}
