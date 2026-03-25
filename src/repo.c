#define _POSIX_C_SOURCE 200809L

#include "repo.h"

#include "config.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int build_mygit_path(char *buf, size_t len,
                            const char *base, const char *suffix)
{
    int written = snprintf(buf, len, "%s/.mygit%s", base, suffix);
    if (written < 0 || (size_t)written >= len) {
        return -1;
    }
    return 0;
}

static int make_dir(const char *path)
{
    if (mkdir(path, 0755) == -1) {
        if (errno == EEXIST) {
            return 0;
        }
        return -1;
    }
    return 0;
}

static int write_head(const char *mygit_path)
{
    char head_path[PATH_MAX];
    int written = snprintf(head_path, sizeof(head_path), "%s/HEAD", mygit_path);
    if (written < 0 || (size_t)written >= sizeof(head_path)) {
        return -1;
    }

    FILE *fp = fopen(head_path, "w");
    if (fp == NULL) {
        return -1;
    }

    if (fputs("ref: refs/heads/master\n", fp) == EOF) {
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

int mygit_init(const char *path)
{
    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

    char mygit_path[PATH_MAX];
    if (build_mygit_path(mygit_path, sizeof(mygit_path), path, "") == -1) {
        errno = ENAMETOOLONG;
        return -1;
    }

    /* Check if .mygit already exists */
    struct stat st;
    if (stat(mygit_path, &st) == 0 && S_ISDIR(st.st_mode)) {
        return 1;
    }

    /* Create .mygit and all subdirectories */
    if (make_dir(mygit_path) == -1) {
        return -1;
    }

    static const char *subdirs[] = {
        "/objects",
        "/objects/info",
        "/objects/pack",
        "/refs",
        "/refs/heads",
        "/refs/tags",
    };
    static const size_t NUM_SUBDIRS = sizeof(subdirs) / sizeof(subdirs[0]);

    for (size_t i = 0; i < NUM_SUBDIRS; i++) {
        char subpath[PATH_MAX];
        if (build_mygit_path(subpath, sizeof(subpath), path, subdirs[i]) == -1) {
            return -1;
        }
        if (make_dir(subpath) == -1) {
            return -1;
        }
    }

    /* Write HEAD file */
    if (write_head(mygit_path) == -1) {
        return -1;
    }

    /* Create default config file */
    if (mygit_config_init(path) == -1) {
        return -1;
    }

    return 0;
}
