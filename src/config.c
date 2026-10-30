/* config.c - key binding table, ~/.terminatorrc parsing, validation, persistence.
 * Format:  bind <action> = <Mod+Mod+Key>     (# comments allowed) */
#include "config.h"
#include <X11/keysym.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char *const NAMES[ACT_COUNT] = {
    [ACT_LEFT] = "move_left", [ACT_RIGHT] = "move_right", [ACT_LINE_START] = "line_start",
    [ACT_LINE_END] = "line_end", [ACT_HIST_PREV] = "history_prev", [ACT_HIST_NEXT] = "history_next",
    [ACT_KILL_TO_START] = "kill_to_start", [ACT_KILL_TO_END] = "kill_to_end", [ACT_KILL_WORD] = "kill_word",
    [ACT_CLEAR] = "clear_screen", [ACT_EOF] = "eof", [ACT_COPY] = "copy", [ACT_PASTE] = "paste",
    [ACT_SEARCH] = "history_search", [ACT_COMPLETE] = "complete", [ACT_INTERRUPT] = "interrupt",
    [ACT_SUSPEND] = "suspend", [ACT_NEW_TAB] = "new_tab", [ACT_CLOSE_TAB] = "close_tab",
    [ACT_NEXT_TAB] = "next_tab", [ACT_PREV_TAB] = "prev_tab", [ACT_SCROLL_UP] = "scroll_up",
    [ACT_SCROLL_DOWN] = "scroll_down",
};

const char *action_name(Action a) { return (a >= 0 && a < ACT_COUNT) ? NAMES[a] : "?"; }
Action action_from_name(const char *n) {
    for (int i = 0; i < ACT_COUNT; i++) if (!strcmp(NAMES[i], n)) return (Action)i;
    return ACT_NONE;
}

static KeySym norm(KeySym k) { return (k >= XK_A && k <= XK_Z) ? k + 32 : k; }

int keyspec_parse(const char *spec, KeyCombo *out, char *err, size_t en) {
    char buf[96];
    if (!spec || !*spec || strlen(spec) >= sizeof buf) { snprintf(err, en, "empty or too long key specification"); return -1; }
    snprintf(buf, sizeof buf, "%s", spec);
    unsigned mods = 0;
    char *save = NULL, *tok, *prev = NULL;
    for (tok = strtok_r(buf, "+", &save); tok; tok = strtok_r(NULL, "+", &save)) {
        if (prev) {                      /* every token before the last is a modifier */
            if (!strcasecmp(prev, "ctrl") || !strcasecmp(prev, "control")) mods |= MOD_CTRL;
            else if (!strcasecmp(prev, "shift")) mods |= MOD_SHIFT;
            else if (!strcasecmp(prev, "alt") || !strcasecmp(prev, "meta")) mods |= MOD_ALT;
            else { snprintf(err, en, "unknown modifier '%s'", prev); return -1; }
        }
        prev = tok;
    }
    if (!prev) { snprintf(err, en, "malformed key specification '%s'", spec); return -1; }
    const char *key = prev;
    KeySym ks = (key[1] == 0 && isalpha((unsigned char)key[0])) ? (KeySym)tolower((unsigned char)key[0]) : XStringToKeysym(key);
    if (ks == NoSymbol) { snprintf(err, en, "unknown key '%s'", key); return -1; }
    ks = norm(ks);
    if (!(mods & (MOD_CTRL | MOD_ALT)) && ks >= 0x20 && ks <= 0x7e) {
        snprintf(err, en, "plain printable key '%s' cannot be bound (it would stop typing working)", key);
        return -1;
    }
    out->ks = ks; out->mods = mods;
    return 0;
}

void keyspec_format(KeyCombo k, char *out, size_t n) {
    char key[48];
    const char *nm = XKeysymToString(k.ks);
    snprintf(key, sizeof key, "%s", nm ? nm : "?");
    if (key[1] == 0 && isalpha((unsigned char)key[0])) key[0] = (char)toupper((unsigned char)key[0]);
    snprintf(out, n, "%s%s%s%s", (k.mods & MOD_CTRL) ? "Ctrl+" : "", (k.mods & MOD_SHIFT) ? "Shift+" : "",
             (k.mods & MOD_ALT) ? "Alt+" : "", key);
}

#define SET(a, m, k) do { c->key[a].ks = (k); c->key[a].mods = (m); } while (0)
void config_defaults(Config *c) {
    memset(c, 0, sizeof *c);
    SET(ACT_LEFT, 0, XK_Left);            SET(ACT_RIGHT, 0, XK_Right);
    SET(ACT_LINE_START, MOD_CTRL, 'a');   SET(ACT_LINE_END, MOD_CTRL, 'e');
    SET(ACT_HIST_PREV, 0, XK_Up);         SET(ACT_HIST_NEXT, 0, XK_Down);
    SET(ACT_KILL_TO_START, MOD_CTRL, 'u'); SET(ACT_KILL_TO_END, MOD_CTRL, 'k');
    SET(ACT_KILL_WORD, MOD_CTRL, 'w');    SET(ACT_CLEAR, MOD_CTRL, 'l');
    SET(ACT_EOF, MOD_CTRL, 'd');
    SET(ACT_COPY, MOD_CTRL | MOD_SHIFT, 'c'); SET(ACT_PASTE, MOD_CTRL | MOD_SHIFT, 'v');
    SET(ACT_SEARCH, MOD_CTRL, 'r');       SET(ACT_COMPLETE, 0, XK_Tab);
    SET(ACT_INTERRUPT, MOD_CTRL, 'c');    SET(ACT_SUSPEND, MOD_CTRL, 'z');
    SET(ACT_NEW_TAB, MOD_CTRL | MOD_SHIFT, 't'); SET(ACT_CLOSE_TAB, MOD_CTRL | MOD_SHIFT, 'w');
    SET(ACT_NEXT_TAB, MOD_CTRL, XK_Page_Down); SET(ACT_PREV_TAB, MOD_CTRL, XK_Page_Up);
    SET(ACT_SCROLL_UP, MOD_SHIFT, XK_Page_Up); SET(ACT_SCROLL_DOWN, MOD_SHIFT, XK_Page_Down);
}

