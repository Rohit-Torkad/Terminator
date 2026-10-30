/* script.h - interpreter for the supported Bash subset. */
#ifndef TERMINATOR_SCRIPT_H
#define TERMINATOR_SCRIPT_H
#include "shell.h"

typedef enum { SK_CMD, SK_IF, SK_ELIF, SK_ELSE, SK_THEN, SK_FI } StmtKind;
typedef struct { StmtKind kind; char *text; int line; } Stmt;
typedef struct { Stmt *s; int n, cap; } StmtList;

/* Split script text into statements and validate if/elif/else/fi structure.
 * Returns 0 on success, -1 with err (includes a line number). */
int  script_parse(const char *text, StmtList *out, char *err, size_t errsz);
void stmts_free(StmtList *l);
/* Open if-blocks in `text` (used for interactive multi-line detection). */
int  script_open_blocks(const char *text);

/* Execute already-validated statements in the current shell context. */
int  script_exec_stmts(ShellState *sh, const StmtList *l);

/* separate=1: ./script.sh, bash script.sh (own variable scope, own cwd, own $1..).
 * separate=0: source (current shell context). */
int  script_run_text(ShellState *sh, const char *text, const char *name,
                     int argc, char **argv, int separate);
int  script_run_file(ShellState *sh, const char *path, int argc, char **argv, int separate);
/* Decide whether argv should be interpreted internally (./x.sh, bash x.sh, bash -c).
 * 1 = handled by interpreter (status in *status); 0 = run as external command. */
int  script_try_command(ShellState *sh, int argc, char **argv, int *status);
/* Cheap check used by the pipeline planner. */
int  script_is_candidate(ShellState *sh, int argc, char **argv);

#endif
