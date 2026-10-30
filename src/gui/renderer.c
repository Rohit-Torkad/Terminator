/* renderer.c - draws tab bar and terminal area into an off-screen pixmap, then
 * copies it to the window (no flicker). Long lines are wrapped here, at draw
 * time, which is why resizing never corrupts the scrollback. */
#include "gui.h"

#define PAD 8

const char *gui_prompt(const Gui *g, const Tab *t) {
    static __thread char buf[96];
    if (t->cont) return "> ";
    snprintf(buf, sizeof buf, "%s@terminator> ", g->user);
    return buf;
}

/* ---- tab bar geometry ---- */
int gui_plus_rect(const Gui *g, int *x, int *w) {
    int tabs_end = PAD;
    if (g->ntabs) { int tx, tw; gui_tab_rect(g, g->ntabs - 1, &tx, &tw); tabs_end = tx + tw; }
    *x = tabs_end; *w = g->ch + 14;
    return 0;
}
int gui_tab_rect(const Gui *g, int i, int *x, int *w) {
    int plus = g->ch + 14;
    int avail = g->w - plus - 2 * PAD;
    int tw = g->ntabs ? avail / g->ntabs : 200;
    if (tw > g->cw * 28) tw = g->cw * 28;
    if (tw < g->cw * 8) tw = g->cw * 8;
    *x = PAD + i * tw; *w = tw;
    return 0;
}
int gui_close_rect(const Gui *g, int tx, int tw, int *cx) { *cx = tx + tw - g->cw - 8; return g->cw + 6; }

void gui_cell_at(const Gui *g, int px, int py, int *row, int *col) {
    int c = (px - PAD) / g->cw, r = (py - gui_bar_height(g) - 4) / g->ch;
    if (c < 0) c = 0;
    if (c >= g->cols) c = g->cols - 1;
    if (r < 0) r = 0;
    if (r >= g->rows) r = g->rows - 1;
    *row = r; *col = c;
}

static void text(Gui *g, int x, int y, const char *s, int n, unsigned long fg, unsigned long bg) {
    XSetForeground(g->dpy, g->gc, fg); XSetBackground(g->dpy, g->gc, bg);
    XDrawImageString(g->dpy, g->pm, g->gc, x, y, s, n);
}
static void rect(Gui *g, int x, int y, int w, int h, unsigned long c) {
    XSetForeground(g->dpy, g->gc, c); XFillRectangle(g->dpy, g->pm, g->gc, x, y, (unsigned)w, (unsigned)h);
}

static void draw_tabbar(Gui *g) {
    int bh = gui_bar_height(g);
    rect(g, 0, 0, g->w, bh, g->c_bar);
    for (int i = 0; i < g->ntabs; i++) {
        int x, w; gui_tab_rect(g, i, &x, &w);
        Tab *t = g->tabs[i];
        int act = i == g->active;
        if (act) { rect(g, x, 0, w, bh - 1, g->c_tab_act); rect(g, x, bh - 3, w, 2, g->c_accent); }
        char disp[PATH_MAX], lab[PATH_MAX + 16];
        path_display(t->cwd, disp, sizeof disp);
        int maxc = (w - 2 * 8 - g->cw - 6) / g->cw;
        char num[16]; snprintf(num, sizeof num, "%d ", i + 1);
        int room = maxc - (int)strlen(num);
        if (room < 4) room = 4;
        if ((int)strlen(disp) > room) snprintf(lab, sizeof lab, "%s..%s", num, disp + strlen(disp) - (room - 2));
        else snprintf(lab, sizeof lab, "%s%s", num, disp);
        text(g, x + 8, (bh - 2 - g->ch) / 2 + g->asc + 2, lab, (int)strlen(lab), act ? g->c_fg : g->c_tab_off, act ? g->c_tab_act : g->c_bar);
        int cx; gui_close_rect(g, x, w, &cx);
        text(g, cx, (bh - 2 - g->ch) / 2 + g->asc + 2, "x", 1, g->c_dim, act ? g->c_tab_act : g->c_bar);
        rect(g, x + w - 1, 4, 1, bh - 9, g->c_sep);
    }
    int px, pw; gui_plus_rect(g, &px, &pw);
    text(g, px + (pw - g->cw) / 2, (bh - 2 - g->ch) / 2 + g->asc + 2, "+", 1, g->c_accent, g->c_bar);
    rect(g, 0, bh - 1, g->w, 1, g->c_sep);
}

