/* shell.c - per-tab shell state, variables, output routing, control messages
 * from the GUI and the engine main loop. Runs identically as a pthread (thread
 * mode) or as a forked process (process mode). */
#include "shell.h"
#include "executor.h"
#include "lexer.h"
#include "script.h"
#include "signals.h"
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char **environ;
#define INTR_MARK "\x03"   /* queue entry meaning "Ctrl+C arrived here" */

/* ---------- variables ---------- */
static Var *var_find(ShellState *sh, const char *name) {
    for (Var *v = sh->vars; v; v = v->next) if (!strcmp(v->name, name)) return v;
    return NULL;
}
void sh_set_var(ShellState *sh, const char *name, const char *value, int exported) {
    Var *v = var_find(sh, name);
    if (!v) { v = xcalloc(1, sizeof *v); v->name = xstrdup(name); v->next = sh->vars; sh->vars = v; }
    else free(v->value);
    v->value = xstrdup(value);
    if (exported >= 0) v->exported = exported;
}
void sh_unset_var(ShellState *sh, const char *name) {
    for (Var **pp = &sh->vars; *pp; pp = &(*pp)->next)
        if (!strcmp((*pp)->name, name)) { Var *d = *pp; *pp = d->next; free(d->name); free(d->value); free(d); return; }
}
const char *sh_get_var(ShellState *sh, const char *name) {
    static __thread char tmp[32];
    if (!strcmp(name, "?")) { snprintf(tmp, sizeof tmp, "%d", sh->last_status); return tmp; }
    if (!strcmp(name, "#")) { snprintf(tmp, sizeof tmp, "%d", sh->nargs); return tmp; }
    if (!strcmp(name, "$")) { snprintf(tmp, sizeof tmp, "%d", (int)getpid()); return tmp; }
    if (name[0] >= '0' && name[0] <= '9' && !name[1]) {
        int k = name[0] - '0';
        if (k == 0) return sh->args[0] ? sh->args[0] : "terminator";
        return (k <= sh->nargs && sh->args[k]) ? sh->args[k] : "";
    }
    Var *v = var_find(sh, name);
    return v ? v->value : NULL;
}
char **sh_build_env(ShellState *sh) {
    int n = 0;
    for (Var *v = sh->vars; v; v = v->next) if (v->exported) n++;
    char **e = xmalloc(sizeof(char *) * (size_t)(n + 1));
    int i = 0;
    for (Var *v = sh->vars; v; v = v->next)
        if (v->exported) { Buf b; buf_init(&b); buf_addf(&b, "%s=%s", v->name, v->value); e[i++] = buf_take(&b); }
    e[i] = NULL;
    return e;
}
void free_env(char **e) { if (!e) return; for (char **p = e; *p; p++) free(*p); free(e); }

/* ---------- output routing ---------- */
void sh_send(ShellState *sh, char type, const char *payload, size_t len) {
    if (sh->out_fd < 0) return;
    if (ipc_send(sh->out_fd, type, payload, (uint32_t)len) < 0) sh->want_exit = 1;   /* GUI is gone */
}

