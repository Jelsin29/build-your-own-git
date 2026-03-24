#define _XOPEN_SOURCE 700

#include "blob.h"
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

/* ---------- tree hash tests ---------- */

static int test_tree_hash_empty(void)
{
    char hex[41];
    MU_ASSERT(mygit_tree_hash(NULL, 0, hex) == 0,
              "tree_hash of empty should succeed");
    MU_ASSERT(strcmp(hex, "4b825dc642cb6eb9a060e54bf8d69288fbee4904") == 0,
              "empty tree hash should match git");
    return 0;
}

static int test_tree_hash_single_blob(void)
{
    /* Tree with one entry: 100644 hello.txt -> "hello world\n" blob */
    mygit_tree_entry entry;
    entry.mode = MYGIT_MODE_FILE;
    strncpy(entry.name, "hello.txt", sizeof(entry.name));
    hex_to_raw("3b18e512dba79e4c8300dd08aeb37f8e728b8dad", entry.hash);

    char hex[41];
    MU_ASSERT(mygit_tree_hash(&entry, 1, hex) == 0,
              "tree_hash single blob should succeed");
    MU_ASSERT(strcmp(hex, "68aba62e560c0ebc3396e8ae9335232cd93a3f60") == 0,
              "single-blob tree hash should match git");
    return 0;
}

static int test_tree_hash_multiple_blobs(void)
{
    /* Tree with three entries: bar.txt, foo.txt, hello.txt
     * Provide them UNSORTED to test sorting logic */
    mygit_tree_entry entries[3];

    entries[0].mode = MYGIT_MODE_FILE;
    strncpy(entries[0].name, "hello.txt", sizeof(entries[0].name));
    hex_to_raw("3b18e512dba79e4c8300dd08aeb37f8e728b8dad", entries[0].hash);

    entries[1].mode = MYGIT_MODE_FILE;
    strncpy(entries[1].name, "bar.txt", sizeof(entries[1].name));
    hex_to_raw("5716ca5987cbf97d6bb54920bea6adde242d87e6", entries[1].hash);

    entries[2].mode = MYGIT_MODE_FILE;
    strncpy(entries[2].name, "foo.txt", sizeof(entries[2].name));
    hex_to_raw("257cc5642cb1a054f08cc83f2d943e56fd3ebe99", entries[2].hash);

    char hex[41];
    MU_ASSERT(mygit_tree_hash(entries, 3, hex) == 0,
              "tree_hash multiple blobs should succeed");
    MU_ASSERT(strcmp(hex, "ece89cf54a0866d733a2e884885d1d26b236a57b") == 0,
              "multi-blob tree hash should match git");
    return 0;
}

static int test_tree_hash_null_hex_out(void)
{
    MU_ASSERT(mygit_tree_hash(NULL, 0, NULL) == -1,
              "tree_hash with NULL hex_out should fail");
    return 0;
}

static int test_tree_hash_null_entries_nonzero_count(void)
{
    char hex[41];
    MU_ASSERT(mygit_tree_hash(NULL, 5, hex) == -1,
              "tree_hash with NULL entries and count>0 should fail");
    return 0;
}

/* ---------- tree write/read tests ---------- */

static int test_tree_write_and_read(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "test repo creation failed");

    /* Write a blob first to have a valid hash */
    const unsigned char blob_data[] = "hello world\n";
    unsigned char *store = NULL;
    size_t store_len = 0;
    {
        char header[64];
        int hdr_len = snprintf(header, sizeof(header), "blob %zu",
                               sizeof(blob_data) - 1);
        store_len = (size_t)hdr_len + 1 + sizeof(blob_data) - 1;
        store = malloc(store_len);
        MU_ASSERT(store != NULL, "malloc should succeed");
        memcpy(store, header, (size_t)hdr_len);
        store[hdr_len] = '\0';
        memcpy(store + hdr_len + 1, blob_data, sizeof(blob_data) - 1);
    }

    char blob_hex[41];
    MU_ASSERT(mygit_object_write(dir, store, store_len, blob_hex) == 0,
              "blob write should succeed");
    free(store);

    /* Create tree with that blob */
    mygit_tree_entry entry;
    entry.mode = MYGIT_MODE_FILE;
    strncpy(entry.name, "hello.txt", sizeof(entry.name));
    hex_to_raw(blob_hex, entry.hash);

    char tree_hex[41];
    MU_ASSERT(mygit_tree_write(dir, &entry, 1, tree_hex) == 0,
              "tree_write should succeed");

    /* Read it back */
    mygit_tree_entry *read_entries = NULL;
    size_t read_count = 0;
    MU_ASSERT(mygit_tree_read(dir, tree_hex, &read_entries, &read_count) == 0,
              "tree_read should succeed");
    MU_ASSERT(read_count == 1, "should have one entry");
    MU_ASSERT(read_entries[0].mode == MYGIT_MODE_FILE,
              "mode should be 100644");
    MU_ASSERT(strcmp(read_entries[0].name, "hello.txt") == 0,
              "name should be hello.txt");
    MU_ASSERT(memcmp(read_entries[0].hash, entry.hash, 20) == 0,
              "hash should match");

    free(read_entries);
    rmrf(dir);
    return 0;
}

