#define _POSIX_C_SOURCE 200809L

#include "index.h"
#include "hash.h"
#include "object.h"

#include <arpa/inet.h>  /* htonl, ntohl, htons, ntohs */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* ---------- internal helpers ---------- */

static int entry_cmp(const void *a, const void *b)
{
    const mygit_index_entry *ea = (const mygit_index_entry *)a;
    const mygit_index_entry *eb = (const mygit_index_entry *)b;
    return strcmp(ea->name, eb->name);
}

static int ensure_capacity(mygit_index *idx)
{
    if (idx->count < idx->capacity) {
        return 0;
    }
    size_t new_cap = (idx->capacity == 0) ? 16 : idx->capacity * 2;
    mygit_index_entry *new_entries = realloc(idx->entries,
                                              new_cap * sizeof(mygit_index_entry));
    if (new_entries == NULL) {
        return -1;
    }
    idx->entries = new_entries;
    idx->capacity = new_cap;
    return 0;
}

/*
 * Compute the padded entry size on disk.
 * Each entry is padded to a multiple of 8 bytes.
 * Fixed fields = 62 bytes (up to and including flags).
 * Then name (NUL-terminated) + padding to 8-byte boundary.
 */
static size_t entry_disk_size(size_t name_len)
{
    /* 62 bytes of fixed fields + name + 1 NUL byte */
    size_t raw = 62 + name_len + 1;
    /* Pad to next 8-byte boundary */
    size_t padded = (raw + 7) & ~(size_t)7;
    return padded;
}

/* ---------- public API ---------- */

void mygit_index_init(mygit_index *idx)
{
    idx->entries = NULL;
    idx->count = 0;
    idx->capacity = 0;
}

void mygit_index_free(mygit_index *idx)
{
    free(idx->entries);
    idx->entries = NULL;
    idx->count = 0;
    idx->capacity = 0;
}

