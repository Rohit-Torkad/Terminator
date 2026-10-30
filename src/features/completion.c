/* completion.c - filename completion for a tab (uses the tab's cwd, not the
 * process cwd). */
#include "completion.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

size_t completion_word_start(const char *line, size_t cursor, char *quote) {
    size_t start = 0; char q = 0;
    for (size_t i = 0; i < cursor; i++) {
        char c = line[i];
        if (q == '\'') { if (c == '\'') q = 0; continue; }
        if (q == '"')  { if (c == '\\' && i + 1 < cursor) i++; else if (c == '"') q = 0; continue; }
        if (c == '\\' && i + 1 < cursor) { i++; continue; }
        if (c == '\'' || c == '"') { q = c; continue; }
        if (c == ' ' || c == '\t' || c == '|' || c == ';' || c == '&' || c == '<' || c == '>' || c == '(') start = i + 1;
    }
    *quote = q;
    return start;
}

static void unescape_word(const char *w, char quote, Buf *out) {
    char q = 0;
    (void)quote;
    for (size_t i = 0; w[i]; i++) {
        char c = w[i];
        if (q) { if (c == q) q = 0; else if (q == '"' && c == '\\' && w[i + 1]) buf_addc(out, w[++i]); else buf_addc(out, c); continue; }
        if (c == '\\' && w[i + 1]) { buf_addc(out, w[++i]); continue; }
        if (c == '\'' || c == '"') { q = c; continue; }
        buf_addc(out, c);
    }
}

int completion_candidates(const char *cwd, const char *rawword, StrList *out) {
    Buf word; buf_init(&word);
    unescape_word(rawword, 0, &word);
    const char *w = buf_str(&word);
    const char *slash = strrchr(w, '/');
    char dirpart[PATH_MAX], prefix[NAME_MAX + 1];
    const char *typed_dir = "";
    size_t tdlen = 0;
    if (slash) { tdlen = (size_t)(slash - w) + 1; snprintf(prefix, sizeof prefix, "%s", slash + 1); }
    else snprintf(prefix, sizeof prefix, "%s", w);
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof tmp, "%.*s", (int)tdlen, w);
    typed_dir = tmp;
    if (tdlen == 0) snprintf(dirpart, sizeof dirpart, "%s", cwd);
    else if (path_resolve(cwd, tdlen ? typed_dir : ".", dirpart, sizeof dirpart) < 0) { buf_free(&word); return 0; }
    DIR *d = opendir(dirpart);
    if (!d) { buf_free(&word); return 0; }
    size_t pl = strlen(prefix);
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (e->d_name[0] == '.' && prefix[0] != '.') continue;    /* hidden only if asked */
        if (strncmp(e->d_name, prefix, pl)) continue;
        char full[PATH_MAX * 2];
        snprintf(full, sizeof full, "%s/%s", dirpart, e->d_name);
        struct stat st;
        int isdir = (e->d_type == DT_DIR) || (e->d_type == DT_LNK && stat(full, &st) == 0 && S_ISDIR(st.st_mode)) ||
                    (e->d_type == DT_UNKNOWN && stat(full, &st) == 0 && S_ISDIR(st.st_mode));
        char cand[PATH_MAX * 2];
        snprintf(cand, sizeof cand, "%.*s%s%s", (int)tdlen, w, e->d_name, isdir ? "/" : "");
        strlist_add(out, cand);
    }
    closedir(d);
    strlist_sort(out);
    buf_free(&word);
    return out->n;
}

char *completion_common_prefix(const StrList *l) {
    if (!l->n) return xstrdup("");
    size_t len = strlen(l->v[0]);
    for (int i = 1; i < l->n; i++) {
        size_t k = 0;
        while (k < len && l->v[i][k] == l->v[0][k]) k++;
        len = k;
    }
    return xstrndup(l->v[0], len);
}

void completion_escape(const char *name, char quote, Buf *out) {
    for (const char *p = name; *p; p++) {
        if (!quote && strchr(" \t\"'\\$`&|;<>()*?[]#~!{}", *p)) buf_addc(out, '\\');
        else if (quote == '"' && strchr("\"\\$`", *p)) buf_addc(out, '\\');
        buf_addc(out, *p);
    }
}
