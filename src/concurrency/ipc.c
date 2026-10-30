/* ipc.c - message framing over pipes. */
#include "ipc.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

int ipc_send(int fd, char type, const void *payload, uint32_t len) {
    if (fd < 0) return -1;
    if (len <= 4096) {                         /* one write => frame is atomic */
        char tmp[5 + 4096];
        tmp[0] = type; memcpy(tmp + 1, &len, 4);
        if (len) memcpy(tmp + 5, payload, len);
        return write_all(fd, tmp, 5 + len) < 0 ? -1 : 0;
    }
    char hdr[5]; hdr[0] = type; memcpy(hdr + 1, &len, 4);
    if (write_all(fd, hdr, 5) < 0 || write_all(fd, payload, len) < 0) return -1;
    return 0;
}

void ipc_reader_init(IpcReader *r) { memset(r, 0, sizeof *r); buf_init(&r->cur); }
void ipc_reader_free(IpcReader *r) { free(r->d); buf_free(&r->cur); memset(r, 0, sizeof *r); }

ssize_t ipc_reader_fill(IpcReader *r, int fd) {
    if (r->off == r->len) r->off = r->len = 0;
    else if (r->off > 65536) { memmove(r->d, r->d + r->off, r->len - r->off); r->len -= r->off; r->off = 0; }
    if (r->cap - r->len < 8192) { r->cap = r->cap ? r->cap * 2 : 16384; r->d = xrealloc(r->d, r->cap); }
    ssize_t n;
    do n = read(fd, r->d + r->len, r->cap - r->len); while (n < 0 && errno == EINTR);
    if (n > 0) r->len += (size_t)n;
    return n;
}

int ipc_reader_next(IpcReader *r, char *type, const char **payload, uint32_t *len) {
    size_t avail = r->len - r->off;
    if (avail < 5) return 0;
    uint32_t l; memcpy(&l, r->d + r->off + 1, 4);
    if (l > IPC_MAX_PAYLOAD) return -1;
    if (avail < 5 + (size_t)l) return 0;
    *type = r->d[r->off];
    buf_reset(&r->cur);
    buf_add(&r->cur, r->d + r->off + 5, l);
    *payload = buf_str(&r->cur);
    *len = l;
    r->off += 5 + l;
    return 1;
}
