/* tab.h - GUI-side view of one tab: scrollback, input line, editing modes and
 * the handle to its shell engine (thread or process). */
#ifndef TERMINATOR_TAB_H
#define TERMINATOR_TAB_H
#include "common.h"
#include "concurrency.h"
#include "ipc.h"

enum { SB_NORMAL = 0, SB_ERR = 1, SB_ECHO = 2, SB_INFO = 3 };
typedef struct { char *s; size_t len, cap; unsigned char style; unsigned short plen; } SbLine;
typedef struct { SbLine *l; int n, cap; size_t wpos; int esc; } Scrollback;

void sb_init(Scrollback *sb);
void sb_free(Scrollback *sb);
void sb_clear(Scrollback *sb);
void sb_append(Scrollback *sb, const char *text, size_t n, int style);  /* terminal-ish: \n \r \b \t, strips ANSI */
void sb_newline(Scrollback *sb);
int  sb_open_empty(const Scrollback *sb);
/* Start a fresh line holding `text` (first plen chars are the prompt). */
void sb_add_line(Scrollback *sb, const char *text, int style, int plen);

typedef enum { TS_PROMPT, TS_RUNNING } TabState;
typedef enum { IM_NORMAL, IM_SEARCH, IM_SELECT } InputMode;

typedef struct Tab {
    int id;
    TabHandle h;
    IpcReader rd;
    char cwd[PATH_MAX];
    Scrollback sb;
    TabState state;
    int cont;                    /* continuation prompt ">" is showing */
    Buf in; size_t cursor;       /* input line */
    int hist_pos; char *hist_saved;
    InputMode im;
    Buf aux;                     /* search term / selection digits */
    char *saved_line;
    StrList sel_items; int sel_kind;       /* 0 completion, 1 history search */
    size_t sel_start, sel_end; char sel_quote;
    int scroll;                  /* visual rows scrolled back */
    int dead, exited, eof;
} Tab;

#endif
