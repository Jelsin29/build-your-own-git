#define _XOPEN_SOURCE 700 /* for mkdtemp, nftw */

#include "ref.h"
#include "repo.h"

#include <errno.h>
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

static int test_ref_resolve_chain_depth_2(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    /* HEAD -> refs/heads/alias -> refs/heads/master -> SHA (2 hops) */
    int wret = write_ref_file(dir, "HEAD", "ref: refs/heads/alias\n");
    MU_ASSERT(wret == 0, "write HEAD failed");

    wret = write_ref_file(dir, "refs/heads/alias",
                          "ref: refs/heads/master\n");
    MU_ASSERT(wret == 0, "write alias failed");

    const char *sha = "abcdef0123456789abcdef0123456789abcdef01";
    char content[64];
    snprintf(content, sizeof(content), "%s\n", sha);
    wret = write_ref_file(dir, "refs/heads/master", content);
    MU_ASSERT(wret == 0, "write master failed");

    char hex_out[41];
    int ret = mygit_ref_resolve(dir, "HEAD", hex_out);
    MU_ASSERT(ret == 0, "resolve should succeed for 2-hop chain");
    MU_ASSERT(strcmp(hex_out, sha) == 0, "SHA should match through chain");

    rmrf(dir);
    return 0;
}

static int test_ref_resolve_chain_at_max_depth(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    /*
     * Build a chain of exactly MYGIT_MAX_REF_DEPTH (8) refs:
     * ref0 -> ref1 -> ref2 -> ... -> ref6 -> ref7 (SHA)
     * That's 7 symbolic hops + 1 direct = 8 iterations total.
     */
    const char *sha = "1111111111222222222233333333334444444444";

    for (int i = 0; i < MYGIT_MAX_REF_DEPTH - 1; i++) {
        char refname[64];
        char target[128];
        snprintf(refname, sizeof(refname), "refs/chain/link%d", i);
        snprintf(target, sizeof(target), "ref: refs/chain/link%d\n", i + 1);
        int wret = write_ref_file(dir, refname, target);
        MU_ASSERT(wret == 0, "write chain link failed");
    }

    /* Last link is a direct SHA */
    char last_ref[64];
    snprintf(last_ref, sizeof(last_ref), "refs/chain/link%d",
             MYGIT_MAX_REF_DEPTH - 1);
    char sha_content[64];
    snprintf(sha_content, sizeof(sha_content), "%s\n", sha);
    int wret = write_ref_file(dir, last_ref, sha_content);
    MU_ASSERT(wret == 0, "write last link failed");

    char hex_out[41];
    int ret = mygit_ref_resolve(dir, "refs/chain/link0", hex_out);
    MU_ASSERT(ret == 0, "resolve should succeed at max depth");
    MU_ASSERT(strcmp(hex_out, sha) == 0, "SHA should match at max depth");

    rmrf(dir);
    return 0;
}

static int test_ref_resolve_circular(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    /* Two refs pointing to each other: A -> B -> A -> B -> ... */
    int wret = write_ref_file(dir, "refs/heads/loopA",
                              "ref: refs/heads/loopB\n");
    MU_ASSERT(wret == 0, "write loopA failed");

    wret = write_ref_file(dir, "refs/heads/loopB",
                          "ref: refs/heads/loopA\n");
    MU_ASSERT(wret == 0, "write loopB failed");

    char hex_out[41];
    errno = 0;
    int ret = mygit_ref_resolve(dir, "refs/heads/loopA", hex_out);
    MU_ASSERT(ret == -1, "circular ref should return -1");
    MU_ASSERT(errno == ELOOP, "circular ref should set errno to ELOOP");

    rmrf(dir);
    return 0;
}

static int test_ref_resolve_exceeds_max_depth(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    /*
     * Build a chain of MYGIT_MAX_REF_DEPTH symbolic refs + 1 direct.
     * That's 9 refs total — the loop runs 8 times and never reaches
     * the direct SHA at link8.
     */
    const char *sha = "aaaaaaaaaa0000000000bbbbbbbbbb1111111111";

    for (int i = 0; i <= MYGIT_MAX_REF_DEPTH; i++) {
        char refname[64];
        snprintf(refname, sizeof(refname), "refs/deep/link%d", i);

        if (i < MYGIT_MAX_REF_DEPTH) {
            char target[128];
            snprintf(target, sizeof(target),
                     "ref: refs/deep/link%d\n", i + 1);
            int wret = write_ref_file(dir, refname, target);
            MU_ASSERT(wret == 0, "write deep link failed");
        } else {
            char sha_content[64];
            snprintf(sha_content, sizeof(sha_content), "%s\n", sha);
            int wret = write_ref_file(dir, refname, sha_content);
            MU_ASSERT(wret == 0, "write deep last link failed");
        }
    }

    char hex_out[41];
    errno = 0;
    int ret = mygit_ref_resolve(dir, "refs/deep/link0", hex_out);
    MU_ASSERT(ret == -1, "exceeding max depth should return -1");
    MU_ASSERT(errno == ELOOP, "exceeding max depth should set errno ELOOP");

    rmrf(dir);
    return 0;
}

static int test_ref_resolve_dangling_symbolic(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    /* HEAD points to a branch that doesn't exist */
    int wret = write_ref_file(dir, "HEAD",
                              "ref: refs/heads/nonexistent\n");
    MU_ASSERT(wret == 0, "write HEAD failed");

    char hex_out[41];
    int ret = mygit_ref_resolve(dir, "HEAD", hex_out);
    MU_ASSERT(ret == 1, "dangling symbolic ref should return 1 (not found)");

    rmrf(dir);
    return 0;
}

static int test_ref_resolve_empty_file(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    /* Create a ref file that exists but is completely empty */
    int wret = write_ref_file(dir, "refs/heads/empty", "");
    MU_ASSERT(wret == 0, "write empty ref file failed");

    char hex_out[41];
    int ret = mygit_ref_resolve(dir, "refs/heads/empty", hex_out);
    MU_ASSERT(ret == -1, "empty ref file should return -1");

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
    MU_RUN_TEST(test_ref_resolve_chain_depth_2);
    MU_RUN_TEST(test_ref_resolve_chain_at_max_depth);
    MU_RUN_TEST(test_ref_resolve_circular);
    MU_RUN_TEST(test_ref_resolve_exceeds_max_depth);
    MU_RUN_TEST(test_ref_resolve_dangling_symbolic);
    MU_RUN_TEST(test_ref_resolve_empty_file);

    fprintf(stderr, "\n--- Results: %d/%d passed, %d failed ---\n\n",
            tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
