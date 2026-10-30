/* builtins.h - builtin command table. */
#ifndef TERMINATOR_BUILTINS_H
#define TERMINATOR_BUILTINS_H
#include "shell.h"

typedef int (*BuiltinFn)(ShellState *sh, int argc, char **argv);
typedef struct { const char *name; BuiltinFn fn; int special; } Builtin;
/* special builtins change shell state or run long operations: only valid as a
 * standalone foreground command. Others may be used inside pipelines. */
const Builtin *builtin_lookup(const char *name);
int builtin_test(ShellState *sh, int argc, char **argv);   /* [ ... ] and test */

#endif
