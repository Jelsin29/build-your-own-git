#define _XOPEN_SOURCE 700

#include "commit.h"
#include "hash.h"
#include "object.h"
#include "repo.h"
#include "tree.h"

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
        fprintf(stderr, "  %-55s", #test_func);                        \
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

static void hex_to_raw(const char *hex, unsigned char raw[20])
{
    for (int i = 0; i < 20; i++) {
        unsigned int byte;
        sscanf(hex + 2 * i, "%2x", &byte);
        raw[i] = (unsigned char)byte;
    }
}

static void make_commit(mygit_commit *c, const char *tree_hex,
                        const char *parent_hex, const char *message,
                        int64_t timestamp)
{
    memset(c, 0, sizeof(*c));
    strncpy(c->tree_hex, tree_hex, 40);
    c->tree_hex[40] = '\0';

    if (parent_hex != NULL) {
        strncpy(c->parent_hex[0], parent_hex, 40);
        c->parent_hex[0][40] = '\0';
        c->parent_count = 1;
    }

    strncpy(c->author_name, "Test", sizeof(c->author_name));
    strncpy(c->author_email, "test@test.com", sizeof(c->author_email));
    c->author_time = timestamp;
    strncpy(c->author_tz, "+0000", sizeof(c->author_tz));

    strncpy(c->committer_name, "Test", sizeof(c->committer_name));
    strncpy(c->committer_email, "test@test.com", sizeof(c->committer_email));
    c->committer_time = timestamp;
    strncpy(c->committer_tz, "+0000", sizeof(c->committer_tz));

    strncpy(c->message, message, sizeof(c->message));
}

/* ---------- commit hash tests ---------- */

static int test_commit_write_root_known_hash(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "test repo creation failed");

    /* Write tree object that matches git's "hello.txt" tree */
    mygit_tree_entry entry;
    entry.mode = MYGIT_MODE_FILE;
    strncpy(entry.name, "hello.txt", sizeof(entry.name));
    hex_to_raw("3b18e512dba79e4c8300dd08aeb37f8e728b8dad", entry.hash);

    char tree_hex[41];
    MU_ASSERT(mygit_tree_write(dir, &entry, 1, tree_hex) == 0,
              "tree write should succeed");
    MU_ASSERT(strcmp(tree_hex,
              "68aba62e560c0ebc3396e8ae9335232cd93a3f60") == 0,
              "tree hash should match known value");

    /* Create root commit matching the deterministic git commit */
    mygit_commit commit;
    make_commit(&commit, tree_hex, NULL, "initial commit", 1700000000);

    char commit_hex[41];
    MU_ASSERT(mygit_commit_write(dir, &commit, commit_hex) == 0,
              "commit write should succeed");
    MU_ASSERT(strcmp(commit_hex,
              "70a8abc652c5147d636f99cdbf8ec7aa5163127f") == 0,
              "root commit hash should match git");

    rmrf(dir);
    return 0;
}

static int test_commit_write_with_parent_known_hash(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "test repo creation failed");

    /* Tree for second commit: hello.txt + foo.txt */
    mygit_tree_entry entries[2];
    entries[0].mode = MYGIT_MODE_FILE;
    strncpy(entries[0].name, "foo.txt", sizeof(entries[0].name));
    hex_to_raw("257cc5642cb1a054f08cc83f2d943e56fd3ebe99", entries[0].hash);

    entries[1].mode = MYGIT_MODE_FILE;
    strncpy(entries[1].name, "hello.txt", sizeof(entries[1].name));
    hex_to_raw("3b18e512dba79e4c8300dd08aeb37f8e728b8dad", entries[1].hash);

    char tree_hex[41];
    MU_ASSERT(mygit_tree_write(dir, entries, 2, tree_hex) == 0,
              "tree write should succeed");
    MU_ASSERT(strcmp(tree_hex,
              "a2fdc5c2ac80fba6883976f96f9423622fafc08e") == 0,
              "tree hash should match known value");

    /* Create commit with parent */
    mygit_commit commit;
    make_commit(&commit, tree_hex,
                "70a8abc652c5147d636f99cdbf8ec7aa5163127f",
                "add foo", 1700001000);

    char commit_hex[41];
    MU_ASSERT(mygit_commit_write(dir, &commit, commit_hex) == 0,
              "commit write should succeed");
    MU_ASSERT(strcmp(commit_hex,
              "32c2561a8ba1803f39b2d7ea2ff7236c015b7c14") == 0,
              "commit with parent hash should match git");

    rmrf(dir);
    return 0;
}

