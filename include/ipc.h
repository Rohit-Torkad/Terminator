/* ipc.h - framed messages exchanged between the GUI and a tab's shell engine.
 * Frame: [type:1][length:4 native endian][payload]. Same code serves pipes in
 * thread mode and process mode. */
#ifndef TERMINATOR_IPC_H
#define TERMINATOR_IPC_H
#include "common.h"

/* GUI -> engine */
#define M_LINE   'L'   /* a line of input (without newline) */
#define M_INTR   'I'   /* Ctrl+C */
#define M_SUSP   'Z'   /* Ctrl+Z */
#define M_EOF    'D'   /* Ctrl+D */
#define M_QUIT   'Q'   /* shut the tab engine down */
/* engine -> GUI */
#define M_OUT    'O'   /* stdout text */
#define M_ERR    'E'   /* stderr text */
#define M_PROMPT 'P'   /* ready for input; payload[0]=='1' => continuation prompt */
#define M_CWD    'W'   /* working directory changed / current cwd */
#define M_EXIT   'X'   /* engine is terminating; payload = decimal status */
#define M_CLEAR  'K'   /* clear the screen */
#define M_RELOAD 'R'   /* key bindings file changed */

#define IPC_MAX_PAYLOAD (1u << 24)

int ipc_send(int fd, char type, const void *payload, uint32_t len);

typedef struct { char *d; size_t len, cap, off; Buf cur; } IpcReader;
void ipc_reader_init(IpcReader *r);
void ipc_reader_free(IpcReader *r);
/* read(2) once; returns bytes read, 0 on EOF, -1 on error (errno set, EAGAIN ok) */
ssize_t ipc_reader_fill(IpcReader *r, int fd);
/* 1 = message available (payload valid until next call), 0 = need more, -1 = corrupt */
int ipc_reader_next(IpcReader *r, char *type, const char **payload, uint32_t *len);

#endif
