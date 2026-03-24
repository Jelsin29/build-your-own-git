#define _XOPEN_SOURCE 700

#include "blob.h"
#include "hash.h"
#include "object.h"
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

static int file_exists(const char *path)
{
    struct stat st;
    return (stat(path, &st) == 0 && S_ISREG(st.st_mode));
}

static char *make_test_repo(char *tmpl)
{
    char *dir = mkdtemp(tmpl);
    if (dir == NULL) return NULL;
    if (mygit_init(dir) != 0) return NULL;
    return dir;
}

static int write_test_file(const char *dir, const char *name,
                           const unsigned char *data, size_t len)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) return -1;
    if (len > 0) {
        if (fwrite(data, 1, len, fp) != len) {
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);
    return 0;
}

/* ---------- SHA-1 tests ---------- */

static int test_sha1_known_vector(void)
{
    const unsigned char input[] = "abc";
    unsigned char hash[20];
    char hex[41];

    MU_ASSERT(mygit_sha1(input, 3, hash) == 0, "sha1 should succeed");
    mygit_hex(hash, hex);

    MU_ASSERT(strcmp(hex, "a9993e364706816aba3e25717850c26c9cd0d89d") == 0,
              "sha1('abc') should match known vector");
    return 0;
}

static int test_sha1_hex_format(void)
{
    const unsigned char input[] = "test";
    unsigned char hash[20];
    char hex[41];

    mygit_sha1(input, 4, hash);
    mygit_hex(hash, hex);

    MU_ASSERT(strlen(hex) == 40, "hex should be 40 chars");
    for (int i = 0; i < 40; i++) {
        int valid = (hex[i] >= '0' && hex[i] <= '9') ||
                    (hex[i] >= 'a' && hex[i] <= 'f');
        MU_ASSERT(valid, "hex chars should be lowercase hex");
    }
    return 0;
}

/* ---------- blob hash tests ---------- */

static int test_blob_hash_known_content(void)
{
    const unsigned char content[] = "what is up, doc?";
    char hex[41];

    MU_ASSERT(mygit_blob_hash(content, 16, hex) == 0,
              "blob_hash should succeed");
    MU_ASSERT(strcmp(hex, "bd9dbf5aae1a3862dd1526723246b20206e5fc37") == 0,
              "blob hash should match git hash-object");
    return 0;
}

static int test_blob_hash_empty(void)
{
    char hex[41];

    MU_ASSERT(mygit_blob_hash(NULL, 0, hex) == 0,
              "blob_hash of empty should succeed");
    MU_ASSERT(strcmp(hex, "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391") == 0,
              "empty blob hash should match git");
    return 0;
}

/* ---------- object write/read tests ---------- */

static int test_object_write_creates_file(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "test repo creation failed");

    const unsigned char store[] = "blob 5\0hello";
    char hex[41];

    MU_ASSERT(mygit_object_write(dir, store, 12, hex) == 0,
              "object_write should succeed");

    char obj_path[512];
    snprintf(obj_path, sizeof(obj_path),
             "%s/.mygit/objects/%.2s/%.38s", dir, hex, hex + 2);

    MU_ASSERT(file_exists(obj_path), "object file should exist on disk");

    rmrf(dir);
    return 0;
}

static int test_object_write_idempotent(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "test repo creation failed");

    const unsigned char store[] = "blob 5\0hello";
    char hex1[41], hex2[41];

    MU_ASSERT(mygit_object_write(dir, store, 12, hex1) == 0,
              "first write should succeed");
    MU_ASSERT(mygit_object_write(dir, store, 12, hex2) == 0,
              "second write should succeed (idempotent)");
    MU_ASSERT(strcmp(hex1, hex2) == 0, "hashes should be identical");

    rmrf(dir);
    return 0;
}

static int test_object_roundtrip(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "test repo creation failed");

    const unsigned char store[] = "blob 5\0hello";
    size_t store_len = 12;
    char hex[41];

    MU_ASSERT(mygit_object_write(dir, store, store_len, hex) == 0,
              "object_write should succeed");

    unsigned char *out = NULL;
    size_t out_len = 0;
    MU_ASSERT(mygit_object_read(dir, hex, &out, &out_len) == 0,
              "object_read should succeed");
    MU_ASSERT(out_len == store_len, "roundtrip length should match");
    MU_ASSERT(memcmp(out, store, store_len) == 0,
              "roundtrip data should match");

    free(out);
    rmrf(dir);
    return 0;
}

static int test_object_read_missing(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "test repo creation failed");

    unsigned char *out = NULL;
    size_t out_len = 0;
    const char fake_hex[] = "0000000000000000000000000000000000000000";

    MU_ASSERT(mygit_object_read(dir, fake_hex, &out, &out_len) == 1,
              "reading missing object should return 1");
    MU_ASSERT(out == NULL, "out should be NULL for missing object");

    rmrf(dir);
    return 0;
}

