/* parser.c - builds pipelines, redirections and && || ; & lists from tokens. */
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { StrList argv; Redir *r; int nr, cap; } CmdB;

static void cmdb_free(CmdB *c) { strlist_free(&c->argv); for (int i = 0; i < c->nr; i++) free(c->r[i].target); free(c->r); }
static int cmdb_empty(const CmdB *c) { return c->argv.n == 0 && c->nr == 0; }

static void finish_cmd(CmdB *c, Pipeline *pl) {
    pl->cmds = xrealloc(pl->cmds, (size_t)(pl->ncmds + 1) * sizeof(Cmd));
    Cmd *d = &pl->cmds[pl->ncmds++];
    d->argc = c->argv.n;
    d->argv = c->argv.v ? c->argv.v : xcalloc(1, sizeof(char *));
    d->redirs = c->r; d->nredirs = c->nr;
    memset(c, 0, sizeof *c);
}

static const char *tokname(TokType t) {
    switch (t) {
    case T_PIPE: return "|"; case T_AMP: return "&"; case T_SEMI: return ";"; case T_LT: return "<";
    case T_GT: return ">"; case T_GTGT: return ">>"; case T_AND: return "&&"; case T_OR: return "||";
    case T_NL: return "newline"; default: return "word";
    }
}

void cmdlist_free(CmdList *l) {
    for (int i = 0; i < l->n; i++) {
        Pipeline *p = &l->items[i];
        for (int j = 0; j < p->ncmds; j++) {
            Cmd *c = &p->cmds[j];
            for (int k = 0; k < c->argc; k++) free(c->argv[k]);
            free(c->argv);
            for (int k = 0; k < c->nredirs; k++) free(c->redirs[k].target);
            free(c->redirs);
        }
        free(p->cmds); free(p->text);
    }
    free(l->items); l->items = NULL; l->n = 0;
}

int parse_tokens(const char *src, const TokenList *tl, CmdList *out, char *err, size_t en) {
    memset(out, 0, sizeof *out);
    CmdB cb; memset(&cb, 0, sizeof cb); strlist_init(&cb.argv);
    Pipeline pl; memset(&pl, 0, sizeof pl);
    size_t ps = 0, pe = 0; int have_start = 0, pending_conn = 0;

    for (int k = 0; k < tl->n; k++) {
        const Token *t = &tl->t[k];
        switch (t->type) {
        case T_WORD:
            if (!have_start) { ps = t->start; have_start = 1; }
            pe = t->end;
            strlist_add(&cb.argv, t->text);
            break;
        case T_LT: case T_GT: case T_GTGT: {
            if (k + 1 >= tl->n || tl->t[k + 1].type != T_WORD) {
                snprintf(err, en, "syntax error near unexpected token `%s'", k + 1 < tl->n ? tokname(tl->t[k + 1].type) : "newline");
                goto fail;
            }
            if (!have_start) { ps = t->start; have_start = 1; }
            const Token *w = &tl->t[++k];
            pe = w->end;
            if (cb.nr == cb.cap) { cb.cap = cb.cap ? cb.cap * 2 : 4; cb.r = xrealloc(cb.r, (size_t)cb.cap * sizeof(Redir)); }
            cb.r[cb.nr].kind = t->type == T_LT ? R_IN : t->type == T_GT ? R_OUT : R_APPEND;
            cb.r[cb.nr++].target = xstrdup(w->text);
            break;
        }
        case T_PIPE:
            if (cmdb_empty(&cb)) { snprintf(err, en, "syntax error near unexpected token `|'"); goto fail; }
            finish_cmd(&cb, &pl); strlist_init(&cb.argv);
            pending_conn = 1;
            break;
        case T_NL: case T_SEMI: case T_AMP: case T_AND: case T_OR:
            if (t->type == T_NL && cmdb_empty(&cb)) break;     /* blank line, or continuation after | && || */
            if (cmdb_empty(&cb) && pl.ncmds == 0) {
                snprintf(err, en, "syntax error near unexpected token `%s'", tokname(t->type));
                goto fail;
            }
            if (cmdb_empty(&cb)) { snprintf(err, en, "syntax error near unexpected token `%s'", tokname(t->type)); goto fail; }
            finish_cmd(&cb, &pl); strlist_init(&cb.argv);
            pl.background = t->type == T_AMP;
            pl.conn = t->type == T_AND ? CONN_AND : t->type == T_OR ? CONN_OR : CONN_SEQ;
            pl.text = xstrndup(src + ps, pe - ps);
            out->items = xrealloc(out->items, (size_t)(out->n + 1) * sizeof(Pipeline));
            out->items[out->n++] = pl;
            memset(&pl, 0, sizeof pl); have_start = 0;
            pending_conn = (t->type == T_AND || t->type == T_OR);
            break;
        }
    }
    if (!cmdb_empty(&cb)) {
        finish_cmd(&cb, &pl); strlist_init(&cb.argv);
        pl.text = xstrndup(src + ps, pe - ps);
        out->items = xrealloc(out->items, (size_t)(out->n + 1) * sizeof(Pipeline));
        out->items[out->n++] = pl;
        memset(&pl, 0, sizeof pl);
        pending_conn = 0;
    } else if (pl.ncmds > 0) { snprintf(err, en, "syntax error: unexpected end of input after `|'"); goto fail; }
    if (pending_conn) { snprintf(err, en, "syntax error: unexpected end of input"); goto fail; }
    cmdb_free(&cb);
    return 0;
fail:
    cmdb_free(&cb);
    for (int j = 0; j < pl.ncmds; j++) { for (int a = 0; a < pl.cmds[j].argc; a++) free(pl.cmds[j].argv[a]); free(pl.cmds[j].argv); for (int a = 0; a < pl.cmds[j].nredirs; a++) free(pl.cmds[j].redirs[a].target); free(pl.cmds[j].redirs); }
    free(pl.cmds); free(pl.text);
    cmdlist_free(out);
    return -1;
}

int parse_list(const char *src, CmdList *out, char *err, size_t en) {
    TokenList tl;
    memset(out, 0, sizeof *out);
    if (lex(src, &tl, err, en) < 0) return -1;
    int rc = parse_tokens(src, &tl, out, err, en);
    tokens_free(&tl);
    return rc;
}
