/* parser.h - token stream -> command list. */
#ifndef TERMINATOR_PARSER_H
#define TERMINATOR_PARSER_H
#include "lexer.h"

typedef enum { R_IN, R_OUT, R_APPEND } RedirKind;
typedef struct { RedirKind kind; char *target; } Redir;       /* target is raw text */
typedef struct { char **argv; int argc; Redir *redirs; int nredirs; } Cmd;

typedef enum { CONN_SEQ, CONN_AND, CONN_OR } Conn;            /* what follows this pipeline */
typedef struct { Cmd *cmds; int ncmds; int background; Conn conn; char *text; } Pipeline;
typedef struct { Pipeline *items; int n; } CmdList;

int  parse_tokens(const char *src, const TokenList *tl, CmdList *out, char *err, size_t errsz);
int  parse_list(const char *src, CmdList *out, char *err, size_t errsz);
void cmdlist_free(CmdList *l);

#endif
