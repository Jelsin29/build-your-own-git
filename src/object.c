#define _POSIX_C_SOURCE 200809L

#include "object.h"
#include "hash.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

static int build_object_dir(char *buf, size_t len,
                            const char *repo_path, const char hex[40])
{
    int n = snprintf(buf, len, "%s/.mygit/objects/%.2s", repo_path, hex);
    if (n < 0 || (size_t)n >= len) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int build_object_path(char *buf, size_t len,
                             const char *repo_path, const char hex[40])
{
    int n = snprintf(buf, len, "%s/.mygit/objects/%.2s/%.38s",
                     repo_path, hex, hex + 2);
    if (n < 0 || (size_t)n >= len) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

int mygit_object_write(const char *repo_path,
                       const unsigned char *store, size_t store_len,
                       char hex_out[41])
{
    if (repo_path == NULL || store == NULL || hex_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    /* Hash the store buffer */
    unsigned char raw_hash[20];
    if (mygit_sha1(store, store_len, raw_hash) == -1) {
        return -1;
    }
    mygit_hex(raw_hash, hex_out);

    /* Build object path */
    char obj_dir[PATH_MAX];
    char obj_path[PATH_MAX];
    if (build_object_dir(obj_dir, sizeof(obj_dir), repo_path, hex_out) == -1) {
        return -1;
    }
    if (build_object_path(obj_path, sizeof(obj_path), repo_path, hex_out) == -1) {
        return -1;
    }

    /* Idempotent: skip if already exists */
    struct stat st;
    if (stat(obj_path, &st) == 0) {
        return 0;
    }

    /* Create prefix directory */
    if (mkdir(obj_dir, 0755) == -1 && errno != EEXIST) {
        return -1;
    }

    /* Compress with zlib */
    uLong compressed_bound = compressBound((uLong)store_len);
    unsigned char *compressed = malloc(compressed_bound);
    if (compressed == NULL) {
        return -1;
    }

    uLongf compressed_len = compressed_bound;
    int zret = compress(compressed, &compressed_len, store, (uLong)store_len);
    if (zret != Z_OK) {
        free(compressed);
        errno = EIO;
        return -1;
    }

    /* Write to disk */
    FILE *fp = fopen(obj_path, "wb");
    if (fp == NULL) {
        free(compressed);
        return -1;
    }

    size_t written = fwrite(compressed, 1, (size_t)compressed_len, fp);
    free(compressed);

    if (written != (size_t)compressed_len) {
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

int mygit_object_read(const char *repo_path,
                      const char hex[40],
                      unsigned char **out, size_t *out_len)
{
    if (repo_path == NULL || hex == NULL || out == NULL || out_len == NULL) {
        errno = EINVAL;
        return -1;
    }

    *out = NULL;
    *out_len = 0;

    /* Build object path */
    char obj_path[PATH_MAX];
    if (build_object_path(obj_path, sizeof(obj_path), repo_path, hex) == -1) {
        return -1;
    }

    /* Open and read compressed file */
    FILE *fp = fopen(obj_path, "rb");
    if (fp == NULL) {
        if (errno == ENOENT) {
            return 1;
        }
        return -1;
    }

    if (fseek(fp, 0, SEEK_END) == -1) {
        fclose(fp);
        return -1;
    }

    long file_size = ftell(fp);
    if (file_size <= 0) {
        fclose(fp);
        errno = EIO;
        return -1;
    }

    rewind(fp);

    unsigned char *file_buf = malloc((size_t)file_size);
    if (file_buf == NULL) {
        fclose(fp);
        return -1;
    }

    size_t nread = fread(file_buf, 1, (size_t)file_size, fp);
    fclose(fp);

    if (nread != (size_t)file_size) {
        free(file_buf);
        errno = EIO;
        return -1;
    }

    /* Decompress with growing buffer */
    size_t decomp_size = (size_t)file_size * 4;
    if (decomp_size < 256) {
        decomp_size = 256;
    }

    for (int attempt = 0; attempt < 5; attempt++) {
        unsigned char *decomp_buf = malloc(decomp_size);
        if (decomp_buf == NULL) {
            free(file_buf);
            return -1;
        }

        uLongf decomp_len = (uLongf)decomp_size;
        int zret = uncompress(decomp_buf, &decomp_len,
                              file_buf, (uLong)nread);

        if (zret == Z_OK) {
            free(file_buf);
            *out = decomp_buf;
            *out_len = (size_t)decomp_len;
            return 0;
        }

        free(decomp_buf);

        if (zret != Z_BUF_ERROR) {
            free(file_buf);
            errno = EIO;
            return -1;
        }

        decomp_size *= 2;
    }

    free(file_buf);
    errno = ENOMEM;
    return -1;
}
