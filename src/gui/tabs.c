/* tabs.c - tab lifecycle and handling of messages coming from a tab's engine. */
#include "gui.h"
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

Tab *gui_active(Gui *g) { return g->ntabs ? g->tabs[g->active] : NULL; }

Tab *tab_open(Gui *g, const char *cwd) {
    if (g->ntabs >= MAX_TABS) return NULL;
    Tab *t = xcalloc(1, sizeof *t);
    t->id = g->next_id++;
    snprintf(t->cwd, sizeof t->cwd, "%s", cwd);
    sb_init(&t->sb); buf_init(&t->in); buf_init(&t->aux); ipc_reader_init(&t->rd); strlist_init(&t->sel_items);
    t->hist_pos = -1; t->state = TS_RUNNING;
    if (tab_engine_spawn(&t->h, t->id, cwd, g->hist, g->process_mode) < 0) {
        sb_free(&t->sb); buf_free(&t->in); buf_free(&t->aux); ipc_reader_free(&t->rd); free(t);
        return NULL;
    }
    fd_set_nonblock(t->h.out_r);
    g->tabs[g->ntabs] = t;
    g->active = g->ntabs++;
    g->sel.active = 0; g->dirty = 1;
    return t;
}

static void tab_free(Tab *t) {
    sb_free(&t->sb); buf_free(&t->in); buf_free(&t->aux); ipc_reader_free(&t->rd);
    strlist_free(&t->sel_items); free(t->hist_saved); free(t->saved_line); free(t);
}

void gui_reap_dying(Gui *g, int block) {
    for (int i = 0; i < g->ndying;) {
        if (block) tab_engine_stop(&g->dying[i], 1);
        if (!g->dying[i].active || tab_engine_reap(&g->dying[i], NULL)) { g->dying[i] = g->dying[--g->ndying]; continue; }
        i++;
    }
}

void tab_close(Gui *g, int idx) {
    if (idx < 0 || idx >= g->ntabs) return;
    Tab *t = g->tabs[idx];
    if (g->process_mode && g->ndying < MAX_TABS) {      /* don't block the GUI waiting for the process */
        tab_engine_stop(&t->h, 0);
        g->dying[g->ndying++] = t->h;
    } else tab_engine_stop(&t->h, 1);
    memmove(&g->tabs[idx], &g->tabs[idx + 1], sizeof(Tab *) * (size_t)(g->ntabs - idx - 1));
    g->ntabs--;
    tab_free(t);
    if (g->active >= g->ntabs) g->active = g->ntabs - 1;
    if (g->active < 0) g->active = 0;
    if (g->ntabs == 0) g->quit = 1;
    g->sel.active = 0; g->dirty = 1;
}

void tab_switch(Gui *g, int idx) {
    if (idx < 0 || idx >= g->ntabs) return;
    g->active = idx; g->sel.active = 0; g->dirty = 1;
}

void tab_send(Tab *t, char type, const char *payload, size_t len) {
    if (t->h.ctl_w >= 0 && !t->dead) ipc_send(t->h.ctl_w, type, payload, (uint32_t)len);
}

void gui_load_config(Gui *g, Tab *report) {
    char path[PATH_MAX]; Buf warn; buf_init(&warn);
    config_path(path, sizeof path);
    config_load(&g->cfg, path, &warn);
    if (report && warn.len) {
        char *save = NULL;
        for (char *ln = strtok_r(warn.d, "\n", &save); ln; ln = strtok_r(NULL, "\n", &save)) {
            char msg[1200]; snprintf(msg, sizeof msg, "warning: %s", ln);
            sb_add_line(&report->sb, msg, SB_INFO, 0);
        }
    }
    buf_free(&warn); g->dirty = 1;
}

static void on_message(Gui *g, Tab *t, char type, const char *p, uint32_t len) {
    switch (type) {
    case M_OUT: sb_append(&t->sb, p, len, SB_NORMAL); g->sel.active = 0; break;
    case M_ERR: sb_append(&t->sb, p, len, SB_ERR); g->sel.active = 0; break;
    case M_PROMPT:
        if (!sb_open_empty(&t->sb)) sb_newline(&t->sb);
        t->state = TS_PROMPT; t->cont = len && p[0] == '1';
        break;
    case M_CWD: snprintf(t->cwd, sizeof t->cwd, "%.*s", (int)len, p); break;
    case M_EXIT: t->exited = 1; break;
    case M_CLEAR: sb_clear(&t->sb); t->scroll = 0; break;
    case M_RELOAD: gui_load_config(g, t); break;
    default: break;
    }
    g->dirty = 1;
}

void tab_read(Gui *g, Tab *t) {
    for (int iter = 0; iter < 16 && !t->eof; iter++) {
        ssize_t n = ipc_reader_fill(&t->rd, t->h.out_r);
        if (n == 0) { t->eof = 1; break; }
        if (n < 0) break;
        char type; const char *p; uint32_t len; int r;
        while ((r = ipc_reader_next(&t->rd, &type, &p, &len)) == 1) on_message(g, t, type, p, len);
        if (r < 0) { t->eof = 1; break; }
    }
    if (t->eof && !t->exited && !t->dead) {            /* engine vanished without saying goodbye */
        int st = 0, gone = tab_engine_reap(&t->h, &st);
        char msg[160];
        if (gone && WIFSIGNALED(st)) snprintf(msg, sizeof msg, "[tab process was killed by signal %d - press any key to close this tab]", WTERMSIG(st));
        else snprintf(msg, sizeof msg, "[tab shell terminated unexpectedly - press any key to close this tab]");
        sb_add_line(&t->sb, msg, SB_ERR, 0);
        t->dead = 1; t->state = TS_RUNNING; g->dirty = 1;
    }
}