void sh_emit(ShellState *sh, int stream, const char *s, size_t n) {
    if (!n) return;
    if (stream == ST_OUT && sh->capture) { buf_add(sh->capture, s, n); return; }
    if (sh->out_fd < 0) { write_all(stream == ST_OUT ? 1 : 2, s, n); return; }
    while (n) {
        size_t c = n > 4000 ? 4000 : n;
        sh_send(sh, stream == ST_OUT ? M_OUT : M_ERR, s, c);
        s += c; n -= c;
    }
}
void sh_out(ShellState *sh, const char *fmt, ...) {
    Buf b; buf_init(&b);
    va_list ap; va_start(ap, fmt);
    char tmp[1024]; int n = vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    if (n >= (int)sizeof tmp) {                      /* long message: format again into heap */
        char *big = xmalloc((size_t)n + 1);
        va_start(ap, fmt); vsnprintf(big, (size_t)n + 1, fmt, ap); va_end(ap);
        sh_emit(sh, ST_OUT, big, (size_t)n); free(big);
    } else if (n > 0) sh_emit(sh, ST_OUT, tmp, (size_t)n);
    buf_free(&b);
}
void sh_err(ShellState *sh, const char *fmt, ...) {
    char tmp[2048];
    int k = snprintf(tmp, sizeof tmp, "terminator: ");
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(tmp + k, sizeof tmp - (size_t)k - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    size_t len = (size_t)k + (size_t)n;
    if (len > sizeof tmp - 2) len = sizeof tmp - 2;
    tmp[len++] = '\n';
    sh_emit(sh, ST_ERR, tmp, len);
}

/* ---------- control messages ---------- */
static void dispatch(ShellState *sh, char type, const char *p, uint32_t len) {
    Job *fg = sh->fg_job;
    switch (type) {
    case M_LINE:
        if (fg && fg->in_fd >= 0) {                  /* foreground program reads this line */
            char *l = xmalloc(len + 2); memcpy(l, p, len); l[len] = '\n';
            ssize_t w = write(fg->in_fd, l, len + 1); (void)w;
            free(l);
        } else strlist_add(&sh->queue, p);          /* type-ahead */
        break;
    case M_INTR:
        if (!sh->busy) {                              /* Ctrl+C at an idle prompt: drop a half-typed multi-line command */
            if (sh->queue.n) strlist_add(&sh->queue, INTR_MARK);   /* keep its place behind queued type-ahead lines */
            else { buf_reset(&sh->partial); sh_send(sh, M_PROMPT, "0", 1); }
            break;
        }
        if (fg) kill(-fg->pgid, SIGINT);
        sh->interrupted = 1;
        break;
    case M_SUSP:
        if (fg && sh->nest == 0) kill(-fg->pgid, SIGTSTP);
        break;
    case M_EOF:
        if (fg && fg->in_fd >= 0) { close(fg->in_fd); fg->in_fd = -1; }
        else if (!sh->busy) sh->want_exit = 1;
        break;
    case M_QUIT:
        sh->want_exit = 1; sh->interrupted = 1;
        break;
    default: break;
    }
}

static void dispatch_buffered(ShellState *sh) {
    char type; const char *p; uint32_t len; int r;
    while ((r = ipc_reader_next(&sh->rd, &type, &p, &len)) == 1) dispatch(sh, type, p, len);
    if (r < 0) { sh->want_exit = 1; sh->ctl_eof = 1; }
}

int sh_handle_control(ShellState *sh) {
    if (sh->ctl_fd < 0 || sh->ctl_eof) return 0;
    ssize_t n = ipc_reader_fill(&sh->rd, sh->ctl_fd);
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR && errno != EWOULDBLOCK)) {
        sh->ctl_eof = 1; sh->want_exit = 1; sh->interrupted = 1;     /* GUI closed the pipe */
        return 0;
    }
    dispatch_buffered(sh);
    return n > 0;
}

/* ---------- lifecycle ---------- */
ShellState *sh_new(const EngineArgs *a) {
    ShellState *sh = xcalloc(1, sizeof *sh);
    sh->tab_id = a->tab_id; sh->process_mode = a->process_mode;
    sh->ctl_fd = a->ctl_fd; sh->out_fd = a->out_fd; sh->hist = a->hist;
    snprintf(sh->cwd, sizeof sh->cwd, "%s", a->cwd[0] ? a->cwd : "/");
    snprintf(sh->oldpwd, sizeof sh->oldpwd, "%s", sh->cwd);
    for (char **e = environ; e && *e; e++) {
        char *eq = strchr(*e, '=');
        if (!eq) continue;
        char *name = xstrndup(*e, (size_t)(eq - *e));
        sh_set_var(sh, name, eq + 1, 1);
        free(name);
    }
    sh_set_var(sh, "PWD", sh->cwd, 1);
    sh_set_var(sh, "OLDPWD", sh->oldpwd, 1);
    if (!sh_get_var(sh, "HOME")) sh_set_var(sh, "HOME", path_home(), 1);
    if (!sh_get_var(sh, "PATH")) sh_set_var(sh, "PATH", "/usr/local/bin:/usr/bin:/bin", 1);
    sh->args[0] = xstrdup("terminator");
    sh->next_job_id = 1;
    ipc_reader_init(&sh->rd);
    strlist_init(&sh->queue);
    buf_init(&sh->partial);
    return sh;
}

