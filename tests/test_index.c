#define _XOPEN_SOURCE 700

#include "index.h"
#include "hash.h"
#include "blob.h"
#include "repo.h"
#include "tree.h"

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
    (void)sb; (void)typeflag; (void)ftwbuf;
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

/* Create a test entry with known values */
static mygit_index_entry make_entry(const char *name, uint32_t mode,
                                     const unsigned char sha1[20])
{
    mygit_index_entry e;
    memset(&e, 0, sizeof(e));
    e.mode = mode;
    memcpy(e.sha1, sha1, 20);
    strncpy(e.name, name, MYGIT_INDEX_MAX_PATH - 1);
    e.ctime_s = 1000;
    e.mtime_s = 2000;
    e.size = 42;
    return e;
}

/* ---------- index init/add/remove/find tests ---------- */

static int test_index_init_empty(void)
{
    mygit_index idx;
    mygit_index_init(&idx);
    MU_ASSERT(idx.entries == NULL, "entries should be NULL");
    MU_ASSERT(idx.count == 0, "count should be 0");
    MU_ASSERT(idx.capacity == 0, "capacity should be 0");
    mygit_index_free(&idx);
    return 0;
}

static int test_index_add_single(void)
{
    mygit_index idx;
    mygit_index_init(&idx);

    unsigned char sha1[20] = {0xaa, 0xbb, 0xcc};
    mygit_index_entry e = make_entry("hello.txt", 0100644, sha1);

    int ret = mygit_index_add(&idx, &e);
    MU_ASSERT(ret == 0, "add should succeed");
    MU_ASSERT(idx.count == 1, "count should be 1");
    MU_ASSERT(strcmp(idx.entries[0].name, "hello.txt") == 0, "name should match");
    MU_ASSERT(idx.entries[0].mode == 0100644, "mode should match");

    mygit_index_free(&idx);
    return 0;
}

static int test_index_add_sorted(void)
{
    mygit_index idx;
    mygit_index_init(&idx);

    unsigned char sha1[20] = {0};
    mygit_index_entry e1 = make_entry("z_last.txt", 0100644, sha1);
    mygit_index_entry e2 = make_entry("a_first.txt", 0100644, sha1);
    mygit_index_entry e3 = make_entry("m_middle.txt", 0100644, sha1);

    mygit_index_add(&idx, &e1);
    mygit_index_add(&idx, &e2);
    mygit_index_add(&idx, &e3);

    MU_ASSERT(idx.count == 3, "count should be 3");
    MU_ASSERT(strcmp(idx.entries[0].name, "a_first.txt") == 0, "first sorted");
    MU_ASSERT(strcmp(idx.entries[1].name, "m_middle.txt") == 0, "middle sorted");
    MU_ASSERT(strcmp(idx.entries[2].name, "z_last.txt") == 0, "last sorted");

    mygit_index_free(&idx);
    return 0;
}

static int test_index_add_update_existing(void)
{
    mygit_index idx;
    mygit_index_init(&idx);

    unsigned char sha1_old[20] = {0x11};
    unsigned char sha1_new[20] = {0x22};
    mygit_index_entry e1 = make_entry("file.txt", 0100644, sha1_old);
    mygit_index_entry e2 = make_entry("file.txt", 0100644, sha1_new);

    mygit_index_add(&idx, &e1);
    mygit_index_add(&idx, &e2);

    MU_ASSERT(idx.count == 1, "should not duplicate");
    MU_ASSERT(memcmp(idx.entries[0].sha1, sha1_new, 20) == 0,
              "SHA should be updated");

    mygit_index_free(&idx);
    return 0;
}

static int test_index_find(void)
{
    mygit_index idx;
    mygit_index_init(&idx);

    unsigned char sha1[20] = {0xaa};
    mygit_index_entry e = make_entry("target.txt", 0100644, sha1);
    mygit_index_add(&idx, &e);

    mygit_index_entry *found = mygit_index_find(&idx, "target.txt");
    MU_ASSERT(found != NULL, "should find existing entry");
    MU_ASSERT(strcmp(found->name, "target.txt") == 0, "name should match");

    mygit_index_entry *not_found = mygit_index_find(&idx, "missing.txt");
    MU_ASSERT(not_found == NULL, "should not find missing entry");

    mygit_index_free(&idx);
    return 0;
}

