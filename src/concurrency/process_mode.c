/* process_mode.c - one forked shell process per tab. The parent (GUI) is the only
 * process that talks to Xlib; the child closes every descriptor it does not own
 * (including the X connection) before running the same engine. */
#include "concurrency.h"
#include "process_utils.h"
#include "signals.h"
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

static void close_all_except(int keep1, int keep2) {
    int fds[1024], n = 0;
    DIR *d = opendir("/proc/self/fd");
    if (d) {
        int dfd = dirfd(d);
        struct dirent *e;
        while ((e = readdir(d)) && n < 1024) {
            int fd = atoi(e->d_name);
            if (e->d_name[0] != '.' && fd > 2 && fd != dfd && fd != keep1 && fd != keep2) fds[n++] = fd;
        }
        closedir(d);
        for (int i = 0; i < n; i++) close(fds[i]);
    } else for (int fd = 3; fd < 1024; fd++) if (fd != keep1 && fd != keep2) close(fd);
}

int tab_engine_spawn_process(TabHandle *h, int tab_id, const char *cwd, History *hist) {
    int ctl[2], out[2];
    if (pipe_cloexec(ctl) < 0) return -1;
    if (pipe_cloexec(out) < 0) { close(ctl[0]); close(ctl[1]); return -1; }
    pid_t pid = fork();
    if (pid < 0) { close(ctl[0]); close(ctl[1]); close(out[0]); close(out[1]); return -1; }
    if (pid == 0) {
        signals_init_engine_process();
        prctl(PR_SET_PDEATHSIG, SIGTERM);              /* die with the GUI */
        close_all_except(ctl[0], out[1]);
        EngineArgs a; memset(&a, 0, sizeof a);
        a.ctl_fd = ctl[0]; a.out_fd = out[1]; a.tab_id = tab_id; a.process_mode = 1; a.hist = hist;
        snprintf(a.cwd, sizeof a.cwd, "%s", cwd);
        int st = engine_main(&a);
        _exit(st & 0xff);                              /* never run the parent's atexit/Xlib cleanup */
    }
    close(ctl[0]); close(out[1]);
    h->ctl_w = ctl[1]; h->out_r = out[0]; h->pid = pid; h->active = 1;
    return 0;
}

int tab_engine_reap_process(TabHandle *h, int *status) {
    if (!h->active) return 1;
    int st; pid_t r = waitpid(h->pid, &st, WNOHANG);
    if (r == h->pid || (r < 0 && errno == ECHILD)) {
        if (status) *status = (r == h->pid) ? st : 0;
        h->active = 0;
        return 1;
    }
    return 0;
}

void tab_engine_stop_process(TabHandle *h, int wait) {
    if (h->ctl_w >= 0) { ipc_send(h->ctl_w, M_QUIT, NULL, 0); close_fd(&h->ctl_w); }
    close_fd(&h->out_r);
    if (!h->active || !wait) return;
    for (int i = 0; i < 300 && h->active; i++) {       /* up to ~3 s, then SIGKILL */
        if (tab_engine_reap_process(h, NULL)) return;
        usleep(10000);
    }
    kill(h->pid, SIGKILL);
    int st; proc_wait_blocking(h->pid, &st);
    h->active = 0;
}