typedef struct { const char *s; int len, style, plen, live; size_t start; } Row;
typedef struct { const char *s; size_t len; int style, plen, live; } LineRef;

static int nrows_for(size_t len, int cols, int live) {
    if (live) return (int)(len / (size_t)cols) + 1;
    return len ? (int)((len + (size_t)cols - 1) / (size_t)cols) : 1;
}

static void draw_content(Gui *g, Tab *t) {
    int cols = g->cols, rows = g->rows;
    Buf live; buf_init(&live);
    int live_plen = 0; size_t cur = 0; int show_live = !t->dead;
    Scrollback *sb = &t->sb;
    int last_regular = sb->n - 2;
    if (t->im == IM_SEARCH) { buf_adds(&live, "Enter search term: "); live_plen = (int)live.len; buf_add(&live, t->aux.d ? t->aux.d : "", t->aux.len); cur = live.len; }
    else if (t->im == IM_SELECT) { buf_addf(&live, "Select [1-%d]: ", t->sel_items.n); live_plen = (int)live.len; buf_add(&live, t->aux.d ? t->aux.d : "", t->aux.len); cur = live.len; }
    else if (t->state == TS_PROMPT) {
        const char *p = gui_prompt(g, t);
        buf_adds(&live, p); live_plen = (int)live.len;
        buf_add(&live, t->in.d ? t->in.d : "", t->in.len); cur = (size_t)live_plen + t->cursor;
        if (!sb_open_empty(sb)) last_regular = sb->n - 1;
    } else {                                     /* running: typed text continues the open output line */
        SbLine *o = &sb->l[sb->n - 1];
        buf_add(&live, o->s ? o->s : "", o->len);
        size_t base = live.len;
        buf_add(&live, t->in.d ? t->in.d : "", t->in.len); cur = base + t->cursor;
    }
    /* collect lines from the bottom until enough rows are known */
    int need = rows + t->scroll, total = 0, nl = 0;
    LineRef *refs = xmalloc(sizeof(LineRef) * (size_t)(sb->n + 2));
    if (show_live) { refs[nl++] = (LineRef){ live.d ? live.d : "", live.len, SB_NORMAL, live_plen, 1 }; total += nrows_for(live.len, cols, 1); }
    for (int i = last_regular; i >= 0 && total < need; i--) {
        SbLine *l = &sb->l[i];
        refs[nl++] = (LineRef){ l->s ? l->s : "", l->len, l->style, l->plen, 0 };
        total += nrows_for(l->len, cols, 0);
    }
    if (total < need) { t->scroll = total > rows ? total - rows : 0; }
    Row *all = xmalloc(sizeof(Row) * (size_t)(total + 1)); int m = 0;
    for (int k = nl - 1; k >= 0; k--) {          /* expand oldest -> newest */
        LineRef *r = &refs[k];
        int nr = nrows_for(r->len, cols, r->live);
        for (int q = 0; q < nr; q++) {
            size_t st = (size_t)q * (size_t)cols; size_t len = r->len > st ? r->len - st : 0;
            if (len > (size_t)cols) len = (size_t)cols;
            int pl = r->plen > (int)st ? r->plen - (int)st : 0; if (pl > (int)len) pl = (int)len;
            all[m++] = (Row){ r->s + (st < r->len ? st : r->len), (int)len, r->style, pl, r->live, st };
        }
    }
    int end = m - t->scroll; if (end < 0) end = 0;
    int first = end - rows; if (first < 0) first = 0;
    for (int i = 0; i < g->nvis; i++) free(g->vis[i]);
    free(g->vis); g->vis = xcalloc((size_t)rows, sizeof(char *)); g->nvis = rows;

    Selection s = g->sel; int sr0 = s.r0, sc0 = s.c0, sr1 = s.r1, sc1 = s.c1;
    if (s.active && (sr0 > sr1 || (sr0 == sr1 && sc0 > sc1))) { int a = sr0, b = sc0; sr0 = sr1; sc0 = sc1; sr1 = a; sc1 = b; }
    int cur_row = -1, cur_col = 0;
    for (int i = first; i < end; i++) {
        Row *r = &all[i]; int vr = i - first, y = gui_bar_height(g) + 4 + vr * g->ch;
        char *plain = xmalloc((size_t)r->len + 1);
        for (int c = 0; c < r->len; c++) { unsigned char ch = (unsigned char)r->s[c]; plain[c] = (ch < 0x20 || ch >= 0x7f) ? '?' : (char)ch; }
        plain[r->len] = 0; g->vis[vr] = plain;
        if (r->live && (size_t)(r->start) <= cur && cur < r->start + (size_t)cols + (i == end - 1 ? 1 : 0) && t->scroll == 0) {
            int rowcheck = (cur / (size_t)cols) == (r->start / (size_t)cols);
            if (rowcheck) { cur_row = vr; cur_col = (int)(cur - r->start); }
        }
        int c = 0;
        while (c < r->len) {                      /* group cells with equal colours into runs */
            int selc = s.active && (vr > sr0 || (vr == sr0 && c >= sc0)) && (vr < sr1 || (vr == sr1 && c <= sc1));
            unsigned long fg = c < r->plen ? g->c_accent : r->style == SB_ERR ? g->c_err : r->style == SB_INFO ? g->c_info : g->c_fg;
            int e = c + 1;
            while (e < r->len) {
                int sel2 = s.active && (vr > sr0 || (vr == sr0 && e >= sc0)) && (vr < sr1 || (vr == sr1 && e <= sc1));
                unsigned long fg2 = e < r->plen ? g->c_accent : r->style == SB_ERR ? g->c_err : r->style == SB_INFO ? g->c_info : g->c_fg;
                if (sel2 != selc || fg2 != fg) break;
                e++;
            }
            text(g, PAD + c * g->cw, y + g->asc, plain + c, e - c, fg, selc ? g->c_sel : g->c_bg);
            c = e;
        }
    }
    if (cur_row >= 0 && show_live && g->focused) {
        int x = PAD + cur_col * g->cw, y = gui_bar_height(g) + 4 + cur_row * g->ch;
        if (g->blink) {
            rect(g, x, y, g->cw, g->ch, g->c_cursor);
            const char *vs = g->vis[cur_row];
            if (cur_col < (int)strlen(vs)) text(g, x, y + g->asc, vs + cur_col, 1, g->c_bg, g->c_cursor);
        }
    } else if (cur_row >= 0 && show_live) {      /* unfocused: hollow cursor */
        XSetForeground(g->dpy, g->gc, g->c_cursor);
        XDrawRectangle(g->dpy, g->pm, g->gc, PAD + cur_col * g->cw, gui_bar_height(g) + 4 + cur_row * g->ch, (unsigned)g->cw - 1, (unsigned)g->ch - 1);
    }
    if (t->scroll > 0) {                          /* scrolled-back indicator */
        char ind[48]; int n = snprintf(ind, sizeof ind, " scrolled back %d rows ", t->scroll);
        text(g, g->w - PAD - n * g->cw, gui_bar_height(g) + 4 + g->asc, ind, n, g->c_bg, g->c_info);
    }
    free(refs); free(all); buf_free(&live);
}

void gui_draw(Gui *g) {
    rect(g, 0, 0, g->w, g->h, g->c_bg);
    draw_tabbar(g);
    Tab *t = gui_active(g);
    if (t) draw_content(g, t);
    XCopyArea(g->dpy, g->pm, g->win, g->gc, 0, 0, (unsigned)g->w, (unsigned)g->h, 0, 0);
    XFlush(g->dpy);
    g->dirty = 0;
}
