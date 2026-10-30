#include "testutil.h"
#include "history.h"
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>

static void *adder(void *arg) {
    History *h = arg; char b[64];
    for (int i = 0; i < 500; i++) { snprintf(b, sizeof b, "cmd-%p-%d", (void *)pthread_self(), i); history_add(h, b); }
    return NULL;
}

int main(void) {
    char file[] = "/tmp/term_hist_XXXXXX";
    int fd = mkstemp(file); close(fd); unlink(file);
    History *h = history_open(file, 0);
    CHECK(history_count(h) == 0);
    CHECK(history_add(h, "ls -la") == 1);
    CHECK(history_add(h, "ls -la") == 0);                    /* consecutive duplicate */
    CHECK(history_add(h, "   ") == 0);                       /* blank */
    CHECK(history_add(h, "pwd") == 1);
    CHECK(history_add(h, "echo hello world") == 1);
    CHECK(history_add(h, "echo hello there") == 1);
    CHECK(history_count(h) == 4);
    char b[HIST_LINE];
    CHECK(history_get(h, 0, b, sizeof b) == 0); CHECK_STR(b, "echo hello there");
    CHECK(history_get(h, 3, b, sizeof b) == 0); CHECK_STR(b, "ls -la");
    CHECK(history_get(h, 4, b, sizeof b) < 0);

    StrList hits; int exact;
    strlist_init(&hits);
    CHECK(history_search(h, "pwd", &hits, &exact) == 1 && exact == 1); strlist_free(&hits);
    CHECK(history_search(h, "echo hello wor", &hits, &exact) == 1 && !exact);   /* longest common substring */
    CHECK_STR(hits.v[0], "echo hello world"); strlist_free(&hits);
    CHECK(history_search(h, "hello", &hits, &exact) == 2 && !exact);            /* two commands tie */
    strlist_free(&hits);
    CHECK(history_search(h, "zzzz", &hits, &exact) == 0); strlist_free(&hits);
    CHECK(history_search(h, "ls", &hits, &exact) == 0);                          /* common substring must be > 2 chars */
    strlist_free(&hits);
    CHECK(lcs_length("abcdef", "xxcdexx") == 3);
    CHECK(lcs_length("", "abc") == 0);

    StrList r; strlist_init(&r); int first;
    history_recent(h, 2, &r, &first);
    CHECK(r.n == 2 && first == 3); CHECK_STR(r.v[0], "echo hello world"); strlist_free(&r);

    history_close(h);                                        /* persistence */
    h = history_open(file, 0);
    CHECK(history_count(h) == 4);
    CHECK(history_get(h, 0, b, sizeof b) == 0); CHECK_STR(b, "echo hello there");

    pthread_t th[4];                                         /* thread mode: mutex */
    for (int i = 0; i < 4; i++) pthread_create(&th[i], NULL, adder, h);
    for (int i = 0; i < 4; i++) pthread_join(th[i], NULL);
    CHECK(history_count(h) == 4 + 2000);
    history_close(h);

    h = history_open(file, 1);                               /* process mode: shared memory + semaphore */
    int base = history_count(h);
    for (int k = 0; k < 4; k++) {
        if (fork() == 0) { char c[32]; for (int i = 0; i < 300; i++) { snprintf(c, sizeof c, "p%d-%d", k, i); history_add(h, c); } _exit(0); }
    }
    int st; while (wait(&st) > 0) {}
    CHECK(history_count(h) == base + 1200);
    history_close(h);

    FILE *f = fopen(file, "w");                              /* trimming of an oversized file */
    for (int i = 0; i < HIST_MAX + 500; i++) fprintf(f, "line%d\n", i);
    fclose(f);
    h = history_open(file, 0);
    CHECK(history_count(h) == HIST_MAX);
    CHECK(history_get(h, 0, b, sizeof b) == 0); CHECK_STR(b, "line10499");
    history_close(h);
    h = history_open("/nonexistent-dir/hist", 0);            /* failed file ops are non-fatal */
    CHECK(h != NULL); CHECK(history_add(h, "x y z") == 1); history_close(h);
    unlink(file);
    TEST_DONE();
}
