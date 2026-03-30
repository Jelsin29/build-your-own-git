#define _POSIX_C_SOURCE 200809L

#include "cmd_ref.h"
#include "commit.h"
#include "config.h"
#include "ref.h"

#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* ---------- log ---------- */

int cmd_log(int argc, char *argv[])
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

/* ---------- branch ---------- */

static int branch_list_cb(const char *refname, const char hex[41],
                          void *user_data)
{
    (void)hex;
    const char *current_branch = (const char *)user_data;

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

int cmd_branch(int argc, char *argv[])
{
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
        return mygit_ref_list(".", "refs/heads", branch_list_cb,
                              current_branch) == 0
                   ? EXIT_SUCCESS : EXIT_FAILURE;
    }

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

/* ---------- update-ref ---------- */

int cmd_update_ref(int argc, char *argv[])
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

/* ---------- show-ref ---------- */

static int show_ref_cb(const char *refname, const char hex[41],
                       void *user_data)
{
    (void)user_data;
    fprintf(stdout, "%s %s\n", hex, refname);
    return 0;
}

int cmd_show_ref(void)
{
    int ret = mygit_ref_list(".", "refs", show_ref_cb, NULL);
    if (ret == -1) {
        fprintf(stderr, "error: failed to list refs: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

/* ---------- config ---------- */

int cmd_config(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "usage: mygit config <key> [<value>]\n");
        return EXIT_FAILURE;
    }

    const char *key = argv[2];

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
        char value[512];
        int ret = mygit_config_get(".", section, name, value, sizeof(value));
        if (ret == 1) {
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

    const char *value = argv[3];
    if (mygit_config_set(".", section, name, value) == -1) {
        fprintf(stderr, "error: failed to set config: %s\n",
                strerror(errno));
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
