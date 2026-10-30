/* jobs.c - job table, child reaping, foreground wait loop and the engine pump.
 * Children are only ever waited on by pid, so concurrent tabs (threads) never
 * steal each other's exit statuses. */
#include "jobs.h"
#include "shell.h"
#include "process_utils.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

Job *job_add(ShellState *sh, pid_t pgid, const pid_t *pids, int n, const char *text, int bg,
             int out_fd, int err_fd, int in_fd) {
    Job *j = NULL;
    for (int i = 0; i < MAX_JOBS; i++) if (!sh->jobs[i].used) { j = &sh->jobs[i]; break; }
    if (!j) return NULL;
    memset(j, 0, sizeof *j);
    j->used = 1; j->pgid = pgid; j->nprocs = n; j->bg = bg;
    for (int i = 0; i < n; i++) { j->pids[i] = pids[i]; j->pstate[i] = PS_RUNNING; }
    j->text = xstrdup(text);
    j->state = JS_RUNNING;
    j->out_fd = out_fd; j->err_fd = err_fd; j->in_fd = in_fd;
    if (out_fd >= 0) fd_set_nonblock(out_fd);
    if (err_fd >= 0) fd_set_nonblock(err_fd);
    if (in_fd >= 0) fd_set_nonblock(in_fd);
    int id = 1;                              /* lowest unused job number */
    for (;; id++) { int busy = 0; for (int i = 0; i < MAX_JOBS; i++) if (sh->jobs[i].used && sh->jobs[i].id == id) busy = 1; if (!busy) break; }
    j->id = id;
    return j;
}

void job_free(ShellState *sh, Job *j) {
    (void)sh;
    close_fd(&j->out_fd); close_fd(&j->err_fd); close_fd(&j->in_fd);
    free(j->text);
    memset(j, 0, sizeof *j);
}

Job *job_find(ShellState *sh, int id) {
    for (int i = 0; i < MAX_JOBS; i++) if (sh->jobs[i].used && !sh->jobs[i].hidden && sh->jobs[i].id == id) return &sh->jobs[i];
    return NULL;
}
Job *job_current(ShellState *sh) {
    Job *best = NULL;
    for (int i = 0; i < MAX_JOBS; i++) {
        Job *j = &sh->jobs[i];
        if (j->used && !j->hidden && (!best || j->id > best->id)) best = j;
    }
    return best;
}

int job_exit_status(const Job *j) { return j->status; }

void jobs_update(ShellState *sh) {
    for (int i = 0; i < MAX_JOBS; i++) {
        Job *j = &sh->jobs[i];
        if (!j->used) continue;
        for (int p = 0; p < j->nprocs; p++) {
            if (j->pstate[p] == PS_EXITED) continue;
            int st; pid_t r = waitpid(j->pids[p], &st, WNOHANG | WUNTRACED | WCONTINUED);
            if (r == j->pids[p]) {
                if (WIFEXITED(st) || WIFSIGNALED(st)) { j->pstate[p] = PS_EXITED; j->pstatus[p] = status_to_code(st); }
                else if (WIFSTOPPED(st)) j->pstate[p] = PS_STOPPED;
                else if (WIFCONTINUED(st)) j->pstate[p] = PS_RUNNING;
            } else if (r < 0 && errno == ECHILD) { j->pstate[p] = PS_EXITED; j->pstatus[p] = 1; }
        }
        int run = 0, stop = 0;
        for (int p = 0; p < j->nprocs; p++) { if (j->pstate[p] == PS_RUNNING) run++; else if (j->pstate[p] == PS_STOPPED) stop++; }
        if (!run && !stop) { j->state = JS_DONE; j->status = j->pstatus[j->nprocs - 1]; }
        else if (run) j->state = JS_RUNNING;
        else j->state = JS_STOPPED;
    }
}

static void deliver(ShellState *sh, Job *j, int stream, const char *b, size_t n) {
    if (j->on_data) j->on_data(sh, j, stream, b, n);
    else sh_emit(sh, stream, b, n);
}

static void read_job_fd(ShellState *sh, Job *j, int stream) {
    int *fdp = stream == ST_OUT ? &j->out_fd : &j->err_fd;
    char buf[8192];
    for (int iter = 0; iter < 16 && *fdp >= 0; iter++) {
        ssize_t n = read(*fdp, buf, sizeof buf);
        if (n > 0) deliver(sh, j, stream, buf, (size_t)n);
        else if (n == 0) { close_fd(fdp); break; }
        else if (errno == EINTR) continue;
        else break;                                   /* EAGAIN: nothing more right now */
    }
}

void job_drain(ShellState *sh, Job *j) {
    read_job_fd(sh, j, ST_OUT);
    read_job_fd(sh, j, ST_ERR);
}

