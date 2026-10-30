/* config.h - user key bindings (~/.terminatorrc). */
#ifndef TERMINATOR_CONFIG_H
#define TERMINATOR_CONFIG_H
#include <X11/Xlib.h>
#include "common.h"

typedef enum {
    ACT_NONE = -1,
    ACT_LEFT, ACT_RIGHT, ACT_LINE_START, ACT_LINE_END,
    ACT_HIST_PREV, ACT_HIST_NEXT,
    ACT_KILL_TO_START, ACT_KILL_TO_END, ACT_KILL_WORD,
    ACT_CLEAR, ACT_EOF, ACT_COPY, ACT_PASTE, ACT_SEARCH, ACT_COMPLETE,
    ACT_INTERRUPT, ACT_SUSPEND,
    ACT_NEW_TAB, ACT_CLOSE_TAB, ACT_NEXT_TAB, ACT_PREV_TAB,
    ACT_SCROLL_UP, ACT_SCROLL_DOWN,
    ACT_COUNT
} Action;

#define MOD_CTRL  ControlMask
#define MOD_SHIFT ShiftMask
#define MOD_ALT   Mod1Mask
#define MOD_MASK  (ControlMask | ShiftMask | Mod1Mask)

typedef struct { KeySym ks; unsigned mods; } KeyCombo;
typedef struct { KeyCombo key[ACT_COUNT]; unsigned char custom[ACT_COUNT]; } Config;

const char *action_name(Action a);
Action      action_from_name(const char *name);
/* "Ctrl+Shift+C" -> combo. Returns 0 on success, -1 with err filled. */
int         keyspec_parse(const char *spec, KeyCombo *out, char *err, size_t errsz);
void        keyspec_format(KeyCombo k, char *out, size_t n);

void        config_defaults(Config *c);
char       *config_path(char *out, size_t n);       /* $HOME/.terminatorrc */
/* Defaults, then the file. Invalid/conflicting entries are reported into
 * `warnings` (one per line) and ignored. Returns the number of warnings. */
int         config_load(Config *c, const char *path, Buf *warnings);
/* Validate and persist one binding (used by the `bind` builtin). */
int         config_set_binding(const char *path, const char *action, const char *spec,
                               char *err, size_t errsz);
Action      config_lookup(const Config *c, KeySym ks, unsigned mods);

#endif
