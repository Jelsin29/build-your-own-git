#include "tree_flatten.h"
#include "hash.h"
#include "tree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int flatten_tree(const char *repo_path, const char *tree_hex,
                 const char *prefix,
                 flat_tree_entry **out, size_t *out_count,
                 size_t *out_cap)
{
    mygit_tree_entry *entries = NULL;
    size_t count = 0;
    if (mygit_tree_read(repo_path, tree_hex, &entries, &count) != 0) {
        return -1;
    }

    for (size_t i = 0; i < count; i++) {
        char full_name[MYGIT_INDEX_MAX_PATH];
        if (prefix[0] != '\0') {
            snprintf(full_name, sizeof(full_name), "%s%s", prefix,
                     entries[i].name);
        } else {
            strncpy(full_name, entries[i].name, sizeof(full_name) - 1);
            full_name[sizeof(full_name) - 1] = '\0';
        }

        if (entries[i].mode == MYGIT_MODE_TREE) {
            char sub_hex[41];
            mygit_hex(entries[i].hash, sub_hex);
            char sub_prefix[MYGIT_INDEX_MAX_PATH + 1];
            snprintf(sub_prefix, sizeof(sub_prefix), "%s/", full_name);
            if (flatten_tree(repo_path, sub_hex, sub_prefix,
                             out, out_count, out_cap) == -1) {
                free(entries);
                return -1;
            }
        } else {
            if (*out_count >= *out_cap) {
                size_t new_cap = (*out_cap == 0) ? 32 : *out_cap * 2;
                flat_tree_entry *new_out = realloc(*out,
                    new_cap * sizeof(flat_tree_entry));
                if (new_out == NULL) {
                    free(entries);
                    return -1;
                }
                *out = new_out;
                *out_cap = new_cap;
            }
            flat_tree_entry *fe = &(*out)[*out_count];
            memset(fe, 0, sizeof(*fe));
            strncpy(fe->name, full_name, sizeof(fe->name) - 1);
            memcpy(fe->sha1, entries[i].hash, 20);
            fe->mode = entries[i].mode;
            (*out_count)++;
        }
    }

    free(entries);
    return 0;
}
