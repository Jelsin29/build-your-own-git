#include "blob.h"
#include "hash.h"
#include "object.h"
#include "repo.h"
#include "tree.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    fprintf(stderr, "mygit: unknown command '%s'\n", argv[1]);
    return EXIT_FAILURE;
}
