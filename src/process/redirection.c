/* redirection.c - open() helpers shared by children and engine-side builtins. */
#include "redirection.h"
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

int redir_open_in(const char *path, char *err, size_t n) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) snprintf(err, n, "%s: %s", path, strerror(errno));
    return fd;
}
int redir_open_out(const char *path, int append, char *err, size_t n) {
    int fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC | (append ? O_APPEND : O_TRUNC), 0644);
    if (fd < 0) snprintf(err, n, "%s: %s", path, strerror(errno));
    return fd;
}
int redir_write_file(ShellState *sh, const char *path, int append, const char *buf, size_t len) {
    char err[PATH_MAX + 128];
    int fd = redir_open_out(path, append, err, sizeof err);
    if (fd < 0) { sh_err(sh, "%s", err); return -1; }
    int rc = (len == 0 || write_all(fd, buf, len) >= 0) ? 0 : -1;
    if (rc < 0) sh_err(sh, "%s: write error: %s", path, strerror(errno));
    close(fd);
    return rc;
}
