#include "diff.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Line splitting --------------------------------------------------- */

size_t mygit_diff_split_lines(const char *text, size_t text_len,
                              const char ***lines_out, size_t **lens_out)
{
    if (!text || text_len == 0) {
        *lines_out = NULL;
        *lens_out = NULL;
        return 0;
    }

    /* Count lines first */
    size_t count = 0;
    for (size_t i = 0; i < text_len; i++) {
        if (text[i] == '\n') {
            count++;
        }
    }
    /* If text doesn't end with newline, there's one more line */
    if (text_len > 0 && text[text_len - 1] != '\n') {
        count++;
    }

    const char **lines = malloc(count * sizeof(const char *));
    size_t *lens = malloc(count * sizeof(size_t));
    if (!lines || !lens) {
        free(lines);
        free(lens);
        return (size_t)-1;
    }

    size_t idx = 0;
    const char *start = text;
    for (size_t i = 0; i < text_len; i++) {
        if (text[i] == '\n') {
            lines[idx] = start;
            lens[idx] = (size_t)(text + i - start);
            idx++;
            start = text + i + 1;
        }
    }
    /* Trailing line without newline */
    if (start < text + text_len) {
        lines[idx] = start;
        lens[idx] = (size_t)(text + text_len - start);
        idx++;
    }

    *lines_out = lines;
    *lens_out = lens;
    return idx;
}

/* --- Myers diff -------------------------------------------------------- */

static int lines_equal(const char *a, size_t a_len,
                       const char *b, size_t b_len)
{
    return a_len == b_len && memcmp(a, b, a_len) == 0;
}

static int result_push(mygit_diff_result *r, mygit_diff_op op,
                       const char *line, size_t line_len,
                       size_t old_lineno, size_t new_lineno)
{
    if (r->edit_count == r->_capacity) {
        size_t new_cap = r->_capacity ? r->_capacity * 2 : 64;
        mygit_diff_edit *tmp = realloc(r->edits, new_cap * sizeof(*tmp));
        if (!tmp) return -1;
        r->edits = tmp;
        r->_capacity = new_cap;
    }
    mygit_diff_edit *e = &r->edits[r->edit_count++];
    e->op = op;
    e->line = line;
    e->line_len = line_len;
    e->old_lineno = old_lineno;
    e->new_lineno = new_lineno;
    return 0;
}

/*
 * Myers diff — shortest edit script (SES).
 *
 * Implementation follows the original paper:
 *   E. Myers, "An O(ND) Difference Algorithm and Its Variations", 1986
 *
 * We trace forward through the edit graph, recording the furthest-reaching
 * endpoints for each diagonal at each edit depth d. Then we backtrace to
 * reconstruct the edit sequence.
 */
