/* history.c - shared history with a mutex (thread mode) or a process-shared
 * semaphore in shared memory (process mode); persisted append-only to a file
 * that is trimmed to the last HIST_MAX lines when opened. */
#include "history.h"
#include <pthread.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct History {
    int shared;
    pthread_mutex_t mu;
    sem_t sem;
    char path[PATH_MAX];
    int head, count;
    long serial;                       /* total entries ever stored (absolute numbering) */
    char lines[HIST_MAX][HIST_LINE];
};

static void lock(History *h) {
    if (!h->shared) { pthread_mutex_lock(&h->mu); return; }
    for (int tries = 0; tries < 2; tries++) {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 2;
        int r;
        while ((r = sem_timedwait(&h->sem, &ts)) < 0 && errno == EINTR) {}
        if (r == 0) return;
        /* A tab process that died while holding the lock would block everyone
         * forever; critical sections are microseconds, so after 2s assume it
         * crashed and release on its behalf. */
        sem_post(&h->sem);
    }
}
static void unlock(History *h) { if (h->shared) sem_post(&h->sem); else pthread_mutex_unlock(&h->mu); }

static void push(History *h, const char *line) {
    int idx;
    if (h->count < HIST_MAX) idx = (h->head + h->count++) % HIST_MAX;
    else { idx = h->head; h->head = (h->head + 1) % HIST_MAX; }
    snprintf(h->lines[idx], HIST_LINE, "%s", line);
    h->serial++;
}

static void load_file(History *h) {
    FILE *f = fopen(h->path, "r");
    if (!f) return;
    char *ln = NULL; size_t cap = 0; ssize_t n; long total = 0;
    while ((n = getline(&ln, &cap, f)) >= 0) {
        while (n && (ln[n - 1] == '\n' || ln[n - 1] == '\r')) ln[--n] = 0;
        if (!n) continue;
        push(h, ln); total++;
    }
    free(ln); fclose(f);
    if (total > HIST_MAX) {            /* rewrite trimmed copy atomically */
        char tmp[PATH_MAX + 16];
        snprintf(tmp, sizeof tmp, "%s.tmp%d", h->path, (int)getpid());
        FILE *o = fopen(tmp, "w");
        if (!o) return;
        for (int i = 0; i < h->count; i++) fprintf(o, "%s\n", h->lines[(h->head + i) % HIST_MAX]);
        if (fclose(o) == 0) rename(tmp, h->path); else unlink(tmp);
    }
}

History *history_open(const char *file, int shared) {
    History *h;
    if (shared) {
        h = mmap(NULL, sizeof *h, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (h == MAP_FAILED) return NULL;
        memset(h, 0, offsetof(History, lines));
        if (sem_init(&h->sem, 1, 1) != 0) { munmap(h, sizeof *h); return NULL; }
    } else {
        h = xcalloc(1, sizeof *h);
        pthread_mutex_init(&h->mu, NULL);
    }
    h->shared = shared;
    snprintf(h->path, sizeof h->path, "%s", file ? file : "");
    if (file && *file) load_file(h);
    return h;
}

void history_close(History *h) {
    if (!h) return;
    if (h->shared) { sem_destroy(&h->sem); munmap(h, sizeof *h); }
    else { pthread_mutex_destroy(&h->mu); free(h); }
}

int history_add(History *h, const char *line) {
    char buf[HIST_LINE];
    size_t j = 0;
    while (*line == ' ' || *line == '\t') line++;
    for (size_t i = 0; line[i] && j < sizeof buf - 1; i++) buf[j++] = (line[i] == '\n' || line[i] == '\r') ? ' ' : line[i];
    while (j && (buf[j - 1] == ' ' || buf[j - 1] == '\t')) j--;
    buf[j] = 0;
    if (!j) return 0;
    lock(h);
    if (h->count && !strcmp(h->lines[(h->head + h->count - 1) % HIST_MAX], buf)) { unlock(h); return 0; }
    push(h, buf);
    if (h->path[0]) {                  /* failure to persist is non-fatal */
        int fd = open(h->path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0600);
        if (fd >= 0) { buf[j] = '\n'; if (write_all(fd, buf, j + 1) < 0) { /* ignore */ } close(fd); }
    }
    unlock(h);
    return 1;
}

int history_count(History *h) { lock(h); int c = h->count; unlock(h); return c; }

int history_get(History *h, int idx, char *out, size_t n) {
    int rc = -1;
    lock(h);
    if (idx >= 0 && idx < h->count) { snprintf(out, n, "%s", h->lines[(h->head + h->count - 1 - idx) % HIST_MAX]); rc = 0; }
    unlock(h);
    return rc;
}

void history_recent(History *h, int max, StrList *out, int *first_number) {
    lock(h);
    int n = h->count < max ? h->count : max;
    if (first_number) *first_number = (int)(h->serial - n + 1);
    for (int i = h->count - n; i < h->count; i++) strlist_add(out, h->lines[(h->head + i) % HIST_MAX]);
    unlock(h);
}

int lcs_length(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    if (!la || !lb) return 0;
    int *row = xcalloc(lb + 1, sizeof(int)), best = 0;
    for (size_t i = 1; i <= la; i++) {
        int prev = 0;                                   /* row[j-1] of previous i */
        for (size_t j = 1; j <= lb; j++) {
            int tmp = row[j];
            row[j] = (a[i - 1] == b[j - 1]) ? prev + 1 : 0;
            if (row[j] > best) best = row[j];
            prev = tmp;
        }
    }
    free(row);
    return best;
}

int history_search(History *h, const char *term, StrList *hits, int *exact) {
    *exact = 0;
    if (!term || !*term) return 0;
    char t[256]; snprintf(t, sizeof t, "%s", term);
    lock(h);
    for (int i = h->count - 1; i >= 0; i--) {           /* most recent exact match */
        const char *e = h->lines[(h->head + i) % HIST_MAX];
        if (!strcmp(e, t)) { strlist_add(hits, e); *exact = 1; unlock(h); return 1; }
    }
    int best = 0;
    int *score = xmalloc(sizeof(int) * (size_t)(h->count + 1));
    for (int i = h->count - 1; i >= 0; i--) {
        score[i] = lcs_length(h->lines[(h->head + i) % HIST_MAX], t);
        if (score[i] > best) best = score[i];
    }
    if (best > 2) {
        for (int i = h->count - 1; i >= 0; i--) {
            if (score[i] != best) continue;
            const char *e = h->lines[(h->head + i) % HIST_MAX];
            int dup = 0;
            for (int k = 0; k < hits->n; k++) if (!strcmp(hits->v[k], e)) { dup = 1; break; }
            if (!dup) strlist_add(hits, e);
        }
    }
    free(score);
    unlock(h);
    return hits->n;
}
