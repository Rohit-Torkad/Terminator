/* lexer.c - tokenizer and completeness detection (state machine over quotes,
 * escapes, $( ) nesting and backticks). */
#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NPOS ((size_t)-1)

static size_t skip_squote(const char *s, size_t i) {      /* i after opening ' */
    while (s[i] && s[i] != '\'') i++;
    return s[i] ? i + 1 : NPOS;
}
static size_t skip_dquote(const char *s, size_t i) {      /* i after opening " */
    while (s[i]) {
        if (s[i] == '\\' && s[i + 1]) { i += 2; continue; }
        if (s[i] == '"') return i + 1;
        if (s[i] == '$' && s[i + 1] == '(') {
            size_t j = lex_find_close_paren(s, i + 2);
            if (j == NPOS) return NPOS;
            i = j + 1; continue;
        }
        if (s[i] == '`') {
            size_t j = lex_find_close_backtick(s, i + 1);
            if (j == NPOS) return NPOS;
            i = j + 1; continue;
        }
        i++;
    }
    return NPOS;
}

size_t lex_find_close_backtick(const char *s, size_t i) {
    while (s[i]) {
        if (s[i] == '\\' && s[i + 1]) { i += 2; continue; }
        if (s[i] == '`') return i;
        i++;
    }
    return NPOS;
}

size_t lex_find_close_paren(const char *s, size_t i) {
    int depth = 1;
    while (s[i]) {
        char c = s[i];
        if (c == '\\' && s[i + 1]) { i += 2; continue; }
        if (c == '\'') { size_t j = skip_squote(s, i + 1); if (j == NPOS) return NPOS; i = j; continue; }
        if (c == '"') { size_t j = skip_dquote(s, i + 1); if (j == NPOS) return NPOS; i = j; continue; }
        if (c == '`') { size_t j = lex_find_close_backtick(s, i + 1); if (j == NPOS) return NPOS; i = j + 1; continue; }
        if (c == '(') depth++;
        else if (c == ')' && --depth == 0) return i;
        i++;
    }
    return NPOS;
}

static int is_op(char c) { return c == '|' || c == '&' || c == ';' || c == '<' || c == '>' || c == '\n'; }

/* returns end index of word starting at i, or NPOS (and *incomplete) on error */
static size_t scan_word(const char *s, size_t i, int *incomplete) {
    while (s[i] && s[i] != ' ' && s[i] != '\t' && s[i] != '\r' && !is_op(s[i])) {
        char c = s[i];
        if (c == '\\') {
            if (!s[i + 1]) { *incomplete = 1; return NPOS; }
            i += 2; continue;
        }
        size_t j;
        if (c == '\'') { j = skip_squote(s, i + 1); if (j == NPOS) { *incomplete = 1; return NPOS; } i = j; continue; }
        if (c == '"')  { j = skip_dquote(s, i + 1);  if (j == NPOS) { *incomplete = 1; return NPOS; } i = j; continue; }
        if (c == '`')  { j = lex_find_close_backtick(s, i + 1); if (j == NPOS) { *incomplete = 1; return NPOS; } i = j + 1; continue; }
        if (c == '$' && s[i + 1] == '(') { j = lex_find_close_paren(s, i + 2); if (j == NPOS) { *incomplete = 1; return NPOS; } i = j + 1; continue; }
        i++;
    }
    return i;
}

static void push(TokenList *tl, TokType t, const char *src, size_t a, size_t b) {
    if (tl->n == tl->cap) { tl->cap = tl->cap ? tl->cap * 2 : 16; tl->t = xrealloc(tl->t, (size_t)tl->cap * sizeof(Token)); }
    Token *k = &tl->t[tl->n++];
    k->type = t; k->start = a; k->end = b; k->text = t == T_WORD ? xstrndup(src + a, b - a) : NULL;
}

void tokens_free(TokenList *tl) {
    for (int i = 0; i < tl->n; i++) free(tl->t[i].text);
    free(tl->t); tl->t = NULL; tl->n = tl->cap = 0;
}

static int lex_impl(const char *s, TokenList *out, char *err, size_t en, int *incomplete) {
    size_t i = 0;
    memset(out, 0, sizeof *out);
    *incomplete = 0;
    while (s[i]) {
        char c = s[i];
        if (c == ' ' || c == '\t' || c == '\r') { i++; continue; }
        if (c == '\\' && s[i + 1] == '\n') { i += 2; continue; }
        if (c == '\\' && !s[i + 1]) { *incomplete = 1; snprintf(err, en, "unexpected end of input after backslash"); goto fail; }
        if (c == '#') { while (s[i] && s[i] != '\n') i++; continue; }
        if (c == '\n') { push(out, T_NL, s, i, i + 1); i++; continue; }
        if (c == '|') { if (s[i + 1] == '|') { push(out, T_OR, s, i, i + 2); i += 2; } else { push(out, T_PIPE, s, i, i + 1); i++; } continue; }
        if (c == '&') { if (s[i + 1] == '&') { push(out, T_AND, s, i, i + 2); i += 2; } else { push(out, T_AMP, s, i, i + 1); i++; } continue; }
        if (c == ';') { push(out, T_SEMI, s, i, i + 1); i++; continue; }
        if (c == '<') { push(out, T_LT, s, i, i + 1); i++; continue; }
        if (c == '>') { if (s[i + 1] == '>') { push(out, T_GTGT, s, i, i + 2); i += 2; } else { push(out, T_GT, s, i, i + 1); i++; } continue; }
        size_t e = scan_word(s, i, incomplete);
        if (e == NPOS) { snprintf(err, en, "unexpected end of input (unterminated quote or substitution)"); goto fail; }
        push(out, T_WORD, s, i, e);
        i = e;
    }
    return 0;
fail:
    tokens_free(out);
    return -1;
}

int lex(const char *src, TokenList *out, char *err, size_t errsz) {
    int inc;
    return lex_impl(src, out, err, errsz, &inc);
}

InputState input_completeness(const char *src) {
    TokenList tl; char err[128]; int inc;
    if (lex_impl(src, &tl, err, sizeof err, &inc) < 0) return inc ? INPUT_INCOMPLETE : INPUT_ERROR;
    InputState r = INPUT_COMPLETE;
    if (tl.n) {
        TokType t = tl.t[tl.n - 1].type;
        if (t == T_PIPE || t == T_AND || t == T_OR) r = INPUT_INCOMPLETE;
    }
    tokens_free(&tl);
    return r;
}