void sh_pump(ShellState *sh, int timeout_ms) {
    struct pollfd fds[1 + 2 * MAX_JOBS];
    Job *jm[1 + 2 * MAX_JOBS]; int sm[1 + 2 * MAX_JOBS];
    int nf = 0;
    if (sh->ctl_fd >= 0 && !sh->ctl_eof) { fds[nf] = (struct pollfd){ sh->ctl_fd, POLLIN, 0 }; jm[nf] = NULL; sm[nf++] = 0; }
    for (int i = 0; i < MAX_JOBS; i++) {
        Job *j = &sh->jobs[i];
        if (!j->used) continue;
        if (j->out_fd >= 0) { fds[nf] = (struct pollfd){ j->out_fd, POLLIN, 0 }; jm[nf] = j; sm[nf++] = ST_OUT; }
        if (j->err_fd >= 0) { fds[nf] = (struct pollfd){ j->err_fd, POLLIN, 0 }; jm[nf] = j; sm[nf++] = ST_ERR; }
    }
    if (nf == 0 && timeout_ms < 0) timeout_ms = 100;
    int r = poll(fds, (nfds_t)nf, timeout_ms);
    if (r > 0) {
        for (int k = 0; k < nf; k++) {
            if (!fds[k].revents) continue;
            if (!jm[k]) sh_handle_control(sh);
            else if (jm[k]->used) read_job_fd(sh, jm[k], sm[k]);
        }
    }
    jobs_update(sh);
}

static void announce(ShellState *sh, Job *j, const char *label) {
    sh_out(sh, "[%d]+  %-22s%s\n", j->id, label, j->text);
}

void job_terminate(ShellState *sh, Job *j) {
    kill(-j->pgid, SIGCONT);
    kill(-j->pgid, SIGTERM);
    for (int i = 0; i < 40; i++) {
        jobs_update(sh);
        if (j->state == JS_DONE) break;
        usleep(10000);
    }
    if (j->state != JS_DONE) {
        kill(-j->pgid, SIGKILL);
        for (int p = 0; p < j->nprocs; p++)
            if (j->pstate[p] != PS_EXITED) { int st; proc_wait_blocking(j->pids[p], &st); j->pstate[p] = PS_EXITED; j->pstatus[p] = status_to_code(st); }
        j->state = JS_DONE; j->status = j->pstatus[j->nprocs - 1];
    }
    job_drain(sh, j);
}

void jobs_kill_all(ShellState *sh) {
    for (int i = 0; i < MAX_JOBS; i++) {
        Job *j = &sh->jobs[i];
        if (!j->used) continue;
        if (j->state != JS_DONE) job_terminate(sh, j);
        job_free(sh, j);
    }
}

int job_wait_fg(ShellState *sh, Job *j) {
    sh->fg_job = j; j->bg = 0; j->notified_stop = 0;
    for (;;) {
        jobs_update(sh);
        if (j->state != JS_RUNNING) break;
        if (sh->want_exit) { job_terminate(sh, j); break; }
        sh_pump(sh, 100);
    }
    job_drain(sh, j);
    sh->fg_job = NULL;
    if (j->state == JS_STOPPED) {
        j->bg = 1; j->notified_stop = 1;
        if (sh->nest == 0) sh_out(sh, "\n[%d]+  %-22s%s\n", j->id, "Stopped", j->text);
        return 128 + SIGTSTP;
    }
    int st = j->status;
    job_free(sh, j);
    return st;
}

int job_resume(ShellState *sh, Job *j, int bg) {
    for (int p = 0; p < j->nprocs; p++) if (j->pstate[p] == PS_STOPPED) j->pstate[p] = PS_RUNNING;
    j->state = JS_RUNNING; j->notified_stop = 0;
    kill(-j->pgid, SIGCONT);
    if (bg) { j->bg = 1; sh_out(sh, "[%d]+ %s &\n", j->id, j->text); return 0; }
    sh_out(sh, "%s\n", j->text);
    return job_wait_fg(sh, j);
}

static const char *done_label(int st, char *buf, size_t n) {
    if (st == 0) return "Done";
    if (st == 128 + SIGTERM) return "Terminated";
    if (st == 128 + SIGKILL) return "Killed";
    if (st == 128 + SIGINT) return "Interrupt";
    snprintf(buf, n, "Exit %d", st);
    return buf;
}

void jobs_notify(ShellState *sh) {
    jobs_update(sh);
    for (int i = 0; i < MAX_JOBS; i++) {
        Job *j = &sh->jobs[i];
        if (!j->used || j->hidden || !j->bg) continue;
        if (j->state == JS_DONE) {
            job_drain(sh, j);
            char tmp[32];
            announce(sh, j, done_label(j->status, tmp, sizeof tmp));
            job_free(sh, j);
        } else if (j->state == JS_STOPPED && !j->notified_stop) {
            announce(sh, j, "Stopped"); j->notified_stop = 1;
        }
    }
}

void jobs_print(ShellState *sh) {
    jobs_update(sh);
    Job *cur = job_current(sh), *prev = NULL;
    for (int i = 0; i < MAX_JOBS; i++) {
        Job *j = &sh->jobs[i];
        if (j->used && !j->hidden && j != cur && (!prev || j->id > prev->id)) prev = j;
    }
    for (int id = 1; id < 10000; id++) {
        Job *j = job_find(sh, id);
        int more = 0;
        for (int i = 0; i < MAX_JOBS; i++) if (sh->jobs[i].used && !sh->jobs[i].hidden && sh->jobs[i].id > id) more = 1;
        if (j) {
            char tmp[32];
            const char *lab = j->state == JS_RUNNING ? "Running" : j->state == JS_STOPPED ? "Stopped" : done_label(j->status, tmp, sizeof tmp);
            sh_out(sh, "[%d]%c  %-22s%s%s\n", j->id, j == cur ? '+' : j == prev ? '-' : ' ', lab, j->text,
                   (j->state == JS_RUNNING && j->bg) ? " &" : "");
            if (j->state == JS_DONE) { job_drain(sh, j); job_free(sh, j); }
        }
        if (!more) break;
    }
}
