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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

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
