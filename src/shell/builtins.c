/* builtins.c - cd exit history multiWatch jobs fg bg source . export unset echo
 * pwd clear bind true false : test [ . State-changing builtins run inside the
 * tab's engine, so `cd` changes only this tab's directory. */
#include "builtins.h"
#include "config.h"
#include "jobs.h"
#include "multiwatch.h"
#include "script.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int bi_cd(ShellState *sh, int argc, char **argv) {
    if (argc > 2) { sh_err(sh, "cd: too many arguments"); return 1; }
    const char *t;
    int print = 0;
    if (argc == 1) { t = sh_get_var(sh, "HOME"); if (!t || !*t) { sh_err(sh, "cd: HOME not set"); return 1; } }
    else if (!strcmp(argv[1], "-")) { t = sh_get_var(sh, "OLDPWD"); if (!t || !*t) { sh_err(sh, "cd: OLDPWD not set"); return 1; } print = 1; }
    else t = argv[1];
    char full[PATH_MAX]; struct stat st;
    if (path_resolve(sh->cwd, t, full, sizeof full) < 0) { sh_err(sh, "cd: %s: path too long", t); return 1; }
    if (stat(full, &st) != 0) { sh_err(sh, "cd: %s: %s", t, strerror(errno)); return 1; }
    if (!S_ISDIR(st.st_mode)) { sh_err(sh, "cd: %s: Not a directory", t); return 1; }
    if (access(full, X_OK) != 0) { sh_err(sh, "cd: %s: %s", t, strerror(errno)); return 1; }
    snprintf(sh->oldpwd, sizeof sh->oldpwd, "%s", sh->cwd);
    snprintf(sh->cwd, sizeof sh->cwd, "%s", full);
    sh_set_var(sh, "OLDPWD", sh->oldpwd, 1);
    sh_set_var(sh, "PWD", sh->cwd, 1);
    if (print) sh_out(sh, "%s\n", sh->cwd);
    sh_send(sh, M_CWD, sh->cwd, strlen(sh->cwd));
    return 0;
}

static int bi_exit(ShellState *sh, int argc, char **argv) {
    long n = sh->last_status;
    if (argc > 2) { sh_err(sh, "exit: too many arguments"); return 1; }
    if (argc == 2 && str_parse_int(argv[1], &n) < 0) { sh_err(sh, "exit: %s: numeric argument required", argv[1]); n = 2; }
    n &= 0xff;
    sh->last_status = (int)n;
    if (sh->nest > 0) sh->script_exit = 1;      /* inside a script/substitution: leave only that */
    else { sh->want_exit = 1; sh->exit_status = (int)n; }
    return (int)n;
}

static int bi_history(ShellState *sh, int argc, char **argv) {
    long cnt = HIST_SHOW;
    if (argc > 2) { sh_err(sh, "history: too many arguments"); return 1; }
    if (argc == 2 && (str_parse_int(argv[1], &cnt) < 0 || cnt < 0)) { sh_err(sh, "history: %s: numeric argument required", argv[1]); return 1; }
    if (cnt > HIST_SHOW) cnt = HIST_SHOW;
    if (!sh->hist) return 0;
    StrList l; strlist_init(&l);
    int first = 1;
    history_recent(sh->hist, (int)cnt, &l, &first);
    for (int i = 0; i < l.n; i++) sh_out(sh, "%5d  %s\n", first + i, l.v[i]);
    strlist_free(&l);
    return 0;
}

static int bi_jobs(ShellState *sh, int argc, char **argv) { (void)argc; (void)argv; jobs_print(sh); return 0; }

static Job *job_arg(ShellState *sh, const char *who, int argc, char **argv) {
    jobs_update(sh);
    Job *j;
    if (argc < 2) j = job_current(sh);
    else {
        const char *a = argv[1]; long id;
        if (*a == '%') a++;
        if (!strcmp(a, "+") || !strcmp(a, "%") || !*a) j = job_current(sh);
        else if (str_parse_int(a, &id) == 0) j = job_find(sh, (int)id);
        else { sh_err(sh, "%s: %s: invalid job specification", who, argv[1]); return NULL; }
    }
    if (!j) sh_err(sh, "%s: %s: no such job", who, argc < 2 ? "current" : argv[1]);
    return j;
}
static int bi_fg(ShellState *sh, int argc, char **argv) {
    Job *j = job_arg(sh, "fg", argc, argv);
    if (!j) return 1;
    if (j->state == JS_DONE) { job_drain(sh, j); int st = j->status; job_free(sh, j); return st; }
    return job_resume(sh, j, 0);
}
static int bi_bg(ShellState *sh, int argc, char **argv) {
    Job *j = job_arg(sh, "bg", argc, argv);
    if (!j) return 1;
    if (j->state == JS_RUNNING) { sh_err(sh, "bg: job %d already in background", j->id); return 0; }
    return job_resume(sh, j, 1);
}

