#define _XOPEN_SOURCE 700 /* for mkdtemp, nftw */

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

static int dir_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
}

static int file_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

static int file_has_content(const char *path, const char *expected)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) return 0;

    char buf[256];
    size_t nread = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);

    buf[nread] = '\0';
    return strcmp(buf, expected) == 0;
}

static char *make_tmpdir(void)
{
    static char template[] = "/tmp/mygit_test_XXXXXX";
    char *tmpdir = mkdtemp(template);
    return tmpdir;
}

/* ---------- tests ---------- */

static int test_init_creates_mygit_dir(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = mkdtemp(tmpl);
    MU_ASSERT(dir != NULL, "mkdtemp failed");

    int result = mygit_init(dir);
    char path[512];
    snprintf(path, sizeof(path), "%s/.mygit", dir);

    MU_ASSERT(result == 0, "mygit_init should return 0");
    MU_ASSERT(dir_exists(path), ".mygit directory should exist");

    rmrf(dir);
    return 0;
}

static int test_init_creates_objects_dir(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = mkdtemp(tmpl);
    MU_ASSERT(dir != NULL, "mkdtemp failed");

    mygit_init(dir);
    char path[512];
    snprintf(path, sizeof(path), "%s/.mygit/objects", dir);

    MU_ASSERT(dir_exists(path), ".mygit/objects should exist");

    rmrf(dir);
    return 0;
}

static int test_init_creates_objects_info_dir(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = mkdtemp(tmpl);
    MU_ASSERT(dir != NULL, "mkdtemp failed");

    mygit_init(dir);
    char path[512];
    snprintf(path, sizeof(path), "%s/.mygit/objects/info", dir);

    MU_ASSERT(dir_exists(path), ".mygit/objects/info should exist");

    rmrf(dir);
    return 0;
}

static int test_init_creates_objects_pack_dir(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = mkdtemp(tmpl);
    MU_ASSERT(dir != NULL, "mkdtemp failed");

    mygit_init(dir);
    char path[512];
    snprintf(path, sizeof(path), "%s/.mygit/objects/pack", dir);

    MU_ASSERT(dir_exists(path), ".mygit/objects/pack should exist");

    rmrf(dir);
    return 0;
}

static int test_init_creates_refs_heads_dir(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = mkdtemp(tmpl);
    MU_ASSERT(dir != NULL, "mkdtemp failed");

    mygit_init(dir);
    char path[512];
    snprintf(path, sizeof(path), "%s/.mygit/refs/heads", dir);

    MU_ASSERT(dir_exists(path), ".mygit/refs/heads should exist");

    rmrf(dir);
    return 0;
}

static int test_init_creates_refs_tags_dir(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = mkdtemp(tmpl);
    MU_ASSERT(dir != NULL, "mkdtemp failed");

    mygit_init(dir);
    char path[512];
    snprintf(path, sizeof(path), "%s/.mygit/refs/tags", dir);

    MU_ASSERT(dir_exists(path), ".mygit/refs/tags should exist");

    rmrf(dir);
    return 0;
}

static int test_init_creates_HEAD_file(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = mkdtemp(tmpl);
    MU_ASSERT(dir != NULL, "mkdtemp failed");

    mygit_init(dir);
    char path[512];
    snprintf(path, sizeof(path), "%s/.mygit/HEAD", dir);

    MU_ASSERT(file_exists(path), ".mygit/HEAD should exist");
    MU_ASSERT(file_has_content(path, "ref: refs/heads/master\n"),
              "HEAD should contain 'ref: refs/heads/master\\n'");

    rmrf(dir);
    return 0;
}

static int test_init_returns_1_if_already_exists(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = mkdtemp(tmpl);
    MU_ASSERT(dir != NULL, "mkdtemp failed");

    int first  = mygit_init(dir);
    int second = mygit_init(dir);

    MU_ASSERT(first == 0,  "first init should return 0");
    MU_ASSERT(second == 1, "second init should return 1");

    rmrf(dir);
    return 0;
}

static int test_init_returns_neg1_on_bad_path(void)
{
    int result = mygit_init("/nonexistent/bad/path");
    MU_ASSERT(result == -1, "init on bad path should return -1");
    return 0;
}

/* ---------- main ---------- */

int main(void)
{
    (void)make_tmpdir; /* suppress unused warning during red phase */

    fprintf(stderr, "\n=== mygit_init tests ===\n\n");

    MU_RUN_TEST(test_init_creates_mygit_dir);
    MU_RUN_TEST(test_init_creates_objects_dir);
    MU_RUN_TEST(test_init_creates_objects_info_dir);
    MU_RUN_TEST(test_init_creates_objects_pack_dir);
    MU_RUN_TEST(test_init_creates_refs_heads_dir);
    MU_RUN_TEST(test_init_creates_refs_tags_dir);
    MU_RUN_TEST(test_init_creates_HEAD_file);
    MU_RUN_TEST(test_init_returns_1_if_already_exists);
    MU_RUN_TEST(test_init_returns_neg1_on_bad_path);

    fprintf(stderr, "\n--- Results: %d/%d passed, %d failed ---\n\n",
            tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
