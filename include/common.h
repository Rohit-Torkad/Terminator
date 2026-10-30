/* common.h - shared constants, dynamic buffer, string list and small helpers. */
#ifndef TERMINATOR_COMMON_H
#define TERMINATOR_COMMON_H

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <sys/types.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define HIST_MAX      10000   /* entries kept in memory and in the history file */
#define HIST_SHOW     1000    /* entries shown by the `history` builtin */
#define HIST_LINE     1024    /* max stored length of one history entry */
#define MAX_TABS      16
#define MAX_JOBS      64
#define MAX_STAGES    64      /* max commands in one pipeline */
#define MAX_WATCH     16      /* max commands in multiWatch */

#define PRINTF_FMT(a, b) __attribute__((format(printf, a, b)))

/* ---- memory (utils/memory.c): allocation failure aborts with a message ---- */
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t sz);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);
char *xstrndup(const char *s, size_t n);

/* Growable byte buffer. d is always NUL terminated once non-NULL. */
typedef struct { char *d; size_t len, cap; } Buf;
void        buf_init(Buf *b);
void        buf_free(Buf *b);
void        buf_reset(Buf *b);
void        buf_reserve(Buf *b, size_t extra);
void        buf_add(Buf *b, const char *s, size_t n);
void        buf_adds(Buf *b, const char *s);
void        buf_addc(Buf *b, char c);
void        buf_addf(Buf *b, const char *fmt, ...) PRINTF_FMT(2, 3);
const char *buf_str(Buf *b);   /* never NULL */
char       *buf_take(Buf *b);  /* caller owns result; buffer is reset */

/* ---- strings (utils/strings.c) ---- */
typedef struct { char **v; int n, cap; } StrList;
void   strlist_init(StrList *l);
void   strlist_add(StrList *l, const char *s);      /* copies s */
void   strlist_add_own(StrList *l, char *s);        /* takes ownership */
void   strlist_free(StrList *l);
void   strlist_sort(StrList *l);
char  *str_trim(char *s);                           /* in place, returns s+skip */
int    str_is_name(const char *s, size_t n);        /* valid shell identifier */
int    str_parse_int(const char *s, long *out);     /* 0 on success */
char  *str_join(char **v, int n, const char *sep);

/* ---- paths (utils/paths.c) ---- */
const char *path_home(void);
/* Make `path` absolute relative to `base`, collapsing . and .. and expanding a
 * leading ~. Never touches the process working directory. */
int    path_resolve(const char *base, const char *path, char *out, size_t n);
void   path_display(const char *cwd, char *out, size_t n);  /* home -> ~ */

/* ---- I/O + logging ---- */
ssize_t write_all(int fd, const void *buf, size_t n);  /* handles EINTR/partial/EAGAIN */
void    log_debug(const char *fmt, ...) PRINTF_FMT(1, 2); /* only if TERMINATOR_DEBUG set */
int     fd_set_nonblock(int fd);

#endif
