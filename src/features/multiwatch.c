/* multiwatch.c - multiWatch ["cmd1", "cmd2", ...]
 * Every command runs as its own concurrent job with its own stdout/stderr
 * pipes; sh_pump() multiplexes all of them (poll) together with the GUI control
 * pipe, so Ctrl+C is seen immediately and nothing blocks. Each chunk of output
 * is printed with the command label, a Unix timestamp and separator lines. A
 * command is restarted MW_INTERVAL_MS after it finishes. */
#include "multiwatch.h"
#include "executor.h"
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { char *cmd; CmdList list; Job *job; long long next_ms; } Watch;

static long long mono_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int multiwatch_parse(const char *s, StrList *cmds, char *err, size_t en) {
    while (*s == ' ' || *s == '\t') s++;
    if (*s != '[') { snprintf(err, en, "expected '['"); return -1; }
    s++;
    for (;;) {
        while (*s == ' ' || *s == '\t' || *s == '\n') s++;
        if (*s == ']' && cmds->n == 0) break;
        if (*s != '"' && *s != '\'') { snprintf(err, en, "expected a quoted command string"); return -1; }
        char q = *s++;
        Buf b; buf_init(&b);
        while (*s && *s != q) {
            if (q == '"' && *s == '\\' && (s[1] == '"' || s[1] == '\\')) s++;
            buf_addc(&b, *s++);
        }
        if (*s != q) { buf_free(&b); snprintf(err, en, "unterminated string"); return -1; }
        s++;
        strlist_add_own(cmds, buf_take(&b));
        while (*s == ' ' || *s == '\t' || *s == '\n') s++;
        if (*s == ',') { s++; continue; }
        if (*s == ']') break;
        snprintf(err, en, "expected ',' or ']'"); return -1;
    }
    s++;                                        /* past ']' */
    while (*s == ' ' || *s == '\t') s++;
    if (*s) { snprintf(err, en, "unexpected text after ']'"); return -1; }
    return 0;
}

static void on_data(ShellState *sh, Job *j, int stream, const char *buf, size_t n) {
    Watch *w = j->ud;
    Buf b; buf_init(&b);
    buf_addf(&b, "\"%s\" , current_time: %ld\n%s\n", w->cmd, (long)time(NULL), MW_SEPARATOR);
    buf_add(&b, buf, n);
    if (!n || buf[n - 1] != '\n') buf_addc(&b, '\n');
    buf_addf(&b, "%s\n", MW_SEPARATOR);
    sh_emit(sh, stream, b.d, b.len);
    buf_free(&b);
}

int multiwatch_run(ShellState *sh, int argc, char **argv) {
    StrList cmds; strlist_init(&cmds);
    char err[200];
    if (sh->raw_argv && sh->raw_argc >= 2 && sh->raw_argv[1][0] == '[') {
        char *joined = str_join(sh->raw_argv + 1, sh->raw_argc - 1, " ");
        int rc = multiwatch_parse(joined, &cmds, err, sizeof err);
        free(joined);
        if (rc < 0) { sh_err(sh, "multiWatch: %s", err); strlist_free(&cmds); return 2; }
    } else for (int i = 1; i < argc; i++) strlist_add(&cmds, argv[i]);
    if (cmds.n == 0 || cmds.n > MAX_WATCH) {
        sh_err(sh, "usage: multiWatch [\"cmd1\", \"cmd2\", ...]  (1-%d commands)", MAX_WATCH);
        strlist_free(&cmds); return 2;
    }
    Watch w[MAX_WATCH]; memset(w, 0, sizeof w);
    int n = cmds.n, rc = 0;
    for (int i = 0; i < n; i++) {                /* syntax-check every command before starting any */
        w[i].cmd = cmds.v[i];
        if (parse_list(w[i].cmd, &w[i].list, err, sizeof err) < 0 || w[i].list.n != 1) {
            sh_err(sh, "multiWatch: \"%s\": %s", w[i].cmd, w[i].list.n > 1 ? "expected a single pipeline" : err);
            for (int k = 0; k < i; k++) cmdlist_free(&w[k].list);
            strlist_free(&cmds); return 2;
        }
    }
    sh->interrupted = 0;
    while (!sh->interrupted && !sh->want_exit) {
        long long now = mono_ms(), wake = now + 200;
        for (int i = 0; i < n; i++) {
            if (!w[i].job && now >= w[i].next_ms) {
                w[i].job = exec_launch(sh, &w[i].list.items[0], 1);
                if (w[i].job) { w[i].job->hidden = 1; w[i].job->on_data = on_data; w[i].job->ud = &w[i]; }
                else w[i].next_ms = now + MW_INTERVAL_MS;
            }
            if (!w[i].job && w[i].next_ms < wake) wake = w[i].next_ms;
        }
        long long t = wake - mono_ms();
        sh_pump(sh, t < 0 ? 0 : (int)t);
        now = mono_ms();
        for (int i = 0; i < n; i++) {
            Job *j = w[i].job;
            if (j && j->state == JS_DONE) {
                job_drain(sh, j); job_free(sh, j);
                w[i].job = NULL; w[i].next_ms = now + MW_INTERVAL_MS;
            }
        }
    }
    if (sh->interrupted) rc = 130;
    for (int i = 0; i < n; i++) {                /* terminate, reap and release everything */
        if (w[i].job) { if (w[i].job->state != JS_DONE) job_terminate(sh, w[i].job); job_free(sh, w[i].job); }
        cmdlist_free(&w[i].list);
    }
    strlist_free(&cmds);
    return rc;
}