static int test_index_remove(void)
{
    mygit_index idx;
    mygit_index_init(&idx);

    unsigned char sha1[20] = {0};
    mygit_index_add(&idx, &(mygit_index_entry){.mode = 0100644, .name = "a.txt"});
    mygit_index_add(&idx, &(mygit_index_entry){.mode = 0100644, .name = "b.txt"});
    memcpy(idx.entries[0].sha1, sha1, 20);
    memcpy(idx.entries[1].sha1, sha1, 20);

    int ret = mygit_index_remove(&idx, "a.txt");
    MU_ASSERT(ret == 0, "remove should succeed");
    MU_ASSERT(idx.count == 1, "count should be 1");
    MU_ASSERT(strcmp(idx.entries[0].name, "b.txt") == 0, "remaining entry");

    ret = mygit_index_remove(&idx, "nonexistent.txt");
    MU_ASSERT(ret == 1, "remove nonexistent should return 1");

    mygit_index_free(&idx);
    return 0;
}

/* ---------- index read/write round-trip tests ---------- */

static int test_index_write_read_roundtrip(void)
{
    char tmpl[] = "/tmp/mygit_idx_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    mygit_index idx_write;
    mygit_index_init(&idx_write);

    unsigned char sha1_a[20], sha1_b[20];
    memset(sha1_a, 0xaa, 20);
    memset(sha1_b, 0xbb, 20);

    mygit_index_entry ea = make_entry("alpha.txt", 0100644, sha1_a);
    mygit_index_entry eb = make_entry("beta.txt", 0100755, sha1_b);
    mygit_index_add(&idx_write, &ea);
    mygit_index_add(&idx_write, &eb);

    int ret = mygit_index_write(dir, &idx_write);
    MU_ASSERT(ret == 0, "write should succeed");

    mygit_index idx_read;
    ret = mygit_index_read(dir, &idx_read);
    MU_ASSERT(ret == 0, "read should succeed");
    MU_ASSERT(idx_read.count == 2, "should read 2 entries");

    MU_ASSERT(strcmp(idx_read.entries[0].name, "alpha.txt") == 0, "name 0");
    MU_ASSERT(idx_read.entries[0].mode == 0100644, "mode 0");
    MU_ASSERT(memcmp(idx_read.entries[0].sha1, sha1_a, 20) == 0, "sha1 0");
    MU_ASSERT(idx_read.entries[0].ctime_s == 1000, "ctime_s 0");
    MU_ASSERT(idx_read.entries[0].mtime_s == 2000, "mtime_s 0");
    MU_ASSERT(idx_read.entries[0].size == 42, "size 0");

    MU_ASSERT(strcmp(idx_read.entries[1].name, "beta.txt") == 0, "name 1");
    MU_ASSERT(idx_read.entries[1].mode == 0100755, "mode 1");
    MU_ASSERT(memcmp(idx_read.entries[1].sha1, sha1_b, 20) == 0, "sha1 1");

    mygit_index_free(&idx_write);
    mygit_index_free(&idx_read);
    rmrf(dir);
    return 0;
}

static int test_index_read_missing(void)
{
    char tmpl[] = "/tmp/mygit_idx_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    mygit_index idx;
    int ret = mygit_index_read(dir, &idx);
    MU_ASSERT(ret == 1, "should return 1 for missing index");
    MU_ASSERT(idx.count == 0, "should be empty");

    mygit_index_free(&idx);
    rmrf(dir);
    return 0;
}

static int test_index_roundtrip_many_entries(void)
{
    char tmpl[] = "/tmp/mygit_idx_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    mygit_index idx_write;
    mygit_index_init(&idx_write);

    /* Add 50 entries with varying name lengths */
    for (int i = 0; i < 50; i++) {
        char name[64];
        snprintf(name, sizeof(name), "dir%d/file_%03d.txt", i % 5, i);
        unsigned char sha1[20];
        memset(sha1, (unsigned char)i, 20);
        mygit_index_entry e = make_entry(name, 0100644, sha1);
        e.size = (uint32_t)(i * 100);
        mygit_index_add(&idx_write, &e);
    }

    MU_ASSERT(idx_write.count == 50, "should have 50 entries");

    int ret = mygit_index_write(dir, &idx_write);
    MU_ASSERT(ret == 0, "write should succeed");

    mygit_index idx_read;
    ret = mygit_index_read(dir, &idx_read);
    MU_ASSERT(ret == 0, "read should succeed");
    MU_ASSERT(idx_read.count == 50, "should read 50 entries");

    /* Verify all entries match */
    for (size_t i = 0; i < idx_write.count; i++) {
        MU_ASSERT(strcmp(idx_write.entries[i].name,
                         idx_read.entries[i].name) == 0,
                  "names should match");
        MU_ASSERT(memcmp(idx_write.entries[i].sha1,
                          idx_read.entries[i].sha1, 20) == 0,
                  "sha1 should match");
        MU_ASSERT(idx_write.entries[i].size == idx_read.entries[i].size,
                  "size should match");
    }

    mygit_index_free(&idx_write);
    mygit_index_free(&idx_read);
    rmrf(dir);
    return 0;
}

