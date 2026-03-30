#include "index.h"
#include "tree.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Build tree objects from a subset of sorted index entries that share
 * a common directory prefix. Handles nested directories recursively.
 *
 * start/end define the range of entries in idx to process.
 * prefix is the current directory path (e.g., "" for root, "src/" for src dir).
 */
static int build_tree(const char *repo_path, const mygit_index *idx,
                      size_t start, size_t end, const char *prefix,
                      char hex_out[41])
{
    size_t prefix_len = strlen(prefix);

    mygit_tree_entry *tree_entries = NULL;
    size_t tree_count = 0;
    size_t tree_cap = 0;

    size_t i = start;
    while (i < end) {
        const char *name = idx->entries[i].name + prefix_len;

        const char *slash = strchr(name, '/');

        if (slash == NULL) {
            /* File at the current level */
            if (tree_count >= tree_cap) {
                tree_cap = (tree_cap == 0) ? 16 : tree_cap * 2;
                mygit_tree_entry *new_te = realloc(tree_entries,
                    tree_cap * sizeof(mygit_tree_entry));
                if (new_te == NULL) {
                    free(tree_entries);
                    return -1;
                }
                tree_entries = new_te;
            }

            mygit_tree_entry *te = &tree_entries[tree_count];
            memset(te, 0, sizeof(*te));
            te->mode = idx->entries[i].mode;
            strncpy(te->name, name, sizeof(te->name) - 1);
            memcpy(te->hash, idx->entries[i].sha1, 20);
            tree_count++;
            i++;
        } else {
            /* Subdirectory */
            size_t dir_name_len = (size_t)(slash - name);
            char subdir[MYGIT_INDEX_MAX_PATH];
            snprintf(subdir, sizeof(subdir), "%.*s",
                     (int)dir_name_len, name);

            char sub_prefix[MYGIT_INDEX_MAX_PATH + 1];
            snprintf(sub_prefix, sizeof(sub_prefix), "%s%s/",
                     prefix, subdir);
            size_t sub_prefix_len = strlen(sub_prefix);

            size_t sub_end = i;
            while (sub_end < end &&
                   strncmp(idx->entries[sub_end].name, sub_prefix,
                           sub_prefix_len) == 0) {
                sub_end++;
            }

            char subtree_hex[41];
            if (build_tree(repo_path, idx, i, sub_end, sub_prefix,
                           subtree_hex) == -1) {
                free(tree_entries);
                return -1;
            }

            if (tree_count >= tree_cap) {
                tree_cap = (tree_cap == 0) ? 16 : tree_cap * 2;
                mygit_tree_entry *new_te = realloc(tree_entries,
                    tree_cap * sizeof(mygit_tree_entry));
                if (new_te == NULL) {
                    free(tree_entries);
                    return -1;
                }
                tree_entries = new_te;
            }

            mygit_tree_entry *te = &tree_entries[tree_count];
            memset(te, 0, sizeof(*te));
            te->mode = MYGIT_MODE_TREE;
            strncpy(te->name, subdir, sizeof(te->name) - 1);

            for (int j = 0; j < 20; j++) {
                unsigned int byte;
                sscanf(subtree_hex + j * 2, "%02x", &byte);
                te->hash[j] = (unsigned char)byte;
            }
            tree_count++;
            i = sub_end;
        }
    }

    int ret = mygit_tree_write(repo_path, tree_entries, tree_count, hex_out);
    free(tree_entries);
    return ret;
}

int mygit_write_tree(const char *repo_path, const mygit_index *idx,
                     char hex_out[41])
{
    if (repo_path == NULL || idx == NULL || hex_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (idx->count == 0) {
        return mygit_tree_write(repo_path, NULL, 0, hex_out);
    }

    return build_tree(repo_path, idx, 0, idx->count, "", hex_out);
}
