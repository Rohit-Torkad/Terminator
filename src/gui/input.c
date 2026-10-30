/* input.c - keyboard decoding (KeyPress -> Action via the user's bindings) kept
 * separate from line editing (Action -> edits on the tab's input buffer). */
#include "gui.h"
#include "completion.h"
#include <ctype.h>

/* ---------- buffer editing ---------- */
static void buf_insert(Buf *b, size_t pos, const char *s, size_t n) {
    buf_reserve(b, n);
    memmove(b->d + pos + n, b->d + pos, b->len - pos + 1);
    memcpy(b->d + pos, s, n);
    b->len += n;
}
static void buf_erase(Buf *b, size_t pos, size_t n) {
    if (pos >= b->len) return;
    if (pos + n > b->len) n = b->len - pos;
    memmove(b->d + pos, b->d + pos + n, b->len - pos - n + 1);
    b->len -= n;
}
static void edited(Tab *t) { t->hist_pos = -1; free(t->hist_saved); t->hist_saved = NULL; t->scroll = 0; }
static void set_input(Tab *t, const char *s) {
    buf_reset(&t->in); buf_adds(&t->in, s); t->cursor = t->in.len;
}

void tab_insert(Tab *t, const char *s, size_t n) {
    if (t->dead) return;
    if (t->im != IM_NORMAL) { buf_add(&t->aux, s, n); return; }
    buf_str(&t->in);
    buf_insert(&t->in, t->cursor, s, n);
    t->cursor += n;
    edited(t);
}

/* ---------- history browsing ---------- */
static void history_browse(Gui *g, Tab *t, int dir) {
    if (!g->hist || t->state != TS_PROMPT) return;
    char line[HIST_LINE];
    if (dir > 0) {
        int next = t->hist_pos + 1;
        if (history_get(g->hist, next, line, sizeof line) < 0) return;
        if (t->hist_pos == -1) t->hist_saved = xstrdup(buf_str(&t->in));
        t->hist_pos = next;
        set_input(t, line);
    } else {
        if (t->hist_pos < 0) return;
        t->hist_pos--;
        if (t->hist_pos < 0) { set_input(t, t->hist_saved ? t->hist_saved : ""); free(t->hist_saved); t->hist_saved = NULL; }
        else if (history_get(g->hist, t->hist_pos, line, sizeof line) == 0) set_input(t, line);
    }
}

/* ---------- completion ---------- */
static void print_numbered(Tab *t, const StrList *l) {
    for (int i = 0; i < l->n; i++) {
        char ln[PATH_MAX + 16]; snprintf(ln, sizeof ln, "  %2d) %s", i + 1, l->v[i]);
        sb_add_line(&t->sb, ln, SB_NORMAL, 0);
    }
}

static void do_complete(Gui *g, Tab *t) {
    if (t->state != TS_PROMPT || t->im != IM_NORMAL) return;
    char q; size_t ws = completion_word_start(buf_str(&t->in), t->cursor, &q);
    char *raw = xstrndup(t->in.d + ws, t->cursor - ws);
    StrList c; strlist_init(&c);
    completion_candidates(t->cwd, raw, &c);
    free(raw);
    if (c.n == 0) { strlist_free(&c); return; }
    Buf rep; buf_init(&rep);
    if (q) buf_addc(&rep, q);
    int single = c.n == 1;
    if (single) {
        completion_escape(c.v[0], q, &rep);
        size_t l = strlen(c.v[0]);
        if (c.v[0][l - 1] != '/') { if (q) buf_addc(&rep, q); buf_addc(&rep, ' '); }
    } else {
        char *pre = completion_common_prefix(&c);
        completion_escape(pre, q, &rep);
        free(pre);
    }
    buf_erase(&t->in, ws, t->cursor - ws);
    buf_insert(&t->in, ws, rep.d, rep.len);
    t->cursor = ws + rep.len;
    size_t new_end = t->cursor;
    buf_free(&rep);
    edited(t);
    if (!single) {                                   /* ambiguity remains: numbered list + selection */
        char echo[PATH_MAX + 128]; const char *pr = gui_prompt(g, t);
        snprintf(echo, sizeof echo, "%s%s", pr, t->in.d);
        sb_add_line(&t->sb, echo, SB_ECHO, (int)strlen(pr));
        print_numbered(t, &c);
        strlist_free(&t->sel_items); t->sel_items = c;
        t->sel_kind = 0; t->sel_start = ws; t->sel_end = new_end; t->sel_quote = q;
        t->im = IM_SELECT; buf_reset(&t->aux);
        return;
    }
    strlist_free(&c);
}

/* ---------- search / selection submit ---------- */
static void leave_mode(Tab *t) {
    t->im = IM_NORMAL; buf_reset(&t->aux);
    if (t->saved_line) { set_input(t, t->saved_line); free(t->saved_line); t->saved_line = NULL; }
}