static int test_index_checksum_integrity(void)
{
    char tmpl[] = "/tmp/mygit_idx_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    /* Write a valid index */
    mygit_index idx;
    mygit_index_init(&idx);
    unsigned char sha1[20] = {0xde, 0xad};
    mygit_index_entry e = make_entry("test.c", 0100644, sha1);
    mygit_index_add(&idx, &e);
    mygit_index_write(dir, &idx);
    mygit_index_free(&idx);

    /* Corrupt one byte in the middle of the file */
    char path[512];
    snprintf(path, sizeof(path), "%s/.mygit/index", dir);
    FILE *fp = fopen(path, "r+b");
    MU_ASSERT(fp != NULL, "should open index file");
    fseek(fp, 20, SEEK_SET); /* somewhere in the middle */
    unsigned char byte = 0xFF;
    fwrite(&byte, 1, 1, fp);
    fclose(fp);

    /* Read should fail due to checksum mismatch */
    mygit_index idx2;
    int ret = mygit_index_read(dir, &idx2);
    MU_ASSERT(ret == -1, "corrupt index should fail to read");

    rmrf(dir);
    return 0;
}

/* ---------- write-tree tests ---------- */

static int test_write_tree_single_file(void)
{
    char tmpl[] = "/tmp/mygit_idx_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    /* Create a real blob so the tree references a valid object */
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/hello.txt", dir);
    FILE *fp = fopen(file_path, "w");
    MU_ASSERT(fp != NULL, "create test file");
    fprintf(fp, "hello world\n");
    fclose(fp);

    char blob_hex[41];
    int ret = mygit_blob_write(dir, file_path, blob_hex);
    MU_ASSERT(ret == 0, "blob_write should succeed");

    /* Build index with this blob */
    mygit_index idx;
    mygit_index_init(&idx);

    mygit_index_entry e;
    memset(&e, 0, sizeof(e));
    e.mode = 0100644;
    strncpy(e.name, "hello.txt", sizeof(e.name));
    /* Convert blob hex to raw sha1 */
    for (int i = 0; i < 20; i++) {
        unsigned int byte;
        sscanf(blob_hex + i * 2, "%02x", &byte);
        e.sha1[i] = (unsigned char)byte;
    }
    mygit_index_add(&idx, &e);

    /* Write tree */
    char tree_hex[41];
    ret = mygit_write_tree(dir, &idx, tree_hex);
    MU_ASSERT(ret == 0, "write_tree should succeed");
    MU_ASSERT(strlen(tree_hex) == 40, "tree hex should be 40 chars");

    /* Verify tree can be read back */
    mygit_tree_entry *entries = NULL;
    size_t count = 0;
    ret = mygit_tree_read(dir, tree_hex, &entries, &count);
    MU_ASSERT(ret == 0, "tree_read should succeed");
    MU_ASSERT(count == 1, "tree should have 1 entry");
    MU_ASSERT(strcmp(entries[0].name, "hello.txt") == 0, "entry name");
    MU_ASSERT(entries[0].mode == MYGIT_MODE_FILE, "entry mode");

    free(entries);
    mygit_index_free(&idx);
    rmrf(dir);
    return 0;
}

