#ifndef MYGIT_INDEX_H
#define MYGIT_INDEX_H

#include <stddef.h>
#include <stdint.h>

#define MYGIT_INDEX_SIGNATURE   0x44495243  /* "DIRC" */
#define MYGIT_INDEX_VERSION     2
#define MYGIT_INDEX_MAX_PATH    4096

/*
 * A single index entry — represents one tracked file.
 * Fields mirror git's on-disk format (v2).
 */
typedef struct {
    uint32_t ctime_s;
    uint32_t ctime_ns;
    uint32_t mtime_s;
    uint32_t mtime_ns;
    uint32_t dev;
    uint32_t ino;
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    unsigned char sha1[20];
    uint16_t flags;             /* 12-bit name length, stage bits, etc. */
    char name[MYGIT_INDEX_MAX_PATH];
} mygit_index_entry;

/*
 * In-memory index (staging area).
 * Entries are sorted by name (memcmp order).
 */
typedef struct {
    mygit_index_entry *entries;
    size_t count;
    size_t capacity;
} mygit_index;

/*
 * Initialize an empty index.
 */
void mygit_index_init(mygit_index *idx);

/*
 * Free all entries in the index.
 */
void mygit_index_free(mygit_index *idx);

/*
 * Read the index file from .mygit/index.
 *
 * Returns  0  on success
 *          1  if index file doesn't exist (idx is left empty)
 *         -1  on error (corrupt file, checksum mismatch, etc.)
 */
int mygit_index_read(const char *repo_path, mygit_index *idx);

/*
 * Write the index to .mygit/index.
 * Entries are written sorted. A trailing SHA-1 checksum covers the entire file.
 *
 * Returns  0  on success
 *         -1  on error
 */
int mygit_index_write(const char *repo_path, const mygit_index *idx);

/*
 * Add or update an entry in the index.
 * If an entry with the same name exists, it's updated. Otherwise inserted.
 * The index is kept sorted after insertion.
 *
 * Returns  0  on success
 *         -1  on error
 */
int mygit_index_add(mygit_index *idx, const mygit_index_entry *entry);

/*
 * Remove an entry by name.
 *
 * Returns  0  if removed
 *          1  if not found
 */
int mygit_index_remove(mygit_index *idx, const char *name);

/*
 * Find an entry by name.
 *
 * Returns pointer to entry or NULL if not found.
 */
mygit_index_entry *mygit_index_find(const mygit_index *idx, const char *name);

/*
 * Serialize the current index into tree objects (recursively).
 * Creates tree objects in the object store for each directory level.
 *
 * Returns  0  on success (hex_out filled with root tree SHA-1)
 *         -1  on error
 */
int mygit_write_tree(const char *repo_path, const mygit_index *idx,
                     char hex_out[41]);

#endif /* MYGIT_INDEX_H */
