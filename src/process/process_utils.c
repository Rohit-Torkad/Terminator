/* process_utils.c - wait-status and fd helpers. */
#include "process_utils.h"
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

int status_to_code(int st) {
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
    if (WIFSTOPPED(st)) return 128 + WSTOPSIG(st);
    return 1;
}
int pipe_cloexec(int fds[2]) { return pipe2(fds, O_CLOEXEC); }
void close_fd(int *fd) { if (*fd >= 0) { close(*fd); *fd = -1; } }
int proc_wait_blocking(pid_t pid, int *st) {
    pid_t r;
    do r = waitpid(pid, st, 0); while (r < 0 && errno == EINTR);
    return r < 0 ? -1 : 0;
}
