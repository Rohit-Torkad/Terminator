/* memory.c - checked allocation and the growable Buf. */
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

static void oom(void) { fputs("terminator: out of memory\n", stderr); abort(); }
void *xmalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) oom(); return p; }
void *xcalloc(size_t n, size_t s) { void *p = calloc(n ? n : 1, s ? s : 1); if (!p) oom(); return p; }
void *xrealloc(void *p, size_t n) { p = realloc(p, n ? n : 1); if (!p) oom(); return p; }
char *xstrdup(const char *s) { size_t n = strlen(s); char *p = xmalloc(n + 1); memcpy(p, s, n + 1); return p; }
char *xstrndup(const char *s, size_t n) { char *p = xmalloc(n + 1); memcpy(p, s, n); p[n] = 0; return p; }

void buf_init(Buf *b) { b->d = NULL; b->len = b->cap = 0; }
void buf_free(Buf *b) { free(b->d); buf_init(b); }
void buf_reset(Buf *b) { b->len = 0; if (b->d) b->d[0] = 0; }
void buf_reserve(Buf *b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return;
    size_t nc = b->cap ? b->cap : 64;
    while (nc < b->len + extra + 1) nc *= 2;
    b->d = xrealloc(b->d, nc);
    b->cap = nc;
}
void buf_add(Buf *b, const char *s, size_t n) {
    buf_reserve(b, n);
    if (n) memcpy(b->d + b->len, s, n);
    b->len += n;
    b->d[b->len] = 0;
}
void buf_adds(Buf *b, const char *s) { buf_add(b, s, strlen(s)); }
void buf_addc(Buf *b, char c) { buf_add(b, &c, 1); }
void buf_addf(Buf *b, const char *fmt, ...) {
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n > 0) {
        buf_reserve(b, (size_t)n);
        vsnprintf(b->d + b->len, (size_t)n + 1, fmt, ap2);
        b->len += (size_t)n;
    }
    va_end(ap2);
}
const char *buf_str(Buf *b) { if (!b->d) buf_reserve(b, 0), b->d[0] = 0; return b->d; }
char *buf_take(Buf *b) { buf_str(b); char *r = b->d; buf_init(b); return r; }
