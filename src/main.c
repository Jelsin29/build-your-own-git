#define _POSIX_C_SOURCE 200809L

#include "blob.h"
#include "commit.h"
#include "config.h"
#include "hash.h"
#include "index.h"
#include "object.h"
#include "ref.h"
#include "repo.h"
#include "tree.h"
#include "tui.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int cmd_init(int argc, char *argv[])
{
    const char *path = (argc >= 3) ? argv[2] : ".";
    int result = mygit_init(path);

    switch (result) {
    case 0:
        fprintf(stdout, "Initialized empty mygit repository in %s/.mygit/\n", path);
        return EXIT_SUCCESS;
    case 1:
        fprintf(stdout, "Reinitialized existing mygit repository in %s/.mygit/\n", path);
        return EXIT_SUCCESS;
    default:
        fprintf(stderr, "error: failed to initialize repository: %s\n",
                strerror(errno));
        return EXIT_FAILURE;
    }
}

static int cmd_hash_object(int argc, char *argv[])
{
    int write_flag = 0;
    const char *file_path = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-w") == 0) {
            write_flag = 1;
        } else {
            file_path = argv[i];
        }
    }

    if (file_path == NULL) {
        fprintf(stderr, "usage: mygit hash-object [-w] <file>\n");
        return EXIT_FAILURE;
    }

    char hex[41];
    if (write_flag) {
        if (mygit_blob_write(".", file_path, hex) == -1) {
            fprintf(stderr, "error: failed to hash object: %s\n",
                    strerror(errno));
            return EXIT_FAILURE;
        }
    } else {
        /* Read file and compute hash without writing */
        FILE *fp = fopen(file_path, "rb");
        if (fp == NULL) {
            fprintf(stderr, "error: cannot open '%s': %s\n",
                    file_path, strerror(errno));
            return EXIT_FAILURE;
        }

        if (fseek(fp, 0, SEEK_END) == -1) {
            fclose(fp);
            fprintf(stderr, "error: cannot read '%s': %s\n",
                    file_path, strerror(errno));
            return EXIT_FAILURE;
        }

        long file_size = ftell(fp);
        if (file_size < 0) {
            fclose(fp);
            return EXIT_FAILURE;
        }

        rewind(fp);

        unsigned char *content = NULL;
        size_t content_len = (size_t)file_size;

        if (content_len > 0) {
            content = malloc(content_len);
            if (content == NULL) {
                fclose(fp);
                return EXIT_FAILURE;
            }
            if (fread(content, 1, content_len, fp) != content_len) {
                free(content);
                fclose(fp);
                return EXIT_FAILURE;
            }
        }

        fclose(fp);

        int ret = mygit_blob_hash(content, content_len, hex);
        free(content);

        if (ret == -1) {
            fprintf(stderr, "error: failed to hash object\n");
            return EXIT_FAILURE;
        }
    }

    fprintf(stdout, "%s\n", hex);
    return EXIT_SUCCESS;
}

static const char *detect_object_type(const unsigned char *store, size_t store_len)
{
    if (store_len >= 5 && memcmp(store, "blob ", 5) == 0) return "blob";
    if (store_len >= 5 && memcmp(store, "tree ", 5) == 0) return "tree";
    if (store_len >= 7 && memcmp(store, "commit ", 7) == 0) return "commit";
    return NULL;
}

static int print_tree_entries(const char *hex)
{
    mygit_tree_entry *entries = NULL;
    size_t count = 0;
    int ret = mygit_tree_read(".", hex, &entries, &count);
    if (ret != 0) {
        return ret;
    }

    char entry_hex[41];
    for (size_t i = 0; i < count; i++) {
        mygit_hex(entries[i].hash, entry_hex);
        const char *type = (entries[i].mode == MYGIT_MODE_TREE)
                               ? "tree" : "blob";
        fprintf(stdout, "%06o %s %s\t%s\n",
                entries[i].mode, type, entry_hex, entries[i].name);
    }

    free(entries);
    return 0;
}

