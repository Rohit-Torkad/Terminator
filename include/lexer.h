/* lexer.h - tokenizer. Words keep their raw text (quotes and escapes intact);
 * expansion.c interprets them later, so operators inside quotes are never
 * mistaken for operators. */
#ifndef TERMINATOR_LEXER_H
#define TERMINATOR_LEXER_H
#include "common.h"

typedef enum { T_WORD, T_PIPE, T_AMP, T_SEMI, T_LT, T_GT, T_GTGT, T_AND, T_OR, T_NL } TokType;
typedef struct { TokType type; char *text; size_t start, end; } Token;
typedef struct { Token *t; int n, cap; } TokenList;

int  lex(const char *src, TokenList *out, char *err, size_t errsz);  /* 0 ok, -1 error */
void tokens_free(TokenList *tl);

/* s[i] is the first char after "$(" ; returns index of the matching ')' or
 * (size_t)-1 when unterminated. Understands nesting, quotes and backticks. */
size_t lex_find_close_paren(const char *s, size_t i);
/* s[i] is the first char after an opening backtick; index of closing one. */
size_t lex_find_close_backtick(const char *s, size_t i);

typedef enum { INPUT_COMPLETE, INPUT_INCOMPLETE, INPUT_ERROR } InputState;
/* Completeness detection: open quote, trailing backslash, open $( or `,
 * trailing | && ||. */
InputState input_completeness(const char *src);

#endif