static void search_submit(Gui *g, Tab *t) {
    char echo[512]; snprintf(echo, sizeof echo, "Enter search term: %s", buf_str(&t->aux));
    sb_add_line(&t->sb, echo, SB_ECHO, 19);
    StrList hits; strlist_init(&hits); int exact = 0;
    int n = g->hist ? history_search(g->hist, t->aux.d, &hits, &exact) : 0;
    if (n == 0) {
        sb_add_line(&t->sb, "No match for search term in history", SB_INFO, 0);
        leave_mode(t);
    } else if (n == 1) {
        free(t->saved_line); t->saved_line = NULL;   /* the match replaces the line being edited */
        t->im = IM_NORMAL; buf_reset(&t->aux); set_input(t, hits.v[0]);
    } else {
        print_numbered(t, &hits);
        strlist_free(&t->sel_items); t->sel_items = hits; hits.v = NULL;
        t->sel_kind = 1; t->im = IM_SELECT; buf_reset(&t->aux);
        return;
    }
    strlist_free(&hits);
}

static void select_submit(Gui *g, Tab *t) {
    (void)g;
    long k;
    if (str_parse_int(buf_str(&t->aux), &k) < 0 || k < 1 || k > t->sel_items.n) {
        sb_add_line(&t->sb, "invalid selection", SB_INFO, 0);
        if (t->sel_kind == 1) leave_mode(t); else { t->im = IM_NORMAL; buf_reset(&t->aux); }
        return;
    }
    const char *item = t->sel_items.v[k - 1];
    if (t->sel_kind == 1) {
        free(t->saved_line); t->saved_line = NULL;
        t->im = IM_NORMAL; buf_reset(&t->aux); set_input(t, item);
        return;
    }
    Buf rep; buf_init(&rep);
    if (t->sel_quote) buf_addc(&rep, t->sel_quote);
    completion_escape(item, t->sel_quote, &rep);
    size_t l = strlen(item);
    if (item[l - 1] != '/') { if (t->sel_quote) buf_addc(&rep, t->sel_quote); buf_addc(&rep, ' '); }
    size_t e = t->sel_end > t->in.len ? t->in.len : t->sel_end;
    size_t s = t->sel_start > e ? e : t->sel_start;
    buf_erase(&t->in, s, e - s);
    buf_insert(&t->in, s, rep.d, rep.len);
    t->cursor = s + rep.len;
    buf_free(&rep);
    t->im = IM_NORMAL; buf_reset(&t->aux);
    edited(t);
}

/* ---------- Enter ---------- */
static void submit(Gui *g, Tab *t) {
    if (t->im == IM_SEARCH) { search_submit(g, t); return; }
    if (t->im == IM_SELECT) { select_submit(g, t); return; }
    const char *line = buf_str(&t->in);
    t->scroll = 0;
    if (t->state == TS_PROMPT) {
        const char *pr = gui_prompt(g, t);
        char *echo = xmalloc(strlen(pr) + t->in.len + 1);
        sprintf(echo, "%s%s", pr, line);
        sb_add_line(&t->sb, echo, SB_ECHO, (int)strlen(pr));
        free(echo);
        t->state = TS_RUNNING;
    } else {                                         /* running: this line is stdin for the program */
        sb_append(&t->sb, line, t->in.len, SB_NORMAL);
        sb_newline(&t->sb);
    }
    tab_send(t, M_LINE, line, t->in.len);
    buf_reset(&t->in); t->cursor = 0; edited(t);
}

/* ---------- actions ---------- */
static void kill_word(Tab *t) {
    size_t e = t->cursor, s = e;
    while (s > 0 && isspace((unsigned char)t->in.d[s - 1])) s--;
    while (s > 0 && !isspace((unsigned char)t->in.d[s - 1])) s--;
    buf_erase(&t->in, s, e - s); t->cursor = s; edited(t);
}

static void interrupt(Gui *g, Tab *t) {
    if (t->im != IM_NORMAL) { leave_mode(t); return; }
    if (t->state == TS_RUNNING) {
        sb_append(&t->sb, "^C", 2, SB_INFO);
        tab_send(t, M_INTR, "", 0);
        return;
    }
    const char *pr = gui_prompt(g, t);
    char *echo = xmalloc(strlen(pr) + t->in.len + 4);
    sprintf(echo, "%s%s^C", pr, buf_str(&t->in));
    sb_add_line(&t->sb, echo, SB_ECHO, (int)strlen(pr));
    free(echo);
    buf_reset(&t->in); t->cursor = 0; edited(t);
    if (t->cont) { t->cont = 0; tab_send(t, M_INTR, "", 0); }   /* engine discards the partial command */
}

