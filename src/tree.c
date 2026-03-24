#define _POSIX_C_SOURCE 200809L

#include "tree.h"
#include "hash.h"
#include "object.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *mode_to_str(uint32_t mode)
{
    switch (mode) {
    case MYGIT_MODE_FILE:    return "100644";
    case MYGIT_MODE_EXEC:    return "100755";
    case MYGIT_MODE_TREE:    return "40000";
    case MYGIT_MODE_SYMLINK: return "120000";
    default:                 return NULL;
    }
}

static uint32_t str_to_mode(const char *s, size_t len)
{
    if (len == 6 && memcmp(s, "100644", 6) == 0) return MYGIT_MODE_FILE;
    if (len == 6 && memcmp(s, "100755", 6) == 0) return MYGIT_MODE_EXEC;
    if (len == 5 && memcmp(s, "40000", 5) == 0)  return MYGIT_MODE_TREE;
    if (len == 6 && memcmp(s, "120000", 6) == 0) return MYGIT_MODE_SYMLINK;
    return 0;
}

static int entry_compare(const void *a, const void *b)
{
    const mygit_tree_entry *ea = (const mygit_tree_entry *)a;
    const mygit_tree_entry *eb = (const mygit_tree_entry *)b;

    /*
     * Git sorts tree entries by name, but appends '/' to directory names
     * for comparison purposes. This ensures "foo" (blob) sorts differently
     * from "foo" (tree) — though in practice names must be unique.
     */
    size_t len_a = strlen(ea->name);
    size_t len_b = strlen(eb->name);
    int is_tree_a = (ea->mode == MYGIT_MODE_TREE);
    int is_tree_b = (eb->mode == MYGIT_MODE_TREE);

    /* Build sort keys: name + optional '/' */
    size_t slen_a = len_a + (is_tree_a ? 1 : 0);
    size_t slen_b = len_b + (is_tree_b ? 1 : 0);
    size_t min_len = (slen_a < slen_b) ? slen_a : slen_b;

    for (size_t i = 0; i < min_len; i++) {
        unsigned char ca = (i < len_a) ? (unsigned char)ea->name[i] : '/';
        unsigned char cb = (i < len_b) ? (unsigned char)eb->name[i] : '/';
        if (ca != cb) {
            return (ca < cb) ? -1 : 1;
        }
    }

    if (slen_a != slen_b) {
        return (slen_a < slen_b) ? -1 : 1;
    }

    return 0;
}

static int build_body(const mygit_tree_entry *entries, size_t count,
                      unsigned char **body_out, size_t *body_len_out)
{
    /* Sort entries (work on a copy to keep const correctness) */
    mygit_tree_entry *sorted = NULL;
    if (count > 0) {
        sorted = malloc(count * sizeof(mygit_tree_entry));
        if (sorted == NULL) {
            return -1;
        }
        memcpy(sorted, entries, count * sizeof(mygit_tree_entry));
        qsort(sorted, count, sizeof(mygit_tree_entry), entry_compare);
    }

    /* Calculate total body size */
    size_t body_len = 0;
    for (size_t i = 0; i < count; i++) {
        const char *mode_str = mode_to_str(sorted[i].mode);
        if (mode_str == NULL) {
            free(sorted);
            errno = EINVAL;
            return -1;
        }
        /* "<mode> <name>\0<20-byte-hash>" */
        body_len += strlen(mode_str) + 1 + strlen(sorted[i].name) + 1 + 20;
    }

    unsigned char *body = malloc(body_len > 0 ? body_len : 1);
    if (body == NULL) {
        free(sorted);
        return -1;
    }

    /* Serialize entries */
    size_t offset = 0;
    for (size_t i = 0; i < count; i++) {
        const char *mode_str = mode_to_str(sorted[i].mode);
        size_t mode_len = strlen(mode_str);
        size_t name_len = strlen(sorted[i].name);

        memcpy(body + offset, mode_str, mode_len);
        offset += mode_len;

        body[offset++] = ' ';

        memcpy(body + offset, sorted[i].name, name_len);
        offset += name_len;

        body[offset++] = '\0';

        memcpy(body + offset, sorted[i].hash, 20);
        offset += 20;
    }

    free(sorted);
    *body_out = body;
    *body_len_out = body_len;
    return 0;
}

static int build_store(const mygit_tree_entry *entries, size_t count,
                       unsigned char **store_out, size_t *store_len_out)
{
    unsigned char *body = NULL;
    size_t body_len = 0;
    if (build_body(entries, count, &body, &body_len) == -1) {
        return -1;
    }

    /* Header: "tree <body_len>\0" */
    char header[64];
    int hdr_len = snprintf(header, sizeof(header), "tree %zu", body_len);
    if (hdr_len < 0) {
        free(body);
        return -1;
    }

    size_t store_len = (size_t)hdr_len + 1 + body_len;
    unsigned char *store = malloc(store_len);
    if (store == NULL) {
        free(body);
        return -1;
    }

    memcpy(store, header, (size_t)hdr_len);
    store[hdr_len] = '\0';
    if (body_len > 0) {
        memcpy(store + hdr_len + 1, body, body_len);
    }

    free(body);
    *store_out = store;
    *store_len_out = store_len;
    return 0;
}

