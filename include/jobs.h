/* jobs.h - per-shell job table, waiting, reaping and job control. */
#ifndef TERMINATOR_JOBS_H
#define TERMINATOR_JOBS_H
#include "common.h"

struct ShellState;
struct Job;
typedef void (*JobDataFn)(struct ShellState *, struct Job *, int stream, const char *buf, size_t n);

typedef enum { JS_RUNNING, JS_STOPPED, JS_DONE } JobState;
enum { PS_RUNNING, PS_STOPPED, PS_EXITED };

typedef struct Job {
    int used, id, bg, hidden, notified_stop;
    pid_t pgid;
    int nprocs;
    pid_t pids[MAX_STAGES];
    int pstate[MAX_STAGES], pstatus[MAX_STAGES];
    char *text;
    JobState state;
    int status;                    /* exit status once done */
    int out_fd, err_fd, in_fd;     /* parent's ends: read stdout, read stderr, write stdin */
    JobDataFn on_data;             /* optional output interceptor (multiWatch) */
    void *ud;
} Job;

Job  *job_add(struct ShellState *sh, pid_t pgid, const pid_t *pids, int n, const char *text,
              int bg, int out_fd, int err_fd, int in_fd);
void  job_free(struct ShellState *sh, Job *j);
Job  *job_find(struct ShellState *sh, int id);
Job  *job_current(struct ShellState *sh);
void  jobs_update(struct ShellState *sh);                 /* non-blocking waitpid sweep */
void  job_drain(struct ShellState *sh, Job *j);           /* read whatever is buffered */
int   job_wait_fg(struct ShellState *sh, Job *j);         /* returns $? ; may leave job stopped */
int   job_resume(struct ShellState *sh, Job *j, int bg);  /* SIGCONT (+ wait if !bg) */
int   job_exit_status(const Job *j);
void  job_terminate(struct ShellState *sh, Job *j);       /* TERM, grace, KILL, reap */
void  jobs_kill_all(struct ShellState *sh);
void  jobs_notify(struct ShellState *sh);                 /* print Done/Stopped for bg jobs */
void  jobs_print(struct ShellState *sh);
/* Poll the control pipe and every job pipe once. Returns after at most timeout_ms. */
void  sh_pump(struct ShellState *sh, int timeout_ms);

#endif
