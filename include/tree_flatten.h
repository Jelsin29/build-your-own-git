#ifndef MYGIT_TREE_FLATTEN_H
#define MYGIT_TREE_FLATTEN_H

#include "index.h" /* MYGIT_INDEX_MAX_PATH */

#include <stddef.h>
#include <stdint.h>

/*
 * Flattened tree entry — a single file path with its SHA-1 and mode.
 * Used to compare HEAD tree vs index or to rebuild the working tree.
 */
typedef struct {
    char name[MYGIT_INDEX_MAX_PATH];
    unsigned char sha1[20];
    uint32_t mode;
} flat_tree_entry;

/*
 * Recursively flatten a tree into a sorted list of path → sha1 pairs.
 *
 * repo_path: path to repository root
 * tree_hex:  40-char hex SHA of the tree to flatten
 * prefix:    current path prefix (use "" for root)
 * out:       pointer to output array (realloc'd as needed)
 * out_count: pointer to current entry count
 * out_cap:   pointer to current array capacity
 *
 * Returns 0 on success, -1 on error.
 */
int flatten_tree(const char *repo_path, const char *tree_hex,
                 const char *prefix,
                 flat_tree_entry **out, size_t *out_count,
                 size_t *out_cap);

#endif /* MYGIT_TREE_FLATTEN_H */