/* ---------- commit read tests ---------- */

static int test_commit_write_and_read(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "test repo creation failed");

    mygit_commit original;
    make_commit(&original,
                "68aba62e560c0ebc3396e8ae9335232cd93a3f60",
                NULL, "test message", 1700000000);

    char hex[41];
    MU_ASSERT(mygit_commit_write(dir, &original, hex) == 0,
              "commit write should succeed");

    mygit_commit read_back;
    MU_ASSERT(mygit_commit_read(dir, hex, &read_back) == 0,
              "commit read should succeed");

    MU_ASSERT(strcmp(read_back.tree_hex, original.tree_hex) == 0,
              "tree hex should match");
    MU_ASSERT(read_back.parent_count == 0,
              "root commit should have 0 parents");
    MU_ASSERT(strcmp(read_back.author_name, "Test") == 0,
              "author name should match");
    MU_ASSERT(strcmp(read_back.author_email, "test@test.com") == 0,
              "author email should match");
    MU_ASSERT(read_back.author_time == 1700000000,
              "author time should match");
    MU_ASSERT(strcmp(read_back.author_tz, "+0000") == 0,
              "author timezone should match");
    MU_ASSERT(strcmp(read_back.committer_name, "Test") == 0,
              "committer name should match");
    MU_ASSERT(strcmp(read_back.message, "test message") == 0,
              "message should match");

    rmrf(dir);
    return 0;
}

static int test_commit_read_with_parent(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "test repo creation failed");

    mygit_commit original;
    make_commit(&original,
                "68aba62e560c0ebc3396e8ae9335232cd93a3f60",
                "0000000000000000000000000000000000000001",
                "child commit", 1700001000);

    char hex[41];
    MU_ASSERT(mygit_commit_write(dir, &original, hex) == 0,
              "commit write should succeed");

    mygit_commit read_back;
    MU_ASSERT(mygit_commit_read(dir, hex, &read_back) == 0,
              "commit read should succeed");

    MU_ASSERT(read_back.parent_count == 1,
              "should have 1 parent");
    MU_ASSERT(strcmp(read_back.parent_hex[0],
              "0000000000000000000000000000000000000001") == 0,
              "parent hex should match");

    rmrf(dir);
    return 0;
}

static int test_commit_read_not_found(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "test repo creation failed");

    mygit_commit commit;
    const char fake[] = "0000000000000000000000000000000000000000";
    MU_ASSERT(mygit_commit_read(dir, fake, &commit) == 1,
              "reading nonexistent commit should return 1");

    rmrf(dir);
    return 0;
}

static int test_commit_read_rejects_blob(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "test repo creation failed");

    const unsigned char store[] = "blob 5\0hello";
    char hex[41];
    MU_ASSERT(mygit_object_write(dir, store, 12, hex) == 0,
              "writing blob should succeed");

    mygit_commit commit;
    MU_ASSERT(mygit_commit_read(dir, hex, &commit) == -1,
              "commit_read of blob should return -1");

    rmrf(dir);
    return 0;
}