/* ---------- blob write/read tests ---------- */

static int test_blob_write_and_read(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "test repo creation failed");

    const unsigned char file_content[] = "hello, blob!\n";
    size_t file_len = 13;

    MU_ASSERT(write_test_file(dir, "test.txt", file_content, file_len) == 0,
              "writing test file should succeed");

    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/test.txt", dir);

    char hex[41];
    MU_ASSERT(mygit_blob_write(dir, file_path, hex) == 0,
              "blob_write should succeed");

    unsigned char *content = NULL;
    size_t content_len = 0;
    MU_ASSERT(mygit_blob_read(dir, hex, &content, &content_len) == 0,
              "blob_read should succeed");
    MU_ASSERT(content_len == file_len, "content length should match");
    MU_ASSERT(memcmp(content, file_content, file_len) == 0,
              "content should match original file");

    free(content);
    rmrf(dir);
    return 0;
}

static int test_blob_write_empty_file(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "test repo creation failed");

    MU_ASSERT(write_test_file(dir, "empty.txt", NULL, 0) == 0,
              "writing empty file should succeed");

    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/empty.txt", dir);

    char hex[41];
    MU_ASSERT(mygit_blob_write(dir, file_path, hex) == 0,
              "blob_write of empty file should succeed");
    MU_ASSERT(strcmp(hex, "e69de29bb2d1d6434b8b29ae775ad8c2e48c5391") == 0,
              "empty blob hash should match git");

    unsigned char *content = NULL;
    size_t content_len = 0;
    MU_ASSERT(mygit_blob_read(dir, hex, &content, &content_len) == 0,
              "blob_read of empty should succeed");
    MU_ASSERT(content_len == 0, "empty blob content length should be 0");

    free(content);
    rmrf(dir);
    return 0;
}

static int test_blob_write_binary_content(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "test repo creation failed");

    unsigned char binary[256];
    for (int i = 0; i < 256; i++) {
        binary[i] = (unsigned char)i;
    }

    MU_ASSERT(write_test_file(dir, "binary.bin", binary, 256) == 0,
              "writing binary file should succeed");

    char file_path[512];
    snprintf(file_path, sizeof(file_path), "%s/binary.bin", dir);

    char hex[41];
    MU_ASSERT(mygit_blob_write(dir, file_path, hex) == 0,
              "blob_write of binary should succeed");

    unsigned char *content = NULL;
    size_t content_len = 0;
    MU_ASSERT(mygit_blob_read(dir, hex, &content, &content_len) == 0,
              "blob_read of binary should succeed");
    MU_ASSERT(content_len == 256, "binary content length should be 256");
    MU_ASSERT(memcmp(content, binary, 256) == 0,
              "binary roundtrip should be byte-for-byte identical");

    free(content);
    rmrf(dir);
    return 0;
}

static int test_blob_read_rejects_non_blob(void)
{
    char tmpl[] = "/tmp/mygit_test_XXXXXX";
    char *dir = make_test_repo(tmpl);
    MU_ASSERT(dir != NULL, "test repo creation failed");

    const unsigned char fake_tree[] = "tree 0";
    size_t fake_len = 7; /* includes the NUL */
    char hex[41];

    MU_ASSERT(mygit_object_write(dir, fake_tree, fake_len, hex) == 0,
              "writing fake tree should succeed");

    unsigned char *content = NULL;
    size_t content_len = 0;
    MU_ASSERT(mygit_blob_read(dir, hex, &content, &content_len) == -1,
              "blob_read of non-blob should return -1");

    rmrf(dir);
    return 0;
}

/* ---------- main ---------- */

int main(void)
{
    fprintf(stderr, "\n=== SHA-1 tests ===\n\n");
    MU_RUN_TEST(test_sha1_known_vector);
    MU_RUN_TEST(test_sha1_hex_format);

    fprintf(stderr, "\n=== Blob hash tests ===\n\n");
    MU_RUN_TEST(test_blob_hash_known_content);
    MU_RUN_TEST(test_blob_hash_empty);

    fprintf(stderr, "\n=== Object store tests ===\n\n");
    MU_RUN_TEST(test_object_write_creates_file);
    MU_RUN_TEST(test_object_write_idempotent);
    MU_RUN_TEST(test_object_roundtrip);
    MU_RUN_TEST(test_object_read_missing);

    fprintf(stderr, "\n=== Blob write/read tests ===\n\n");
    MU_RUN_TEST(test_blob_write_and_read);
    MU_RUN_TEST(test_blob_write_empty_file);
    MU_RUN_TEST(test_blob_write_binary_content);
    MU_RUN_TEST(test_blob_read_rejects_non_blob);

    fprintf(stderr, "\n--- Results: %d/%d passed, %d failed ---\n\n",
            tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
