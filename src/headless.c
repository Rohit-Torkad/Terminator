/* headless.c - see headless.h. */
#include "headless.h"
#include "concurrency.h"
#include "ipc.h"
#include "signals.h"
#include <errno.h>
#include <poll.h>
#include <time.h>
#include <unistd.h>

static long long now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int headless_run(int process_mode, const char *cwd) {
    signals_init_gui();
    History *h = shared_history_create(process_mode);
    TabHandle th;
    char start[PATH_MAX];
    if (cwd) snprintf(start, sizeof start, "%s", cwd);
    else if (!getcwd(start, sizeof start)) snprintf(start, sizeof start, "/");
    if (!h || tab_engine_spawn(&th, 1, start, h, process_mode) < 0) { fprintf(stderr, "terminator: cannot start engine\n"); return 1; }

    IpcReader rd; ipc_reader_init(&rd);
    char *pending = NULL; size_t pcap = 0;
    int ready = 0, done = 0, status = 0, stdin_eof = 0, sent_eof = 0;
    long long wait_until = 0;
    int show_prompts = getenv("TERMINATOR_HEADLESS_PROMPTS") != NULL;   /* test hook: print "$ " / "> " */

    while (!done) {
        long long now = now_ms();
        /* feed input */
        for (;;) {
            if (!pending && !stdin_eof) {
                ssize_t n = getline(&pending, &pcap, stdin);
                if (n < 0) { stdin_eof = 1; free(pending); pending = NULL; pcap = 0; }
                else { while (n && (pending[n - 1] == '\n' || pending[n - 1] == '\r')) pending[--n] = 0; }
            }
            if (!pending || now < wait_until) break;       /* honour @@WAIT@@ before any directive */
            long ms;
            if (!strcmp(pending, "@@INT@@")) ipc_send(th.ctl_w, M_INTR, NULL, 0);
            else if (!strcmp(pending, "@@SUSP@@")) ipc_send(th.ctl_w, M_SUSP, NULL, 0);
            else if (!strncmp(pending, "@@LINE ", 7)) ipc_send(th.ctl_w, M_LINE, pending + 7, (uint32_t)strlen(pending + 7));  /* stdin of the running program */
            else if (!strcmp(pending, "@@EOF@@")) ipc_send(th.ctl_w, M_EOF, NULL, 0);
            else if (sscanf(pending, "@@WAIT %ld@@", &ms) == 1) wait_until = now + ms;
            else if (ready && now >= wait_until) { ipc_send(th.ctl_w, M_LINE, pending, (uint32_t)strlen(pending)); ready = 0; }
            else break;                                  /* wait for prompt / timer */
            free(pending); pending = NULL; pcap = 0;
        }
        if (stdin_eof && !pending && ready && now >= wait_until && !sent_eof) { ipc_send(th.ctl_w, M_EOF, NULL, 0); sent_eof = 1; }

        int timeout = -1;
        if (wait_until > now) timeout = (int)(wait_until - now);
        struct pollfd pf = { th.out_r, POLLIN, 0 };
        int r = poll(&pf, 1, timeout);
        if (r < 0 && errno != EINTR) break;
        if (g_terminate) break;
        if (r > 0) {
            ssize_t n = ipc_reader_fill(&rd, th.out_r);
            if (n == 0) { done = 1; }
            char type; const char *p; uint32_t len;
            while (ipc_reader_next(&rd, &type, &p, &len) == 1) {
                switch (type) {
                case M_OUT: fwrite(p, 1, len, stdout); fflush(stdout); break;
                case M_ERR: fflush(stdout); fwrite(p, 1, len, stderr); fflush(stderr); break;
                case M_PROMPT:
                    ready = 1;
                    if (show_prompts) { fwrite(len && p[0] == '1' ? "> " : "$ ", 1, 2, stdout); fflush(stdout); }
                    break;
                case M_EXIT: status = atoi(p); done = 1; break;
                default: break;
                }
            }
        }
    }
    tab_engine_stop(&th, 1);
    ipc_reader_free(&rd);
    shared_history_destroy(h);
    free(pending);
    return status;
}
