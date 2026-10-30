/* concurrency.h - thread mode / process mode tab spawning and shared state. */
#ifndef TERMINATOR_CONCURRENCY_H
#define TERMINATOR_CONCURRENCY_H
#include <pthread.h>
#include "shell.h"

typedef struct {
    int process_mode;
    int ctl_w, out_r;       /* GUI side ends */
    pthread_t thr;          /* thread mode */
    pid_t pid;              /* process mode */
    int active;             /* engine thread/process not yet released */
} TabHandle;

/* Creates the pipes and starts an engine for the tab. 0 on success. */
int  tab_engine_spawn(TabHandle *h, int tab_id, const char *cwd, History *hist, int process_mode);
/* Close GUI ends, ask the engine to stop and release thread/process resources. */
void tab_engine_stop(TabHandle *h, int wait);
/* Non-blocking reap in process mode; 1 if the engine process is gone. */
int  tab_engine_reap(TabHandle *h, int *status);

/* Shared history for the whole application (path from $HOME). */
History *shared_history_create(int process_mode);
void     shared_history_destroy(History *h);

#endif
