/* thread_mode.c - one POSIX thread per tab, same process as the GUI.
 * The engine only touches its own ShellState and the two pipe ends it was given;
 * the shared History is protected by a mutex. X11 is never called from here. */
#include "concurrency.h"
#include "process_utils.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int tab_engine_spawn_thread(TabHandle *h, int tab_id, const char *cwd, History *hist) {
    int ctl[2], out[2];
    if (pipe_cloexec(ctl) < 0) return -1;
    if (pipe_cloexec(out) < 0) { close(ctl[0]); close(ctl[1]); return -1; }
    EngineArgs *a = xcalloc(1, sizeof *a);
    a->ctl_fd = ctl[0]; a->out_fd = out[1]; a->tab_id = tab_id; a->process_mode = 0; a->hist = hist;
    snprintf(a->cwd, sizeof a->cwd, "%s", cwd);
    if (pthread_create(&h->thr, NULL, engine_thread_main, a) != 0) {
        close(ctl[0]); close(ctl[1]); close(out[0]); close(out[1]); free(a);
        return -1;
    }
    h->ctl_w = ctl[1]; h->out_r = out[0]; h->active = 1;
    return 0;
}

void tab_engine_stop_thread(TabHandle *h) {
    if (h->ctl_w >= 0) { ipc_send(h->ctl_w, M_QUIT, NULL, 0); close_fd(&h->ctl_w); }
    close_fd(&h->out_r);                 /* engine's writes now fail instead of blocking */
    if (h->active) { pthread_join(h->thr, NULL); h->active = 0; }
}