static int cmd_cat_file(int argc, char *argv[])
{
    if (argc < 4) {
        fprintf(stderr, "usage: mygit cat-file <-p|-t|-s> <hash>\n");
        return EXIT_FAILURE;
    }

    const char *flag = argv[2];
    const char *hex = argv[3];

    if (strlen(hex) != 40) {
        fprintf(stderr, "error: not a valid object name '%s'\n", hex);
        return EXIT_FAILURE;
    }

    /* Read raw object to detect type */
    unsigned char *store = NULL;
    size_t store_len = 0;
    int ret = mygit_object_read(".", hex, &store, &store_len);
    if (ret == 1) {
        fprintf(stderr, "fatal: Not a valid object name %s\n", hex);
        return EXIT_FAILURE;
    }
    if (ret == -1) {
        fprintf(stderr, "error: failed to read object: %s\n",
                strerror(errno));
        return EXIT_FAILURE;
    }

    const char *type = detect_object_type(store, store_len);
    free(store);

    if (type == NULL) {
        fprintf(stderr, "error: unknown object type\n");
        return EXIT_FAILURE;
    }

    if (strcmp(flag, "-t") == 0) {
        fprintf(stdout, "%s\n", type);
        return EXIT_SUCCESS;
    }

    if (strcmp(flag, "-p") == 0) {
        if (strcmp(type, "blob") == 0) {
            unsigned char *content = NULL;
            size_t content_len = 0;
            ret = mygit_blob_read(".", hex, &content, &content_len);
            if (ret != 0) {
                fprintf(stderr, "error: failed to read blob\n");
                return EXIT_FAILURE;
            }
            if (content_len > 0) {
                fwrite(content, 1, content_len, stdout);
            }
            free(content);
            return EXIT_SUCCESS;
        }

        if (strcmp(type, "tree") == 0) {
            ret = print_tree_entries(hex);
            if (ret != 0) {
                fprintf(stderr, "error: failed to read tree\n");
                return EXIT_FAILURE;
            }
            return EXIT_SUCCESS;
        }

        if (strcmp(type, "commit") == 0) {
            mygit_commit c;
            ret = mygit_commit_read(".", hex, &c);
            if (ret != 0) {
                fprintf(stderr, "error: failed to read commit\n");
                return EXIT_FAILURE;
            }
            fprintf(stdout, "tree %s\n", c.tree_hex);
            for (size_t i = 0; i < c.parent_count; i++) {
                fprintf(stdout, "parent %s\n", c.parent_hex[i]);
            }
            fprintf(stdout, "author %s <%s> %" PRId64 " %s\n",
                    c.author_name, c.author_email,
                    c.author_time, c.author_tz);
            fprintf(stdout, "committer %s <%s> %" PRId64 " %s\n",
                    c.committer_name, c.committer_email,
                    c.committer_time, c.committer_tz);
            fprintf(stdout, "\n%s\n", c.message);
            return EXIT_SUCCESS;
        }

        fprintf(stderr, "error: cat-file -p not implemented for '%s'\n", type);
        return EXIT_FAILURE;
    }

    fprintf(stderr, "error: unsupported flag '%s'\n", flag);
    return EXIT_FAILURE;
}

