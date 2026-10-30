/* expansion.c - word expansion. Order within a word: quote handling, tilde,
 * parameter expansion, command substitution (innermost first, via recursion
 * through the same parser/executor), word splitting of unquoted results,
 * then wildcard expansion. No hidden system shell is involved. */
#include "expansion.h"
#include "lexer.h"
#include <dirent.h>
#include <fnmatch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct {
    ShellState *sh;
    Buf s, q;            /* text and parallel 'q'(quoted)/'u'(unquoted) flags */
    int has, nosplit, do_glob;
    StrList *out;
} Ctx;

static void fadd(Ctx *c, char ch, int quoted) { buf_addc(&c->s, ch); buf_addc(&c->q, quoted ? 'q' : 'u'); c->has = 1; }

static int has_wild(const Ctx *c) {
    for (size_t i = 0; i < c->s.len; i++)
        if (c->q.d[i] == 'u' && (c->s.d[i] == '*' || c->s.d[i] == '?' || c->s.d[i] == '[')) return 1;
    return 0;
}

static void fend(Ctx *c) {
    if (!c->has) return;
    if (c->do_glob && has_wild(c)) {
        Buf pat; buf_init(&pat);
        for (size_t i = 0; i < c->s.len; i++) {
            if (c->q.d[i] == 'q' && strchr("*?[]\\", c->s.d[i])) buf_addc(&pat, '\\');
            buf_addc(&pat, c->s.d[i]);
        }
        StrList g; strlist_init(&g);
        if (glob_expand(c->sh->cwd, buf_str(&pat), &g) > 0) {
            for (int i = 0; i < g.n; i++) strlist_add_own(c->out, g.v[i]);
            free(g.v);
        } else strlist_add(c->out, buf_str(&c->s));
        buf_free(&pat);
    } else strlist_add(c->out, buf_str(&c->s));
    buf_reset(&c->s); buf_reset(&c->q); c->has = 0;
}

static void emit_value(Ctx *c, const char *v, int quoted) {
    for (const char *p = v; *p; p++) {
        if (!quoted && !c->nosplit && (*p == ' ' || *p == '\t' || *p == '\n')) fend(c);
        else fadd(c, *p, quoted || c->nosplit);
    }
    if (quoted) c->has = 1;
}

static char *unescape_backtick(const char *s, size_t n) {
    Buf b; buf_init(&b);
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n && (s[i + 1] == '`' || s[i + 1] == '\\' || s[i + 1] == '$')) i++;
        buf_addc(&b, s[i]);
    }
    return buf_take(&b);
}

/* raw[*i] == '$' */
static void expand_dollar(Ctx *c, const char *raw, size_t *i, int quoted) {
    size_t p = *i + 1;
    char ch = raw[p];
    if (ch == '(') {
        size_t j = lex_find_close_paren(raw, p + 1);
        if (j == (size_t)-1) { fadd(c, '$', quoted); (*i)++; return; }
        char *cmd = xstrndup(raw + p + 1, j - p - 1);
        char *res = sh_command_substitute(c->sh, cmd);
        emit_value(c, res, quoted);
        free(cmd); free(res);
        *i = j + 1; return;
    }
    char name[256]; size_t n = 0;
    if (ch == '{') {
        const char *e = strchr(raw + p, '}');
        if (!e) { fadd(c, '$', quoted); (*i)++; return; }
        n = (size_t)(e - (raw + p + 1));
        if (n >= sizeof name) n = sizeof name - 1;
        memcpy(name, raw + p + 1, n); name[n] = 0;
        p = (size_t)(e - raw) + 1;
    } else if (ch == '?' || ch == '#' || ch == '$' || (ch >= '0' && ch <= '9')) {
        name[0] = ch; name[1] = 0; p++;
    } else if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_') {
        while ((raw[p] >= 'a' && raw[p] <= 'z') || (raw[p] >= 'A' && raw[p] <= 'Z') || raw[p] == '_' || (raw[p] >= '0' && raw[p] <= '9')) {
            if (n < sizeof name - 1) name[n++] = raw[p];
            p++;
        }
        name[n] = 0;
    } else { fadd(c, '$', quoted); (*i)++; return; }
    const char *v = sh_get_var(c->sh, name);
    emit_value(c, v ? v : "", quoted);
    *i = p;
}

