/* signals.c - signal policy. Ctrl+C / Ctrl+Z arrive as X11 key events, never as
 * tty signals, so the shell engine delivers them explicitly to job process groups. */
#include "signals.h"
#include <string.h>
#include <pthread.h>

volatile sig_atomic_t g_terminate = 0;
static void on_term(int s) { (void)s; g_terminate = 1; }

void signals_init_gui(void) {
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL); sigaction(SIGTSTP, &sa, NULL);
    sigaction(SIGTTIN, &sa, NULL); sigaction(SIGTTOU, &sa, NULL);
    sa.sa_handler = on_term;       /* no SA_RESTART: poll() must wake up */
    sigaction(SIGINT, &sa, NULL); sigaction(SIGTERM, &sa, NULL); sigaction(SIGHUP, &sa, NULL);
}
void signals_init_engine_process(void) {
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN;
    sigaction(SIGINT, &sa, NULL); sigaction(SIGQUIT, &sa, NULL); sigaction(SIGTSTP, &sa, NULL);
    sigaction(SIGPIPE, &sa, NULL); sigaction(SIGTTIN, &sa, NULL); sigaction(SIGTTOU, &sa, NULL);
    sa.sa_handler = SIG_DFL;
    sigaction(SIGTERM, &sa, NULL); sigaction(SIGHUP, &sa, NULL);
}
void signals_reset_child(void) {
    static const int sigs[] = { SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU, SIGPIPE, SIGCHLD, SIGHUP, SIGTERM, SIGCONT };
    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_DFL;
    for (size_t i = 0; i < sizeof sigs / sizeof *sigs; i++) sigaction(sigs[i], &sa, NULL);
    sigset_t e; sigemptyset(&e);
    pthread_sigmask(SIG_SETMASK, &e, NULL);
}
