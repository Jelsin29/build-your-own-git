#ifndef MYGIT_DIFF_H
#define MYGIT_DIFF_H

#include <stddef.h>

/* Edit types for diff output */
typedef enum {
    DIFF_EQUAL,
    DIFF_INSERT,
    DIFF_DELETE
} mygit_diff_op;

/* A single edit in the diff */
typedef struct {
    mygit_diff_op op;
    const char   *line;   /* NOT NUL-terminated — use line_len */
    size_t        line_len;
    size_t        old_lineno; /* 1-based, 0 if INSERT */
    size_t        new_lineno; /* 1-based, 0 if DELETE */
} mygit_diff_edit;

/* Result of diffing two texts */
typedef struct {
    mygit_diff_edit *edits;
    size_t           edit_count;
    size_t           _capacity; /* internal */
} mygit_diff_result;

/*
 * Split text into lines. Returns an array of line pointers (into the
 * original buffer) and their lengths. Caller must free *lines and *lens.
 *
 * Returns line count, or (size_t)-1 on error.
 */
size_t mygit_diff_split_lines(const char *text, size_t text_len,
                              const char ***lines, size_t **lens);

/*
 * Compute the Myers diff between two sequences of lines.
 *
 * a_lines/a_lens/a_count: the "old" text lines
 * b_lines/b_lens/b_count: the "new" text lines
 *
 * result: output — caller must call mygit_diff_free() when done.
 *
 * Returns 0 on success, -1 on error.
 */
int mygit_diff_myers(const char **a_lines, const size_t *a_lens, size_t a_count,
                     const char **b_lines, const size_t *b_lens, size_t b_count,
                     mygit_diff_result *result);

/*
 * Free a diff result allocated by mygit_diff_myers.
 */
void mygit_diff_free(mygit_diff_result *result);

/*
 * Print a unified-style diff to stdout.
 *
 * old_name / new_name: file labels for the --- / +++ lines.
 * context_lines: number of context lines around each hunk (default 3).
 */
void mygit_diff_print_unified(const mygit_diff_result *result,
                              const char *old_name, const char *new_name,
                              int context_lines);

#endif /* MYGIT_DIFF_H */