static int test_commit_write_null_params(void)
{
    char hex[41];
    mygit_commit commit;
    memset(&commit, 0, sizeof(commit));

    MU_ASSERT(mygit_commit_write(NULL, &commit, hex) == -1,
              "null repo should fail");
    MU_ASSERT(mygit_commit_write(".", NULL, hex) == -1,
              "null commit should fail");
    MU_ASSERT(mygit_commit_write(".", &commit, NULL) == -1,
              "null hex_out should fail");
    return 0;
}

static int test_commit_write_invalid_tree_hex(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "test repo creation failed");

    mygit_commit commit;
    memset(&commit, 0, sizeof(commit));
    strncpy(commit.tree_hex, "short", sizeof(commit.tree_hex));

    char hex[41];
    MU_ASSERT(mygit_commit_write(dir, &commit, hex) == -1,
              "commit with invalid tree hex should fail");

    rmrf(dir);
    return 0;
}

static int test_commit_parent_chain(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "test repo creation failed");

    const char *tree = "68aba62e560c0ebc3396e8ae9335232cd93a3f60";

    /* Create root commit */
    mygit_commit root;
    make_commit(&root, tree, NULL, "root", 1700000000);
    char root_hex[41];
    MU_ASSERT(mygit_commit_write(dir, &root, root_hex) == 0,
              "root commit should succeed");

    /* Create child commit */
    mygit_commit child;
    make_commit(&child, tree, root_hex, "child", 1700001000);
    char child_hex[41];
    MU_ASSERT(mygit_commit_write(dir, &child, child_hex) == 0,
              "child commit should succeed");

    /* Create grandchild */
    mygit_commit grandchild;
    make_commit(&grandchild, tree, child_hex, "grandchild", 1700002000);
    char gc_hex[41];
    MU_ASSERT(mygit_commit_write(dir, &grandchild, gc_hex) == 0,
              "grandchild commit should succeed");

    /* Walk the chain back — copy parent hex before reading into same struct */
    mygit_commit read;
    char next_hex[41];

    MU_ASSERT(mygit_commit_read(dir, gc_hex, &read) == 0,
              "read grandchild should succeed");
    MU_ASSERT(strcmp(read.message, "grandchild") == 0,
              "grandchild message");
    MU_ASSERT(read.parent_count == 1, "grandchild has 1 parent");
    memcpy(next_hex, read.parent_hex[0], 41);

    MU_ASSERT(mygit_commit_read(dir, next_hex, &read) == 0,
              "read child should succeed");
    MU_ASSERT(strcmp(read.message, "child") == 0, "child message");
    MU_ASSERT(read.parent_count == 1, "child has 1 parent");
    memcpy(next_hex, read.parent_hex[0], 41);

    MU_ASSERT(mygit_commit_read(dir, next_hex, &read) == 0,
              "read root should succeed");
    MU_ASSERT(strcmp(read.message, "root") == 0, "root message");
    MU_ASSERT(read.parent_count == 0, "root has 0 parents");

    rmrf(dir);
    return 0;
}

/* ---------- main ---------- */

int main(void)
{
    fprintf(stderr, "\n=== Commit hash tests ===\n\n");
    MU_RUN_TEST(test_commit_write_root_known_hash);
    MU_RUN_TEST(test_commit_write_with_parent_known_hash);

    fprintf(stderr, "\n=== Commit write/read tests ===\n\n");
    MU_RUN_TEST(test_commit_write_and_read);
    MU_RUN_TEST(test_commit_read_with_parent);
    MU_RUN_TEST(test_commit_read_not_found);
    MU_RUN_TEST(test_commit_read_rejects_blob);

    fprintf(stderr, "\n=== Commit error handling tests ===\n\n");
    MU_RUN_TEST(test_commit_write_null_params);
    MU_RUN_TEST(test_commit_write_invalid_tree_hex);

    fprintf(stderr, "\n=== Commit DAG tests ===\n\n");
    MU_RUN_TEST(test_commit_parent_chain);

    fprintf(stderr, "\n--- Results: %d/%d passed, %d failed ---\n\n",
            tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
