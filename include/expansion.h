/* expansion.h - quote removal, $VAR, $?, $#, $1..$9, $(...), `...`, ~, wildcards. */
#ifndef TERMINATOR_EXPANSION_H
#define TERMINATOR_EXPANSION_H
#include "shell.h"

/* Expand one raw word into zero or more fields (word splitting + globbing). */
int   expand_word(ShellState *sh, const char *raw, StrList *out, int do_glob);
/* Expand to exactly one string (no splitting, no globbing). Caller frees. */
char *expand_string(ShellState *sh, const char *raw);
/* Per-tab wildcard expansion using fnmatch; never uses the process cwd. */
int   glob_expand(const char *cwd, const char *pattern, StrList *out);
/* Implemented in executor.c: run `cmd`, capture stdout, strip trailing newlines. */
char *sh_command_substitute(ShellState *sh, const char *cmd);

#endif
