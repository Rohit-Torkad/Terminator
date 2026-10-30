/* paths.c - purely lexical path handling so that per-tab working directories
 * never depend on the process-wide cwd (which threads share). */
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <unistd.h>

const char *path_home(void) {
    const char *h = getenv("HOME");
    if (h && *h) return h;
    struct passwd *pw = getpwuid(getuid());
    return pw && pw->pw_dir ? pw->pw_dir : "/";
}

int path_resolve(const char *base, const char *path, char *out, size_t n) {
    Buf full; buf_init(&full);
    if (path[0] == '~' && (path[1] == '/' || path[1] == 0)) { buf_adds(&full, path_home()); buf_adds(&full, path + 1); }
    else if (path[0] == '/') buf_adds(&full, path);
    else { buf_adds(&full, base); buf_addc(&full, '/'); buf_adds(&full, path); }
    /* collapse components */
    char **comp = xmalloc(sizeof(char *) * (full.len / 1 + 2));
    int nc = 0;
    char *s = full.d, *save = NULL;
    for (char *t = strtok_r(s, "/", &save); t; t = strtok_r(NULL, "/", &save)) {
        if (!strcmp(t, ".")) continue;
        if (!strcmp(t, "..")) { if (nc) nc--; continue; }
        comp[nc++] = t;
    }
    Buf res; buf_init(&res);
    if (!nc) buf_addc(&res, '/');
    for (int i = 0; i < nc; i++) { buf_addc(&res, '/'); buf_adds(&res, comp[i]); }
    int rc = res.len < n ? 0 : -1;
    if (rc == 0) memcpy(out, res.d, res.len + 1);
    free(comp); buf_free(&full); buf_free(&res);
    return rc;
}

void path_display(const char *cwd, char *out, size_t n) {
    const char *h = path_home();
    size_t hl = strlen(h);
    if (hl > 1 && !strncmp(cwd, h, hl) && (cwd[hl] == '/' || cwd[hl] == 0)) snprintf(out, n, "~%s", cwd + hl);
    else snprintf(out, n, "%s", cwd);
}
