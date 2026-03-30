#define _POSIX_C_SOURCE 200809L

#include "cmd_porcelain.h"
#include "blob.h"
#include "commit.h"
#include "config.h"
#include "hash.h"
#include "index.h"
#include "ref.h"
#include "tree_flatten.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ---------- add ---------- */

int cmd_add(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: mygit add <file>...\n");
        return EXIT_FAILURE;
    }

    mygit_index idx;
    int ret = mygit_index_read(".", &idx);
    if (ret == -1) {
        fprintf(stderr, "error: failed to read index: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    for (int i = 2; i < argc; i++) {
        const char *file_path = argv[i];

        struct stat st;
        if (stat(file_path, &st) == -1) {
            fprintf(stderr, "error: cannot stat '%s': %s\n",
                    file_path, strerror(errno));
            mygit_index_free(&idx);
            return EXIT_FAILURE;
        }

        char blob_hex[41];
        if (mygit_blob_write(".", file_path, blob_hex) == -1) {
            fprintf(stderr, "error: failed to hash '%s': %s\n",
                    file_path, strerror(errno));
            mygit_index_free(&idx);
            return EXIT_FAILURE;
        }

        mygit_index_entry entry;
        memset(&entry, 0, sizeof(entry));

        entry.ctime_s  = (uint32_t)st.st_ctim.tv_sec;
        entry.ctime_ns = (uint32_t)st.st_ctim.tv_nsec;
        entry.mtime_s  = (uint32_t)st.st_mtim.tv_sec;
        entry.mtime_ns = (uint32_t)st.st_mtim.tv_nsec;
        entry.dev      = (uint32_t)st.st_dev;
        entry.ino      = (uint32_t)st.st_ino;
        entry.mode     = (st.st_mode & 0111) ? 0100755 : 0100644;
        entry.uid      = (uint32_t)st.st_uid;
        entry.gid      = (uint32_t)st.st_gid;
        entry.size     = (uint32_t)st.st_size;

        for (int j = 0; j < 20; j++) {
            unsigned int byte;
            sscanf(blob_hex + j * 2, "%02x", &byte);
            entry.sha1[j] = (unsigned char)byte;
        }

        strncpy(entry.name, file_path, MYGIT_INDEX_MAX_PATH - 1);

        if (mygit_index_add(&idx, &entry) == -1) {
            fprintf(stderr, "error: failed to add '%s' to index\n", file_path);
            mygit_index_free(&idx);
            return EXIT_FAILURE;
        }
    }

    if (mygit_index_write(".", &idx) == -1) {
        fprintf(stderr, "error: failed to write index: %s\n", strerror(errno));
        mygit_index_free(&idx);
        return EXIT_FAILURE;
    }

    mygit_index_free(&idx);
    return EXIT_SUCCESS;
}

/* ---------- commit ---------- */

