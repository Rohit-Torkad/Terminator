/* executor.c - Expansion -> Execution -> Status. Plans each pipeline stage as
 * external program, in-engine builtin, or in-engine script, then runs it. */
#include "executor.h"
#include "builtins.h"
#include "expansion.h"
#include "pipeline.h"
#include "redirection.h"
#include "script.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void free_stage(Stage *s) {
    for (int i = 0; i < s->argc; i++) free(s->argv[i]);
    free(s->argv); free(s->in_path); free(s->out_path); free(s->prod_buf);
    memset(s, 0, sizeof *s);
}

/* NAME=value word? Fills *eq (index of '='). Only unquoted identifiers count. */
static int is_assignment(const char *w, size_t *eq) {
    const char *e = strchr(w, '=');
    if (!e || !str_is_name(w, (size_t)(e - w))) return 0;
    *eq = (size_t)(e - w);
    return 1;
}

/* Expand one command's words and redirections into st. Returns 0 or -1 (error already reported). */
static int plan_cmd(ShellState *sh, const Cmd *c, Stage *st) {
    StrList av; strlist_init(&av);
    for (int k = 0; k < c->argc; k++) expand_word(sh, c->argv[k], &av, 1);
    st->argc = av.n;
    st->argv = av.v ? av.v : xcalloc(1, sizeof(char *));
    for (int k = 0; k < c->nredirs; k++) {
        char *t = expand_string(sh, c->redirs[k].target);
        if (!*t) { sh_err(sh, "%s: ambiguous redirect", c->redirs[k].target); free(t); return -1; }
        char full[PATH_MAX];
        if (path_resolve(sh->cwd, t, full, sizeof full) < 0) { sh_err(sh, "%s: path too long", t); free(t); return -1; }
        free(t);
        if (c->redirs[k].kind == R_IN) { free(st->in_path); st->in_path = xstrdup(full); }
        else { free(st->out_path); st->out_path = xstrdup(full); st->append = c->redirs[k].kind == R_APPEND; }
    }
    return 0;
}

/* Run an in-engine action (builtin or script) with stdout diverted to `out`. */
typedef int (*InlineFn)(ShellState *, Stage *, void *);
static int run_inline(ShellState *sh, Stage *st, InlineFn fn, void *ctx, Buf *out) {
    Buf *save = sh->capture;
    sh->capture = out;
    int rc = fn(sh, st, ctx);
    sh->capture = save;
    return rc;
}
static int fn_builtin(ShellState *sh, Stage *st, void *ctx) { return ((const Builtin *)ctx)->fn(sh, st->argc, st->argv); }
static int fn_script(ShellState *sh, Stage *st, void *ctx) {
    (void)ctx;
    int status = 1;
    script_try_command(sh, st->argc, st->argv, &status);
    return status;
}

/* In a pipeline or background job, builtins and scripts run inside the engine
 * first; a forked child then replays their output into the pipe. */
static int make_producers(ShellState *sh, Stage *st, int n) {
    for (int i = 0; i < n; i++) {
        Stage *s = &st[i];
        if (s->argc == 0) { s->producer = 1; continue; }
        const Builtin *bi = builtin_lookup(s->argv[0]);
        int is_script = !bi && script_is_candidate(sh, s->argc, s->argv);
        if (!bi && !is_script) continue;
        if (bi && bi->special) {
            sh_err(sh, "%s: cannot be used in a pipeline or in the background", s->argv[0]);
            return -1;
        }
        Buf cap; buf_init(&cap);
        s->prod_status = run_inline(sh, s, bi ? fn_builtin : fn_script, (void *)bi, &cap);
        s->producer = 1; s->prod_len = cap.len; s->prod_buf = buf_take(&cap);
        if (sh->interrupted) return -1;
    }
    return 0;
}

