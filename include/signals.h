/* signals.h - signal policy for GUI, tab engines and child processes. */
#ifndef TERMINATOR_SIGNALS_H
#define TERMINATOR_SIGNALS_H
#include <signal.h>

extern volatile sig_atomic_t g_terminate;     /* set by SIGINT/SIGTERM/SIGHUP in the GUI */
void signals_init_gui(void);                  /* ignore SIGPIPE/TSTP/TTIN/TTOU, flag on TERM/INT/HUP */
void signals_init_engine_process(void);       /* tab process: ignore INT/QUIT/TSTP/PIPE */
void signals_reset_child(void);               /* restore defaults + unblock in a forked child */

#endif
