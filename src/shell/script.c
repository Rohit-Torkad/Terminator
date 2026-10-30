/* script.c - interpreter for the required Bash subset: comments, assignments,
 * $NAME/$1..$9/$#/$?, substitution, pipes, redirection, exit [n], nested
 * if/elif/else/fi with [ ] tests. The script is parsed and validated in full
 * before the first command runs. Nothing is delegated to /bin/bash or /bin/sh. */
#include "script.h"
#include "executor.h"
#include "lexer.h"
#include "parser.h"
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

void stmts_free(StmtList *l) { for (int i = 0; i < l->n; i++) free(l->s[i].text); free(l->s); l->s = NULL; l->n = l->cap = 0; }

static void add_stmt(StmtList *l, StmtKind k, const char *text, size_t n, int line) {
    if (l->n == l->cap) { l->cap = l->cap ? l->cap * 2 : 16; l->s = xrealloc(l->s, (size_t)l->cap * sizeof(Stmt)); }
    l->s[l->n].kind = k; l->s[l->n].text = text ? xstrndup(text, n) : NULL; l->s[l->n].line = line;
    l->n++;
}

static int line_of(const char *src, size_t off) { int ln = 1; for (size_t i = 0; i < off; i++) if (src[i] == '\n') ln++; return ln; }

/* Split into statements at ; and newlines (but not inside quotes, and not after |, &&, ||). */
static int split_stmts(const char *src, StmtList *out, char *err, size_t en) {
    TokenList tl; char e2[200];
    memset(out, 0, sizeof *out);
    if (lex(src, &tl, e2, sizeof e2) < 0) { snprintf(err, en, "syntax error: %s", e2); return -1; }
    int i = 0;
    while (i < tl.n) {
        while (i < tl.n && (tl.t[i].type == T_SEMI || tl.t[i].type == T_NL)) i++;
        if (i >= tl.n) break;
        int a = i, b = i;                              /* statement is tokens [a, b) */
        while (b < tl.n) {
            TokType t = tl.t[b].type;
            if (t == T_SEMI) break;
            if (t == T_NL) {
                TokType pt = b > a ? tl.t[b - 1].type : T_NL;
                if (pt != T_PIPE && pt != T_AND && pt != T_OR) break;
            }
            b++;
        }
        i = b;
        int line = line_of(src, tl.t[a].start);
        const Token *f = &tl.t[a];
        const char *kw = f->type == T_WORD ? f->text : "";
        size_t end = tl.t[b - 1].end;
        int rest = a + 1 < b;
        size_t rs = rest ? tl.t[a + 1].start : 0;
        if (!strcmp(kw, "if") || !strcmp(kw, "elif")) {
            if (!rest) { snprintf(err, en, "line %d: syntax error: '%s' needs a condition", line, kw); goto fail; }
            add_stmt(out, kw[1] == 'f' ? SK_IF : SK_ELIF, src + rs, end - rs, line);
        } else if (!strcmp(kw, "then")) {
            add_stmt(out, SK_THEN, NULL, 0, line);
            if (rest) add_stmt(out, SK_CMD, src + rs, end - rs, line);
        } else if (!strcmp(kw, "else")) {
            add_stmt(out, SK_ELSE, NULL, 0, line);
            if (rest) add_stmt(out, SK_CMD, src + rs, end - rs, line);
        } else if (!strcmp(kw, "fi")) {
            if (rest) { snprintf(err, en, "line %d: syntax error near unexpected token after 'fi'", line); goto fail; }
            add_stmt(out, SK_FI, NULL, 0, line);
        } else if (!strcmp(kw, "while") || !strcmp(kw, "until") || !strcmp(kw, "for") || !strcmp(kw, "do") ||
                   !strcmp(kw, "done") || !strcmp(kw, "case") || !strcmp(kw, "esac") || !strcmp(kw, "function")) {
            snprintf(err, en, "line %d: '%s' is not supported by this shell (only if/elif/else/fi)", line, kw);
            goto fail;
        } else add_stmt(out, SK_CMD, src + f->start, end - f->start, line);
    }
    tokens_free(&tl);
    return 0;
fail:
    tokens_free(&tl); stmts_free(out);
    return -1;
}

