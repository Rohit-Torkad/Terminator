/* shared_state.c - dispatch between the two execution modes and own the history
 * that is shared by every tab (mutex in thread mode; shared memory + process-
 * shared semaphore in process mode). */
#include "concurrency.h"
#include <stdio.h>
#include <stdlib.h>

int tab_engine_spawn_thread(TabHandle *h, int tab_id, const char *cwd, History *hist);
void tab_engine_stop_thread(TabHandle *h);
int tab_engine_spawn_process(TabHandle *h, int tab_id, const char *cwd, History *hist);
void tab_engine_stop_process(TabHandle *h, int wait);
int tab_engine_reap_process(TabHandle *h, int *status);

int tab_engine_spawn(TabHandle *h, int tab_id, const char *cwd, History *hist, int process_mode) {
    h->process_mode = process_mode; h->ctl_w = h->out_r = -1; h->active = 0; h->pid = -1;
    return process_mode ? tab_engine_spawn_process(h, tab_id, cwd, hist)
                        : tab_engine_spawn_thread(h, tab_id, cwd, hist);
}

void tab_engine_stop(TabHandle *h, int wait) {
    if (h->process_mode) tab_engine_stop_process(h, wait); else tab_engine_stop_thread(h);
}

int tab_engine_reap(TabHandle *h, int *status) {
    if (!h->process_mode) { if (status) *status = 0; return 0; }
    return tab_engine_reap_process(h, status);
}

History *shared_history_create(int process_mode) {
    char path[PATH_MAX];
    const char *over = getenv("TERMINATOR_HISTFILE");
    if (over && *over) snprintf(path, sizeof path, "%s", over);
    else snprintf(path, sizeof path, "%s/.terminator_history", path_home());
    return history_open(path, process_mode);
}

void shared_history_destroy(History *h) { history_close(h); }
