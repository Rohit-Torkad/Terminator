#include "testutil.h"
#include "parser.h"

int main(void) {
    CmdList l; char e[160];
    CHECK(parse_list("ls -la | grep x | wc -l", &l, e, sizeof e) == 0);
    CHECK(l.n == 1 && l.items[0].ncmds == 3);
    CHECK_STR(l.items[0].cmds[1].argv[0], "grep");
    CHECK_STR(l.items[0].text, "ls -la | grep x | wc -l");
    cmdlist_free(&l);

    CHECK(parse_list("sort < in.txt > out.txt", &l, e, sizeof e) == 0);
    CHECK(l.items[0].cmds[0].nredirs == 2);
    CHECK(l.items[0].cmds[0].redirs[0].kind == R_IN && l.items[0].cmds[0].redirs[1].kind == R_OUT);
    CHECK_STR(l.items[0].cmds[0].redirs[1].target, "out.txt");
    cmdlist_free(&l);

    CHECK(parse_list("sleep 5 & echo hi; true && false || x", &l, e, sizeof e) == 0);
    CHECK(l.n == 5);
    CHECK(l.items[0].background == 1 && l.items[1].background == 0);
    CHECK(l.items[2].conn == CONN_AND && l.items[3].conn == CONN_OR && l.items[4].conn == CONN_SEQ);
    cmdlist_free(&l);

    CHECK(parse_list("echo \"a b\" 'c d'", &l, e, sizeof e) == 0);
    CHECK(l.items[0].cmds[0].argc == 3);                 /* quoted strings stay single words */
    cmdlist_free(&l);

    CHECK(parse_list("| ls", &l, e, sizeof e) < 0);
    CHECK(parse_list("ls |", &l, e, sizeof e) < 0);
    CHECK(parse_list("cat <", &l, e, sizeof e) < 0);
    CHECK(parse_list("cat > > x", &l, e, sizeof e) < 0);
    CHECK(parse_list("; ls", &l, e, sizeof e) < 0);
    CHECK(parse_list("echo \"unterminated", &l, e, sizeof e) < 0);
    CHECK(parse_list("   ", &l, e, sizeof e) == 0 && l.n == 0);
    cmdlist_free(&l);
    TEST_DONE();
}