static int cmd_ls_tree(int argc, char *argv[])
{
    int name_only = 0;
    const char *hex = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--name-only") == 0) {
            name_only = 1;
        } else {
            hex = argv[i];
        }
    }

    if (hex == NULL || strlen(hex) != 40) {
        fprintf(stderr, "usage: mygit ls-tree [--name-only] <tree-sha>\n");
        return EXIT_FAILURE;
    }

    mygit_tree_entry *entries = NULL;
    size_t count = 0;
    int ret = mygit_tree_read(".", hex, &entries, &count);

    if (ret == 1) {
        fprintf(stderr, "fatal: not a valid object name %s\n", hex);
        return EXIT_FAILURE;
    }
    if (ret == -1) {
        fprintf(stderr, "error: failed to read tree: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    char entry_hex[41];
    for (size_t i = 0; i < count; i++) {
        if (name_only) {
            fprintf(stdout, "%s\n", entries[i].name);
        } else {
            mygit_hex(entries[i].hash, entry_hex);
            const char *type = (entries[i].mode == MYGIT_MODE_TREE)
                                   ? "tree" : "blob";
            fprintf(stdout, "%06o %s %s\t%s\n",
                    entries[i].mode, type, entry_hex, entries[i].name);
        }
    }

    free(entries);
    return EXIT_SUCCESS;
}

static int cmd_commit_tree(int argc, char *argv[])
{
    const char *tree_hex = NULL;
    const char *parent_hex = NULL;
    const char *message = NULL;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            parent_hex = argv[++i];
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            message = argv[++i];
        } else {
            tree_hex = argv[i];
        }
    }

    if (tree_hex == NULL || message == NULL) {
        fprintf(stderr,
                "usage: mygit commit-tree <tree-sha> [-p <parent>] -m <message>\n");
        return EXIT_FAILURE;
    }

    if (strlen(tree_hex) != 40) {
        fprintf(stderr, "error: not a valid tree hash '%s'\n", tree_hex);
        return EXIT_FAILURE;
    }

    mygit_commit commit;
    memset(&commit, 0, sizeof(commit));
    strncpy(commit.tree_hex, tree_hex, 40);
    commit.tree_hex[40] = '\0';

    if (parent_hex != NULL) {
        if (strlen(parent_hex) != 40) {
            fprintf(stderr, "error: not a valid parent hash '%s'\n",
                    parent_hex);
            return EXIT_FAILURE;
        }
        strncpy(commit.parent_hex[0], parent_hex, 40);
        commit.parent_hex[0][40] = '\0';
        commit.parent_count = 1;
    }

    /* Try to read user identity from config, fall back to defaults */
    char cfg_name[128];
    char cfg_email[128];
    if (mygit_config_get(".", "user", "name", cfg_name, sizeof(cfg_name)) != 0)
        strncpy(cfg_name, "mygit", sizeof(cfg_name));
    if (mygit_config_get(".", "user", "email", cfg_email, sizeof(cfg_email)) != 0)
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

    char hex[41];
    if (mygit_commit_write(".", &commit, hex) == -1) {
        fprintf(stderr, "error: failed to create commit: %s\n",
                strerror(errno));
        return EXIT_FAILURE;
    }

    fprintf(stdout, "%s\n", hex);
    return EXIT_SUCCESS;
}

static int cmd_log(int argc, char *argv[])
{
    char current[41];

    if (argc >= 3) {
        const char *hex = argv[2];
        if (strlen(hex) != 40) {
            fprintf(stderr, "error: not a valid commit hash '%s'\n", hex);
            return EXIT_FAILURE;
        }
        strncpy(current, hex, 41);
    } else {
        /* No argument — resolve HEAD */
        int ret = mygit_head_resolve(".", current);
        if (ret == 1) {
            fprintf(stderr,
                    "fatal: your current branch has no commits yet\n");
            return EXIT_FAILURE;
        }
        if (ret == -1) {
            fprintf(stderr, "error: failed to resolve HEAD: %s\n",
                    strerror(errno));
            return EXIT_FAILURE;
        }
    }

    while (current[0] != '\0') {
        mygit_commit commit;
        int ret = mygit_commit_read(".", current, &commit);
        if (ret == 1) {
            fprintf(stderr, "fatal: bad object %s\n", current);
            return EXIT_FAILURE;
        }
        if (ret == -1) {
            fprintf(stderr, "error: failed to read commit: %s\n",
                    strerror(errno));
            return EXIT_FAILURE;
        }

        fprintf(stdout, "commit %s\n", current);
        fprintf(stdout, "Author: %s <%s>\n",
                commit.author_name, commit.author_email);

        /* Format timestamp */
        time_t ts = (time_t)commit.author_time;
        struct tm *tm_info = gmtime(&ts);
        char date_buf[64];
        strftime(date_buf, sizeof(date_buf), "%c %z", tm_info);
        fprintf(stdout, "Date:   %s\n", date_buf);
        fprintf(stdout, "\n    %s\n\n", commit.message);

        if (commit.parent_count > 0) {
            strncpy(current, commit.parent_hex[0], 41);
        } else {
            break;
        }
    }

    return EXIT_SUCCESS;
}

