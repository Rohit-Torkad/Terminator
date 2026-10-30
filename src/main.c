/* main.c - command line: ./terminator [useProcess=True] [--headless] [--cwd DIR] */
#include "common.h"
#include "gui.h"
#include "headless.h"
#include <strings.h>
#include <unistd.h>

static void usage(void) {
    puts("usage: terminator [useProcess=True|False] [--headless] [--cwd DIR]\n"
         "  (default)        thread mode: one shell engine thread per tab\n"
         "  useProcess=True  process mode: one forked shell process per tab\n"
         "  --headless       no GUI; read commands from stdin (used by the test-suite)");
}

int main(int argc, char **argv) {
    int process_mode = 0, headless = 0;
    const char *cwd = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strncasecmp(argv[i], "useProcess=", 11)) {
            const char *v = argv[i] + 11;
            process_mode = !strcasecmp(v, "true") || !strcmp(v, "1") || !strcasecmp(v, "yes");
        } else if (!strcmp(argv[i], "--process")) process_mode = 1;
        else if (!strcmp(argv[i], "--headless")) headless = 1;
        else if (!strcmp(argv[i], "--cwd") && i + 1 < argc) cwd = argv[++i];
        else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) { usage(); return 0; }
        else { fprintf(stderr, "terminator: unknown argument '%s'\n", argv[i]); usage(); return 2; }
    }
    return headless ? headless_run(process_mode, cwd) : gui_run(process_mode, cwd);
}
