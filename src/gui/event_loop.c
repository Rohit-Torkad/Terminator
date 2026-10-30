/* event_loop.c - one poll() loop waits on the X connection and on every tab's
 * output pipe, so the GUI never blocks on a running command and never spins. */
#include "gui.h"
#include "signals.h"
#include <errno.h>
#include <poll.h>
#include <unistd.h>

void gui_mouse(Gui *g, XEvent *ev) {
    if (ev->type == ButtonPress) {
        int x = ev->xbutton.x, y = ev->xbutton.y, b = (int)ev->xbutton.button;
        if (y < gui_bar_height(g)) {
            if (b != Button1 && b != Button2) return;
            for (int i = 0; i < g->ntabs; i++) {
                int tx, tw, cx; gui_tab_rect(g, i, &tx, &tw);
                if (x < tx || x >= tx + tw) continue;
                int cwid = gui_close_rect(g, tx, tw, &cx);
                if (b == Button2 || (x >= cx - 2 && x < cx + cwid)) tab_close(g, i); else tab_switch(g, i);
                g->dirty = 1; return;
            }
            int px, pw; gui_plus_rect(g, &px, &pw);
            if (b == Button1 && x >= px && x < px + pw) {
                Tab *cur = gui_active(g);
                tab_open(g, cur ? cur->cwd : "/");
            }
            g->dirty = 1; return;
        }
        Tab *t = gui_active(g);
        if (!t) return;
        if (b == Button1) {
            g->dragging = 1; g->sel.active = 0;
            gui_cell_at(g, x, y, &g->sel.r0, &g->sel.c0);
            g->sel.r1 = g->sel.r0; g->sel.c1 = g->sel.c0;
            g->dirty = 1;
        } else if (b == Button2) clip_request_paste(g, 1);
        else if (b == Button4) { t->scroll += 3; g->sel.active = 0; g->dirty = 1; }
        else if (b == Button5) { t->scroll -= 3; if (t->scroll < 0) t->scroll = 0; g->sel.active = 0; g->dirty = 1; }
    } else if (ev->type == MotionNotify && g->dragging) {
        gui_cell_at(g, ev->xmotion.x, ev->xmotion.y, &g->sel.r1, &g->sel.c1);
        g->sel.active = !(g->sel.r0 == g->sel.r1 && g->sel.c0 == g->sel.c1);
        g->dirty = 1;
    } else if (ev->type == ButtonRelease && ev->xbutton.button == Button1 && g->dragging) {
        g->dragging = 0;
        if (g->sel.active) {                      /* selected text becomes the PRIMARY selection */
            char *s = gui_selection_text(g);
            if (s && *s) clip_copy(g, s);
            free(s);
        }
        g->dirty = 1;
    }
}

static void handle_x(Gui *g, XEvent *ev) {
    switch (ev->type) {
    case Expose: if (ev->xexpose.count == 0) g->dirty = 1; break;
    case ConfigureNotify:
        if (ev->xconfigure.width != g->w || ev->xconfigure.height != g->h) gui_resize(g, ev->xconfigure.width, ev->xconfigure.height);
        break;
    case KeyPress: gui_key(g, &ev->xkey); break;
    case ButtonPress: case ButtonRelease: case MotionNotify: gui_mouse(g, ev); break;
    case FocusIn: g->focused = 1; g->dirty = 1; break;
    case FocusOut: g->focused = 0; g->dirty = 1; break;
    case SelectionRequest: clip_on_request(g, &ev->xselectionrequest); break;
    case SelectionNotify: clip_on_notify(g, &ev->xselection); break;
    case SelectionClear: clip_on_clear(g); break;
    case ClientMessage:
        if ((Atom)ev->xclient.data.l[0] == g->a_delete) g->quit = 1;       /* window close button */
        break;
    default: break;
    }
}

int gui_run(int process_mode, const char *cwd) {
    Gui g;
    signals_init_gui();
    if (gui_init(&g, process_mode) < 0) return 1;
    g.hist = shared_history_create(process_mode);
    if (!g.hist) { fprintf(stderr, "terminator: cannot create shared history\n"); gui_shutdown(&g); return 1; }
    char start[PATH_MAX];
    if (cwd) snprintf(start, sizeof start, "%s", cwd);
    else if (!getcwd(start, sizeof start)) snprintf(start, sizeof start, "/");
    Tab *first = tab_open(&g, start);
    if (!first) { fprintf(stderr, "terminator: cannot start the first tab\n"); shared_history_destroy(g.hist); gui_shutdown(&g); return 1; }
    gui_load_config(&g, first);

    while (!g.quit && !g_terminate) {
        while (XPending(g.dpy)) { XEvent ev; XNextEvent(g.dpy, &ev); handle_x(&g, &ev); }
        if (g.quit) break;
        if (g.dirty) gui_draw(&g);

        struct pollfd fds[1 + MAX_TABS]; Tab *who[1 + MAX_TABS]; int nf = 0;
        fds[nf] = (struct pollfd){ ConnectionNumber(g.dpy), POLLIN, 0 }; who[nf++] = NULL;
        for (int i = 0; i < g.ntabs; i++)
            if (!g.tabs[i]->eof && g.tabs[i]->h.out_r >= 0) { fds[nf] = (struct pollfd){ g.tabs[i]->h.out_r, POLLIN, 0 }; who[nf++] = g.tabs[i]; }
        int r = poll(fds, (nfds_t)nf, 500);                 /* 500 ms: cursor blink + reaping */
        if (r < 0) { if (errno == EINTR) continue; break; }
        if (r == 0) { g.blink = !g.blink; g.dirty = g.focused; gui_reap_dying(&g, 0); continue; }
        for (int k = 1; k < nf; k++) if (fds[k].revents) tab_read(&g, who[k]);
        for (int i = g.ntabs - 1; i >= 0; i--)               /* tabs whose shell exited normally close themselves */
            if (g.tabs[i]->exited) tab_close(&g, i);
        gui_reap_dying(&g, 0);
    }

    for (int i = 0; i < g.ntabs; i++) tab_send(g.tabs[i], M_QUIT, "", 0);   /* ask all engines first, then wait */
    while (g.ntabs) tab_close(&g, g.ntabs - 1);
    gui_reap_dying(&g, 1);
    shared_history_destroy(g.hist);
    gui_shutdown(&g);
    return 0;
}