static int test_tree_write_multiple_entries(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "test repo creation failed");

    /* Create entries unsorted to verify sorting */
    mygit_tree_entry entries[3];

    entries[0].mode = MYGIT_MODE_FILE;
    strncpy(entries[0].name, "zebra.txt", sizeof(entries[0].name));
    hex_to_raw("3b18e512dba79e4c8300dd08aeb37f8e728b8dad", entries[0].hash);

    entries[1].mode = MYGIT_MODE_FILE;
    strncpy(entries[1].name, "alpha.txt", sizeof(entries[1].name));
    hex_to_raw("5716ca5987cbf97d6bb54920bea6adde242d87e6", entries[1].hash);

    entries[2].mode = MYGIT_MODE_FILE;
    strncpy(entries[2].name, "middle.txt", sizeof(entries[2].name));
    hex_to_raw("257cc5642cb1a054f08cc83f2d943e56fd3ebe99", entries[2].hash);

    char tree_hex[41];
    MU_ASSERT(mygit_tree_write(dir, entries, 3, tree_hex) == 0,
              "tree_write with 3 entries should succeed");

    mygit_tree_entry *read_entries = NULL;
    size_t read_count = 0;
    MU_ASSERT(mygit_tree_read(dir, tree_hex, &read_entries, &read_count) == 0,
              "tree_read should succeed");
    MU_ASSERT(read_count == 3, "should have three entries");

    /* Entries should come back sorted */
    MU_ASSERT(strcmp(read_entries[0].name, "alpha.txt") == 0,
              "first entry should be alpha.txt");
    MU_ASSERT(strcmp(read_entries[1].name, "middle.txt") == 0,
              "second entry should be middle.txt");
    MU_ASSERT(strcmp(read_entries[2].name, "zebra.txt") == 0,
              "third entry should be zebra.txt");

    free(read_entries);
    rmrf(dir);
    return 0;
}

static int test_tree_write_empty(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "test repo creation failed");

    char tree_hex[41];
    MU_ASSERT(mygit_tree_write(dir, NULL, 0, tree_hex) == 0,
              "writing empty tree should succeed");
    MU_ASSERT(strcmp(tree_hex, "4b825dc642cb6eb9a060e54bf8d69288fbee4904") == 0,
              "empty tree hash should match git");

    /* Read it back — should have 0 entries */
    mygit_tree_entry *read_entries = NULL;
    size_t read_count = 0;
    MU_ASSERT(mygit_tree_read(dir, tree_hex, &read_entries, &read_count) == 0,
              "reading empty tree should succeed");
    MU_ASSERT(read_count == 0, "empty tree should have 0 entries");
    MU_ASSERT(read_entries == NULL, "entries pointer should be NULL");

    rmrf(dir);
    return 0;
}

