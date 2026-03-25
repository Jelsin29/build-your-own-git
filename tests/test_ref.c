#define _XOPEN_SOURCE 700 /* for mkdtemp, nftw */

#include "ref.h"
#include "repo.h"

#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---------- minunit-style test macros ---------- */

static int tests_run    = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define MU_ASSERT(test, message)                                        \
    do {                                                                \
        if (!(test)) {                                                  \
            fprintf(stderr, "  FAIL: %s (%s:%d)\n",                    \
                    message, __FILE__, __LINE__);                       \
            return 1;                                                   \
        }                                                               \
    } while (0)

#define MU_RUN_TEST(test_func)                                          \
    do {                                                                \
        fprintf(stderr, "  %-50s", #test_func);                        \
        tests_run++;                                                    \
        if (test_func() == 0) {                                        \
            tests_passed++;                                             \
            fprintf(stderr, "PASS\n");                                  \
        } else {                                                        \
            tests_failed++;                                             \
        }                                                               \
    } while (0)

/* ---------- helpers ---------- */

static int remove_cb(const char *fpath, const struct stat *sb,
                     int typeflag, struct FTW *ftwbuf)
{
    (void)sb;
    (void)typeflag;
    (void)ftwbuf;
    return remove(fpath);
}

static int rmrf(const char *path)
{
    return nftw(path, remove_cb, 64, FTW_DEPTH | FTW_PHYS);
}

static char *make_test_repo(char *tmpl)
{
    char *dir = mkdtemp(tmpl);
    if (dir == NULL) return NULL;
    if (mygit_init(dir) != 0) return NULL;
    return dir;
}

/*
 * Write content to a ref file under .mygit/<refname>.
 * Creates parent directories as needed.
 */
static int write_ref_file(const char *repo_path, const char *refname,
                          const char *content)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/.mygit/%s", repo_path, refname);

    /* Create parent directories if needed */
    char dir_path[512];
    snprintf(dir_path, sizeof(dir_path), "%s", path);
    for (char *p = dir_path + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(dir_path, 0755);
            *p = '/';
        }
    }

    FILE *fp = fopen(path, "w");
    if (fp == NULL) return -1;
    fputs(content, fp);
    fclose(fp);
    return 0;
}

/* ---------- tests ---------- */

static int test_ref_resolve_direct(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    const char *sha = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4";
    char content[64];
    snprintf(content, sizeof(content), "%s\n", sha);
    int wret = write_ref_file(dir, "refs/heads/master", content);
    MU_ASSERT(wret == 0, "write_ref_file failed");

    char hex_out[41];
    int ret = mygit_ref_resolve(dir, "refs/heads/master", hex_out);
    MU_ASSERT(ret == 0, "resolve should return 0 for direct ref");
    MU_ASSERT(strcmp(hex_out, sha) == 0, "resolved SHA should match");

    rmrf(dir);
    return 0;
}

static int test_ref_resolve_not_found(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    char hex_out[41];
    int ret = mygit_ref_resolve(dir, "refs/heads/nonexistent", hex_out);
    MU_ASSERT(ret == 1, "resolve should return 1 for missing ref");

    rmrf(dir);
    return 0;
}

static int test_ref_resolve_head_symbolic(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    /* HEAD already contains "ref: refs/heads/master\n" from mygit_init */
    const char *sha = "a94a8fe5ccb19ba61c4c0873d391e987982fbbd3";
    char content[64];
    snprintf(content, sizeof(content), "%s\n", sha);
    int wret = write_ref_file(dir, "refs/heads/master", content);
    MU_ASSERT(wret == 0, "write_ref_file failed");

    char hex_out[41];
    int ret = mygit_ref_resolve(dir, "HEAD", hex_out);
    MU_ASSERT(ret == 0, "resolve HEAD should return 0");
    MU_ASSERT(strcmp(hex_out, sha) == 0, "HEAD should resolve to master SHA");

    rmrf(dir);
    return 0;
}

static int test_ref_resolve_invalid_content(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    int wret = write_ref_file(dir, "refs/heads/broken", "not-a-valid-sha\n");
    MU_ASSERT(wret == 0, "write_ref_file failed");

    char hex_out[41];
    int ret = mygit_ref_resolve(dir, "refs/heads/broken", hex_out);
    MU_ASSERT(ret == -1, "resolve should return -1 for invalid content");

    rmrf(dir);
    return 0;
}

/* ---------- main ---------- */

int main(void)
{
    fprintf(stderr, "\n=== mygit_ref tests ===\n\n");

    MU_RUN_TEST(test_ref_resolve_direct);
    MU_RUN_TEST(test_ref_resolve_not_found);
    MU_RUN_TEST(test_ref_resolve_head_symbolic);
    MU_RUN_TEST(test_ref_resolve_invalid_content);

    fprintf(stderr, "\n--- Results: %d/%d passed, %d failed ---\n\n",
            tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
