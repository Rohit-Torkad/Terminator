/* executor.h - run parsed command lists. */
#ifndef TERMINATOR_EXECUTOR_H
#define TERMINATOR_EXECUTOR_H
#include "shell.h"
#include "parser.h"

int exec_line(ShellState *sh, const char *text);       /* parse + run, updates $? */
int exec_list(ShellState *sh, CmdList *l);
/* Start one pipeline without waiting (multiWatch). stdin is /dev/null. */
Job *exec_launch(ShellState *sh, Pipeline *p, int bg);

#endif
