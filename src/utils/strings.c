/* strings.c - StrList and small string helpers. */
#include "common.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

void strlist_init(StrList *l) { l->v = NULL; l->n = l->cap = 0; }
void strlist_add_own(StrList *l, char *s) {
    if (l->n + 1 >= l->cap) { l->cap = l->cap ? l->cap * 2 : 8; l->v = xrealloc(l->v, (size_t)l->cap * sizeof *l->v); }
    l->v[l->n++] = s;
    l->v[l->n] = NULL;
}
void strlist_add(StrList *l, const char *s) { strlist_add_own(l, xstrdup(s)); }
void strlist_free(StrList *l) { for (int i = 0; i < l->n; i++) free(l->v[i]); free(l->v); strlist_init(l); }
static int cmp(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
void strlist_sort(StrList *l) { if (l->n > 1) qsort(l->v, (size_t)l->n, sizeof *l->v, cmp); }

char *str_trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    size_t n = strlen(s);
    while (n && isspace((unsigned char)s[n - 1])) s[--n] = 0;
    return s;
}
int str_is_name(const char *s, size_t n) {
    if (!n || !(isalpha((unsigned char)s[0]) || s[0] == '_')) return 0;
    for (size_t i = 1; i < n; i++) if (!(isalnum((unsigned char)s[i]) || s[i] == '_')) return 0;
    return 1;
}
int str_parse_int(const char *s, long *out) {
    char *end; errno = 0;
    if (!*s) return -1;
    long v = strtol(s, &end, 10);
    if (errno || *end) return -1;
    *out = v;
    return 0;
}
char *str_join(char **v, int n, const char *sep) {
    Buf b; buf_init(&b);
    for (int i = 0; i < n; i++) { if (i) buf_adds(&b, sep); buf_adds(&b, v[i]); }
    return buf_take(&b);
}
