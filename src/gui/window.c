/* window.c - X connection, window, fonts, colours, resize. */
#include "gui.h"
#include <pwd.h>
#include <unistd.h>

static unsigned long color(Gui *g, const char *hex) {
    XColor c;
    Colormap cm = DefaultColormap(g->dpy, g->scr);
    if (!XParseColor(g->dpy, cm, hex, &c) || !XAllocColor(g->dpy, cm, &c)) return WhitePixel(g->dpy, g->scr);
    return c.pixel;
}

int gui_bar_height(const Gui *g) { return g->ch + 14; }

static void make_pixmap(Gui *g) {
    if (g->pm) XFreePixmap(g->dpy, g->pm);
    g->pmw = g->w > 0 ? g->w : 1; g->pmh = g->h > 0 ? g->h : 1;
    g->pm = XCreatePixmap(g->dpy, g->win, (unsigned)g->pmw, (unsigned)g->pmh, (unsigned)DefaultDepth(g->dpy, g->scr));
}

void gui_resize(Gui *g, int w, int h) {
    g->w = w; g->h = h;
    int uw = w - 16, uh = h - gui_bar_height(g) - 8;
    g->cols = uw / g->cw; g->rows = uh / g->ch;
    if (g->cols < 10) g->cols = 10;
    if (g->rows < 2) g->rows = 2;
    make_pixmap(g);
    g->sel.active = 0;
    g->dirty = 1;
}

int gui_init(Gui *g, int process_mode) {
    memset(g, 0, sizeof *g);
    g->process_mode = process_mode;
    g->dpy = XOpenDisplay(NULL);
    if (!g->dpy) { fprintf(stderr, "terminator: cannot open X display (is DISPLAY set and an X server running?)\n"); return -1; }
    g->scr = DefaultScreen(g->dpy);
    static const char *const fonts[] = {
        "-misc-fixed-medium-r-normal--18-*-*-*-c-90-iso8859-1", "-misc-fixed-medium-r-normal--15-*-*-*-c-90-iso8859-1",
        "9x15", "9x18", "8x16", "7x14", "fixed", NULL };
    for (int i = 0; fonts[i] && !g->font; i++) g->font = XLoadQueryFont(g->dpy, fonts[i]);
    if (!g->font) { fprintf(stderr, "terminator: no usable X font found\n"); XCloseDisplay(g->dpy); return -1; }
    g->cw = g->font->max_bounds.width; g->ch = g->font->ascent + g->font->descent + 1; g->asc = g->font->ascent;
    g->c_bg = color(g, "#1a1b26");  g->c_fg = color(g, "#c0caf5");   g->c_accent = color(g, "#7aa2f7");
    g->c_err = color(g, "#f7768e"); g->c_bar = color(g, "#13141c");  g->c_tab_act = color(g, "#24283b");
    g->c_tab_off = color(g, "#7f849c"); g->c_sep = color(g, "#2f334d"); g->c_sel = color(g, "#3d59a1");
    g->c_cursor = color(g, "#c0caf5"); g->c_info = color(g, "#e0af68"); g->c_dim = color(g, "#565f89");
    g->w = g->cw * 100 + 16; g->h = g->ch * 30 + gui_bar_height(g) + 8;
    g->win = XCreateSimpleWindow(g->dpy, RootWindow(g->dpy, g->scr), 0, 0, (unsigned)g->w, (unsigned)g->h, 0, 0, g->c_bg);
    XSelectInput(g->dpy, g->win, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask | ButtonMotionMask |
                                  StructureNotifyMask | FocusChangeMask);
    XStoreName(g->dpy, g->win, process_mode ? "Terminator (process mode)" : "Terminator");
    XClassHint ch = { (char *)"terminator", (char *)"Terminator" };
    XSetClassHint(g->dpy, g->win, &ch);
    XSizeHints sh; memset(&sh, 0, sizeof sh);
    sh.flags = PMinSize; sh.min_width = g->cw * 30 + 16; sh.min_height = g->ch * 6 + gui_bar_height(g) + 8;
    XSetWMNormalHints(g->dpy, g->win, &sh);
    g->a_delete = XInternAtom(g->dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g->dpy, g->win, &g->a_delete, 1);
    g->a_clip = XInternAtom(g->dpy, "CLIPBOARD", False);
    g->a_utf8 = XInternAtom(g->dpy, "UTF8_STRING", False);
    g->a_targets = XInternAtom(g->dpy, "TARGETS", False);
    g->a_prop = XInternAtom(g->dpy, "TERMINATOR_PASTE", False);
    g->a_text = XInternAtom(g->dpy, "TEXT", False);
    g->a_primary = XA_PRIMARY; g->a_string = XA_STRING;
    XGCValues v; v.font = g->font->fid;
    g->gc = XCreateGC(g->dpy, g->win, GCFont, &v);
    config_defaults(&g->cfg);
    struct passwd *pw = getpwuid(getuid());
    const char *u = getenv("USER");
    snprintf(g->user, sizeof g->user, "%s", u && *u ? u : (pw ? pw->pw_name : "user"));
    g->focused = 1; g->blink = 1;
    gui_resize(g, g->w, g->h);
    XMapWindow(g->dpy, g->win);
    XFlush(g->dpy);
    return 0;
}

void gui_shutdown(Gui *g) {
    if (!g->dpy) return;
    if (g->pm) XFreePixmap(g->dpy, g->pm);
    XFreeGC(g->dpy, g->gc);
    XFreeFont(g->dpy, g->font);
    XDestroyWindow(g->dpy, g->win);
    XCloseDisplay(g->dpy);
    for (int i = 0; i < g->nvis; i++) free(g->vis[i]);
    free(g->vis); free(g->clip);
    g->dpy = NULL;
}
