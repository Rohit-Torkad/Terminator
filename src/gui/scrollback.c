/* scrollback.c - per-tab output buffer. Stores logical lines; wrapping to the
 * window width happens at render time, so resizing never corrupts contents. */
#include "tab.h"

#define SB_MAX_LINES 20000

static void line_init(SbLine *l) { memset(l, 0, sizeof *l); }

void sb_init(Scrollback *sb) { memset(sb, 0, sizeof *sb); sb->cap = 256; sb->l = xcalloc((size_t)sb->cap, sizeof(SbLine)); sb->n = 1; }
void sb_free(Scrollback *sb) { for (int i = 0; i < sb->n; i++) free(sb->l[i].s); free(sb->l); memset(sb, 0, sizeof *sb); }
void sb_clear(Scrollback *sb) {
    for (int i = 0; i < sb->n; i++) free(sb->l[i].s);
    memset(sb->l, 0, sizeof(SbLine) * (size_t)sb->cap);
    sb->n = 1; sb->wpos = 0; sb->esc = 0;
}

void sb_newline(Scrollback *sb) {
    if (sb->n == sb->cap) { sb->cap *= 2; sb->l = xrealloc(sb->l, (size_t)sb->cap * sizeof(SbLine)); }
    line_init(&sb->l[sb->n++]);
    sb->wpos = 0;
    if (sb->n > SB_MAX_LINES + 1000) {           /* drop the oldest 1000 lines in one go */
        for (int i = 0; i < 1000; i++) free(sb->l[i].s);
        memmove(sb->l, sb->l + 1000, sizeof(SbLine) * (size_t)(sb->n - 1000));
        sb->n -= 1000;
    }
}

int sb_open_empty(const Scrollback *sb) { return sb->l[sb->n - 1].len == 0; }

static void put(Scrollback *sb, char c, int style) {
    SbLine *l = &sb->l[sb->n - 1];
    if (sb->wpos > l->len) sb->wpos = l->len;
    if (sb->wpos == l->len) {
        if (l->len + 2 > l->cap) { l->cap = l->cap ? l->cap * 2 : 64; l->s = xrealloc(l->s, l->cap); }
        l->s[l->len++] = c; l->s[l->len] = 0;
    } else l->s[sb->wpos] = c;
    sb->wpos++;
    if (style == SB_ERR) l->style = SB_ERR;
    else if (l->style == SB_NORMAL) l->style = (unsigned char)style;
}

void sb_append(Scrollback *sb, const char *t, size_t n, int style) {
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)t[i];
        if (sb->esc == 1) {                      /* after ESC */
            if (c == '[') sb->esc = 2; else if (c == ']') sb->esc = 3; else sb->esc = 0;
            continue;
        }
        if (sb->esc == 2) { if (c >= 0x40 && c <= 0x7e) sb->esc = 0; continue; }           /* CSI ... final */
        if (sb->esc == 3) { if (c == 7 || c == '\\') sb->esc = 0; continue; }              /* OSC ... BEL/ST */
        switch (c) {
        case 0x1b: sb->esc = 1; break;
        case '\n': sb_newline(sb); break;
        case '\r': sb->wpos = 0; break;
        case '\b': if (sb->wpos) sb->wpos--; break;
        case '\t': do put(sb, ' ', style); while (sb->wpos % 8); break;
        default: if (c >= 0x20 || c >= 0x80) put(sb, (char)c, style); break;
        }
    }
}

void sb_add_line(Scrollback *sb, const char *text, int style, int plen) {
    if (!sb_open_empty(sb)) sb_newline(sb);
    sb_append(sb, text, strlen(text), style);
    sb->l[sb->n - 1].style = (unsigned char)style;
    sb->l[sb->n - 1].plen = (unsigned short)plen;
    sb_newline(sb);
}