static int test_tree_with_subtree(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "test repo creation failed");

    /* Write a blob */
    unsigned char blob_hash[20];
    hex_to_raw("3b18e512dba79e4c8300dd08aeb37f8e728b8dad", blob_hash);

    /* Write inner tree (subtree with one blob) */
    mygit_tree_entry inner_entry;
    inner_entry.mode = MYGIT_MODE_FILE;
    strncpy(inner_entry.name, "nested.txt", sizeof(inner_entry.name));
    memcpy(inner_entry.hash, blob_hash, 20);

    char inner_hex[41];
    MU_ASSERT(mygit_tree_write(dir, &inner_entry, 1, inner_hex) == 0,
              "writing inner tree should succeed");

    /* Write outer tree referencing blob + subtree */
    mygit_tree_entry outer[2];
    outer[0].mode = MYGIT_MODE_FILE;
    strncpy(outer[0].name, "readme.txt", sizeof(outer[0].name));
    memcpy(outer[0].hash, blob_hash, 20);

    outer[1].mode = MYGIT_MODE_TREE;
    strncpy(outer[1].name, "subdir", sizeof(outer[1].name));
    hex_to_raw(inner_hex, outer[1].hash);

    char outer_hex[41];
    MU_ASSERT(mygit_tree_write(dir, outer, 2, outer_hex) == 0,
              "writing outer tree should succeed");

    /* Read it back */
    mygit_tree_entry *read_entries = NULL;
    size_t read_count = 0;
    MU_ASSERT(mygit_tree_read(dir, outer_hex, &read_entries, &read_count) == 0,
              "reading outer tree should succeed");
    MU_ASSERT(read_count == 2, "outer tree should have 2 entries");

    /* Find the tree entry */
    int found_tree = 0;
    int found_blob = 0;
    for (size_t i = 0; i < read_count; i++) {
        if (read_entries[i].mode == MYGIT_MODE_TREE) {
            MU_ASSERT(strcmp(read_entries[i].name, "subdir") == 0,
                      "tree entry should be 'subdir'");
            found_tree = 1;
        } else {
            MU_ASSERT(strcmp(read_entries[i].name, "readme.txt") == 0,
                      "blob entry should be 'readme.txt'");
            found_blob = 1;
        }
    }
    MU_ASSERT(found_tree && found_blob,
              "should have both a tree and blob entry");

    free(read_entries);
    rmrf(dir);
    return 0;
}

static int test_tree_read_not_found(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "test repo creation failed");

    mygit_tree_entry *entries = NULL;
    size_t count = 0;
    const char fake[] = "0000000000000000000000000000000000000000";
    MU_ASSERT(mygit_tree_read(dir, fake, &entries, &count) == 1,
              "reading nonexistent tree should return 1");

    rmrf(dir);
    return 0;
}

static int test_tree_read_rejects_blob(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "test repo creation failed");

    /* Store a blob object */
    const unsigned char store[] = "blob 5\0hello";
    char hex[41];
    MU_ASSERT(mygit_object_write(dir, store, 12, hex) == 0,
              "writing blob should succeed");

    /* Try to read it as a tree */
    mygit_tree_entry *entries = NULL;
    size_t count = 0;
    MU_ASSERT(mygit_tree_read(dir, hex, &entries, &count) == -1,
              "tree_read of blob should return -1");

    rmrf(dir);
    return 0;
}

static int test_tree_hash_matches_write(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "test repo creation failed");

    mygit_tree_entry entry;
    entry.mode = MYGIT_MODE_FILE;
    strncpy(entry.name, "test.txt", sizeof(entry.name));
    hex_to_raw("3b18e512dba79e4c8300dd08aeb37f8e728b8dad", entry.hash);

    char hash_hex[41];
    MU_ASSERT(mygit_tree_hash(&entry, 1, hash_hex) == 0,
              "tree_hash should succeed");

    char write_hex[41];
    MU_ASSERT(mygit_tree_write(dir, &entry, 1, write_hex) == 0,
              "tree_write should succeed");

    MU_ASSERT(strcmp(hash_hex, write_hex) == 0,
              "tree_hash and tree_write should produce same hash");

    rmrf(dir);
    return 0;
}

/* ---------- main ---------- */

int main(void)
{
    fprintf(stderr, "\n=== Tree hash tests ===\n\n");
    MU_RUN_TEST(test_tree_hash_empty);
    MU_RUN_TEST(test_tree_hash_single_blob);
    MU_RUN_TEST(test_tree_hash_multiple_blobs);
    MU_RUN_TEST(test_tree_hash_null_hex_out);
    MU_RUN_TEST(test_tree_hash_null_entries_nonzero_count);

    fprintf(stderr, "\n=== Tree write/read tests ===\n\n");
    MU_RUN_TEST(test_tree_write_and_read);
    MU_RUN_TEST(test_tree_write_multiple_entries);
    MU_RUN_TEST(test_tree_write_empty);
    MU_RUN_TEST(test_tree_with_subtree);
    MU_RUN_TEST(test_tree_read_not_found);
    MU_RUN_TEST(test_tree_read_rejects_blob);
    MU_RUN_TEST(test_tree_hash_matches_write);

    fprintf(stderr, "\n--- Results: %d/%d passed, %d failed ---\n\n",
            tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