int mygit_tree_hash(const mygit_tree_entry *entries, size_t count,
                    char hex_out[41])
{
    if (hex_out == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (entries == NULL && count > 0) {
        errno = EINVAL;
        return -1;
    }

    unsigned char *store = NULL;
    size_t store_len = 0;
    if (build_store(entries, count, &store, &store_len) == -1) {
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

int mygit_tree_write(const char *repo_path,
                     const mygit_tree_entry *entries, size_t count,
                     char hex_out[41])
{
    if (repo_path == NULL || hex_out == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (entries == NULL && count > 0) {
        errno = EINVAL;
        return -1;
    }

    unsigned char *store = NULL;
    size_t store_len = 0;
    if (build_store(entries, count, &store, &store_len) == -1) {
        return -1;
    }

    int ret = mygit_object_write(repo_path, store, store_len, hex_out);
    free(store);
    return ret;
}

int mygit_tree_read(const char *repo_path, const char hex[40],
                    mygit_tree_entry **entries, size_t *count)
{
    if (repo_path == NULL || hex == NULL || entries == NULL || count == NULL) {
        errno = EINVAL;
        return -1;
    }

    *entries = NULL;
    *count = 0;

    /* Read raw object */
    unsigned char *store = NULL;
    size_t store_len = 0;
    int ret = mygit_object_read(repo_path, hex, &store, &store_len);
    if (ret != 0) {
        return ret;
    }

    /* Find NUL after header */
    unsigned char *nul = memchr(store, '\0', store_len);
    if (nul == NULL) {
        free(store);
        errno = EINVAL;
        return -1;
    }

    /* Validate "tree " header */
    size_t hdr_len = (size_t)(nul - store);
    if (hdr_len < 6 || memcmp(store, "tree ", 5) != 0) {
        free(store);
        errno = EINVAL;
        return -1;
    }

    /* Parse body */
    size_t body_offset = hdr_len + 1;
    size_t body_len = store_len - body_offset;
    unsigned char *body = store + body_offset;

    /* First pass: count entries */
    size_t entry_count = 0;
    size_t pos = 0;
    while (pos < body_len) {
        /* Find space after mode */
        unsigned char *sp = memchr(body + pos, ' ', body_len - pos);
        if (sp == NULL) {
            free(store);
            errno = EINVAL;
            return -1;
        }

        /* Find NUL after name */
        size_t after_sp = (size_t)(sp - body) + 1;
        unsigned char *name_end = memchr(body + after_sp, '\0',
                                         body_len - after_sp);
        if (name_end == NULL) {
            free(store);
            errno = EINVAL;
            return -1;
        }

        /* Skip 20-byte hash */
        size_t hash_start = (size_t)(name_end - body) + 1;
        if (hash_start + 20 > body_len) {
            free(store);
            errno = EINVAL;
            return -1;
        }

        entry_count++;
        pos = hash_start + 20;
    }

    if (entry_count == 0) {
        free(store);
        return 0;
    }

    /* Allocate entries */
    mygit_tree_entry *result = calloc(entry_count, sizeof(mygit_tree_entry));
    if (result == NULL) {
        free(store);
        return -1;
    }

    /* Second pass: parse entries */
    pos = 0;
    for (size_t i = 0; i < entry_count; i++) {
        /* Mode: bytes up to space */
        unsigned char *sp = memchr(body + pos, ' ', body_len - pos);
        size_t mode_len = (size_t)(sp - (body + pos));
        result[i].mode = str_to_mode((const char *)(body + pos), mode_len);
        if (result[i].mode == 0) {
            free(result);
            free(store);
            errno = EINVAL;
            return -1;
        }

        /* Name: bytes after space until NUL */
        size_t name_start = (size_t)(sp - body) + 1;
        unsigned char *name_end = memchr(body + name_start, '\0',
                                         body_len - name_start);
        size_t name_len = (size_t)(name_end - (body + name_start));

        if (name_len >= sizeof(result[i].name)) {
            free(result);
            free(store);
            errno = ENAMETOOLONG;
            return -1;
        }

        memcpy(result[i].name, body + name_start, name_len);
        result[i].name[name_len] = '\0';

        /* Hash: 20 bytes after NUL */
        size_t hash_start = (size_t)(name_end - body) + 1;
        memcpy(result[i].hash, body + hash_start, 20);

        pos = hash_start + 20;
    }

    free(store);
    *entries = result;
    *count = entry_count;
    return 0;
}