int script_parse(const char *text, StmtList *out, char *err, size_t en) {
    if (split_stmts(text, out, err, en) < 0) return -1;
    struct { int need_then, seen_else, line; } stack[256];
    int sp = 0;
    for (int i = 0; i < out->n; i++) {
        Stmt *s = &out->s[i];
        char e2[200];
        switch (s->kind) {
        case SK_IF:
            if (sp >= 256) { snprintf(err, en, "line %d: if blocks nested too deeply", s->line); goto fail; }
            stack[sp].need_then = 1; stack[sp].seen_else = 0; stack[sp].line = s->line; sp++;
            break;
        case SK_ELIF:
            if (!sp || stack[sp - 1].seen_else || stack[sp - 1].need_then) { snprintf(err, en, "line %d: syntax error: unexpected 'elif'", s->line); goto fail; }
            stack[sp - 1].need_then = 1;
            break;
        case SK_ELSE:
            if (!sp || stack[sp - 1].seen_else || stack[sp - 1].need_then) { snprintf(err, en, "line %d: syntax error: unexpected 'else'", s->line); goto fail; }
            stack[sp - 1].seen_else = 1;
            break;
        case SK_THEN:
            if (!sp || !stack[sp - 1].need_then) { snprintf(err, en, "line %d: syntax error: unexpected 'then'", s->line); goto fail; }
            stack[sp - 1].need_then = 0;
            break;
        case SK_FI:
            if (!sp) { snprintf(err, en, "line %d: syntax error: unexpected 'fi'", s->line); goto fail; }
            if (stack[sp - 1].need_then) { snprintf(err, en, "line %d: syntax error: missing 'then'", s->line); goto fail; }
            sp--;
            break;
        case SK_CMD:
            if (sp && stack[sp - 1].need_then) { snprintf(err, en, "line %d: syntax error: expected 'then' before command", s->line); goto fail; }
            break;
        }
        if (s->text) {                                   /* validate command syntax up front */
            CmdList cl;
            if (parse_list(s->text, &cl, e2, sizeof e2) < 0) { snprintf(err, en, "line %d: %s", s->line, e2); goto fail; }
            cmdlist_free(&cl);
        }
    }
    if (sp) { snprintf(err, en, "syntax error: missing 'fi' for 'if' on line %d", stack[sp - 1].line); goto fail; }
    return 0;
fail:
    stmts_free(out);
    return -1;
}

int script_open_blocks(const char *text) {
    StmtList sl; char err[64];
    if (split_stmts(text, &sl, err, sizeof err) < 0) return 0;
    int d = 0;
    for (int i = 0; i < sl.n; i++) { if (sl.s[i].kind == SK_IF) d++; else if (sl.s[i].kind == SK_FI && d > 0) d--; }
    stmts_free(&sl);
    return d;
}

/* ---------- execution ---------- */
typedef struct { ShellState *sh; const StmtList *l; int pos; } Run;
static int stop(ShellState *sh) { return sh->interrupted || sh->script_exit || sh->want_exit; }

static void run_block(Run *r, int exec);

static void run_if(Run *r, int exec) {
    ShellState *sh = r->sh;
    int taken = 0;
    for (;;) {
        const Stmt *c = &r->l->s[r->pos];
        int cond = 1;
        if (exec && !taken && !stop(sh)) { exec_line(sh, c->text); cond = sh->last_status; }
        r->pos++;
        if (r->pos < r->l->n && r->l->s[r->pos].kind == SK_THEN) r->pos++;
        int run = exec && !taken && cond == 0 && !stop(sh);
        run_block(r, run);
        if (run) taken = 1;
        const Stmt *k = &r->l->s[r->pos];
        if (k->kind == SK_ELIF) continue;
        if (k->kind == SK_ELSE) {
            r->pos++;
            int er = exec && !taken && !stop(sh);
            run_block(r, er);
            if (er) taken = 1;
        }
        r->pos++;                                        /* fi */
        if (exec && !taken && !stop(sh)) sh->last_status = 0;
        return;
    }
}

static void run_block(Run *r, int exec) {
    while (r->pos < r->l->n) {
        const Stmt *s = &r->l->s[r->pos];
        if (s->kind == SK_ELIF || s->kind == SK_ELSE || s->kind == SK_FI) return;
        if (s->kind == SK_IF) { run_if(r, exec); continue; }
        if (s->kind == SK_CMD && exec && !stop(r->sh)) exec_line(r->sh, s->text);
        r->pos++;
    }
}

int script_exec_stmts(ShellState *sh, const StmtList *l) {
    Run r = { sh, l, 0 };
    run_block(&r, 1);
    return sh->last_status;
}

static Var *clone_exported(Var *v) {
    Var *head = NULL;
    for (; v; v = v->next) {
        if (!v->exported) continue;
        Var *c = xcalloc(1, sizeof *c);
        c->name = xstrdup(v->name); c->value = xstrdup(v->value); c->exported = 1;
        c->next = head; head = c;
    }
    return head;
}
static void free_vars(Var *v) { while (v) { Var *n = v->next; free(v->name); free(v->value); free(v); v = n; } }