char *config_path(char *out, size_t n) {
    const char *over = getenv("TERMINATOR_RC");
    if (over && *over) snprintf(out, n, "%s", over);
    else snprintf(out, n, "%s/.terminatorrc", path_home());
    return out;
}

/* Returns the first other action whose binding equals k, or ACT_NONE. */
static Action find_conflict(const Config *c, Action self, KeyCombo k) {
    for (int i = 0; i < ACT_COUNT; i++)
        if (i != (int)self && c->key[i].ks == k.ks && c->key[i].mods == k.mods) return (Action)i;
    return ACT_NONE;
}

int config_load(Config *c, const char *path, Buf *warn) {
    config_defaults(c);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    Config def = *c;
    int nw = 0, lineno = 0;
    char *ln = NULL; size_t cap = 0;
    while (getline(&ln, &cap, f) >= 0) {
        lineno++;
        char *s = str_trim(ln);
        if (!*s || *s == '#') continue;
        char act[64], spec[64];
        if (sscanf(s, "bind %63s = %63s", act, spec) != 2) {
            buf_addf(warn, "%s:%d: malformed line (expected: bind <action> = <Key>)\n", path, lineno); nw++; continue;
        }
        Action a = action_from_name(act);
        if (a == ACT_NONE) { buf_addf(warn, "%s:%d: unknown action '%s'\n", path, lineno, act); nw++; continue; }
        KeyCombo k; char err[160];
        if (keyspec_parse(spec, &k, err, sizeof err) < 0) { buf_addf(warn, "%s:%d: %s\n", path, lineno, err); nw++; continue; }
        c->key[a] = k; c->custom[a] = 1;
    }
    free(ln); fclose(f);
    /* Conflict pass on the final table. Every custom binding involved in a clash is
     * reverted to its default, so the result never depends on the order of the lines. */
    for (int again = 1; again;) {
        again = 0;
        unsigned char revert[ACT_COUNT]; memset(revert, 0, sizeof revert);
        for (int i = 0; i < ACT_COUNT; i++) {
            Action o = find_conflict(c, (Action)i, c->key[i]);
            if (o == ACT_NONE) continue;
            char kn[48]; keyspec_format(c->key[i], kn, sizeof kn);
            if (c->custom[i] && !revert[i]) {
                buf_addf(warn, "%s: %s is bound to both %s and %s; reverting %s to its default\n", path, kn,
                         action_name((Action)i), action_name(o), action_name((Action)i));
                nw++;
            }
            if (c->custom[i]) revert[i] = 1;
            if (c->custom[o]) revert[o] = 1;
        }
        for (int i = 0; i < ACT_COUNT; i++)
            if (revert[i]) { c->key[i] = def.key[i]; c->custom[i] = 0; again = 1; }
    }
    return nw;
}

Action config_lookup(const Config *c, KeySym ks, unsigned mods) {
    ks = norm(ks); mods &= MOD_MASK;
    for (int i = 0; i < ACT_COUNT; i++) if (c->key[i].ks == ks && c->key[i].mods == mods) return (Action)i;
    return ACT_NONE;
}

int config_set_binding(const char *path, const char *action, const char *spec, char *err, size_t en) {
    Action a = action_from_name(action);
    if (a == ACT_NONE) { snprintf(err, en, "unknown action '%s'", action); return -1; }
    KeyCombo k;
    if (keyspec_parse(spec, &k, err, en) < 0) return -1;
    Config cur; Buf w; buf_init(&w);
    config_load(&cur, path, &w); buf_free(&w);
    Action other = find_conflict(&cur, a, k);
    if (other != ACT_NONE) { snprintf(err, en, "%s is already bound to '%s'; rebind that action first", spec, action_name(other)); return -1; }
    /* rewrite file: replace existing line for this action, else append */
    Buf out; buf_init(&out);
    FILE *f = fopen(path, "r"); int replaced = 0;
    char norm_spec[64]; keyspec_format(k, norm_spec, sizeof norm_spec);
    if (f) {
        char *ln = NULL; size_t cap = 0;
        while (getline(&ln, &cap, f) >= 0) {
            char a2[64], s2[64], tmp[1024]; snprintf(tmp, sizeof tmp, "%s", ln);
            if (sscanf(str_trim(tmp), "bind %63s = %63s", a2, s2) == 2 && !strcmp(a2, action)) {
                if (!replaced) buf_addf(&out, "bind %s = %s\n", action, norm_spec);
                replaced = 1;
            } else buf_adds(&out, ln);
        }
        free(ln); fclose(f);
    }
    if (!replaced) buf_addf(&out, "bind %s = %s\n", action, norm_spec);
    char tmp[PATH_MAX + 16]; snprintf(tmp, sizeof tmp, "%s.tmp%d", path, (int)getpid());
    FILE *o = fopen(tmp, "w");
    if (!o) { snprintf(err, en, "cannot write %s", path); buf_free(&out); return -1; }
    fputs(buf_str(&out), o);
    if (fclose(o) != 0 || rename(tmp, path) != 0) { snprintf(err, en, "cannot save %s", path); unlink(tmp); buf_free(&out); return -1; }
    buf_free(&out);
    return 0;
}
