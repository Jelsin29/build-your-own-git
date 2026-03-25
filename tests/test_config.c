#define _XOPEN_SOURCE 700 /* for mkdtemp, nftw */

#include "config.h"
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

/*
 * Create a temp directory with .mygit/config containing the given text.
 * Writes the temp dir path into dir_out (must be at least 64 bytes).
 * Returns 0 on success, -1 on failure.
 */
static int setup_config(char *dir_out, const char *config_content)
{
    char tmpl[] = "/tmp/mygit_cfg_XXXXXX";
    char *dir = mkdtemp(tmpl);
    if (dir == NULL)
        return -1;

    char mygit_dir[256];
    snprintf(mygit_dir, sizeof(mygit_dir), "%s/.mygit", dir);
    if (mkdir(mygit_dir, 0755) != 0)
        return -1;

    char config_path[256];
    snprintf(config_path, sizeof(config_path), "%s/.mygit/config", dir);

    FILE *fp = fopen(config_path, "w");
    if (fp == NULL)
        return -1;

    fputs(config_content, fp);
    fclose(fp);

    strcpy(dir_out, dir);
    return 0;
}

/* ---------- tests ---------- */

static int test_config_get_basic(void)
{
    char dir[256];
    int rc = setup_config(dir, "[user]\n\tname = John\n");
    MU_ASSERT(rc == 0, "setup_config failed");

    char value[MYGIT_MAX_VALUE];
    int result = mygit_config_get(dir, "user", "name", value, sizeof(value));

    MU_ASSERT(result == 0, "config_get should return 0 (found)");
    MU_ASSERT(strcmp(value, "John") == 0, "value should be 'John'");

    rmrf(dir);
    return 0;
}

static int test_config_get_with_spaces(void)
{
    char dir[256];
    int rc = setup_config(dir, "[core]\n\teditor = vim --noplugin\n");
    MU_ASSERT(rc == 0, "setup_config failed");

    char value[MYGIT_MAX_VALUE];
    int result = mygit_config_get(dir, "core", "editor",
                                  value, sizeof(value));

    MU_ASSERT(result == 0, "config_get should return 0 (found)");
    MU_ASSERT(strcmp(value, "vim --noplugin") == 0,
              "value should be 'vim --noplugin'");

    rmrf(dir);
    return 0;
}

static int test_config_get_no_spaces(void)
{
    char dir[256];
    int rc = setup_config(dir, "[core]\nbare=false\n");
    MU_ASSERT(rc == 0, "setup_config failed");

    char value[MYGIT_MAX_VALUE];
    int result = mygit_config_get(dir, "core", "bare",
                                  value, sizeof(value));

    MU_ASSERT(result == 0, "config_get should return 0 (found)");
    MU_ASSERT(strcmp(value, "false") == 0, "value should be 'false'");

    rmrf(dir);
    return 0;
}

static int test_config_get_not_found_key(void)
{
    char dir[256];
    int rc = setup_config(dir, "[user]\n\tname = John\n");
    MU_ASSERT(rc == 0, "setup_config failed");

    char value[MYGIT_MAX_VALUE];
    int result = mygit_config_get(dir, "user", "email",
                                  value, sizeof(value));

    MU_ASSERT(result == 1, "config_get should return 1 (not found)");

    rmrf(dir);
    return 0;
}

static int test_config_get_not_found_section(void)
{
    char dir[256];
    int rc = setup_config(dir, "[user]\n\tname = John\n");
    MU_ASSERT(rc == 0, "setup_config failed");

    char value[MYGIT_MAX_VALUE];
    int result = mygit_config_get(dir, "core", "name",
                                  value, sizeof(value));

    MU_ASSERT(result == 1, "config_get should return 1 (not found)");

    rmrf(dir);
    return 0;
}

static int test_config_get_missing_file(void)
{
    char tmpl[] = "/tmp/mygit_cfg_XXXXXX";
    char *dir = mkdtemp(tmpl);
    MU_ASSERT(dir != NULL, "mkdtemp failed");

    /* no .mygit/config exists */
    char value[MYGIT_MAX_VALUE];
    int result = mygit_config_get(dir, "user", "name",
                                  value, sizeof(value));

    MU_ASSERT(result == 1, "config_get should return 1 (missing file)");

    rmrf(dir);
    return 0;
}

