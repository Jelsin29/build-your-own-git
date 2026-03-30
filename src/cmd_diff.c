#include "cmd_diff.h"
#include "blob.h"
#include "commit.h"
#include "diff.h"
#include "hash.h"
#include "index.h"
#include "ref.h"
#include "tree_flatten.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Diff a single file: read old content from blob, compare against new text.
 */
static void diff_blobs(const char *path,
                       const char *old_hex, const unsigned char *new_data,
                       size_t new_len)
{
    unsigned char *old_data = NULL;
    size_t old_len = 0;

    if (old_hex != NULL) {
        if (mygit_blob_read(".", old_hex, &old_data, &old_len) != 0) {
            old_len = 0;
        }
    }

    const char **a_lines = NULL, **b_lines = NULL;
    size_t *a_lens = NULL, *b_lens = NULL;
    size_t a_count = mygit_diff_split_lines((const char *)old_data, old_len,
                                            &a_lines, &a_lens);
    size_t b_count = mygit_diff_split_lines((const char *)new_data, new_len,
                                            &b_lines, &b_lens);

    if (a_count == (size_t)-1 || b_count == (size_t)-1) {
        free(old_data);
        free(a_lines); free(a_lens);
        free(b_lines); free(b_lens);
        return;
    }

    mygit_diff_result result;
    if (mygit_diff_myers(a_lines, a_lens, a_count,
                         b_lines, b_lens, b_count, &result) == 0) {
        char old_name[MYGIT_INDEX_MAX_PATH + 3];
        char new_name[MYGIT_INDEX_MAX_PATH + 3];
        snprintf(old_name, sizeof(old_name), "a/%s", path);
        snprintf(new_name, sizeof(new_name), "b/%s", path);
        mygit_diff_print_unified(&result, old_name, new_name, 3);
        mygit_diff_free(&result);
    }

    free(old_data);
    free(a_lines); free(a_lens);
    free(b_lines); free(b_lens);
}

/* ---------- diff --cached: index vs HEAD ---------- */

static int diff_cached(const mygit_index *idx)
{
    char head_hex[41];
    flat_tree_entry *head_files = NULL;
    size_t head_count = 0, head_cap = 0;

    int ret = mygit_head_resolve(".", head_hex);
    if (ret == 0) {
        mygit_commit c;
        if (mygit_commit_read(".", head_hex, &c) == 0) {
            flatten_tree(".", c.tree_hex, "", &head_files,
                         &head_count, &head_cap);
        }
    }

    for (size_t i = 0; i < idx->count; i++) {
        char idx_hex[41];
        mygit_hex(idx->entries[i].sha1, idx_hex);

        /* Find in HEAD */
        int found_in_head = 0;
        for (size_t j = 0; j < head_count; j++) {
            if (strcmp(idx->entries[i].name, head_files[j].name) == 0) {
                found_in_head = 1;
                if (memcmp(idx->entries[i].sha1, head_files[j].sha1,
                           20) != 0) {
                    char old_hex[41];
                    mygit_hex(head_files[j].sha1, old_hex);

                    unsigned char *new_data = NULL;
                    size_t new_len = 0;
                    if (mygit_blob_read(".", idx_hex, &new_data,
                                        &new_len) == 0) {
                        diff_blobs(idx->entries[i].name, old_hex,
                                   new_data, new_len);
                        free(new_data);
                    }
                }
                break;
            }
        }

        /* New file (in index, not in HEAD) */
        if (!found_in_head) {
            unsigned char *new_data = NULL;
            size_t new_len = 0;
            if (mygit_blob_read(".", idx_hex, &new_data, &new_len) == 0) {
                diff_blobs(idx->entries[i].name, NULL, new_data, new_len);
                free(new_data);
            }
        }
    }

    free(head_files);
    return EXIT_SUCCESS;
}

/* ---------- diff: working tree vs index ---------- */

static int diff_worktree(const mygit_index *idx)
{
    for (size_t i = 0; i < idx->count; i++) {
        char idx_hex[41];
        mygit_hex(idx->entries[i].sha1, idx_hex);

        FILE *fp = fopen(idx->entries[i].name, "rb");
        if (fp == NULL) continue;

        fseek(fp, 0, SEEK_END);
        long fsize = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        unsigned char *wt_data = NULL;
        size_t wt_len = 0;
        if (fsize > 0) {
            wt_data = malloc((size_t)fsize);
            if (wt_data != NULL) {
                wt_len = fread(wt_data, 1, (size_t)fsize, fp);
            }
        }
        fclose(fp);

        char wt_hex[41];
        if (mygit_blob_hash(wt_data, wt_len, wt_hex) == 0) {
            if (strcmp(wt_hex, idx_hex) != 0) {
                diff_blobs(idx->entries[i].name, idx_hex, wt_data, wt_len);
            }
        }

        free(wt_data);
    }

    return EXIT_SUCCESS;
}

/* ---------- entry point ---------- */

int cmd_diff(int argc, char *argv[])
{
    int cached = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--cached") == 0 ||
            strcmp(argv[i], "--staged") == 0) {
            cached = 1;
        }
    }

    mygit_index idx;
    int ret = mygit_index_read(".", &idx);
    if (ret == -1) {
        fprintf(stderr, "error: failed to read index\n");
        return EXIT_FAILURE;
    }
    if (ret == 1) {
        mygit_index_init(&idx);
    }

    int result;
    if (cached) {
        result = diff_cached(&idx);
    } else {
        result = diff_worktree(&idx);
    }

    mygit_index_free(&idx);
    return result;
}
