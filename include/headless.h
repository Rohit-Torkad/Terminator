/* headless.h - GUI-less frontend that drives one tab engine from stdin/stdout.
 * Used by the automated integration tests; exercises the same engine, IPC
 * protocol and execution modes as the X11 frontend. */
#ifndef TERMINATOR_HEADLESS_H
#define TERMINATOR_HEADLESS_H
/* stdin lines are commands (sent when the engine is at a prompt). Directives, sent at once:
 *   @@INT@@  @@SUSP@@  @@EOF@@  @@WAIT <ms>@@  @@LINE <text>  (stdin of the running program) */
int headless_run(int process_mode, const char *cwd);
#endif
