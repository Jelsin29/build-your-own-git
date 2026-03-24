#define _POSIX_C_SOURCE 200809L

#include "blob.h"
#include "hash.h"
#include "object.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int build_store(const unsigned char *content, size_t content_len,
                       unsigned char **store_out, size_t *store_len_out)
{
    char header[64];
    int hdr_len = snprintf(header, sizeof(header), "blob %zu", content_len);
    if (hdr_len < 0) {
        return -1;
    }

    /* store = header + NUL + content */
    size_t store_len = (size_t)hdr_len + 1 + content_len;
    unsigned char *store = malloc(store_len);
    if (store == NULL) {
        return -1;
    }

    memcpy(store, header, (size_t)hdr_len);
    store[hdr_len] = '\0';
    if (content_len > 0) {
        memcpy(store + hdr_len + 1, content, content_len);
    }

    *store_out = store;
    *store_len_out = store_len;
    return 0;
}

int mygit_blob_hash(const unsigned char *content, size_t content_len,
                    char hex_out[41])
{
    if (hex_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    /* Allow NULL content only when content_len == 0 (empty blob) */
    if (content == NULL && content_len > 0) {
        errno = EINVAL;
        return -1;
    }

    unsigned char *store = NULL;
    size_t store_len = 0;
    if (build_store(content, content_len, &store, &store_len) == -1) {
        return -1;
    }

    unsigned char raw_hash[20];
    int ret = mygit_sha1(store, store_len, raw_hash);
    free(store);

    if (ret == -1) {
        return -1;
    }

    mygit_hex(raw_hash, hex_out);
    return 0;
}

int mygit_blob_write(const char *repo_path, const char *file_path,
                     char hex_out[41])
{
    if (repo_path == NULL || file_path == NULL || hex_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    /* Read file */
    FILE *fp = fopen(file_path, "rb");
    if (fp == NULL) {
        return -1;
    }

    if (fseek(fp, 0, SEEK_END) == -1) {
        fclose(fp);
        return -1;
    }

    long file_size = ftell(fp);
    if (file_size < 0) {
        fclose(fp);
        return -1;
    }

    rewind(fp);

    unsigned char *content = NULL;
    size_t content_len = (size_t)file_size;

    if (content_len > 0) {
        content = malloc(content_len);
        if (content == NULL) {
            fclose(fp);
            return -1;
        }

        size_t nread = fread(content, 1, content_len, fp);
        if (nread != content_len) {
            free(content);
            fclose(fp);
            return -1;
        }
    }

    fclose(fp);

    /* Build store buffer */
    unsigned char *store = NULL;
    size_t store_len = 0;
    if (build_store(content, content_len, &store, &store_len) == -1) {
        free(content);
        return -1;
    }

    free(content);

    /* Write to object store */
    int ret = mygit_object_write(repo_path, store, store_len, hex_out);
    free(store);

    return ret;
}

int mygit_blob_read(const char *repo_path, const char hex[40],
                    unsigned char **content, size_t *content_len)
{
    if (repo_path == NULL || hex == NULL || content == NULL || content_len == NULL) {
        errno = EINVAL;
        return -1;
    }

    *content = NULL;
    *content_len = 0;

    /* Read raw object */
    unsigned char *store = NULL;
    size_t store_len = 0;
    int ret = mygit_object_read(repo_path, hex, &store, &store_len);
    if (ret != 0) {
        return ret;
    }

    /* Find NUL separator */
    unsigned char *nul = memchr(store, '\0', store_len);
    if (nul == NULL) {
        free(store);
        errno = EINVAL;
        return -1;
    }

    /* Validate header starts with "blob " */
    size_t hdr_len = (size_t)(nul - store);
    if (hdr_len < 6 || memcmp(store, "blob ", 5) != 0) {
        free(store);
        errno = EINVAL;
        return -1;
    }

    /* Extract content */
    size_t data_offset = hdr_len + 1;
    size_t data_len = store_len - data_offset;

    if (data_len > 0) {
        unsigned char *out = malloc(data_len);
        if (out == NULL) {
            free(store);
            return -1;
        }
        memcpy(out, store + data_offset, data_len);
        *content = out;
    }

    *content_len = data_len;
    free(store);
    return 0;
}
