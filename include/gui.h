/* gui.h - Xlib frontend shared declarations. Only the GUI (main) thread/process
 * ever calls Xlib. */
#ifndef TERMINATOR_GUI_H
#define TERMINATOR_GUI_H
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include "common.h"
#include "config.h"
#include "history.h"
#include "tab.h"

typedef struct { int active, r0, c0, r1, c1; } Selection;

typedef struct Gui {
    Display *dpy; int scr; Window win; GC gc; Pixmap pm; int pmw, pmh;
    XFontStruct *font; int cw, ch, asc;
    int w, h, cols, rows;
    unsigned long c_bg, c_fg, c_accent, c_err, c_bar, c_tab_act, c_tab_off, c_sep, c_sel, c_cursor, c_info, c_dim;
    Atom a_delete, a_clip, a_utf8, a_targets, a_prop, a_primary, a_string, a_text;
    Tab *tabs[MAX_TABS]; int ntabs, active, next_id;
    History *hist; Config cfg; int process_mode;
    char user[64];
    char *clip; Selection sel; int dragging;
    char **vis; int nvis;                      /* plain text of the visible rows (for copy) */
    int dirty, focused, blink, quit;
    TabHandle dying[MAX_TABS]; int ndying;     /* process-mode engines being shut down */
} Gui;

int  gui_run(int process_mode, const char *cwd);

/* window.c */
int  gui_init(Gui *g, int process_mode);
void gui_shutdown(Gui *g);
void gui_resize(Gui *g, int w, int h);
int  gui_bar_height(const Gui *g);

/* tabs.c */
Tab *gui_active(Gui *g);
Tab *tab_open(Gui *g, const char *cwd);
void tab_close(Gui *g, int idx);
void tab_switch(Gui *g, int idx);
void tab_read(Gui *g, Tab *t);                       /* drain engine pipe, apply messages */
void tab_send(Tab *t, char type, const char *payload, size_t len);
void gui_reap_dying(Gui *g, int block);
void gui_load_config(Gui *g, Tab *report_to);

/* renderer.c */
void gui_draw(Gui *g);
int  gui_tab_rect(const Gui *g, int i, int *x, int *w);   /* geometry of tab i */
int  gui_plus_rect(const Gui *g, int *x, int *w);
const char *gui_prompt(const Gui *g, const Tab *t);
int  gui_close_rect(const Gui *g, int tab_x, int tab_w, int *cx);
void gui_cell_at(const Gui *g, int px, int py, int *row, int *col);

/* input.c */
void gui_key(Gui *g, XKeyEvent *ev);
void tab_insert(Tab *t, const char *s, size_t n);
void tab_apply_action(Gui *g, Tab *t, Action a);

/* clipboard.c */
void  clip_copy(Gui *g, const char *text);
void  clip_request_paste(Gui *g, int primary);
void  clip_on_request(Gui *g, XSelectionRequestEvent *ev);
void  clip_on_notify(Gui *g, XSelectionEvent *ev);
void  clip_on_clear(Gui *g);
char *gui_selection_text(Gui *g);

/* event_loop.c */
void gui_mouse(Gui *g, XEvent *ev);

#endif
