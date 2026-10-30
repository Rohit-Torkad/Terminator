/* multiwatch.h - the multiWatch builtin. */
#ifndef TERMINATOR_MULTIWATCH_H
#define TERMINATOR_MULTIWATCH_H
#include "shell.h"

#define MW_SEPARATOR "<----------------------------------------------------->"
#define MW_INTERVAL_MS 2000

int multiwatch_run(ShellState *sh, int argc, char **argv);
/* Parse ["cmd1", "cmd2"] into command strings. 0 ok, -1 error. */
int multiwatch_parse(const char *args, StrList *cmds, char *err, size_t errsz);

#endif
