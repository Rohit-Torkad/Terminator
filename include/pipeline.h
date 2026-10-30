/* pipeline.h - launching a pipeline: pipes, fork, redirections, process groups. */
#ifndef TERMINATOR_PIPELINE_H
#define TERMINATOR_PIPELINE_H
#include "shell.h"

typedef struct {
    char **argv; int argc;
    char *in_path, *out_path;      /* absolute paths, or NULL */
    int append;
    char *prod_buf; size_t prod_len; int producer, prod_status;  /* in-engine builtin/script output replayed by a child */
} Stage;

/* Starts all stages (N-1 pipes, one process group), registers a job and returns
 * it without waiting. give_stdin: connect a pipe the GUI can feed. */
Job *pipeline_start(ShellState *sh, Stage *st, int n, const char *text, int bg, int give_stdin);

#endif