static int test_config_get_case_insensitive(void)
{
    char dir[256];
    int rc = setup_config(dir, "[USER]\n\tNAME = Alice\n");
    MU_ASSERT(rc == 0, "setup_config failed");

    char value[MYGIT_MAX_VALUE];
    int result = mygit_config_get(dir, "user", "name",
                                  value, sizeof(value));

    MU_ASSERT(result == 0, "config_get should return 0 (found)");
    MU_ASSERT(strcmp(value, "Alice") == 0, "value should be 'Alice'");

    rmrf(dir);
    return 0;
}

/* ---------- config_set tests ---------- */

static int test_config_set_new_section(void)
{
    char dir[256];
    int rc = setup_config(dir, "");
    MU_ASSERT(rc == 0, "setup_config failed");

    rc = mygit_config_set(dir, "user", "name", "Alice");
    MU_ASSERT(rc == 0, "config_set should return 0");

    char value[MYGIT_MAX_VALUE];
    rc = mygit_config_get(dir, "user", "name", value, sizeof(value));
    MU_ASSERT(rc == 0, "config_get should find the value");
    MU_ASSERT(strcmp(value, "Alice") == 0, "value should be 'Alice'");

    rmrf(dir);
    return 0;
}

static int test_config_set_existing_key(void)
{
    char dir[256];
    int rc = setup_config(dir, "[user]\n\tname = John\n");
    MU_ASSERT(rc == 0, "setup_config failed");

    rc = mygit_config_set(dir, "user", "name", "Alice");
    MU_ASSERT(rc == 0, "config_set should return 0");

    char value[MYGIT_MAX_VALUE];
    rc = mygit_config_get(dir, "user", "name", value, sizeof(value));
    MU_ASSERT(rc == 0, "config_get should find the value");
    MU_ASSERT(strcmp(value, "Alice") == 0, "value should be 'Alice'");

    rmrf(dir);
    return 0;
}

static int test_config_set_new_key_existing_section(void)
{
    char dir[256];
    int rc = setup_config(dir, "[user]\n\tname = Alice\n");
    MU_ASSERT(rc == 0, "setup_config failed");

    rc = mygit_config_set(dir, "user", "email", "alice@example.com");
    MU_ASSERT(rc == 0, "config_set should return 0");

    /* Both keys should exist */
    char value[MYGIT_MAX_VALUE];
    rc = mygit_config_get(dir, "user", "name", value, sizeof(value));
    MU_ASSERT(rc == 0, "original key should still exist");
    MU_ASSERT(strcmp(value, "Alice") == 0, "original value preserved");

    rc = mygit_config_get(dir, "user", "email", value, sizeof(value));
    MU_ASSERT(rc == 0, "new key should exist");
    MU_ASSERT(strcmp(value, "alice@example.com") == 0,
              "value should be 'alice@example.com'");

    rmrf(dir);
    return 0;
}

static int test_config_set_creates_file(void)
{
    char tmpl[] = "/tmp/mygit_cfg_XXXXXX";
    char *dir = mkdtemp(tmpl);
    MU_ASSERT(dir != NULL, "mkdtemp failed");

    /* Create .mygit dir but NOT the config file */
    char mygit_dir[256];
    snprintf(mygit_dir, sizeof(mygit_dir), "%s/.mygit", dir);
    mkdir(mygit_dir, 0755);

    int rc = mygit_config_set(dir, "core", "bare", "false");
    MU_ASSERT(rc == 0, "config_set should create file");

    char value[MYGIT_MAX_VALUE];
    rc = mygit_config_get(dir, "core", "bare", value, sizeof(value));
    MU_ASSERT(rc == 0, "config_get should find value in new file");
    MU_ASSERT(strcmp(value, "false") == 0, "value should be 'false'");

    rmrf(dir);
    return 0;
}

static int test_config_set_roundtrip(void)
{
    char dir[256];
    int rc = setup_config(dir, "");
    MU_ASSERT(rc == 0, "setup_config failed");

    rc = mygit_config_set(dir, "remote", "url",
                          "https://github.com/test/repo.git");
    MU_ASSERT(rc == 0, "config_set should return 0");

    char value[MYGIT_MAX_VALUE];
    rc = mygit_config_get(dir, "remote", "url", value, sizeof(value));
    MU_ASSERT(rc == 0, "roundtrip get should succeed");
    MU_ASSERT(strcmp(value, "https://github.com/test/repo.git") == 0,
              "roundtrip value should match");

    rmrf(dir);
    return 0;
}

/* ---------- config_init tests ---------- */