static int bi_source(ShellState *sh, int argc, char **argv) {
    if (argc < 2) { sh_err(sh, "%s: filename argument required", argv[0]); return 2; }
    return script_run_file(sh, argv[1], argc - 2, argv + 2, 0);
}

static int bi_export(ShellState *sh, int argc, char **argv) {
    if (argc == 1) {
        for (Var *v = sh->vars; v; v = v->next) if (v->exported) sh_out(sh, "export %s=%s\n", v->name, v->value);
        return 0;
    }
    int rc = 0;
    for (int i = 1; i < argc; i++) {
        char *eq = strchr(argv[i], '=');
        size_t nl = eq ? (size_t)(eq - argv[i]) : strlen(argv[i]);
        if (!str_is_name(argv[i], nl)) { sh_err(sh, "export: `%s': not a valid identifier", argv[i]); rc = 1; continue; }
        char *name = xstrndup(argv[i], nl);
        if (eq) sh_set_var(sh, name, eq + 1, 1);
        else { const char *v = sh_get_var(sh, name); sh_set_var(sh, name, v ? v : "", 1); }
        free(name);
    }
    return rc;
}
static int bi_unset(ShellState *sh, int argc, char **argv) { for (int i = 1; i < argc; i++) sh_unset_var(sh, argv[i]); return 0; }

static int bi_echo(ShellState *sh, int argc, char **argv) {
    int i = 1, nl = 1;
    if (i < argc && !strcmp(argv[i], "-n")) { nl = 0; i++; }
    Buf b; buf_init(&b);
    for (; i < argc; i++) { buf_adds(&b, argv[i]); if (i + 1 < argc) buf_addc(&b, ' '); }
    if (nl) buf_addc(&b, '\n');
    sh_emit(sh, ST_OUT, buf_str(&b), b.len);
    buf_free(&b);
    return 0;
}
static int bi_pwd(ShellState *sh, int argc, char **argv) { (void)argc; (void)argv; sh_out(sh, "%s\n", sh->cwd); return 0; }
static int bi_clear(ShellState *sh, int argc, char **argv) { (void)argc; (void)argv; sh_send(sh, M_CLEAR, "", 0); return 0; }
static int bi_true(ShellState *sh, int argc, char **argv) { (void)sh; (void)argc; (void)argv; return 0; }
static int bi_false(ShellState *sh, int argc, char **argv) { (void)sh; (void)argc; (void)argv; return 1; }
static int bi_multiwatch(ShellState *sh, int argc, char **argv) { return multiwatch_run(sh, argc, argv); }

static int bi_bind(ShellState *sh, int argc, char **argv) {
    char path[PATH_MAX]; config_path(path, sizeof path);
    if (argc == 1) {
        Config c; Buf w; buf_init(&w);
        config_load(&c, path, &w);
        for (int a = 0; a < ACT_COUNT; a++) {
            char k[48]; keyspec_format(c.key[a], k, sizeof k);
            sh_out(sh, "%-16s %s%s\n", action_name((Action)a), k, c.custom[a] ? "  (custom)" : "");
        }
        if (w.len) sh_emit(sh, ST_ERR, w.d, w.len);
        buf_free(&w);
        return 0;
    }
    if (argc != 3) { sh_err(sh, "usage: bind [<action> <Key>]   e.g. bind line_start Ctrl+B"); return 2; }
    char err[200];
    if (config_set_binding(path, argv[1], argv[2], err, sizeof err) < 0) { sh_err(sh, "bind: %s", err); return 1; }
    sh_send(sh, M_RELOAD, "", 0);
    sh_out(sh, "bound %s to %s (saved in %s)\n", argv[1], argv[2], path);
    return 0;
}

