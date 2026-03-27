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

/* ---------- Phase 3: ref update + validation tests ---------- */

static int test_ref_update_creates_file(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    const char *sha = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4";
    int ret = mygit_ref_update(dir, "refs/heads/master", sha);
    MU_ASSERT(ret == 0, "ref_update should succeed");

    /* Verify file contents */
    char path[512];
    snprintf(path, sizeof(path), "%s/.mygit/refs/heads/master", dir);
    FILE *fp = fopen(path, "r");
    MU_ASSERT(fp != NULL, "ref file should exist");
    char content[64];
    char *got = fgets(content, sizeof(content), fp);
    fclose(fp);
    MU_ASSERT(got != NULL, "should read content");

    /* Strip newline for comparison */
    size_t clen = strlen(content);
    if (clen > 0 && content[clen - 1] == '\n') content[clen - 1] = '\0';
    MU_ASSERT(strcmp(content, sha) == 0, "file should contain the SHA");

    rmrf(dir);
    return 0;
}

static int test_ref_update_and_resolve_roundtrip(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    const char *sha = "a94a8fe5ccb19ba61c4c0873d391e987982fbbd3";
    int ret = mygit_ref_update(dir, "refs/heads/main", sha);
    MU_ASSERT(ret == 0, "update should succeed");

    char hex_out[41];
    ret = mygit_ref_resolve(dir, "refs/heads/main", hex_out);
    MU_ASSERT(ret == 0, "resolve should succeed after update");
    MU_ASSERT(strcmp(hex_out, sha) == 0, "roundtrip SHA should match");

    rmrf(dir);
    return 0;
}

static int test_ref_update_creates_parent_dirs(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    const char *sha = "1111111111222222222233333333334444444444";
    int ret = mygit_ref_update(dir, "refs/heads/feature/cool", sha);
    MU_ASSERT(ret == 0, "update with nested dirs should succeed");

    char hex_out[41];
    ret = mygit_ref_resolve(dir, "refs/heads/feature/cool", hex_out);
    MU_ASSERT(ret == 0, "resolve nested ref should succeed");
    MU_ASSERT(strcmp(hex_out, sha) == 0, "nested ref SHA should match");

    rmrf(dir);
    return 0;
}

static int test_ref_update_rejects_dotdot(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    const char *sha = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4";
    int ret = mygit_ref_update(dir, "refs/../etc/passwd", sha);
    MU_ASSERT(ret == -1, "dotdot in refname should be rejected");
    MU_ASSERT(errno == EINVAL, "errno should be EINVAL");

    rmrf(dir);
    return 0;
}

static int test_ref_update_rejects_absolute(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    const char *sha = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4";
    int ret = mygit_ref_update(dir, "/refs/heads/master", sha);
    MU_ASSERT(ret == -1, "absolute refname should be rejected");
    MU_ASSERT(errno == EINVAL, "errno should be EINVAL");

    rmrf(dir);
    return 0;
}

static int test_ref_update_rejects_empty(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    const char *sha = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4";
    int ret = mygit_ref_update(dir, "", sha);
    MU_ASSERT(ret == -1, "empty refname should be rejected");

    rmrf(dir);
    return 0;
}

static int test_ref_update_rejects_lock_suffix(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    const char *sha = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4";
    int ret = mygit_ref_update(dir, "refs/heads/master.lock", sha);
    MU_ASSERT(ret == -1, ".lock suffix should be rejected");
    MU_ASSERT(errno == EINVAL, "errno should be EINVAL");

    rmrf(dir);
    return 0;
}

static int test_ref_update_rejects_double_slash(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    const char *sha = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4";
    int ret = mygit_ref_update(dir, "refs//heads/master", sha);
    MU_ASSERT(ret == -1, "double slash should be rejected");
    MU_ASSERT(errno == EINVAL, "errno should be EINVAL");

    rmrf(dir);
    return 0;
}

static int test_ref_update_rejects_invalid_hex(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    /* Use a 40-char string that contains non-hex chars */
    char bad_hex[41] = "gggggggggggggggggggggggggggggggggggggggg";
    int ret = mygit_ref_update(dir, "refs/heads/master", bad_hex);
    MU_ASSERT(ret == -1, "invalid hex should be rejected");
    MU_ASSERT(errno == EINVAL, "errno should be EINVAL");

    rmrf(dir);
    return 0;
}

/* ---------- Phase 4: ref listing + HEAD tests ---------- */

struct ref_collect {
    int count;
    char names[16][MYGIT_MAX_REFNAME];
};