int mygit_index_read(const char *repo_path, mygit_index *idx)
{
    char path[PATH_MAX];
    int w = snprintf(path, sizeof(path), "%s/.mygit/index", repo_path);
    if (w < 0 || (size_t)w >= sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        if (errno == ENOENT) {
            mygit_index_init(idx);
            return 1; /* No index file yet */
        }
        return -1;
    }

    /* Read entire file into memory for checksum verification */
    if (fseek(fp, 0, SEEK_END) == -1) {
        fclose(fp);
        return -1;
    }
    long file_size = ftell(fp);
    if (file_size < 0 || file_size < 32) { /* Min: 12 header + 20 checksum */
        fclose(fp);
        errno = EINVAL;
        return -1;
    }
    rewind(fp);

    unsigned char *data = malloc((size_t)file_size);
    if (data == NULL) {
        fclose(fp);
        return -1;
    }
    if (fread(data, 1, (size_t)file_size, fp) != (size_t)file_size) {
        free(data);
        fclose(fp);
        return -1;
    }
    fclose(fp);

    /* Verify checksum: SHA-1 of everything except last 20 bytes */
    size_t content_len = (size_t)file_size - 20;
    unsigned char computed_hash[20];
    mygit_sha1(data, content_len, computed_hash);
    if (memcmp(computed_hash, data + content_len, 20) != 0) {
        free(data);
        errno = EINVAL;
        return -1;
    }

    /* Parse header */
    uint32_t sig, ver, nentries;
    memcpy(&sig, data, 4);
    memcpy(&ver, data + 4, 4);
    memcpy(&nentries, data + 8, 4);
    sig = ntohl(sig);
    ver = ntohl(ver);
    nentries = ntohl(nentries);

    if (sig != MYGIT_INDEX_SIGNATURE || ver != MYGIT_INDEX_VERSION) {
        free(data);
        errno = EINVAL;
        return -1;
    }

    mygit_index_init(idx);

    /* Parse entries */
    size_t offset = 12;
    for (uint32_t i = 0; i < nentries; i++) {
        if (offset + 62 > content_len) {
            mygit_index_free(idx);
            free(data);
            errno = EINVAL;
            return -1;
        }

        mygit_index_entry entry;
        memset(&entry, 0, sizeof(entry));

        uint32_t tmp32;
        uint16_t tmp16;

        memcpy(&tmp32, data + offset +  0, 4); entry.ctime_s  = ntohl(tmp32);
        memcpy(&tmp32, data + offset +  4, 4); entry.ctime_ns = ntohl(tmp32);
        memcpy(&tmp32, data + offset +  8, 4); entry.mtime_s  = ntohl(tmp32);
        memcpy(&tmp32, data + offset + 12, 4); entry.mtime_ns = ntohl(tmp32);
        memcpy(&tmp32, data + offset + 16, 4); entry.dev      = ntohl(tmp32);
        memcpy(&tmp32, data + offset + 20, 4); entry.ino      = ntohl(tmp32);
        memcpy(&tmp32, data + offset + 24, 4); entry.mode     = ntohl(tmp32);
        memcpy(&tmp32, data + offset + 28, 4); entry.uid      = ntohl(tmp32);
        memcpy(&tmp32, data + offset + 32, 4); entry.gid      = ntohl(tmp32);
        memcpy(&tmp32, data + offset + 36, 4); entry.size     = ntohl(tmp32);
        memcpy(entry.sha1, data + offset + 40, 20);
        memcpy(&tmp16, data + offset + 60, 2); entry.flags    = ntohs(tmp16);

        /* Extract name length from flags (lower 12 bits) */
        uint16_t name_len = entry.flags & 0x0FFF;
        if (name_len == 0x0FFF) {
            /* Extended name: use strlen */
            name_len = (uint16_t)strlen((const char *)(data + offset + 62));
        }

        if (offset + 62 + name_len >= content_len) {
            mygit_index_free(idx);
            free(data);
            errno = EINVAL;
            return -1;
        }

        if (name_len >= MYGIT_INDEX_MAX_PATH) {
            mygit_index_free(idx);
            free(data);
            errno = ENAMETOOLONG;
            return -1;
        }

        memcpy(entry.name, data + offset + 62, name_len);
        entry.name[name_len] = '\0';

        if (mygit_index_add(idx, &entry) == -1) {
            mygit_index_free(idx);
            free(data);
            return -1;
        }

        offset += entry_disk_size(name_len);
    }

    free(data);
    return 0;
}

