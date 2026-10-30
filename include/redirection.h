/* redirection.h - helpers for redirections performed by the engine itself
 * (builtins) and by child processes. */
#ifndef TERMINATOR_REDIRECTION_H
#define TERMINATOR_REDIRECTION_H
#include "shell.h"

/* Open `path` for input/output in the calling process (no exit). Returns fd or -1.
 * Messages are formatted into err. */
int redir_open_in(const char *path, char *err, size_t n);
int redir_open_out(const char *path, int append, char *err, size_t n);
/* Write buf to a redirect target from the engine (builtin output redirection). */
int redir_write_file(ShellState *sh, const char *path, int append, const char *buf, size_t len);

#endif
