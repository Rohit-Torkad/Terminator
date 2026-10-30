/* shell.h - per-tab shell state and the engine entry points. */
#ifndef TERMINATOR_SHELL_H
#define TERMINATOR_SHELL_H
#include "common.h"
#include "history.h"
#include "ipc.h"
#include "jobs.h"

typedef struct Var { char *name, *value; int exported; struct Var *next; } Var;

typedef struct {
    int ctl_fd, out_fd, tab_id, process_mode;
    char cwd[PATH_MAX];
    History *hist;
} EngineArgs;

typedef struct ShellState {
    int tab_id, process_mode;
    int ctl_fd, out_fd;            /* -1 => no GUI: text goes to stdout/stderr */
    int ctl_eof;
    char cwd[PATH_MAX], oldpwd[PATH_MAX];
    Var *vars;
    int last_status;
    char *args[10];                /* $0..$9 */
    int nargs;                     /* $# */
    History *hist;
    Job jobs[MAX_JOBS];
    int next_job_id;
    Job *fg_job;
    Buf *capture;                  /* when set, stdout text is diverted here */
    int nest;                      /* script / substitution nesting */
    int interrupted, want_exit, exit_status, script_exit, busy;
    char **raw_argv; int raw_argc;  /* unexpanded words of the builtin being run (multiWatch) */
    IpcReader rd;
    StrList queue;                 /* type-ahead lines */
    Buf partial;                   /* incomplete multi-line command */
} ShellState;

enum { ST_OUT = 0, ST_ERR = 1 };

ShellState *sh_new(const EngineArgs *a);
void        sh_free(ShellState *sh);
int         engine_main(EngineArgs *a);        /* runs until exit; returns status */
void       *engine_thread_main(void *arg);     /* pthread entry, frees arg */

void sh_emit(ShellState *sh, int stream, const char *s, size_t n);
void sh_out(ShellState *sh, const char *fmt, ...) PRINTF_FMT(2, 3);
void sh_err(ShellState *sh, const char *fmt, ...) PRINTF_FMT(2, 3);  /* "terminator: ..." */
void sh_send(ShellState *sh, char type, const char *payload, size_t len);

void        sh_set_var(ShellState *sh, const char *name, const char *value, int exported);
const char *sh_get_var(ShellState *sh, const char *name);   /* includes $?, $#, $0-$9, $$ */
void        sh_unset_var(ShellState *sh, const char *name);
char      **sh_build_env(ShellState *sh);                   /* NULL-terminated, free with free_env */
void        free_env(char **e);

int  sh_handle_control(ShellState *sh);   /* dispatch pending GUI messages */
int  sh_process_line(ShellState *sh, const char *line);  /* completeness -> history -> execute */

#endif
