#include "testutil.h"
#include "expansion.h"
#include "executor.h"
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

static ShellState *mk(const char *cwd) {
    EngineArgs a; memset(&a, 0, sizeof a);
    a.ctl_fd = -1; a.out_fd = -1; snprintf(a.cwd, sizeof a.cwd, "%s", cwd);
    return sh_new(&a);
}
static char *ex(ShellState *sh, const char *raw) {
    StrList l; strlist_init(&l);
    expand_word(sh, raw, &l, 1);
    char *r = str_join(l.v, l.n, "|");
    strlist_free(&l);
    return r;
}
#define EXPECT(raw, want) do { char *_r = ex(sh, raw); CHECK_STR(_r, want); free(_r); } while (0)

int main(void) {
    char dir[] = "/tmp/term_exp_XXXXXX";
    if (!mkdtemp(dir)) return 2;
    char p[512];
    const char *names[] = { "a.txt", "b.txt", "c.md", ".hidden.txt" };
    for (int i = 0; i < 4; i++) { snprintf(p, sizeof p, "%s/%s", dir, names[i]); close(open(p, O_CREAT | O_WRONLY, 0644)); }
    snprintf(p, sizeof p, "%s/sub", dir); mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/sub/x.txt", dir); close(open(p, O_CREAT | O_WRONLY, 0644));

    ShellState *sh = mk(dir);
    sh_set_var(sh, "NAME", "world", 0);
    sh_set_var(sh, "SP", "a b", 0);
    sh->last_status = 7; sh->nargs = 2; free(sh->args[1]); free(sh->args[2]);
    sh->args[1] = xstrdup("one"); sh->args[2] = xstrdup("two");

    EXPECT("hello", "hello");
    EXPECT("$NAME", "world");
    EXPECT("${NAME}s", "worlds");
    EXPECT("\"hi $NAME\"", "hi world");
    EXPECT("'hi $NAME'", "hi $NAME");                         /* single quotes: literal */
    EXPECT("\\$NAME", "$NAME");                               /* escaped */
    EXPECT("$?", "7");
    EXPECT("$#", "2");
    EXPECT("$1-$2", "one-two");
    EXPECT("$9", "");                                         /* unset positional */
    EXPECT("$SP", "a|b");                                     /* unquoted expansion splits */
    EXPECT("\"$SP\"", "a b");                                 /* quoted does not */
    EXPECT("$(echo hi)", "hi");
    EXPECT("`echo back`", "back");
    EXPECT("\"$(echo 'a   b')\"", "a   b");
    EXPECT("$(echo $(echo $(echo deep)))", "deep");           /* nested, innermost first */
    EXPECT("x$(echo mid)y", "xmidy");
    EXPECT("*.txt", "a.txt|b.txt");                           /* hidden files not matched */
    EXPECT("*.zip", "*.zip");                                 /* no match: pattern stays */
    EXPECT("'*.txt'", "*.txt");                               /* quoted: no globbing */
    EXPECT("sub/*.txt", "sub/x.txt");
    EXPECT("?.md", "c.md");
    EXPECT("[ab].txt", "a.txt|b.txt");
    EXPECT("a\\ b", "a b");
    EXPECT("\"\"", "");                                       /* empty quoted word is one empty field */
    char *s = expand_string(sh, "$SP  $NAME"); CHECK_STR(s, "a b  world"); free(s);

    sh_free(sh);
    snprintf(p, sizeof p, "rm -rf %s", dir); if (system(p)) {}
    TEST_DONE();
}
