/* completion.h - filename completion relative to a tab's working directory. */
#ifndef TERMINATOR_COMPLETION_H
#define TERMINATOR_COMPLETION_H
#include "common.h"

/* Find the word that ends at `cursor` (honouring quotes and backslash escapes).
 * Returns start offset; *quote is 0, '\'' or '"' if inside an open quote. */
size_t completion_word_start(const char *line, size_t cursor, char *quote);

/* Candidates for `word` (already unescaped). Each candidate is the full
 * replacement text for the word, e.g. "dir/" for directories. */
int completion_candidates(const char *cwd, const char *word, StrList *out);

/* Longest common prefix of all strings in `l`. */
char *completion_common_prefix(const StrList *l);

/* Shell-escape a completed name (spaces, quotes, ...) unless inside quotes. */
void completion_escape(const char *name, char quote, Buf *out);

#endif