static void expand_raw(Ctx *c, const char *raw) {
    size_t i = 0;
    while (raw[i]) {
        char ch = raw[i];
        if (ch == '\\') {
            if (raw[i + 1] == '\n') { i += 2; continue; }
            if (raw[i + 1]) { fadd(c, raw[i + 1], 1); i += 2; } else { fadd(c, '\\', 1); i++; }
        } else if (ch == '\'') {
            i++; c->has = 1;
            while (raw[i] && raw[i] != '\'') fadd(c, raw[i++], 1);
            if (raw[i]) i++;
        } else if (ch == '"') {
            i++; c->has = 1;
            while (raw[i] && raw[i] != '"') {
                if (raw[i] == '\\' && raw[i + 1] && strchr("$`\"\\\n", raw[i + 1])) {
                    if (raw[i + 1] != '\n') fadd(c, raw[i + 1], 1);
                    i += 2;
                } else if (raw[i] == '$') expand_dollar(c, raw, &i, 1);
                else if (raw[i] == '`') {
                    size_t j = lex_find_close_backtick(raw, i + 1);
                    if (j == (size_t)-1) { fadd(c, '`', 1); i++; continue; }
                    char *cmd = unescape_backtick(raw + i + 1, j - i - 1);
                    char *res = sh_command_substitute(c->sh, cmd);
                    emit_value(c, res, 1); free(cmd); free(res);
                    i = j + 1;
                } else fadd(c, raw[i++], 1);
            }
            if (raw[i]) i++;
        } else if (ch == '$') expand_dollar(c, raw, &i, 0);
        else if (ch == '`') {
            size_t j = lex_find_close_backtick(raw, i + 1);
            if (j == (size_t)-1) { fadd(c, '`', 0); i++; continue; }
            char *cmd = unescape_backtick(raw + i + 1, j - i - 1);
            char *res = sh_command_substitute(c->sh, cmd);
            emit_value(c, res, 0); free(cmd); free(res);
            i = j + 1;
        } else if (ch == '~' && i == 0 && (raw[1] == '/' || raw[1] == 0)) {
            const char *h = sh_get_var(c->sh, "HOME");
            for (const char *p = h ? h : path_home(); *p; p++) fadd(c, *p, 1);
            i++;
        } else { fadd(c, ch, 0); i++; }
    }
}

int expand_word(ShellState *sh, const char *raw, StrList *out, int do_glob) {
    Ctx c; memset(&c, 0, sizeof c);
    c.sh = sh; c.out = out; c.do_glob = do_glob;
    buf_init(&c.s); buf_init(&c.q);
    int before = out->n;
    expand_raw(&c, raw);
    fend(&c);
    buf_free(&c.s); buf_free(&c.q);
    return out->n - before;
}

char *expand_string(ShellState *sh, const char *raw) {
    Ctx c; memset(&c, 0, sizeof c);
    StrList dummy; strlist_init(&dummy);
    c.sh = sh; c.out = &dummy; c.nosplit = 1;
    buf_init(&c.s); buf_init(&c.q);
    expand_raw(&c, raw);
    char *r = buf_take(&c.s);
    buf_free(&c.q); strlist_free(&dummy);
    return r;
}

/* ---------- wildcards ---------- */
static int comp_has_wild(const char *s) {
    for (; *s; s++) { if (*s == '\\' && s[1]) { s++; continue; } if (*s == '*' || *s == '?' || *s == '[') return 1; }
    return 0;
}
static void comp_unescape(const char *s, char *out) {
    while (*s) { if (*s == '\\' && s[1]) s++; *out++ = *s++; }
    *out = 0;
}

static void glob_rec(const char *cwd, const char *prefix, char **comps, int ci, int nc, StrList *out) {
    if (ci == nc) {
        char full[PATH_MAX]; struct stat st;
        if (path_resolve(cwd, prefix[0] ? prefix : ".", full, sizeof full) == 0 && lstat(full, &st) == 0) {
            size_t l = strlen(prefix);
            strlist_add_own(out, xstrndup(prefix, (l > 1 && prefix[l - 1] == '/') ? l - 1 : l));
        }
        return;
    }
    const char *comp = comps[ci];
    int last = ci == nc - 1;
    char dir[PATH_MAX];
    if (path_resolve(cwd, prefix[0] ? prefix : ".", dir, sizeof dir) < 0) return;
    if (!comp_has_wild(comp)) {
        char name[NAME_MAX + 1]; comp_unescape(comp, name);
        char np[PATH_MAX * 2]; snprintf(np, sizeof np, "%s%s%s", prefix, name, last ? "" : "/");
        glob_rec(cwd, np, comps, ci + 1, nc, out);
        return;
    }
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (fnmatch(comp, e->d_name, FNM_PERIOD) != 0) continue;
        char np[PATH_MAX * 2];
        snprintf(np, sizeof np, "%s%s%s", prefix, e->d_name, last ? "" : "/");
        if (!last) {                               /* intermediate must be a directory */
            char sub[PATH_MAX * 2]; struct stat st;
            snprintf(sub, sizeof sub, "%s/%s", dir, e->d_name);
            if (stat(sub, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        }
        glob_rec(cwd, np, comps, ci + 1, nc, out);
    }
    closedir(d);
}

int glob_expand(const char *cwd, const char *pattern, StrList *out) {
    char *copy = xstrdup(pattern);
    char **comps = xmalloc(sizeof(char *) * (strlen(pattern) / 1 + 2));
    int nc = 0; char *save = NULL;
    for (char *t = strtok_r(copy, "/", &save); t; t = strtok_r(NULL, "/", &save)) comps[nc++] = t;
    int before = out->n;
    glob_rec(cwd, pattern[0] == '/' ? "/" : "", comps, 0, nc, out);
    /* sort only the newly added part */
    StrList tmp; strlist_init(&tmp);
    for (int i = before; i < out->n; i++) strlist_add_own(&tmp, out->v[i]);
    strlist_sort(&tmp);
    for (int i = 0; i < tmp.n; i++) out->v[before + i] = tmp.v[i];
    free(tmp.v);
    free(comps); free(copy);
    return out->n - before;
}