static int collect_cb(const char *refname, const char hex[41], void *user_data)
{
    (void)hex;
    struct ref_collect *rc = (struct ref_collect *)user_data;
    if (rc->count < 16) {
        strncpy(rc->names[rc->count], refname, MYGIT_MAX_REFNAME - 1);
        rc->names[rc->count][MYGIT_MAX_REFNAME - 1] = '\0';
        rc->count++;
    }
    return 0;
}

static int test_ref_list_multiple(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    const char *sha = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4";
    mygit_ref_update(dir, "refs/heads/master", sha);
    mygit_ref_update(dir, "refs/heads/develop", sha);

    struct ref_collect rc = {0};
    int ret = mygit_ref_list(dir, "refs/heads", collect_cb, &rc);
    MU_ASSERT(ret == 0, "ref_list should succeed");
    MU_ASSERT(rc.count == 2, "should find 2 refs");

    rmrf(dir);
    return 0;
}

static int test_ref_list_nested(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    const char *sha = "e3b0c44298fc1c149afbf4c8996fb92427ae41e4";
    mygit_ref_update(dir, "refs/heads/master", sha);
    mygit_ref_update(dir, "refs/heads/feature/login", sha);

    struct ref_collect rc = {0};
    int ret = mygit_ref_list(dir, "refs/heads", collect_cb, &rc);
    MU_ASSERT(ret == 0, "ref_list should succeed");
    MU_ASSERT(rc.count == 2, "should find both flat and nested refs");

    rmrf(dir);
    return 0;
}

static int test_ref_list_empty_dir(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    struct ref_collect rc = {0};
    int ret = mygit_ref_list(dir, "refs/tags", collect_cb, &rc);
    MU_ASSERT(ret == 0, "ref_list on empty dir should succeed");
    MU_ASSERT(rc.count == 0, "should find 0 refs in empty dir");

    rmrf(dir);
    return 0;
}

static int test_head_resolve_symbolic(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    const char *sha = "a94a8fe5ccb19ba61c4c0873d391e987982fbbd3";
    mygit_ref_update(dir, "refs/heads/master", sha);

    char hex_out[41];
    int ret = mygit_head_resolve(dir, hex_out);
    MU_ASSERT(ret == 0, "head_resolve should succeed");
    MU_ASSERT(strcmp(hex_out, sha) == 0, "HEAD should resolve to master SHA");

    rmrf(dir);
    return 0;
}

static int test_head_resolve_empty_repo(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    /* HEAD -> refs/heads/master but master doesn't exist */
    char hex_out[41];
    int ret = mygit_head_resolve(dir, hex_out);
    MU_ASSERT(ret == 1, "head_resolve on empty repo should return 1");

    rmrf(dir);
    return 0;
}

static int test_head_resolve_detached(void)
{
    char tmpl[] = "/tmp/mygit_ref_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    /* Write bare SHA into HEAD (detached state) */
    const char *sha = "abcdef0123456789abcdef0123456789abcdef01";
    char content[64];
    snprintf(content, sizeof(content), "%s\n", sha);
    write_ref_file(dir, "HEAD", content);

    char hex_out[41];
    int ret = mygit_head_resolve(dir, hex_out);
    MU_ASSERT(ret == 0, "detached HEAD should resolve");
    MU_ASSERT(strcmp(hex_out, sha) == 0, "detached HEAD SHA should match");

    rmrf(dir);
    return 0;
}

/* ---------- main ---------- */

int main(void)
{
    fprintf(stderr, "\n=== mygit_ref tests ===\n\n");

    /* Phase 1-2: resolve tests */
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

    /* Phase 3: update + validation tests */
    MU_RUN_TEST(test_ref_update_creates_file);
    MU_RUN_TEST(test_ref_update_and_resolve_roundtrip);
    MU_RUN_TEST(test_ref_update_creates_parent_dirs);
    MU_RUN_TEST(test_ref_update_rejects_dotdot);
    MU_RUN_TEST(test_ref_update_rejects_absolute);
    MU_RUN_TEST(test_ref_update_rejects_empty);
    MU_RUN_TEST(test_ref_update_rejects_lock_suffix);
    MU_RUN_TEST(test_ref_update_rejects_double_slash);
    MU_RUN_TEST(test_ref_update_rejects_invalid_hex);

    /* Phase 4: listing + HEAD tests */
    MU_RUN_TEST(test_ref_list_multiple);
    MU_RUN_TEST(test_ref_list_nested);
    MU_RUN_TEST(test_ref_list_empty_dir);
    MU_RUN_TEST(test_head_resolve_symbolic);
    MU_RUN_TEST(test_head_resolve_empty_repo);
    MU_RUN_TEST(test_head_resolve_detached);

    fprintf(stderr, "\n--- Results: %d/%d passed, %d failed ---\n\n",
            tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