int cmd_commit_porcelain(int argc, char *argv[])
{
    const char *message = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            message = argv[++i];
        }
    }

    if (message == NULL) {
        fprintf(stderr, "usage: mygit commit -m <message>\n");
        return EXIT_FAILURE;
    }

    /* 1. Read index */
    mygit_index idx;
    int ret = mygit_index_read(".", &idx);
    if (ret == -1) {
        fprintf(stderr, "error: failed to read index: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    if (ret == 1 || idx.count == 0) {
        fprintf(stderr, "nothing to commit (empty index)\n");
        mygit_index_free(&idx);
        return EXIT_FAILURE;
    }

    /* 2. Write tree from index */
    char tree_hex[41];
    if (mygit_write_tree(".", &idx, tree_hex) == -1) {
        fprintf(stderr, "error: failed to write tree: %s\n", strerror(errno));
        mygit_index_free(&idx);
        return EXIT_FAILURE;
    }
    mygit_index_free(&idx);

    /* 3. Resolve HEAD for parent commit */
    char parent_hex[41] = {0};
    int has_parent = 0;
    ret = mygit_head_resolve(".", parent_hex);
    if (ret == 0) {
        has_parent = 1;
    } else if (ret == -1) {
        fprintf(stderr, "error: failed to resolve HEAD: %s\n",
                strerror(errno));
        return EXIT_FAILURE;
    }

    /* 4. Build commit object */
    mygit_commit commit;
    memset(&commit, 0, sizeof(commit));
    strncpy(commit.tree_hex, tree_hex, 40);
    commit.tree_hex[40] = '\0';

    if (has_parent) {
        strncpy(commit.parent_hex[0], parent_hex, 40);
        commit.parent_hex[0][40] = '\0';
        commit.parent_count = 1;
    }

    char cfg_name[128], cfg_email[128];
    if (mygit_config_get(".", "user", "name", cfg_name, sizeof(cfg_name)) != 0)
        strncpy(cfg_name, "mygit", sizeof(cfg_name));
    if (mygit_config_get(".", "user", "email", cfg_email,
                         sizeof(cfg_email)) != 0)
        strncpy(cfg_email, "mygit@local", sizeof(cfg_email));

    strncpy(commit.author_name, cfg_name, sizeof(commit.author_name));
    strncpy(commit.author_email, cfg_email, sizeof(commit.author_email));
    commit.author_time = (int64_t)time(NULL);
    strncpy(commit.author_tz, "+0000", sizeof(commit.author_tz));
    strncpy(commit.committer_name, cfg_name, sizeof(commit.committer_name));
    strncpy(commit.committer_email, cfg_email,
            sizeof(commit.committer_email));
    commit.committer_time = commit.author_time;
    strncpy(commit.committer_tz, "+0000", sizeof(commit.committer_tz));
    strncpy(commit.message, message, sizeof(commit.message) - 1);

    char commit_hex[41];
    if (mygit_commit_write(".", &commit, commit_hex) == -1) {
        fprintf(stderr, "error: failed to create commit: %s\n",
                strerror(errno));
        return EXIT_FAILURE;
    }

    /* 5. Update HEAD (or the branch HEAD points to) */
    FILE *fp = fopen("./.mygit/HEAD", "r");
    if (fp == NULL) {
        fprintf(stderr, "error: failed to read HEAD\n");
        return EXIT_FAILURE;
    }
    char head_content[256];
    if (fgets(head_content, (int)sizeof(head_content), fp) == NULL) {
        fclose(fp);
        fprintf(stderr, "error: failed to read HEAD content\n");
        return EXIT_FAILURE;
    }
    fclose(fp);

    size_t hlen = strlen(head_content);
    if (hlen > 0 && head_content[hlen - 1] == '\n')
        head_content[hlen - 1] = '\0';

    if (strncmp(head_content, "ref: ", 5) == 0) {
        const char *branch_ref = head_content + 5;
        if (mygit_ref_update(".", branch_ref, commit_hex) == -1) {
            fprintf(stderr, "error: failed to update %s\n", branch_ref);
            return EXIT_FAILURE;
        }
    } else {
        FILE *hfp = fopen("./.mygit/HEAD", "w");
        if (hfp == NULL) {
            fprintf(stderr, "error: failed to update HEAD\n");
            return EXIT_FAILURE;
        }
        fprintf(hfp, "%s\n", commit_hex);
        fclose(hfp);
    }

    if (strncmp(head_content, "ref: refs/heads/", 16) == 0) {
        fprintf(stdout, "[%s %.*s] %s\n",
                head_content + 16, 7, commit_hex, message);
    } else {
        fprintf(stdout, "[detached %.*s] %s\n", 7, commit_hex, message);
    }

    return EXIT_SUCCESS;
}

/* ---------- status ---------- */

int cmd_status(void)
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

    mygit_index idx;
    ret = mygit_index_read(".", &idx);
    if (ret == -1) {
        fprintf(stderr, "error: failed to read index\n");
        free(head_files);
        return EXIT_FAILURE;
    }
    if (ret == 1) {
        mygit_index_init(&idx);
    }

    FILE *fp = fopen("./.mygit/HEAD", "r");
    if (fp != NULL) {
        char line[256];
        if (fgets(line, (int)sizeof(line), fp) != NULL) {
            size_t len = strlen(line);
            if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
            if (strncmp(line, "ref: refs/heads/", 16) == 0) {
                fprintf(stdout, "On branch %s\n", line + 16);
            } else {
                fprintf(stdout, "HEAD detached at %.7s\n", line);
            }
        }
        fclose(fp);
    }

    int has_staged = 0;
    for (size_t i = 0; i < idx.count; i++) {
        int found = 0;
        for (size_t j = 0; j < head_count; j++) {
            if (strcmp(idx.entries[i].name, head_files[j].name) == 0) {
                found = 1;
                if (memcmp(idx.entries[i].sha1, head_files[j].sha1, 20) != 0) {
                    if (!has_staged) {
                        fprintf(stdout, "\nChanges to be committed:\n");
                        has_staged = 1;
                    }
                    fprintf(stdout, "\tmodified:   %s\n",
                            idx.entries[i].name);
                }
                break;
            }
        }
        if (!found) {
            if (!has_staged) {
                fprintf(stdout, "\nChanges to be committed:\n");
                has_staged = 1;
            }
            fprintf(stdout, "\tnew file:   %s\n", idx.entries[i].name);
        }
    }

    for (size_t j = 0; j < head_count; j++) {
        if (mygit_index_find(&idx, head_files[j].name) == NULL) {
            if (!has_staged) {
                fprintf(stdout, "\nChanges to be committed:\n");
                has_staged = 1;
            }
            fprintf(stdout, "\tdeleted:    %s\n", head_files[j].name);
        }
    }

    if (!has_staged && idx.count == 0 && head_count == 0) {
        fprintf(stdout, "\nnothing to commit (empty repository)\n");
    } else if (!has_staged) {
        fprintf(stdout, "\nnothing to commit, working tree clean\n");
    }

    mygit_index_free(&idx);
    free(head_files);
    return EXIT_SUCCESS;
}