int mygit_diff_myers(const char **a, const size_t *a_lens, size_t a_count,
                     const char **b, const size_t *b_lens, size_t b_count,
                     mygit_diff_result *result)
{
    memset(result, 0, sizeof(*result));

    int n = (int)a_count;
    int m = (int)b_count;

    /* Handle trivial cases */
    if (n == 0 && m == 0) return 0;

    if (n == 0) {
        for (int j = 0; j < m; j++) {
            if (result_push(result, DIFF_INSERT, b[j], b_lens[j],
                            0, (size_t)(j + 1)) < 0) return -1;
        }
        return 0;
    }
    if (m == 0) {
        for (int i = 0; i < n; i++) {
            if (result_push(result, DIFF_DELETE, a[i], a_lens[i],
                            (size_t)(i + 1), 0) < 0) return -1;
        }
        return 0;
    }

    int max_d = n + m;
    /* V array: v[k + max_d] = x for diagonal k */
    size_t v_size = (size_t)(2 * max_d + 1);
    int *v = malloc(v_size * sizeof(int));
    if (!v) return -1;

    /* Store all V snapshots for backtracing */
    int **trace = NULL;
    int trace_count = 0;
    int trace_cap = 0;

    v[max_d + 1] = 0; /* sentinel for d=0 */

    int found_d = -1;
    for (int d = 0; d <= max_d; d++) {
        for (int k = -d; k <= d; k += 2) {
            int x;
            if (k == -d || (k != d && v[k - 1 + max_d] < v[k + 1 + max_d])) {
                x = v[k + 1 + max_d]; /* move down (insert) */
            } else {
                x = v[k - 1 + max_d] + 1; /* move right (delete) */
            }
            int y = x - k;

            /* Follow diagonal (equal lines) */
            while (x < n && y < m &&
                   lines_equal(a[x], a_lens[x], b[y], b_lens[y])) {
                x++;
                y++;
            }

            v[k + max_d] = x;

            if (x >= n && y >= m) {
                found_d = d;
                /* Save final V snapshot before goto */
                if (trace_count == trace_cap) {
                    int new_cap = trace_cap ? trace_cap * 2 : 16;
                    int **tmp = realloc(trace, (size_t)new_cap * sizeof(int *));
                    if (!tmp) goto fail;
                    trace = tmp;
                    trace_cap = new_cap;
                }
                int *snap = malloc(v_size * sizeof(int));
                if (!snap) goto fail;
                memcpy(snap, v, v_size * sizeof(int));
                trace[trace_count++] = snap;
                goto backtrace;
            }
        }

        /* Save V snapshot AFTER processing this depth */
        if (trace_count == trace_cap) {
            int new_cap = trace_cap ? trace_cap * 2 : 16;
            int **tmp = realloc(trace, (size_t)new_cap * sizeof(int *));
            if (!tmp) goto fail;
            trace = tmp;
            trace_cap = new_cap;
        }
        int *snap = malloc(v_size * sizeof(int));
        if (!snap) goto fail;
        memcpy(snap, v, v_size * sizeof(int));
        trace[trace_count++] = snap;
    }

backtrace:;
    if (found_d < 0) goto fail;

    /* Backtrace to find the edit path */
    /* We'll build the edits in reverse, then flip */
    mygit_diff_edit *rev = NULL;
    size_t rev_count = 0;
    size_t rev_cap = 0;

    /* trace[d] = V after processing depth d. trace[d-1] = V after depth d-1. */
    int x = n, y = m;
    for (int d = found_d; d > 0; d--) {
        int *prev_v = trace[d - 1];
        int k = x - y;
        int prev_k;
        int prev_x;

        if (k == -d || (k != d && prev_v[k - 1 + max_d] < prev_v[k + 1 + max_d])) {
            prev_k = k + 1;
        } else {
            prev_k = k - 1;
        }
        prev_x = prev_v[prev_k + max_d];
        int prev_y = prev_x - prev_k;

        /* Diagonal moves (equal lines) — from (prev_x, prev_y) to mid point */
        /* The non-diagonal move happens FIRST, then diagonal */
        /* mid point: the cell before diagonal started */
        int mid_x, mid_y;
        if (prev_k == k + 1) {
            /* Came from above: insert */
            mid_x = prev_x;
            mid_y = prev_y + 1;
        } else {
            /* Came from left: delete */
            mid_x = prev_x + 1;
            mid_y = prev_y;
        }

        /* Emit diagonal moves (equal) from (mid_x,mid_y) to (x,y) in REVERSE */
        while (x > mid_x && y > mid_y) {
            x--;
            y--;
            if (rev_count == rev_cap) {
                size_t nc = rev_cap ? rev_cap * 2 : 64;
                mygit_diff_edit *tmp = realloc(rev, nc * sizeof(*tmp));
                if (!tmp) { free(rev); goto fail; }
                rev = tmp;
                rev_cap = nc;
            }
            rev[rev_count].op = DIFF_EQUAL;
            rev[rev_count].line = a[x];
            rev[rev_count].line_len = a_lens[x];
            rev[rev_count].old_lineno = (size_t)(x + 1);
            rev[rev_count].new_lineno = (size_t)(y + 1);
            rev_count++;
        }

        /* Emit the non-diagonal move */
        if (rev_count == rev_cap) {
            size_t nc = rev_cap ? rev_cap * 2 : 64;
            mygit_diff_edit *tmp = realloc(rev, nc * sizeof(*tmp));
            if (!tmp) { free(rev); goto fail; }
            rev = tmp;
            rev_cap = nc;
        }
        if (prev_k == k + 1) {
            /* Insert */
            y--;
            rev[rev_count].op = DIFF_INSERT;
            rev[rev_count].line = b[y];
            rev[rev_count].line_len = b_lens[y];
            rev[rev_count].old_lineno = 0;
            rev[rev_count].new_lineno = (size_t)(y + 1);
            rev_count++;
        } else {
            /* Delete */
            x--;
            rev[rev_count].op = DIFF_DELETE;
            rev[rev_count].line = a[x];
            rev[rev_count].line_len = a_lens[x];
            rev[rev_count].old_lineno = (size_t)(x + 1);
            rev[rev_count].new_lineno = 0;
            rev_count++;
        }
    }

    /* Remaining diagonal at d=0 */
    while (x > 0 && y > 0) {
        x--;
        y--;
        if (rev_count == rev_cap) {
            size_t nc = rev_cap ? rev_cap * 2 : 64;
            mygit_diff_edit *tmp = realloc(rev, nc * sizeof(*tmp));
            if (!tmp) { free(rev); goto fail; }
            rev = tmp;
            rev_cap = nc;
        }
        rev[rev_count].op = DIFF_EQUAL;
        rev[rev_count].line = a[x];
        rev[rev_count].line_len = a_lens[x];
        rev[rev_count].old_lineno = (size_t)(x + 1);
        rev[rev_count].new_lineno = (size_t)(y + 1);
        rev_count++;
    }

    /* Reverse into result */
    for (size_t i = rev_count; i > 0; i--) {
        mygit_diff_edit *e = &rev[i - 1];
        if (result_push(result, e->op, e->line, e->line_len,
                        e->old_lineno, e->new_lineno) < 0) {
            free(rev);
            goto fail;
        }
    }

    free(rev);
    for (int i = 0; i < trace_count; i++) free(trace[i]);
    free(trace);
    free(v);
    return 0;

fail:
    for (int i = 0; i < trace_count; i++) free(trace[i]);
    free(trace);
    free(v);
    mygit_diff_free(result);
    return -1;
}