static int exec_pipeline(ShellState *sh, Pipeline *p) {
    int n = p->ncmds, rc = 0;
    if (n > MAX_STAGES) { sh_err(sh, "pipeline too long"); return 1; }
    Stage st[MAX_STAGES];
    memset(st, 0, sizeof st);

    /* NAME=value [NAME=value ...] as the whole command sets shell variables */
    if (n == 1 && !p->background && p->cmds[0].argc > 0) {
        const Cmd *c = &p->cmds[0]; size_t eq; int all = 1;
        for (int k = 0; k < c->argc; k++) if (!is_assignment(c->argv[k], &eq)) { all = 0; break; }
        if (all) {
            sh->last_status = 0;                      /* result: status of the last command substitution, else 0 */
            for (int k = 0; k < c->argc; k++) {
                is_assignment(c->argv[k], &eq);
                char *name = xstrndup(c->argv[k], eq);
                char *val = expand_string(sh, c->argv[k] + eq + 1);
                sh_set_var(sh, name, val, -1);
                free(name); free(val);
            }
            return sh->last_status;
        }
    }

    for (int i = 0; i < n; i++) if (plan_cmd(sh, &p->cmds[i], &st[i]) < 0) { rc = 1; goto done; }

    if (n == 1 && !p->background) {                       /* standalone: engine-side builtins/scripts */
        Stage *s = &st[0];
        if (s->argc == 0) { rc = 0; goto done; }
        const Builtin *bi = builtin_lookup(s->argv[0]);
        int is_script = !bi && script_is_candidate(sh, s->argc, s->argv);
        if (bi || is_script) {
            if (s->in_path) {                             /* a missing input file must stop the command */
                char err[PATH_MAX + 128]; int fd = redir_open_in(s->in_path, err, sizeof err);
                if (fd < 0) { sh_err(sh, "%s", err); rc = 1; goto done; }
                close(fd);
            }
            if (s->out_path) {
                Buf cap; buf_init(&cap);
                rc = run_inline(sh, s, bi ? fn_builtin : fn_script, (void *)bi, &cap);
                if (redir_write_file(sh, s->out_path, s->append, cap.d ? cap.d : "", cap.len) < 0) rc = 1;
                buf_free(&cap);
            } else {
                sh->raw_argv = p->cmds[0].argv; sh->raw_argc = p->cmds[0].argc;
                rc = bi ? bi->fn(sh, s->argc, s->argv) : fn_script(sh, s, NULL);
                sh->raw_argv = NULL; sh->raw_argc = 0;
            }
            goto done;
        }
        Job *j = pipeline_start(sh, st, 1, p->text, 0, 1);
        rc = j ? job_wait_fg(sh, j) : 1;
        goto done;
    }

    /* pipelines and background jobs */
    if (make_producers(sh, st, n) < 0) { rc = sh->interrupted ? 130 : 1; goto done; }
    {
        Job *j = pipeline_start(sh, st, n, p->text, p->background, !p->background);
        if (!j) { rc = 1; goto done; }
        if (p->background) {
            pid_t pg = j->pgid;
            sh_out(sh, "[%d] %d\n", j->id, (int)pg);
            rc = 0;
        } else rc = job_wait_fg(sh, j);
    }
done:
    for (int i = 0; i < n; i++) free_stage(&st[i]);
    return rc;
}

int exec_list(ShellState *sh, CmdList *l) {
    int rc = sh->last_status;
    for (int i = 0; i < l->n; i++) {
        if (sh->interrupted || sh->script_exit || sh->want_exit) break;
        if (i > 0) {
            Conn c = l->items[i - 1].conn;
            if ((c == CONN_AND && rc != 0) || (c == CONN_OR && rc == 0)) continue;
        }
        rc = exec_pipeline(sh, &l->items[i]);
        sh->last_status = rc;
    }
    if (sh->interrupted && !sh->want_exit) { rc = 130; sh->last_status = rc; }
    return rc;
}

int exec_line(ShellState *sh, const char *text) {
    CmdList l; char err[256];
    if (parse_list(text, &l, err, sizeof err) < 0) { sh_err(sh, "%s", err); sh->last_status = 2; return 2; }
    int rc = exec_list(sh, &l);
    cmdlist_free(&l);
    return rc;
}

char *sh_command_substitute(ShellState *sh, const char *cmd) {
    Buf cap; buf_init(&cap);
    Buf *save = sh->capture;
    int se = sh->script_exit;
    sh->capture = &cap; sh->nest++;
    exec_line(sh, cmd);
    sh->nest--; sh->capture = save; sh->script_exit = se;
    while (cap.len && cap.d[cap.len - 1] == '\n') cap.d[--cap.len] = 0;
    return buf_take(&cap);
}

Job *exec_launch(ShellState *sh, Pipeline *p, int bg) {
    Stage st[MAX_STAGES];
    int n = p->ncmds;
    memset(st, 0, sizeof st);
    if (n > MAX_STAGES) { sh_err(sh, "pipeline too long"); return NULL; }
    Job *j = NULL;
    for (int i = 0; i < n; i++) if (plan_cmd(sh, &p->cmds[i], &st[i]) < 0) goto out;
    if (make_producers(sh, st, n) < 0) goto out;
    j = pipeline_start(sh, st, n, p->text, bg, 0);
out:
    for (int i = 0; i < n; i++) free_stage(&st[i]);
    return j;
}
