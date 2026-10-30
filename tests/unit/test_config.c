#include "testutil.h"
#include "config.h"
#include <X11/keysym.h>
#include <unistd.h>

static void write_file(const char *p, const char *s) { FILE *f = fopen(p, "w"); fputs(s, f); fclose(f); }

int main(void) {
    Config c; config_defaults(&c);
    CHECK(config_lookup(&c, 'a', MOD_CTRL) == ACT_LINE_START);
    CHECK(config_lookup(&c, 'A', MOD_CTRL) == ACT_LINE_START);          /* case-insensitive letters */
    CHECK(config_lookup(&c, 'c', MOD_CTRL | MOD_SHIFT) == ACT_COPY);
    CHECK(config_lookup(&c, 'c', MOD_CTRL) == ACT_INTERRUPT);
    CHECK(config_lookup(&c, XK_Tab, 0) == ACT_COMPLETE);
    CHECK(config_lookup(&c, XK_Up, 0) == ACT_HIST_PREV);
    CHECK(config_lookup(&c, 'x', MOD_CTRL) == ACT_NONE);

    KeyCombo k; char err[200];
    CHECK(keyspec_parse("Ctrl+Shift+V", &k, err, sizeof err) == 0 && k.ks == 'v' && k.mods == (MOD_CTRL | MOD_SHIFT));
    CHECK(keyspec_parse("Alt+Left", &k, err, sizeof err) == 0 && k.ks == XK_Left && k.mods == MOD_ALT);
    CHECK(keyspec_parse("Ctrl+Bogus", &k, err, sizeof err) < 0);        /* unknown key */
    CHECK(keyspec_parse("Hyper+A", &k, err, sizeof err) < 0);           /* unknown modifier */
    CHECK(keyspec_parse("x", &k, err, sizeof err) < 0);                 /* plain printable would break typing */
    CHECK(keyspec_parse("", &k, err, sizeof err) < 0);
    char out[64]; keyspec_format((KeyCombo){ 'a', MOD_CTRL }, out, sizeof out); CHECK_STR(out, "Ctrl+A");

    char path[] = "/tmp/term_rc_XXXXXX"; int fd = mkstemp(path); close(fd);
    write_file(path, "# my bindings\nbind line_start = Ctrl+B\nbind kill_word = Alt+BackSpace\nbind nosuch = Ctrl+Q\n"
                     "bind complete = Ctrl+Bogus\nbind eof = Ctrl+Q\nbind history_search = Ctrl+Q\ngarbage line\n");
    Buf w; buf_init(&w);
    int nw = config_load(&c, path, &w);
    CHECK(config_lookup(&c, 'b', MOD_CTRL) == ACT_LINE_START);          /* valid custom binding applied */
    CHECK(config_lookup(&c, 'a', MOD_CTRL) == ACT_NONE);                /* default replaced */
    CHECK(config_lookup(&c, XK_BackSpace, MOD_ALT) == ACT_KILL_WORD);
    CHECK(config_lookup(&c, XK_Tab, 0) == ACT_COMPLETE);                /* invalid entry: default kept */
    CHECK(config_lookup(&c, 'd', MOD_CTRL) == ACT_EOF);                 /* conflicting pair reverted to defaults */
    CHECK(config_lookup(&c, 'r', MOD_CTRL) == ACT_SEARCH);
    CHECK(config_lookup(&c, 'q', MOD_CTRL) == ACT_NONE);
    CHECK(nw >= 4); CHECK(strstr(buf_str(&w), "unknown action") != NULL); CHECK(strstr(buf_str(&w), "unknown key") != NULL);
    CHECK(strstr(buf_str(&w), "bound to both") != NULL);
    buf_free(&w);

    CHECK(config_set_binding(path, "line_end", "Ctrl+N", err, sizeof err) == 0);       /* persisted */
    CHECK(config_set_binding(path, "line_end", "Ctrl+B", err, sizeof err) < 0);        /* conflicts with line_start */
    CHECK(config_set_binding(path, "nosuch", "Ctrl+N", err, sizeof err) < 0);
    buf_init(&w); config_load(&c, path, &w); buf_free(&w);
    CHECK(config_lookup(&c, 'n', MOD_CTRL) == ACT_LINE_END);
    CHECK(config_set_binding(path, "line_end", "Ctrl+M", err, sizeof err) == 0);       /* replaces, no duplicate line */
    buf_init(&w); config_load(&c, path, &w); buf_free(&w);
    CHECK(config_lookup(&c, 'm', MOD_CTRL) == ACT_LINE_END && config_lookup(&c, 'n', MOD_CTRL) == ACT_NONE);
    unlink(path);
    TEST_DONE();
}
