/* history.h - shared, searchable, persistent command history.
 * One History object lives in malloc memory (thread mode, pthread mutex) or in
 * an anonymous MAP_SHARED mapping (process mode, process-shared semaphore). */
#ifndef TERMINATOR_HISTORY_H
#define TERMINATOR_HISTORY_H
#include "common.h"

typedef struct History History;

History *history_open(const char *file, int shared_between_processes);
void     history_close(History *h);
int      history_add(History *h, const char *line);           /* 1 if stored */
int      history_count(History *h);
/* idx 0 = newest. Returns 0 and fills out, or -1 if out of range. */
int      history_get(History *h, int idx, char *out, size_t n);
/* Copies the newest `max` entries (oldest first) and their absolute numbers. */
void     history_recent(History *h, int max, StrList *out, int *first_number);
/* Ctrl+R semantics: exact (most recent) match; else entries sharing the longest
 * common substring (> 2 chars). Returns number of hits, *exact set to 1/0. */
int      history_search(History *h, const char *term, StrList *hits, int *exact);
int      lcs_length(const char *a, const char *b);            /* longest common substring */

#endif
