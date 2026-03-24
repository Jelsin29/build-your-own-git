#include "blob.h"
#include "commit.h"
#include "hash.h"
#include "object.h"
#include "repo.h"
#include "tree.h"
#include "tui.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

    strncpy(commit.author_name, "mygit", sizeof(commit.author_name));
    strncpy(commit.author_email, "mygit@local", sizeof(commit.author_email));
    commit.author_time = (int64_t)time(NULL);
    strncpy(commit.author_tz, "+0000", sizeof(commit.author_tz));

    strncpy(commit.committer_name, "mygit", sizeof(commit.committer_name));
    strncpy(commit.committer_email, "mygit@local",
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
    if (argc < 3) {
        fprintf(stderr, "usage: mygit log <commit-sha>\n");
        return EXIT_FAILURE;
    }

    const char *hex = argv[2];
    if (strlen(hex) != 40) {
        fprintf(stderr, "error: not a valid commit hash '%s'\n", hex);
        return EXIT_FAILURE;
    }

    char current[41];
    strncpy(current, hex, 41);

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

    if (strcmp(argv[1], "tui") == 0) {
        return tui_run(".") == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    fprintf(stderr, "mygit: unknown command '%s'\n", argv[1]);
    return EXIT_FAILURE;
}