static int test_write_tree_nested_dirs(void)
{
    char tmpl[] = "/tmp/mygit_idx_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    /* Create blobs for test files */
    char file_path[512];
    unsigned char sha1_root[20], sha1_src[20], sha1_lib[20];

    /* Root file */
    snprintf(file_path, sizeof(file_path), "%s/readme.txt", dir);
    FILE *fp = fopen(file_path, "w");
    fprintf(fp, "readme content\n");
    fclose(fp);
    char hex[41];
    mygit_blob_write(dir, file_path, hex);
    for (int i = 0; i < 20; i++) {
        unsigned int b; sscanf(hex + i * 2, "%02x", &b);
        sha1_root[i] = (unsigned char)b;
    }

    /* src/main.c */
    snprintf(file_path, sizeof(file_path), "%s/main.c", dir);
    fp = fopen(file_path, "w");
    fprintf(fp, "int main() { return 0; }\n");
    fclose(fp);
    mygit_blob_write(dir, file_path, hex);
    for (int i = 0; i < 20; i++) {
        unsigned int b; sscanf(hex + i * 2, "%02x", &b);
        sha1_src[i] = (unsigned char)b;
    }

    /* src/lib/util.c */
    snprintf(file_path, sizeof(file_path), "%s/util.c", dir);
    fp = fopen(file_path, "w");
    fprintf(fp, "void util() {}\n");
    fclose(fp);
    mygit_blob_write(dir, file_path, hex);
    for (int i = 0; i < 20; i++) {
        unsigned int b; sscanf(hex + i * 2, "%02x", &b);
        sha1_lib[i] = (unsigned char)b;
    }

    /* Build index with nested structure */
    mygit_index idx;
    mygit_index_init(&idx);

    mygit_index_entry e;
    memset(&e, 0, sizeof(e));
    e.mode = 0100644;

    strncpy(e.name, "readme.txt", sizeof(e.name));
    memcpy(e.sha1, sha1_root, 20);
    mygit_index_add(&idx, &e);

    memset(&e, 0, sizeof(e));
    e.mode = 0100644;
    strncpy(e.name, "src/main.c", sizeof(e.name));
    memcpy(e.sha1, sha1_src, 20);
    mygit_index_add(&idx, &e);

    memset(&e, 0, sizeof(e));
    e.mode = 0100644;
    strncpy(e.name, "src/lib/util.c", sizeof(e.name));
    memcpy(e.sha1, sha1_lib, 20);
    mygit_index_add(&idx, &e);

    /* Write tree — should create nested tree objects */
    char tree_hex[41];
    int ret = mygit_write_tree(dir, &idx, tree_hex);
    MU_ASSERT(ret == 0, "write_tree should succeed");

    /* Verify root tree has 2 entries: readme.txt (blob) + src (tree) */
    mygit_tree_entry *entries = NULL;
    size_t count = 0;
    ret = mygit_tree_read(dir, tree_hex, &entries, &count);
    MU_ASSERT(ret == 0, "root tree read");
    MU_ASSERT(count == 2, "root should have 2 entries");

    /* Entries are sorted: readme.txt, src */
    MU_ASSERT(strcmp(entries[0].name, "readme.txt") == 0, "root entry 0");
    MU_ASSERT(entries[0].mode == MYGIT_MODE_FILE, "readme is blob");
    MU_ASSERT(strcmp(entries[1].name, "src") == 0, "root entry 1");
    MU_ASSERT(entries[1].mode == MYGIT_MODE_TREE, "src is tree");

    /* Verify src subtree has 2 entries: lib (tree) + main.c (blob) */
    char src_hex[41];
    mygit_hex(entries[1].hash, src_hex);
    free(entries);

    ret = mygit_tree_read(dir, src_hex, &entries, &count);
    MU_ASSERT(ret == 0, "src tree read");
    MU_ASSERT(count == 2, "src should have 2 entries");
    MU_ASSERT(strcmp(entries[0].name, "lib") == 0, "src entry 0 is lib");
    MU_ASSERT(entries[0].mode == MYGIT_MODE_TREE, "lib is tree");
    MU_ASSERT(strcmp(entries[1].name, "main.c") == 0, "src entry 1");

    free(entries);
    mygit_index_free(&idx);
    rmrf(dir);
    return 0;
}

static int test_write_tree_empty_index(void)
{
    char tmpl[] = "/tmp/mygit_idx_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "make_test_repo failed");

    mygit_index idx;
    mygit_index_init(&idx);

    char tree_hex[41];
    int ret = mygit_write_tree(dir, &idx, tree_hex);
    MU_ASSERT(ret == 0, "write_tree on empty index should succeed");
    MU_ASSERT(strlen(tree_hex) == 40, "should produce valid hash");

    mygit_index_free(&idx);
    rmrf(dir);
    return 0;
}

/* ---------- main ---------- */

int main(void)
{
    fprintf(stderr, "\n=== mygit_index tests ===\n\n");

    /* Basic operations */
    MU_RUN_TEST(test_index_init_empty);
    MU_RUN_TEST(test_index_add_single);
    MU_RUN_TEST(test_index_add_sorted);
    MU_RUN_TEST(test_index_add_update_existing);
    MU_RUN_TEST(test_index_find);
    MU_RUN_TEST(test_index_remove);

    /* Read/write round-trip */
    MU_RUN_TEST(test_index_write_read_roundtrip);
    MU_RUN_TEST(test_index_read_missing);
    MU_RUN_TEST(test_index_roundtrip_many_entries);
    MU_RUN_TEST(test_index_checksum_integrity);

    /* write-tree */
    MU_RUN_TEST(test_write_tree_single_file);
    MU_RUN_TEST(test_write_tree_nested_dirs);
    MU_RUN_TEST(test_write_tree_empty_index);

    fprintf(stderr, "\n--- Results: %d/%d passed, %d failed ---\n\n",
            tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