static int cmd_add(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: mygit add <file>...\n");
        return EXIT_FAILURE;
    }

    /* Read existing index (or start empty) */
    mygit_index idx;
    int ret = mygit_index_read(".", &idx);
    if (ret == -1) {
        fprintf(stderr, "error: failed to read index: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }

    for (int i = 2; i < argc; i++) {
        const char *file_path = argv[i];

        /* Stat the file for metadata */
        struct stat st;
        if (stat(file_path, &st) == -1) {
            fprintf(stderr, "error: cannot stat '%s': %s\n",
                    file_path, strerror(errno));
            mygit_index_free(&idx);
            return EXIT_FAILURE;
        }

        /* Create blob object */
        char blob_hex[41];
        if (mygit_blob_write(".", file_path, blob_hex) == -1) {
            fprintf(stderr, "error: failed to hash '%s': %s\n",
                    file_path, strerror(errno));
            mygit_index_free(&idx);
            return EXIT_FAILURE;
        }

        /* Build index entry */
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

        /* Convert hex SHA to raw bytes */
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

    /* Write updated index */
    if (mygit_index_write(".", &idx) == -1) {
        fprintf(stderr, "error: failed to write index: %s\n", strerror(errno));
        mygit_index_free(&idx);
        return EXIT_FAILURE;
    }

    mygit_index_free(&idx);
    return EXIT_SUCCESS;
}

static int cmd_commit_porcelain(int argc, char *argv[])
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
    /* ret == 1 means no commits yet — root commit, no parent */

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

    /* Read author from config */
    char cfg_name[128], cfg_email[128];
    if (mygit_config_get(".", "user", "name", cfg_name, sizeof(cfg_name)) != 0)
        strncpy(cfg_name, "mygit", sizeof(cfg_name));
    if (mygit_config_get(".", "user", "email", cfg_email, sizeof(cfg_email)) != 0)
        strncpy(cfg_email, "mygit@local", sizeof(cfg_email));

    strncpy(commit.author_name, cfg_name, sizeof(commit.author_name));
    strncpy(commit.author_email, cfg_email, sizeof(commit.author_email));
    commit.author_time = (int64_t)time(NULL);
    strncpy(commit.author_tz, "+0000", sizeof(commit.author_tz));
    strncpy(commit.committer_name, cfg_name, sizeof(commit.committer_name));
    strncpy(commit.committer_email, cfg_email, sizeof(commit.committer_email));
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
    /* Read HEAD to see if it's symbolic */
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

    /* Strip newline */
    size_t hlen = strlen(head_content);
    if (hlen > 0 && head_content[hlen - 1] == '\n')
        head_content[hlen - 1] = '\0';

    if (strncmp(head_content, "ref: ", 5) == 0) {
        /* Symbolic HEAD — update the branch it points to */
        const char *branch_ref = head_content + 5;
        if (mygit_ref_update(".", branch_ref, commit_hex) == -1) {
            fprintf(stderr, "error: failed to update %s\n", branch_ref);
            return EXIT_FAILURE;
        }
    } else {
        /* Detached HEAD — update HEAD directly */
        FILE *hfp = fopen("./.mygit/HEAD", "w");
        if (hfp == NULL) {
            fprintf(stderr, "error: failed to update HEAD\n");
            return EXIT_FAILURE;
        }
        fprintf(hfp, "%s\n", commit_hex);
        fclose(hfp);
    }

    /* Extract short branch name for display */
    if (strncmp(head_content, "ref: refs/heads/", 16) == 0) {
        fprintf(stdout, "[%s %.*s] %s\n",
                head_content + 16, 7, commit_hex, message);
    } else {
        fprintf(stdout, "[detached %.*s] %s\n", 7, commit_hex, message);
    }

    return EXIT_SUCCESS;
}

static int cmd_write_tree(void)
{
    mygit_index idx;
    int ret = mygit_index_read(".", &idx);
    if (ret == -1) {
        fprintf(stderr, "error: failed to read index: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    if (ret == 1) {
        /* No index — empty tree */
        mygit_index_init(&idx);
    }

    char tree_hex[41];
    ret = mygit_write_tree(".", &idx, tree_hex);
    if (ret == -1) {
        fprintf(stderr, "error: failed to write tree: %s\n", strerror(errno));
        mygit_index_free(&idx);
        return EXIT_FAILURE;
    }

    fprintf(stdout, "%s\n", tree_hex);
    mygit_index_free(&idx);
    return EXIT_SUCCESS;
}

/* ---------- status ---------- */

/*
 * Flatten a tree into a sorted list of path → sha1 pairs.
 * Used to compare HEAD tree vs index.
 */
typedef struct {
    char name[MYGIT_INDEX_MAX_PATH];
    unsigned char sha1[20];
    uint32_t mode;
} flat_tree_entry;

static int flatten_tree(const char *repo_path, const char *tree_hex,
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
            snprintf(full_name, sizeof(full_name), "%s%s", prefix, entries[i].name);
        } else {
            strncpy(full_name, entries[i].name, sizeof(full_name) - 1);
            full_name[sizeof(full_name) - 1] = '\0';
        }

        if (entries[i].mode == MYGIT_MODE_TREE) {
            /* Recurse into subtree */
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
            /* File entry */
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

static int cmd_status(void)
{
    /* Resolve HEAD → commit → tree */
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

    /* Read index */
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

    /* Read HEAD for branch name */
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

    /* Compare HEAD tree vs index → "Changes to be committed" */
    int has_staged = 0;
    for (size_t i = 0; i < idx.count; i++) {
        /* Check if this file is new or modified vs HEAD */
        int found = 0;
        for (size_t j = 0; j < head_count; j++) {
            if (strcmp(idx.entries[i].name, head_files[j].name) == 0) {
                found = 1;
                if (memcmp(idx.entries[i].sha1, head_files[j].sha1, 20) != 0) {
                    if (!has_staged) {
                        fprintf(stdout, "\nChanges to be committed:\n");
                        has_staged = 1;
                    }
                    fprintf(stdout, "\tmodified:   %s\n", idx.entries[i].name);
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

    /* Files in HEAD but not in index → staged deletion */
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

/* ---------- branch ---------- */

static int branch_list_cb(const char *refname, const char hex[41],
                          void *user_data)
{
    (void)hex;
    const char *current_branch = (const char *)user_data;

    /* Extract branch name from refs/heads/<name> */
    const char *name = refname;
    if (strncmp(refname, "refs/heads/", 11) == 0) {
        name = refname + 11;
    }

    if (current_branch != NULL && strcmp(name, current_branch) == 0) {
        fprintf(stdout, "* %s\n", name);
    } else {
        fprintf(stdout, "  %s\n", name);
    }
    return 0;
}

static int cmd_branch(int argc, char *argv[])
{
    /* Determine current branch from HEAD */
    char current_branch[256] = {0};
    FILE *fp = fopen("./.mygit/HEAD", "r");
    if (fp != NULL) {
        char line[256];
        if (fgets(line, (int)sizeof(line), fp) != NULL) {
            size_t len = strlen(line);
            if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
            if (strncmp(line, "ref: refs/heads/", 16) == 0) {
                strncpy(current_branch, line + 16,
                        sizeof(current_branch) - 1);
            }
        }
        fclose(fp);
    }

    if (argc == 2) {
        /* List branches */
        return mygit_ref_list(".", "refs/heads", branch_list_cb,
                              current_branch) == 0
                   ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    /* Check for -d flag */
    if (argc >= 3 && strcmp(argv[2], "-d") == 0) {
        if (argc < 4) {
            fprintf(stderr, "usage: mygit branch -d <name>\n");
            return EXIT_FAILURE;
        }
        const char *name = argv[3];
        if (strcmp(name, current_branch) == 0) {
            fprintf(stderr, "error: cannot delete branch '%s' "
                    "checked out at '.'\n", name);
            return EXIT_FAILURE;
        }
        char ref_path[PATH_MAX];
        snprintf(ref_path, sizeof(ref_path),
                 "./.mygit/refs/heads/%s", name);
        if (unlink(ref_path) == -1) {
            fprintf(stderr, "error: branch '%s' not found\n", name);
            return EXIT_FAILURE;
        }
        fprintf(stdout, "Deleted branch %s\n", name);
        return EXIT_SUCCESS;
    }

    /* Create branch */
    const char *name = argv[2];
    char head_hex[41];
    int ret = mygit_head_resolve(".", head_hex);
    if (ret != 0) {
        fprintf(stderr, "fatal: not a valid object name: 'HEAD'\n");
        return EXIT_FAILURE;
    }

    char refname[MYGIT_MAX_REFNAME];
    snprintf(refname, sizeof(refname), "refs/heads/%s", name);
    if (mygit_ref_update(".", refname, head_hex) == -1) {
        fprintf(stderr, "error: failed to create branch '%s': %s\n",
                name, strerror(errno));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/* ---------- checkout ---------- */

static int cmd_checkout(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: mygit checkout <branch>\n");
        return EXIT_FAILURE;
    }

    const char *target = argv[2];

    /* Check if target is a branch name */
    char refname[MYGIT_MAX_REFNAME];
    snprintf(refname, sizeof(refname), "refs/heads/%s", target);

    char target_hex[41];
    int ret = mygit_ref_resolve(".", refname, target_hex);
    if (ret != 0) {
        fprintf(stderr, "error: pathspec '%s' did not match any branch\n",
                target);
        return EXIT_FAILURE;
    }

    /* Read the target commit's tree */
    mygit_commit c;
    ret = mygit_commit_read(".", target_hex, &c);
    if (ret != 0) {
        fprintf(stderr, "error: failed to read commit %s\n", target_hex);
        return EXIT_FAILURE;
    }

    /* Flatten the target tree */
    flat_tree_entry *target_files = NULL;
    size_t target_count = 0, target_cap = 0;
    if (flatten_tree(".", c.tree_hex, "", &target_files,
                     &target_count, &target_cap) == -1) {
        fprintf(stderr, "error: failed to read tree\n");
        return EXIT_FAILURE;
    }

    /* Rebuild index from the target tree */
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

    /* Write the new index */
    if (mygit_index_write(".", &new_idx) == -1) {
        fprintf(stderr, "error: failed to write index\n");
        mygit_index_free(&new_idx);
        free(target_files);
        return EXIT_FAILURE;
    }

    /* Restore working tree files from blobs */
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
            /* Create directories recursively */
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

    /* Update HEAD to point to the branch */
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

static int cmd_update_ref(int argc, char *argv[])
{
    if (argc < 4) {
        fprintf(stderr, "usage: mygit update-ref <refname> <sha>\n");
        return EXIT_FAILURE;
    }

    const char *refname = argv[2];
    const char *hex = argv[3];

    if (strlen(hex) != 40) {
        fprintf(stderr, "error: not a valid SHA-1 '%s'\n", hex);
        return EXIT_FAILURE;
    }

    if (mygit_ref_update(".", refname, hex) == -1) {
        fprintf(stderr, "error: failed to update ref '%s': %s\n",
                refname, strerror(errno));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static int show_ref_cb(const char *refname, const char hex[41], void *user_data)
{
    (void)user_data;
    fprintf(stdout, "%s %s\n", hex, refname);
    return 0;
}

static int cmd_show_ref(void)
{
    int ret = mygit_ref_list(".", "refs", show_ref_cb, NULL);
    if (ret == -1) {
        fprintf(stderr, "error: failed to list refs: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static int cmd_config(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: mygit config <key> [<value>]\n");
        return EXIT_FAILURE;
    }

    const char *key = argv[2];

    /* Split key on first '.' to get section.name */
    const char *dot = strchr(key, '.');
    if (dot == NULL || dot == key || dot[1] == '\0') {
        fprintf(stderr, "error: key does not contain a section: '%s'\n", key);
        return EXIT_FAILURE;
    }

    char section[128];
    size_t sec_len = (size_t)(dot - key);
    if (sec_len >= sizeof(section)) {
        fprintf(stderr, "error: section name too long\n");
        return EXIT_FAILURE;
    }
    memcpy(section, key, sec_len);
    section[sec_len] = '\0';

    const char *name = dot + 1;

    if (argc == 3) {
        /* Get mode */
        char value[512];
        int ret = mygit_config_get(".", section, name, value, sizeof(value));
        if (ret == 1) {
            /* Not found — git exits with status 1 silently */
            return EXIT_FAILURE;
        }
        if (ret == -1) {
            fprintf(stderr, "error: failed to read config: %s\n",
                    strerror(errno));
            return EXIT_FAILURE;
        }
        fprintf(stdout, "%s\n", value);
        return EXIT_SUCCESS;
    }

    /* Set mode (argc >= 4) */
    const char *value = argv[3];
    if (mygit_config_set(".", section, name, value) == -1) {
        fprintf(stderr, "error: failed to set config: %s\n",
                strerror(errno));
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: mygit <command> [<args>]\n");
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "init") == 0) {
        return cmd_init(argc, argv);
    }

    if (strcmp(argv[1], "hash-object") == 0) {
        return cmd_hash_object(argc, argv);
    }

    if (strcmp(argv[1], "cat-file") == 0) {
        return cmd_cat_file(argc, argv);
    }

    if (strcmp(argv[1], "ls-tree") == 0) {
        return cmd_ls_tree(argc, argv);
    }

    if (strcmp(argv[1], "commit-tree") == 0) {
        return cmd_commit_tree(argc, argv);
    }

    if (strcmp(argv[1], "status") == 0) {
        return cmd_status();
    }

    if (strcmp(argv[1], "commit") == 0) {
        return cmd_commit_porcelain(argc, argv);
    }

    if (strcmp(argv[1], "branch") == 0) {
        return cmd_branch(argc, argv);
    }

    if (strcmp(argv[1], "checkout") == 0) {
        return cmd_checkout(argc, argv);
    }

    if (strcmp(argv[1], "log") == 0) {
        return cmd_log(argc, argv);
    }

    if (strcmp(argv[1], "add") == 0) {
        return cmd_add(argc, argv);
    }

    if (strcmp(argv[1], "write-tree") == 0) {
        return cmd_write_tree();
    }

    if (strcmp(argv[1], "update-ref") == 0) {
        return cmd_update_ref(argc, argv);
    }

    if (strcmp(argv[1], "show-ref") == 0) {
        return cmd_show_ref();
    }

    if (strcmp(argv[1], "config") == 0) {
        return cmd_config(argc, argv);
    }

    if (strcmp(argv[1], "tui") == 0) {
        return tui_run(".") == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    fprintf(stderr, "mygit: unknown command '%s'\n", argv[1]);
    return EXIT_FAILURE;
}