int mygit_index_write(const char *repo_path, const mygit_index *idx)
{
    char path[PATH_MAX];
    char tmp_path[PATH_MAX];
    int w = snprintf(path, sizeof(path), "%s/.mygit/index", repo_path);
    if (w < 0 || (size_t)w >= sizeof(path)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    w = snprintf(tmp_path, sizeof(tmp_path), "%s/.mygit/index.tmp", repo_path);
    if (w < 0 || (size_t)w >= sizeof(tmp_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    /* Calculate total size */
    size_t total = 12; /* header */
    for (size_t i = 0; i < idx->count; i++) {
        total += entry_disk_size(strlen(idx->entries[i].name));
    }
    total += 20; /* trailing checksum */

    unsigned char *buf = calloc(1, total);
    if (buf == NULL) {
        return -1;
    }

    /* Write header */
    uint32_t sig = htonl(MYGIT_INDEX_SIGNATURE);
    uint32_t ver = htonl(MYGIT_INDEX_VERSION);
    uint32_t cnt = htonl((uint32_t)idx->count);
    memcpy(buf, &sig, 4);
    memcpy(buf + 4, &ver, 4);
    memcpy(buf + 8, &cnt, 4);

    /* Write entries (already sorted by mygit_index_add) */
    size_t offset = 12;
    for (size_t i = 0; i < idx->count; i++) {
        const mygit_index_entry *e = &idx->entries[i];
        size_t name_len = strlen(e->name);

        uint32_t tmp32;
        uint16_t tmp16;

        tmp32 = htonl(e->ctime_s);  memcpy(buf + offset +  0, &tmp32, 4);
        tmp32 = htonl(e->ctime_ns); memcpy(buf + offset +  4, &tmp32, 4);
        tmp32 = htonl(e->mtime_s);  memcpy(buf + offset +  8, &tmp32, 4);
        tmp32 = htonl(e->mtime_ns); memcpy(buf + offset + 12, &tmp32, 4);
        tmp32 = htonl(e->dev);      memcpy(buf + offset + 16, &tmp32, 4);
        tmp32 = htonl(e->ino);      memcpy(buf + offset + 20, &tmp32, 4);
        tmp32 = htonl(e->mode);     memcpy(buf + offset + 24, &tmp32, 4);
        tmp32 = htonl(e->uid);      memcpy(buf + offset + 28, &tmp32, 4);
        tmp32 = htonl(e->gid);      memcpy(buf + offset + 32, &tmp32, 4);
        tmp32 = htonl(e->size);     memcpy(buf + offset + 36, &tmp32, 4);
        memcpy(buf + offset + 40, e->sha1, 20);

        /* flags: lower 12 bits = name length (capped at 0xFFF) */
        uint16_t flags = (name_len < 0x0FFF) ? (uint16_t)name_len : 0x0FFF;
        /* Preserve stage bits from original entry */
        flags |= (e->flags & 0xF000);
        tmp16 = htons(flags);
        memcpy(buf + offset + 60, &tmp16, 2);

        memcpy(buf + offset + 62, e->name, name_len);
        /* Padding is already zero from calloc */

        offset += entry_disk_size(name_len);
    }

    /* Compute and write trailing checksum (SHA-1 of header + entries) */
    size_t content_len = total - 20;
    unsigned char checksum[20];
    mygit_sha1(buf, content_len, checksum);
    memcpy(buf + content_len, checksum, 20);

    /* Atomic write */
    FILE *fp = fopen(tmp_path, "wb");
    if (fp == NULL) {
        free(buf);
        return -1;
    }
    if (fwrite(buf, 1, total, fp) != total) {
        fclose(fp);
        free(buf);
        unlink(tmp_path);
        return -1;
    }
    if (fflush(fp) != 0) {
        fclose(fp);
        free(buf);
        unlink(tmp_path);
        return -1;
    }
    fclose(fp);
    free(buf);

    if (rename(tmp_path, path) == -1) {
        unlink(tmp_path);
        return -1;
    }

    return 0;
}

int mygit_index_add(mygit_index *idx, const mygit_index_entry *entry)
{
    if (idx == NULL || entry == NULL) {
        errno = EINVAL;
        return -1;
    }

    /* Check if entry with same name already exists */
    for (size_t i = 0; i < idx->count; i++) {
        if (strcmp(idx->entries[i].name, entry->name) == 0) {
            idx->entries[i] = *entry;
            return 0;
        }
    }

    /* New entry — ensure capacity and insert */
    if (ensure_capacity(idx) == -1) {
        return -1;
    }

    idx->entries[idx->count] = *entry;
    idx->count++;

    /* Keep sorted */
    qsort(idx->entries, idx->count, sizeof(mygit_index_entry), entry_cmp);
    return 0;
}

int mygit_index_remove(mygit_index *idx, const char *name)
{
    for (size_t i = 0; i < idx->count; i++) {
        if (strcmp(idx->entries[i].name, name) == 0) {
            /* Shift remaining entries down */
            memmove(&idx->entries[i], &idx->entries[i + 1],
                    (idx->count - i - 1) * sizeof(mygit_index_entry));
            idx->count--;
            return 0;
        }
    }
    return 1; /* Not found */
}

mygit_index_entry *mygit_index_find(const mygit_index *idx, const char *name)
{
    for (size_t i = 0; i < idx->count; i++) {
        if (strcmp(idx->entries[i].name, name) == 0) {
            return &idx->entries[i];
        }
    }
    return NULL;
}
