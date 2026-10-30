#include "testutil.h"
#include "lexer.h"

static int count(const char *s, TokType t) {
    TokenList tl; char e[128]; int n = 0;
    if (lex(s, &tl, e, sizeof e) < 0) return -1;
    for (int i = 0; i < tl.n; i++) if (tl.t[i].type == t) n++;
    tokens_free(&tl);
    return n;
}

int main(void) {
    TokenList tl; char e[128];
    CHECK(lex("echo \"a | b\" 'c > d' | wc -l", &tl, e, sizeof e) == 0);
    CHECK(tl.n == 6);                                   /* echo "a | b" 'c > d' | wc -l */
    CHECK(tl.t[1].type == T_WORD); CHECK_STR(tl.t[1].text, "\"a | b\"");
    CHECK(tl.t[3].type == T_PIPE);
    tokens_free(&tl);
    CHECK(count("a | b | c", T_PIPE) == 2);
    CHECK(count("a < in > out", T_LT) == 1 && count("a < in > out", T_GT) == 1);
    CHECK(count("a >> out", T_GTGT) == 1);
    CHECK(count("a && b || c", T_AND) == 1 && count("a && b || c", T_OR) == 1);
    CHECK(count("sleep 1 &", T_AMP) == 1);
    CHECK(count("echo 'a;b' \"c&d\" \\; x", T_SEMI) == 0);          /* quoted/escaped operators are not operators */
    CHECK(count("echo $(echo a | tr a b) x", T_PIPE) == 0);          /* operators inside $( ) belong to the substitution */
    CHECK(count("echo `echo a | cat`", T_PIPE) == 0);
    CHECK(count("echo a # comment | more", T_PIPE) == 0);
    CHECK(count("a\nb", T_NL) == 1);

    CHECK(input_completeness("echo hi") == INPUT_COMPLETE);
    CHECK(input_completeness("echo \"abc") == INPUT_INCOMPLETE);
    CHECK(input_completeness("echo 'abc") == INPUT_INCOMPLETE);
    CHECK(input_completeness("echo abc \\") == INPUT_INCOMPLETE);
    CHECK(input_completeness("echo $(echo hi") == INPUT_INCOMPLETE);
    CHECK(input_completeness("echo $(echo $(echo hi)") == INPUT_INCOMPLETE);
    CHECK(input_completeness("echo `date") == INPUT_INCOMPLETE);
    CHECK(input_completeness("ls |") == INPUT_INCOMPLETE);
    CHECK(input_completeness("a &&") == INPUT_INCOMPLETE);
    CHECK(input_completeness("echo \"a\nb\"") == INPUT_COMPLETE);    /* quote closed on the 2nd line */
    CHECK(input_completeness("echo 'it''s'") == INPUT_COMPLETE);
    CHECK(input_completeness("echo \"$(echo \"x\")\"") == INPUT_COMPLETE);

    size_t p = lex_find_close_paren("echo (nested) ) tail", 0);        /* nested parens */
    CHECK(p == 14);
    CHECK(lex_find_close_paren("a \")\" b)", 0) == 7);                /* ')' in quotes is skipped */
    TEST_DONE();
}