static int test_config_init_creates_default(void)
{
    char tmpl[] = "/tmp/mygit_cfg_XXXXXX";
    char *dir = mkdtemp(tmpl);
    MU_ASSERT(dir != NULL, "mkdtemp failed");

    char mygit_dir[256];
    snprintf(mygit_dir, sizeof(mygit_dir), "%s/.mygit", dir);
    mkdir(mygit_dir, 0755);

    int rc = mygit_config_init(dir);
    MU_ASSERT(rc == 0, "config_init should return 0");

    char value[MYGIT_MAX_VALUE];
    rc = mygit_config_get(dir, "core", "repositoryformatversion",
                          value, sizeof(value));
    MU_ASSERT(rc == 0, "should find repositoryformatversion");
    MU_ASSERT(strcmp(value, "0") == 0, "version should be '0'");

    rc = mygit_config_get(dir, "core", "filemode", value, sizeof(value));
    MU_ASSERT(rc == 0, "should find filemode");
    MU_ASSERT(strcmp(value, "true") == 0, "filemode should be 'true'");

    rc = mygit_config_get(dir, "core", "bare", value, sizeof(value));
    MU_ASSERT(rc == 0, "should find bare");
    MU_ASSERT(strcmp(value, "false") == 0, "bare should be 'false'");

    rmrf(dir);
    return 0;
}

static int test_config_init_idempotent(void)
{
    char tmpl[] = "/tmp/mygit_cfg_XXXXXX";
    char *dir = mkdtemp(tmpl);
    MU_ASSERT(dir != NULL, "mkdtemp failed");

    char mygit_dir[256];
    snprintf(mygit_dir, sizeof(mygit_dir), "%s/.mygit", dir);
    mkdir(mygit_dir, 0755);

    int rc = mygit_config_init(dir);
    MU_ASSERT(rc == 0, "first config_init should succeed");

    rc = mygit_config_init(dir);
    MU_ASSERT(rc == 0, "second config_init should succeed");

    /* Values should still be correct */
    char value[MYGIT_MAX_VALUE];
    rc = mygit_config_get(dir, "core", "repositoryformatversion",
                          value, sizeof(value));
    MU_ASSERT(rc == 0, "should find repositoryformatversion after double init");
    MU_ASSERT(strcmp(value, "0") == 0, "version should still be '0'");

    rmrf(dir);
    return 0;
}

static int test_init_creates_config(void)
{
    char tmpl[] = "/tmp/mygit_cfg_XXXXXX";
    char *dir = mkdtemp(tmpl);
    MU_ASSERT(dir != NULL, "mkdtemp failed");

    int rc = mygit_init(dir);
    MU_ASSERT(rc == 0, "mygit_init should succeed");

    /* mygit_init should now create .mygit/config with defaults */
    char value[MYGIT_MAX_VALUE];
    rc = mygit_config_get(dir, "core", "repositoryformatversion",
                          value, sizeof(value));
    MU_ASSERT(rc == 0, "init should create config with core values");
    MU_ASSERT(strcmp(value, "0") == 0, "version should be '0'");

    rc = mygit_config_get(dir, "core", "bare", value, sizeof(value));
    MU_ASSERT(rc == 0, "should find bare after init");
    MU_ASSERT(strcmp(value, "false") == 0, "bare should be 'false'");

    rmrf(dir);
    return 0;
}

/* ---------- main ---------- */

int main(void)
{
    fprintf(stderr, "\n=== mygit_config tests ===\n\n");

    MU_RUN_TEST(test_config_get_basic);
    MU_RUN_TEST(test_config_get_with_spaces);
    MU_RUN_TEST(test_config_get_no_spaces);
    MU_RUN_TEST(test_config_get_not_found_key);
    MU_RUN_TEST(test_config_get_not_found_section);
    MU_RUN_TEST(test_config_get_missing_file);
    MU_RUN_TEST(test_config_get_case_insensitive);
    MU_RUN_TEST(test_config_set_new_section);
    MU_RUN_TEST(test_config_set_existing_key);
    MU_RUN_TEST(test_config_set_new_key_existing_section);
    MU_RUN_TEST(test_config_set_creates_file);
    MU_RUN_TEST(test_config_set_roundtrip);
    MU_RUN_TEST(test_config_init_creates_default);
    MU_RUN_TEST(test_config_init_idempotent);
    MU_RUN_TEST(test_init_creates_config);

    fprintf(stderr, "\n--- Results: %d/%d passed, %d failed ---\n\n",
            tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
