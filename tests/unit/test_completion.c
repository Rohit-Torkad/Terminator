#include "testutil.h"
#include "completion.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void) {
    char dir[] = "/tmp/term_comp_XXXXXX"; if (!mkdtemp(dir)) return 2;
    char p[600];
    const char *files[] = { "alpha.txt", "alpine.txt", "beta.md", ".dot", "with space.txt" };
    for (int i = 0; i < 5; i++) { snprintf(p, sizeof p, "%s/%s", dir, files[i]); close(open(p, O_CREAT | O_WRONLY, 0644)); }
    snprintf(p, sizeof p, "%s/docs", dir); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/docs/inner.c", dir); close(open(p, O_CREAT | O_WRONLY, 0644));

    StrList c; strlist_init(&c);
    CHECK(completion_candidates(dir, "be", &c) == 1); CHECK_STR(c.v[0], "beta.md"); strlist_free(&c);
    CHECK(completion_candidates(dir, "al", &c) == 2);
    char *pre = completion_common_prefix(&c); CHECK_STR(pre, "alp"); free(pre); strlist_free(&c);
    CHECK(completion_candidates(dir, "zzz", &c) == 0); strlist_free(&c);
    CHECK(completion_candidates(dir, "do", &c) == 1); CHECK_STR(c.v[0], "docs/"); strlist_free(&c);        /* dirs get a slash */
    CHECK(completion_candidates(dir, "docs/in", &c) == 1); CHECK_STR(c.v[0], "docs/inner.c"); strlist_free(&c);
    CHECK(completion_candidates(dir, ".d", &c) == 1); strlist_free(&c);                                    /* hidden when asked */
    int n = completion_candidates(dir, "", &c); CHECK(n == 5); strlist_free(&c);                           /* hidden not listed */
    CHECK(completion_candidates(dir, "wi", &c) == 1); strlist_free(&c);
    snprintf(p, sizeof p, "%s/al", dir);
    CHECK(completion_candidates("/", p + 1, &c) == 2); strlist_free(&c);                                   /* absolute path */
    CHECK(completion_candidates(dir, "\"with sp", &c) == 1); strlist_free(&c);                             /* inside quotes */

    char q; const char *line = "cat file1 | grep al";
    CHECK(completion_word_start(line, strlen(line), &q) == 17 && q == 0);
    line = "echo \"my fi";  CHECK(completion_word_start(line, strlen(line), &q) == 5 && q == '"');
    line = "ls a\\ b";      CHECK(completion_word_start(line, strlen(line), &q) == 3 && q == 0);       /* escaped space */
    line = "ls ";           CHECK(completion_word_start(line, strlen(line), &q) == 3);

    Buf b; buf_init(&b); completion_escape("with space.txt", 0, &b); CHECK_STR(b.d, "with\\ space.txt"); buf_free(&b);
    snprintf(p, sizeof p, "rm -rf %s", dir); if (system(p)) {}
    TEST_DONE();
}
