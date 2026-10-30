/* logging.c - optional debug log and low-level I/O helpers. */
#include "common.h"
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

void log_debug(const char *fmt, ...) {
    if (!getenv("TERMINATOR_DEBUG")) return;
    va_list ap; va_start(ap, fmt);
    fputs("[terminator] ", stderr); vfprintf(stderr, fmt, ap); fputc('\n', stderr);
    va_end(ap);
}

ssize_t write_all(int fd, const void *buf, size_t n) {
    const char *p = buf; size_t left = n;
    while (left) {
        ssize_t w = write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN) { struct pollfd pf = { fd, POLLOUT, 0 }; poll(&pf, 1, 1000); continue; }
            return -1;
        }
        p += w; left -= (size_t)w;
    }
    return (ssize_t)n;
}

int fd_set_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL);
    return fl < 0 ? -1 : fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}