void mygit_diff_free(mygit_diff_result *result)
{
    free(result->edits);
    memset(result, 0, sizeof(*result));
}

/* --- Unified diff output ---------------------------------------------- */

/*
 * Find hunk boundaries: each hunk is a contiguous region of changes
 * with `context_lines` of equal lines around it. Adjacent hunks are merged.
 */
void mygit_diff_print_unified(const mygit_diff_result *result,
                              const char *old_name, const char *new_name,
                              int context_lines)
{
    if (!result || result->edit_count == 0) return;

    /* Check if there are any actual changes */
    int has_changes = 0;
    for (size_t i = 0; i < result->edit_count; i++) {
        if (result->edits[i].op != DIFF_EQUAL) {
            has_changes = 1;
            break;
        }
    }
    if (!has_changes) return;

    printf("--- %s\n", old_name ? old_name : "a");
    printf("+++ %s\n", new_name ? new_name : "b");

    size_t i = 0;
    while (i < result->edit_count) {
        /* Find next change */
        while (i < result->edit_count && result->edits[i].op == DIFF_EQUAL) {
            i++;
        }
        if (i >= result->edit_count) break;

        /* Start of hunk: back up by context_lines */
        size_t hunk_start = i;
        if ((int)hunk_start > context_lines) {
            hunk_start -= (size_t)context_lines;
        } else {
            hunk_start = 0;
        }

        /* Find end of hunk: scan forward, merging nearby changes */
        size_t hunk_end = i;
        while (hunk_end < result->edit_count) {
            /* Skip current change block */
            while (hunk_end < result->edit_count &&
                   result->edits[hunk_end].op != DIFF_EQUAL) {
                hunk_end++;
            }
            /* Count consecutive equal lines */
            size_t eq_start = hunk_end;
            while (hunk_end < result->edit_count &&
                   result->edits[hunk_end].op == DIFF_EQUAL) {
                hunk_end++;
            }
            /* If gap is small enough and there's another change, merge */
            size_t gap = hunk_end - eq_start;
            if (hunk_end < result->edit_count && (int)gap <= 2 * context_lines) {
                continue; /* merge with next change */
            }
            /* Otherwise, include trailing context and stop */
            hunk_end = eq_start;
            size_t trail = (size_t)context_lines;
            size_t avail = result->edit_count - eq_start;
            if (trail > avail) trail = avail;
            hunk_end = eq_start + trail;
            break;
        }

        /* Compute hunk header line numbers */
        size_t old_start = 0, old_count = 0;
        size_t new_start = 0, new_count = 0;
        for (size_t j = hunk_start; j < hunk_end; j++) {
            const mygit_diff_edit *e = &result->edits[j];
            if (e->op == DIFF_EQUAL || e->op == DIFF_DELETE) {
                if (old_count == 0) old_start = e->old_lineno;
                old_count++;
            }
            if (e->op == DIFF_EQUAL || e->op == DIFF_INSERT) {
                if (new_count == 0) new_start = e->new_lineno;
                new_count++;
            }
        }
        if (old_start == 0) old_start = 1;
        if (new_start == 0) new_start = 1;

        printf("@@ -%zu,%zu +%zu,%zu @@\n",
               old_start, old_count, new_start, new_count);

        for (size_t j = hunk_start; j < hunk_end; j++) {
            const mygit_diff_edit *e = &result->edits[j];
            char prefix = ' ';
            if (e->op == DIFF_INSERT) prefix = '+';
            else if (e->op == DIFF_DELETE) prefix = '-';
            printf("%c%.*s\n", prefix, (int)e->line_len, e->line);
        }

        i = hunk_end;
    }
}