void sh_free(ShellState *sh) {
    while (sh->vars) sh_unset_var(sh, sh->vars->name);
    for (int i = 0; i < 10; i++) free(sh->args[i]);
    for (int i = 0; i < MAX_JOBS; i++) if (sh->jobs[i].used) job_free(sh, &sh->jobs[i]);
    ipc_reader_free(&sh->rd);
    strlist_free(&sh->queue);
    buf_free(&sh->partial);
    free(sh);
}

/* ---------- one input line: completeness -> history -> parse/execute -> prompt ---------- */
int sh_process_line(ShellState *sh, const char *line) {
    if (sh->partial.len) buf_addc(&sh->partial, '\n');
    buf_adds(&sh->partial, line);
    const char *text = buf_str(&sh->partial);
    InputState st = input_completeness(text);
    if (st == INPUT_COMPLETE && script_open_blocks(text) > 0) st = INPUT_INCOMPLETE;
    if (st == INPUT_INCOMPLETE) { sh_send(sh, M_PROMPT, "1", 1); return sh->last_status; }

    char *cmd = buf_take(&sh->partial);
    if (sh->hist) history_add(sh->hist, cmd);
    sh->interrupted = 0; sh->script_exit = 0; sh->busy = 1;

    StmtList sl; char err[256];
    if (script_parse(cmd, &sl, err, sizeof err) < 0) {
        sh_err(sh, "%s", err);
        sh->last_status = 2;
    } else {
        script_exec_stmts(sh, &sl);
        stmts_free(&sl);
    }
    free(cmd);
    sh->busy = 0;
    return sh->last_status;
}

static void send_ready(ShellState *sh) {
    sh_send(sh, M_CWD, sh->cwd, strlen(sh->cwd));
    sh_send(sh, M_PROMPT, "0", 1);
}

int engine_main(EngineArgs *a) {
    ShellState *sh = sh_new(a);
    if (sh->ctl_fd >= 0) fd_set_nonblock(sh->ctl_fd);
    send_ready(sh);
    while (!sh->want_exit) {
        if (sh->queue.n) {                           /* run type-ahead */
            char *l = sh->queue.v[0];
            memmove(sh->queue.v, sh->queue.v + 1, sizeof(char *) * (size_t)sh->queue.n);
            sh->queue.n--;
            if (!strcmp(l, INTR_MARK)) buf_reset(&sh->partial);      /* Ctrl+C: abandon the half-typed command */
            else sh_process_line(sh, l);
            free(l);
            if (!sh->want_exit && !sh->queue.n && !sh->partial.len) send_ready(sh);
            continue;
        }
        jobs_notify(sh);
        int any = 0;
        for (int i = 0; i < MAX_JOBS; i++) if (sh->jobs[i].used) any = 1;
        sh_pump(sh, any ? 200 : -1);
    }
    int status = sh->exit_status ? sh->exit_status : sh->last_status;
    jobs_kill_all(sh);
    char num[16]; int n = snprintf(num, sizeof num, "%d", status);
    sh_send(sh, M_EXIT, num, (size_t)n);
    sh_free(sh);
    return status;
}

void *engine_thread_main(void *arg) {
    EngineArgs *a = arg;
    engine_main(a);
    if (a->ctl_fd >= 0) close(a->ctl_fd);
    if (a->out_fd >= 0) close(a->out_fd);
    free(a);
    return NULL;
}