/* ---------- test / [ ---------- */
typedef struct { ShellState *sh; char **a; int n, i, err; } TestCtx;
static int is_binop(const char *s) {
    static const char *const ops[] = { "=", "==", "!=", "-eq", "-ne", "-gt", "-ge", "-lt", "-le" };
    for (size_t k = 0; k < sizeof ops / sizeof *ops; k++) if (!strcmp(s, ops[k])) return 1;
    return 0;
}
static int is_unop(const char *s) {
    return s[0] == '-' && s[1] && !s[2] && strchr("fdesrwxzLn", s[1]);
}
static int t_unary(TestCtx *t, const char *op, const char *arg) {
    char full[PATH_MAX]; struct stat st;
    switch (op[1]) {
    case 'z': return arg[0] == 0;
    case 'n': return arg[0] != 0;
    }
    if (path_resolve(t->sh->cwd, arg, full, sizeof full) < 0) return 0;
    switch (op[1]) {
    case 'e': return stat(full, &st) == 0;
    case 'f': return stat(full, &st) == 0 && S_ISREG(st.st_mode);
    case 'd': return stat(full, &st) == 0 && S_ISDIR(st.st_mode);
    case 's': return stat(full, &st) == 0 && st.st_size > 0;
    case 'L': return lstat(full, &st) == 0 && S_ISLNK(st.st_mode);
    case 'r': return access(full, R_OK) == 0;
    case 'w': return access(full, W_OK) == 0;
    case 'x': return access(full, X_OK) == 0;
    }
    return 0;
}
static int t_binary(TestCtx *t, const char *l, const char *op, const char *r) {
    if (!strcmp(op, "=") || !strcmp(op, "==")) return !strcmp(l, r);
    if (!strcmp(op, "!=")) return strcmp(l, r) != 0;
    long a, b;
    if (str_parse_int(l, &a) < 0 || str_parse_int(r, &b) < 0) {
        sh_err(t->sh, "test: integer expression expected: '%s' %s '%s'", l, op, r);
        t->err = 1; return 0;
    }
    if (!strcmp(op, "-eq")) return a == b;
    if (!strcmp(op, "-ne")) return a != b;
    if (!strcmp(op, "-gt")) return a > b;
    if (!strcmp(op, "-ge")) return a >= b;
    if (!strcmp(op, "-lt")) return a < b;
    return a <= b;
}
static int t_expr(TestCtx *t);
static int t_primary(TestCtx *t) {
    if (t->i >= t->n) { sh_err(t->sh, "test: missing argument"); t->err = 1; return 0; }
    char *s = t->a[t->i];
    if (!strcmp(s, "!") && t->i + 1 < t->n) { t->i++; return !t_primary(t); }
    if (!strcmp(s, "(")) {
        t->i++;
        int v = t_expr(t);
        if (t->i < t->n && !strcmp(t->a[t->i], ")")) t->i++; else { sh_err(t->sh, "test: missing ')'"); t->err = 1; }
        return v;
    }
    if (t->i + 2 < t->n + 0 && is_binop(t->a[t->i + 1])) {
        int v = t_binary(t, s, t->a[t->i + 1], t->a[t->i + 2]);
        t->i += 3; return v;
    }
    if (is_unop(s) && t->i + 1 < t->n) { int v = t_unary(t, s, t->a[t->i + 1]); t->i += 2; return v; }
    t->i++;
    return s[0] != 0;
}
static int t_and(TestCtx *t) {
    int v = t_primary(t);
    while (t->i < t->n && !strcmp(t->a[t->i], "-a")) { t->i++; int r = t_primary(t); v = v && r; }
    return v;
}
static int t_expr(TestCtx *t) {
    int v = t_and(t);
    while (t->i < t->n && !strcmp(t->a[t->i], "-o")) { t->i++; int r = t_and(t); v = v || r; }
    return v;
}
int builtin_test(ShellState *sh, int argc, char **argv) {
    int n = argc - 1;
    if (!strcmp(argv[0], "[")) {
        if (n < 1 || strcmp(argv[argc - 1], "]")) { sh_err(sh, "[: missing `]'"); return 2; }
        n--;
    }
    TestCtx t = { sh, argv + 1, n, 0, 0 };
    if (n == 0) return 1;
    int v = t_expr(&t);
    if (!t.err && t.i < t.n) { sh_err(sh, "test: too many arguments"); t.err = 1; }
    return t.err ? 2 : (v ? 0 : 1);
}

static const Builtin TABLE[] = {
    { "cd", bi_cd, 1 }, { "exit", bi_exit, 1 }, { "history", bi_history, 0 }, { "multiWatch", bi_multiwatch, 1 },
    { "jobs", bi_jobs, 0 }, { "fg", bi_fg, 1 }, { "bg", bi_bg, 1 }, { "source", bi_source, 1 }, { ".", bi_source, 1 },
    { "export", bi_export, 1 }, { "unset", bi_unset, 1 }, { "echo", bi_echo, 0 }, { "pwd", bi_pwd, 0 },
    { "clear", bi_clear, 1 }, { "bind", bi_bind, 1 }, { "true", bi_true, 0 }, { ":", bi_true, 0 },
    { "false", bi_false, 0 }, { "test", builtin_test, 0 }, { "[", builtin_test, 0 },
};

const Builtin *builtin_lookup(const char *name) {
    for (size_t i = 0; i < sizeof TABLE / sizeof *TABLE; i++) if (!strcmp(TABLE[i].name, name)) return &TABLE[i];
    return NULL;
}
