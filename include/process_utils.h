/* process_utils.h - small helpers around wait statuses and fds. */
#ifndef TERMINATOR_PROCESS_UTILS_H
#define TERMINATOR_PROCESS_UTILS_H
#include <sys/types.h>
int status_to_code(int wstatus);           /* exit code or 128+signal */
int pipe_cloexec(int fds[2]);              /* pipe2(O_CLOEXEC) */
void close_fd(int *fd);                    /* close and set to -1 */
int  proc_wait_blocking(pid_t pid, int *st);
#endif