int script_run_text(ShellState *sh, const char *text, const char *name, int argc, char **argv, int separate) {
    StmtList sl; char err[300];
    if (script_parse(text, &sl, err, sizeof err) < 0) {      /* validate before executing anything */
        sh_err(sh, "%s: %s", name, err);
        sh->last_status = 2;
        return 2;
    }
    char *saved_args[10]; int saved_nargs = sh->nargs;
    int set_args = separate || argc > 0;
    Var *saved_vars = NULL; char saved_cwd[PATH_MAX], saved_old[PATH_MAX];
    memcpy(saved_args, sh->args, sizeof saved_args);
    if (set_args) {
        memset(sh->args, 0, sizeof sh->args);
        sh->args[0] = xstrdup(name);
        sh->nargs = argc > 9 ? 9 : argc;
        for (int i = 0; i < sh->nargs; i++) sh->args[i + 1] = xstrdup(argv[i]);
    }
    if (separate) {
        saved_vars = sh->vars;
        sh->vars = clone_exported(saved_vars);
        snprintf(saved_cwd, sizeof saved_cwd, "%s", sh->cwd);
        snprintf(saved_old, sizeof saved_old, "%s", sh->oldpwd);
    }
    sh->nest++; sh->script_exit = 0;
    script_exec_stmts(sh, &sl);
    int rc = sh->last_status;
    sh->nest--; sh->script_exit = 0;
    if (set_args) {
        for (int i = 0; i < 10; i++) free(sh->args[i]);
        memcpy(sh->args, saved_args, sizeof saved_args);
        sh->nargs = saved_nargs;
    }
    if (separate) {
        free_vars(sh->vars); sh->vars = saved_vars;
        snprintf(sh->cwd, sizeof sh->cwd, "%s", saved_cwd);
        snprintf(sh->oldpwd, sizeof sh->oldpwd, "%s", saved_old);
    }
    stmts_free(&sl);
    sh->last_status = rc;
    return rc;
}

static char *read_whole(const char *path, int *err) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) { *err = errno; return NULL; }
    Buf b; buf_init(&b);
    char tmp[8192]; ssize_t n;
    while ((n = read(fd, tmp, sizeof tmp)) != 0) {
        if (n < 0) { if (errno == EINTR) continue; *err = errno; close(fd); buf_free(&b); return NULL; }
        buf_add(&b, tmp, (size_t)n);
    }
    close(fd);
    return buf_take(&b);
}

int script_run_file(ShellState *sh, const char *path, int argc, char **argv, int separate) {
    char full[PATH_MAX]; int err = 0;
    if (path_resolve(sh->cwd, path, full, sizeof full) < 0) { sh_err(sh, "%s: path too long", path); return 1; }
    char *text = read_whole(full, &err);
    if (!text) { sh_err(sh, "%s: %s", path, strerror(err)); sh->last_status = err == ENOENT ? 127 : 126; return sh->last_status; }
    int rc = script_run_text(sh, text, path, argc, argv, separate);
    free(text);
    return rc;
}

static int is_shell_name(const char *interp) {
    char tmp[PATH_MAX]; snprintf(tmp, sizeof tmp, "%s", interp);
    const char *b = basename(tmp);
    return !strcmp(b, "bash") || !strcmp(b, "sh") || !strcmp(b, "dash");
}

int script_is_candidate(ShellState *sh, int argc, char **argv) {
    (void)argc;
    if (is_shell_name(argv[0])) return 1;
    if (!strchr(argv[0], '/')) return 0;
    char full[PATH_MAX];
    if (path_resolve(sh->cwd, argv[0], full, sizeof full) < 0) return 0;
    struct stat st;
    if (stat(full, &st) != 0 || !S_ISREG(st.st_mode) || access(full, X_OK) != 0) return 0;
    int fd = open(full, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    char head[256]; ssize_t n = read(fd, head, sizeof head - 1);
    close(fd);
    if (n <= 0) return 0;
    head[n] = 0;
    if (n >= 4 && !memcmp(head, "\177ELF", 4)) return 0;
    if (head[0] == '#' && head[1] == '!') {                 /* only shell shebangs are interpreted here */
        char *nl = strchr(head, '\n'); if (nl) *nl = 0;
        char *save = NULL, *t1 = strtok_r(head + 2, " \t", &save);
        if (!t1) return 0;
        char tmp[PATH_MAX]; snprintf(tmp, sizeof tmp, "%s", t1);
        if (!strcmp(basename(tmp), "env")) { t1 = strtok_r(NULL, " \t", &save); if (!t1) return 0; }
        return is_shell_name(t1);
    }
    return memchr(head, 0, (size_t)n) == NULL;               /* no shebang, text file: treat as script */
}

int script_try_command(ShellState *sh, int argc, char **argv, int *status) {
    if (!script_is_candidate(sh, argc, argv)) return 0;
    if (is_shell_name(argv[0])) {
        int i = 1; const char *code = NULL;
        while (i < argc && argv[i][0] == '-' && argv[i][1]) {
            if (!strcmp(argv[i], "--")) { i++; break; }
            if (!strcmp(argv[i], "-c")) {
                if (i + 1 >= argc) { sh_err(sh, "%s: -c: option requires an argument", argv[0]); *status = 2; return 1; }
                code = argv[i + 1]; i += 2; break;
            }
            i++;                                           /* -e -x -u ... accepted and ignored */
        }
        if (code) {
            const char *nm = i < argc ? argv[i] : "bash";
            *status = script_run_text(sh, code, nm, i < argc ? argc - i - 1 : 0, argv + (i < argc ? i + 1 : i), 1);
        } else if (i < argc) *status = script_run_file(sh, argv[i], argc - i - 1, argv + i + 1, 1);
        else { sh_err(sh, "%s: interactive shells are not supported; give a script file or -c", argv[0]); *status = 1; }
        return 1;
    }
    *status = script_run_file(sh, argv[0], argc - 1, argv + 1, 1);
    return 1;
}
