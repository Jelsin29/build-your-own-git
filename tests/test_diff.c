#include "../include/diff.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg)                                                 \
    do {                                                                   \
        tests_run++;                                                       \
        if (!(cond)) {                                                     \
            fprintf(stderr, "  FAIL: %s (line %d)\n", msg, __LINE__);      \
            return;                                                        \
        }                                                                  \
        tests_passed++;                                                    \
    } while (0)

#define RUN(fn)                                                            \
    do {                                                                   \
        printf("  %-50s", #fn);                                            \
        fn();                                                              \
        printf("PASS\n");                                                  \
    } while (0)

/* --- split_lines tests ------------------------------------------------ */

static void test_split_empty(void)
{
    const char **lines = NULL;
    size_t *lens = NULL;
    size_t count = mygit_diff_split_lines("", 0, &lines, &lens);
    ASSERT(count == 0, "empty text has 0 lines");
    ASSERT(lines == NULL, "lines is NULL for empty");
    ASSERT(lens == NULL, "lens is NULL for empty");
}

static void test_split_single_line(void)
{
    const char *text = "hello\n";
    const char **lines = NULL;
    size_t *lens = NULL;
    size_t count = mygit_diff_split_lines(text, strlen(text), &lines, &lens);
    ASSERT(count == 1, "one line");
    ASSERT(lens[0] == 5, "line length is 5");
    ASSERT(memcmp(lines[0], "hello", 5) == 0, "line content");
    free(lines);
    free(lens);
}

static void test_split_no_trailing_newline(void)
{
    const char *text = "hello";
    const char **lines = NULL;
    size_t *lens = NULL;
    size_t count = mygit_diff_split_lines(text, strlen(text), &lines, &lens);
    ASSERT(count == 1, "one line without newline");
    ASSERT(lens[0] == 5, "line length");
    free(lines);
    free(lens);
}

static void test_split_multiple_lines(void)
{
    const char *text = "aaa\nbbb\nccc\n";
    const char **lines = NULL;
    size_t *lens = NULL;
    size_t count = mygit_diff_split_lines(text, strlen(text), &lines, &lens);
    ASSERT(count == 3, "three lines");
    ASSERT(lens[0] == 3 && memcmp(lines[0], "aaa", 3) == 0, "line 1");
    ASSERT(lens[1] == 3 && memcmp(lines[1], "bbb", 3) == 0, "line 2");
    ASSERT(lens[2] == 3 && memcmp(lines[2], "ccc", 3) == 0, "line 3");
    free(lines);
    free(lens);
}

/* --- Myers diff tests ------------------------------------------------- */

static void test_diff_identical(void)
{
    const char *la[] = {"aaa", "bbb", "ccc"};
    size_t ll[] = {3, 3, 3};
    mygit_diff_result r;
    int rc = mygit_diff_myers(la, ll, 3, la, ll, 3, &r);
    ASSERT(rc == 0, "diff succeeds");
    ASSERT(r.edit_count == 3, "3 edits");
    for (size_t i = 0; i < r.edit_count; i++) {
        ASSERT(r.edits[i].op == DIFF_EQUAL, "all equal");
    }
    mygit_diff_free(&r);
}

static void test_diff_all_insert(void)
{
    const char *b[] = {"aaa", "bbb"};
    size_t bl[] = {3, 3};
    mygit_diff_result r;
    int rc = mygit_diff_myers(NULL, NULL, 0, b, bl, 2, &r);
    ASSERT(rc == 0, "diff succeeds");
    ASSERT(r.edit_count == 2, "2 inserts");
    ASSERT(r.edits[0].op == DIFF_INSERT, "first is insert");
    ASSERT(r.edits[1].op == DIFF_INSERT, "second is insert");
    mygit_diff_free(&r);
}

static void test_diff_all_delete(void)
{
    const char *a[] = {"aaa", "bbb"};
    size_t al[] = {3, 3};
    mygit_diff_result r;
    int rc = mygit_diff_myers(a, al, 2, NULL, NULL, 0, &r);
    ASSERT(rc == 0, "diff succeeds");
    ASSERT(r.edit_count == 2, "2 deletes");
    ASSERT(r.edits[0].op == DIFF_DELETE, "first is delete");
    ASSERT(r.edits[1].op == DIFF_DELETE, "second is delete");
    mygit_diff_free(&r);
}

static void test_diff_empty_both(void)
{
    mygit_diff_result r;
    int rc = mygit_diff_myers(NULL, NULL, 0, NULL, NULL, 0, &r);
    ASSERT(rc == 0, "diff succeeds");
    ASSERT(r.edit_count == 0, "no edits");
    mygit_diff_free(&r);
}

static void test_diff_single_insert(void)
{
    const char *a[] = {"aaa", "ccc"};
    size_t al[] = {3, 3};
    const char *b[] = {"aaa", "bbb", "ccc"};
    size_t bl[] = {3, 3, 3};
    mygit_diff_result r;
    int rc = mygit_diff_myers(a, al, 2, b, bl, 3, &r);
    ASSERT(rc == 0, "diff succeeds");

    /* Should be: EQUAL(aaa), INSERT(bbb), EQUAL(ccc) */
    int found_insert = 0;
    int equal_count = 0;
    for (size_t i = 0; i < r.edit_count; i++) {
        if (r.edits[i].op == DIFF_INSERT) {
            found_insert = 1;
            ASSERT(r.edits[i].line_len == 3, "insert line len");
            ASSERT(memcmp(r.edits[i].line, "bbb", 3) == 0, "insert content");
        }
        if (r.edits[i].op == DIFF_EQUAL) equal_count++;
    }
    ASSERT(found_insert, "found the insert");
    ASSERT(equal_count == 2, "two equal lines");
    mygit_diff_free(&r);
}

static void test_diff_single_delete(void)
{
    const char *a[] = {"aaa", "bbb", "ccc"};
    size_t al[] = {3, 3, 3};
    const char *b[] = {"aaa", "ccc"};
    size_t bl[] = {3, 3};
    mygit_diff_result r;
    int rc = mygit_diff_myers(a, al, 3, b, bl, 2, &r);
    ASSERT(rc == 0, "diff succeeds");

    int found_delete = 0;
    int equal_count = 0;
    for (size_t i = 0; i < r.edit_count; i++) {
        if (r.edits[i].op == DIFF_DELETE) {
            found_delete = 1;
            ASSERT(memcmp(r.edits[i].line, "bbb", 3) == 0, "delete content");
        }
        if (r.edits[i].op == DIFF_EQUAL) equal_count++;
    }
    ASSERT(found_delete, "found the delete");
    ASSERT(equal_count == 2, "two equal lines");
    mygit_diff_free(&r);
}

static void test_diff_replace(void)
{
    const char *a[] = {"aaa", "bbb", "ccc"};
    size_t al[] = {3, 3, 3};
    const char *b[] = {"aaa", "xxx", "ccc"};
    size_t bl[] = {3, 3, 3};
    mygit_diff_result r;
    int rc = mygit_diff_myers(a, al, 3, b, bl, 3, &r);
    ASSERT(rc == 0, "diff succeeds");

    /* Should have: EQUAL(aaa), DELETE(bbb), INSERT(xxx), EQUAL(ccc) */
    int del = 0, ins = 0, eq = 0;
    for (size_t i = 0; i < r.edit_count; i++) {
        if (r.edits[i].op == DIFF_DELETE) del++;
        if (r.edits[i].op == DIFF_INSERT) ins++;
        if (r.edits[i].op == DIFF_EQUAL) eq++;
    }
    ASSERT(del == 1, "one delete");
    ASSERT(ins == 1, "one insert");
    ASSERT(eq == 2, "two equal");
    mygit_diff_free(&r);
}

static void test_diff_line_numbers(void)
{
    const char *a[] = {"aaa", "bbb"};
    size_t al[] = {3, 3};
    const char *b[] = {"aaa", "ccc", "bbb"};
    size_t bl[] = {3, 3, 3};
    mygit_diff_result r;
    int rc = mygit_diff_myers(a, al, 2, b, bl, 3, &r);
    ASSERT(rc == 0, "diff succeeds");

    /* Check that line numbers are 1-based and correct */
    for (size_t i = 0; i < r.edit_count; i++) {
        if (r.edits[i].op == DIFF_EQUAL) {
            ASSERT(r.edits[i].old_lineno > 0, "old lineno > 0");
            ASSERT(r.edits[i].new_lineno > 0, "new lineno > 0");
        } else if (r.edits[i].op == DIFF_INSERT) {
            ASSERT(r.edits[i].old_lineno == 0, "insert: old lineno = 0");
            ASSERT(r.edits[i].new_lineno > 0, "insert: new lineno > 0");
        } else if (r.edits[i].op == DIFF_DELETE) {
            ASSERT(r.edits[i].old_lineno > 0, "delete: old lineno > 0");
            ASSERT(r.edits[i].new_lineno == 0, "delete: new lineno = 0");
        }
    }
    mygit_diff_free(&r);
}

static void test_diff_completely_different(void)
{
    const char *a[] = {"aaa", "bbb"};
    size_t al[] = {3, 3};
    const char *b[] = {"xxx", "yyy"};
    size_t bl[] = {3, 3};
    mygit_diff_result r;
    int rc = mygit_diff_myers(a, al, 2, b, bl, 2, &r);
    ASSERT(rc == 0, "diff succeeds");

    int del = 0, ins = 0;
    for (size_t i = 0; i < r.edit_count; i++) {
        if (r.edits[i].op == DIFF_DELETE) del++;
        if (r.edits[i].op == DIFF_INSERT) ins++;
    }
    ASSERT(del == 2, "two deletes");
    ASSERT(ins == 2, "two inserts");
    mygit_diff_free(&r);
}

static void test_diff_large_common_prefix(void)
{
    const char *a[] = {"1", "2", "3", "4", "5", "old"};
    size_t al[] = {1, 1, 1, 1, 1, 3};
    const char *b[] = {"1", "2", "3", "4", "5", "new"};
    size_t bl[] = {1, 1, 1, 1, 1, 3};
    mygit_diff_result r;
    int rc = mygit_diff_myers(a, al, 6, b, bl, 6, &r);
    ASSERT(rc == 0, "diff succeeds");

    int eq = 0;
    for (size_t i = 0; i < r.edit_count; i++) {
        if (r.edits[i].op == DIFF_EQUAL) eq++;
    }
    ASSERT(eq == 5, "five equal lines (common prefix)");
    mygit_diff_free(&r);
}

/* --- Unified output test (just verify it doesn't crash) --------------- */

static void test_unified_output_no_crash(void)
{
    const char *a[] = {"aaa", "bbb", "ccc"};
    size_t al[] = {3, 3, 3};
    const char *b[] = {"aaa", "xxx", "ccc"};
    size_t bl[] = {3, 3, 3};
    mygit_diff_result r;
    int rc = mygit_diff_myers(a, al, 3, b, bl, 3, &r);
    ASSERT(rc == 0, "diff succeeds");

    /* Redirect stdout to /dev/null to avoid test noise */
    FILE *old_stdout = stdout;
    stdout = fopen("/dev/null", "w");
    mygit_diff_print_unified(&r, "a/file.txt", "b/file.txt", 3);
    fclose(stdout);
    stdout = old_stdout;

    ASSERT(1, "unified output didn't crash");
    mygit_diff_free(&r);
}

static void test_unified_no_changes(void)
{
    const char *a[] = {"aaa", "bbb"};
    size_t al[] = {3, 3};
    mygit_diff_result r;
    int rc = mygit_diff_myers(a, al, 2, a, al, 2, &r);
    ASSERT(rc == 0, "diff succeeds");

    /* Should not print anything for identical files */
    FILE *old_stdout = stdout;
    stdout = fopen("/dev/null", "w");
    mygit_diff_print_unified(&r, "a", "b", 3);
    fclose(stdout);
    stdout = old_stdout;

    ASSERT(1, "no crash on identical");
    mygit_diff_free(&r);
}

int main(void)
{
    printf("\n=== Split lines tests ===\n\n");
    RUN(test_split_empty);
    RUN(test_split_single_line);
    RUN(test_split_no_trailing_newline);
    RUN(test_split_multiple_lines);

    printf("\n=== Myers diff tests ===\n\n");
    RUN(test_diff_identical);
    RUN(test_diff_all_insert);
    RUN(test_diff_all_delete);
    RUN(test_diff_empty_both);
    RUN(test_diff_single_insert);
    RUN(test_diff_single_delete);
    RUN(test_diff_replace);
    RUN(test_diff_line_numbers);
    RUN(test_diff_completely_different);
    RUN(test_diff_large_common_prefix);

    printf("\n=== Unified output tests ===\n\n");
    RUN(test_unified_output_no_crash);
    RUN(test_unified_no_changes);

    printf("\n--- Results: %d/%d passed, %d failed ---\n\n",
           tests_passed, tests_run, tests_run - tests_passed);

    return (tests_passed == tests_run) ? EXIT_SUCCESS : EXIT_FAILURE;
}
