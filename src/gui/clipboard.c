/* clipboard.c - X11 selection handling for copy (Ctrl+Shift+C / mouse select)
 * and paste (Ctrl+Shift+V / middle click). */
#include "gui.h"

void clip_copy(Gui *g, const char *text) {
    free(g->clip);
    g->clip = xstrdup(text);
    XSetSelectionOwner(g->dpy, g->a_clip, g->win, CurrentTime);
    XSetSelectionOwner(g->dpy, g->a_primary, g->win, CurrentTime);
    XFlush(g->dpy);
}

void clip_on_clear(Gui *g) { g->sel.active = 0; g->dirty = 1; }

void clip_on_request(Gui *g, XSelectionRequestEvent *rq) {
    XSelectionEvent r; memset(&r, 0, sizeof r);
    r.type = SelectionNotify; r.display = rq->display; r.requestor = rq->requestor;
    r.selection = rq->selection; r.target = rq->target; r.time = rq->time;
    Atom prop = rq->property ? rq->property : rq->target;
    r.property = prop;
    if (!g->clip) r.property = None;
    else if (rq->target == g->a_targets) {
        Atom t[4] = { g->a_targets, g->a_utf8, g->a_string, g->a_text };
        XChangeProperty(g->dpy, rq->requestor, prop, XA_ATOM, 32, PropModeReplace, (unsigned char *)t, 4);
    } else if (rq->target == g->a_utf8 || rq->target == g->a_string || rq->target == g->a_text) {
        XChangeProperty(g->dpy, rq->requestor, prop, rq->target == g->a_text ? g->a_utf8 : rq->target, 8,
                        PropModeReplace, (unsigned char *)g->clip, (int)strlen(g->clip));
    } else r.property = None;
    XSendEvent(g->dpy, rq->requestor, False, 0, (XEvent *)&r);
    XFlush(g->dpy);
}

void clip_request_paste(Gui *g, int primary) {
    Atom sel = primary ? g->a_primary : g->a_clip;
    if (XGetSelectionOwner(g->dpy, sel) == g->win && g->clip) {      /* our own text: no round trip */
        Tab *t = gui_active(g);
        if (t) { char *c = xstrdup(g->clip); for (char *p = c; *p; p++) if (*p == '\n' || *p == '\r') *p = ' '; tab_insert(t, c, strlen(c)); free(c); }
        g->dirty = 1;
        return;
    }
    XConvertSelection(g->dpy, sel, g->a_utf8, g->a_prop, g->win, CurrentTime);
    XFlush(g->dpy);
}

void clip_on_notify(Gui *g, XSelectionEvent *ev) {
    if (ev->property == None) return;
    Atom type; int fmt; unsigned long n, left; unsigned char *data = NULL;
    if (XGetWindowProperty(g->dpy, g->win, g->a_prop, 0, 1 << 18, True, AnyPropertyType, &type, &fmt, &n, &left, &data) != Success || !data) return;
    Tab *t = gui_active(g);
    if (t && fmt == 8 && n) {
        char *c = xstrndup((char *)data, n);
        size_t len = strlen(c);
        while (len && (c[len - 1] == '\n' || c[len - 1] == '\r')) c[--len] = 0;      /* pasting never auto-runs */
        for (char *p = c; *p; p++) if (*p == '\n' || *p == '\r' || *p == '\t') *p = ' ';
        tab_insert(t, c, strlen(c));
        free(c);
    }
    XFree(data);
    g->dirty = 1;
}

char *gui_selection_text(Gui *g) {
    if (!g->sel.active || !g->vis) return NULL;
    int r0 = g->sel.r0, c0 = g->sel.c0, r1 = g->sel.r1, c1 = g->sel.c1;
    if (r0 > r1 || (r0 == r1 && c0 > c1)) { int a = r0, b = c0; r0 = r1; c0 = c1; r1 = a; c1 = b; }
    Buf b; buf_init(&b);
    for (int r = r0; r <= r1 && r < g->nvis; r++) {
        const char *row = g->vis[r] ? g->vis[r] : "";
        int len = (int)strlen(row), from = r == r0 ? c0 : 0, to = r == r1 ? c1 + 1 : len;
        if (to > len) to = len;
        if (from < to) buf_add(&b, row + from, (size_t)(to - from));
        if (r != r1) buf_addc(&b, '\n');
    }
    return buf_take(&b);
}