/* ---------- checkout ---------- */

int cmd_checkout(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: mygit checkout <branch>\n");
        return EXIT_FAILURE;
    }

    const char *target = argv[2];

    char refname[MYGIT_MAX_REFNAME];
    snprintf(refname, sizeof(refname), "refs/heads/%s", target);

    char target_hex[41];
    int ret = mygit_ref_resolve(".", refname, target_hex);
    if (ret != 0) {
        fprintf(stderr, "error: pathspec '%s' did not match any branch\n",
                target);
        return EXIT_FAILURE;
    }

    mygit_commit c;
    ret = mygit_commit_read(".", target_hex, &c);
    if (ret != 0) {
        fprintf(stderr, "error: failed to read commit %s\n", target_hex);
        return EXIT_FAILURE;
    }

    flat_tree_entry *target_files = NULL;
    size_t target_count = 0, target_cap = 0;
    if (flatten_tree(".", c.tree_hex, "", &target_files,
                     &target_count, &target_cap) == -1) {
        fprintf(stderr, "error: failed to read tree\n");
        return EXIT_FAILURE;
    }

    mygit_index new_idx;
    mygit_index_init(&new_idx);

    for (size_t i = 0; i < target_count; i++) {
        mygit_index_entry entry;
        memset(&entry, 0, sizeof(entry));
        entry.mode = target_files[i].mode;
        memcpy(entry.sha1, target_files[i].sha1, 20);
        strncpy(entry.name, target_files[i].name, MYGIT_INDEX_MAX_PATH - 1);
        mygit_index_add(&new_idx, &entry);
    }

    if (mygit_index_write(".", &new_idx) == -1) {
        fprintf(stderr, "error: failed to write index\n");
        mygit_index_free(&new_idx);
        free(target_files);
        return EXIT_FAILURE;
    }

    for (size_t i = 0; i < target_count; i++) {
        char hex[41];
        mygit_hex(target_files[i].sha1, hex);

        unsigned char *content = NULL;
        size_t content_len = 0;
        if (mygit_blob_read(".", hex, &content, &content_len) != 0) {
            fprintf(stderr, "warning: failed to read blob for %s\n",
                    target_files[i].name);
            continue;
        }

        /* Create parent directories if needed */
        char dir_buf[MYGIT_INDEX_MAX_PATH];
        strncpy(dir_buf, target_files[i].name, sizeof(dir_buf) - 1);
        dir_buf[sizeof(dir_buf) - 1] = '\0';
        char *last_slash = strrchr(dir_buf, '/');
        if (last_slash != NULL) {
            *last_slash = '\0';
            for (char *p = dir_buf; *p != '\0'; p++) {
                if (*p == '/') {
                    *p = '\0';
                    mkdir(dir_buf, 0755);
                    *p = '/';
                }
            }
            mkdir(dir_buf, 0755);
        }

        FILE *fp = fopen(target_files[i].name, "wb");
        if (fp != NULL) {
            if (content_len > 0) {
                fwrite(content, 1, content_len, fp);
            }
            fclose(fp);
        }
        free(content);
    }

    mygit_index_free(&new_idx);
    free(target_files);

    FILE *head_fp = fopen("./.mygit/HEAD", "w");
    if (head_fp == NULL) {
        fprintf(stderr, "error: failed to update HEAD\n");
        return EXIT_FAILURE;
    }
    fprintf(head_fp, "ref: refs/heads/%s\n", target);
    fclose(head_fp);

    fprintf(stdout, "Switched to branch '%s'\n", target);
    return EXIT_SUCCESS;
}
