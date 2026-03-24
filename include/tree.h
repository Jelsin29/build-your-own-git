#ifndef MYGIT_TREE_H
#define MYGIT_TREE_H

#include <stddef.h>
#include <stdint.h>

#define MYGIT_MODE_FILE    0100644
#define MYGIT_MODE_EXEC    0100755
#define MYGIT_MODE_TREE    0040000
#define MYGIT_MODE_SYMLINK 0120000

typedef struct {
    uint32_t mode;
    char     name[256];
    unsigned char hash[20];
} mygit_tree_entry;

/*
 * Compute the tree SHA-1 without writing to disk.
 * Entries are sorted by name (trees append '/' for comparison).
 *
 * Returns 0 on success, -1 on error.
 */
int mygit_tree_hash(const mygit_tree_entry *entries, size_t count,
                    char hex_out[41]);

/*
 * Create a tree object from entries and store in the object store.
 * Entries are sorted internally — caller does not need to pre-sort.
 *
 * Returns 0 on success, -1 on error (errno set).
 */
int mygit_tree_write(const char *repo_path,
                     const mygit_tree_entry *entries, size_t count,
                     char hex_out[41]);

/*
 * Read a tree object from the object store and parse its entries.
 *
 * `entries` is set to a malloc'd array — CALLER MUST FREE.
 * `count` is set to the number of entries.
 *
 * Returns  0 on success
 *          1 if not found
 *         -1 on error
 */
int mygit_tree_read(const char *repo_path, const char hex[40],
                    mygit_tree_entry **entries, size_t *count);

#endif /* MYGIT_TREE_H */