void tab_apply_action(Gui *g, Tab *t, Action a) {
    int prompt = t->state == TS_PROMPT;
    if (t->im != IM_NORMAL) {                      /* modal prompts accept only a few actions */
        if (a == ACT_INTERRUPT) interrupt(g, t);
        else if (a == ACT_KILL_TO_START) buf_reset(&t->aux);
        else if (a == ACT_PASTE) clip_request_paste(g, 0);
        return;
    }
    switch (a) {
    case ACT_LEFT: if (t->cursor) t->cursor--; break;
    case ACT_RIGHT: if (t->cursor < t->in.len) t->cursor++; break;
    case ACT_LINE_START: t->cursor = 0; break;
    case ACT_LINE_END: t->cursor = t->in.len; break;
    case ACT_HIST_PREV: history_browse(g, t, +1); break;
    case ACT_HIST_NEXT: history_browse(g, t, -1); break;
    case ACT_KILL_TO_START: buf_erase(&t->in, 0, t->cursor); t->cursor = 0; edited(t); break;
    case ACT_KILL_TO_END: buf_erase(&t->in, t->cursor, t->in.len); edited(t); break;
    case ACT_KILL_WORD: kill_word(t); break;
    case ACT_CLEAR: sb_clear(&t->sb); t->scroll = 0; break;
    case ACT_EOF:
        if (t->in.len && prompt) { buf_erase(&t->in, t->cursor, 1); edited(t); }     /* like readline: delete char */
        else tab_send(t, M_EOF, "", 0);                                                /* close stdin / exit shell */
        break;
    case ACT_COPY: { char *s = gui_selection_text(g); if (s && *s) clip_copy(g, s); free(s); break; }
    case ACT_PASTE: clip_request_paste(g, 0); break;
    case ACT_SEARCH:
        if (prompt) { free(t->saved_line); t->saved_line = xstrdup(buf_str(&t->in)); t->im = IM_SEARCH; buf_reset(&t->aux); }
        break;
    case ACT_COMPLETE: do_complete(g, t); break;
    case ACT_INTERRUPT: interrupt(g, t); break;
    case ACT_SUSPEND: if (t->state == TS_RUNNING) { sb_append(&t->sb, "^Z", 2, SB_INFO); tab_send(t, M_SUSP, "", 0); } break;
    case ACT_NEW_TAB: { Tab *cur = gui_active(g); if (!tab_open(g, cur ? cur->cwd : "/")) { if (cur) sb_add_line(&cur->sb, "cannot open another tab", SB_ERR, 0); } break; }
    case ACT_CLOSE_TAB: tab_close(g, g->active); break;
    case ACT_NEXT_TAB: if (g->ntabs) tab_switch(g, (g->active + 1) % g->ntabs); break;
    case ACT_PREV_TAB: if (g->ntabs) tab_switch(g, (g->active + g->ntabs - 1) % g->ntabs); break;
    case ACT_SCROLL_UP: t->scroll += g->rows - 1; break;
    case ACT_SCROLL_DOWN: t->scroll -= g->rows - 1; if (t->scroll < 0) t->scroll = 0; break;
    default: break;
    }
    g->sel.active = 0; g->dirty = 1;
}

/* ---------- KeyPress decoding ---------- */
void gui_key(Gui *g, XKeyEvent *ev) {
    Tab *t = gui_active(g);
    if (!t) return;
    char txt[64]; KeySym shifted = NoSymbol;
    int n = XLookupString(ev, txt, sizeof txt, &shifted, NULL);
    KeySym base = XLookupKeysym(ev, 0);
    unsigned mods = ev->state & MOD_MASK;
    g->blink = 1; g->dirty = 1;
    if (t->dead) { tab_close(g, g->active); return; }
    if (mods == MOD_ALT && base >= XK_1 && base <= XK_9) { tab_switch(g, (int)(base - XK_1)); return; }
    Action a = config_lookup(&g->cfg, base, mods);
    if (a != ACT_NONE) { tab_apply_action(g, t, a); return; }
    switch (shifted) {
    case XK_Return: case XK_KP_Enter: submit(g, t); g->sel.active = 0; return;
    case XK_BackSpace:
        if (t->im != IM_NORMAL) { if (t->aux.len) buf_erase(&t->aux, t->aux.len - 1, 1); }
        else if (t->cursor) { buf_erase(&t->in, t->cursor - 1, 1); t->cursor--; edited(t); }
        g->sel.active = 0; return;
    case XK_Delete: case XK_KP_Delete:
        if (t->im == IM_NORMAL) { buf_erase(&t->in, t->cursor, 1); edited(t); }
        return;
    case XK_Home: if (t->im == IM_NORMAL) t->cursor = 0; return;
    case XK_End: if (t->im == IM_NORMAL) t->cursor = t->in.len; return;
    case XK_Escape: if (t->im != IM_NORMAL) leave_mode(t); return;
    default: break;
    }
    if (!(mods & (MOD_CTRL | MOD_ALT)) && n > 0) {          /* ordinary typing */
        char buf[64]; size_t k = 0;
        for (int i = 0; i < n; i++) { unsigned char c = (unsigned char)txt[i]; if (c >= 0x20 && c != 0x7f) buf[k++] = (char)c; }
        if (k) { tab_insert(t, buf, k); g->sel.active = 0; }
    }
}
