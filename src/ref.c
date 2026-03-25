#define _POSIX_C_SOURCE 200809L

#include "ref.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#define HEX_SHA1_LEN      40
#define SYMBOLIC_PREFIX    "ref: "
#define SYMBOLIC_PREFIX_LEN 5

/* ---------- static helpers ---------- */

static int build_ref_path(char *buf, size_t len,
                          const char *repo_path, const char *refname)
{
    int written = snprintf(buf, len, "%s/.mygit/%s", repo_path, refname);
    if (written < 0 || (size_t)written >= len) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int is_hex_char(int c)
{
    return (c >= '0' && c <= '9') ||
           (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

static int is_valid_hex(const char *str, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (!is_hex_char(str[i])) {
            return 0;
        }
    }
    return 1;
}

/* ---------- public API ---------- */

int mygit_ref_resolve(const char *repo_path, const char *refname,
                      char hex_out[41])
{
    if (repo_path == NULL || refname == NULL || hex_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    char current_ref[MYGIT_MAX_REFNAME];
    if (strlen(refname) >= sizeof(current_ref)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    strncpy(current_ref, refname, sizeof(current_ref) - 1);
    current_ref[sizeof(current_ref) - 1] = '\0';

    for (int depth = 0; depth < MYGIT_MAX_REF_DEPTH; depth++) {
        char path[PATH_MAX];
        if (build_ref_path(path, sizeof(path), repo_path, current_ref) == -1) {
            return -1;
        }

        FILE *fp = fopen(path, "r");
        if (fp == NULL) {
            if (errno == ENOENT) {
                return 1; /* ref not found */
            }
            return -1;
        }

        char line[MYGIT_MAX_REFNAME + HEX_SHA1_LEN];
        if (fgets(line, (int)sizeof(line), fp) == NULL) {
            fclose(fp);
            errno = EIO;
            return -1;
        }
        fclose(fp);

        /* Strip trailing newline */
        size_t line_len = strlen(line);
        if (line_len > 0 && line[line_len - 1] == '\n') {
            line[line_len - 1] = '\0';
            line_len--;
        }

        /* Check for symbolic ref */
        if (line_len > SYMBOLIC_PREFIX_LEN &&
            strncmp(line, SYMBOLIC_PREFIX, SYMBOLIC_PREFIX_LEN) == 0) {
            const char *target = line + SYMBOLIC_PREFIX_LEN;
            if (strlen(target) >= sizeof(current_ref)) {
                errno = ENAMETOOLONG;
                return -1;
            }
            strncpy(current_ref, target, sizeof(current_ref) - 1);
            current_ref[sizeof(current_ref) - 1] = '\0';
            continue;
        }

        /* Check for direct ref (40-char hex) */
        if (line_len == HEX_SHA1_LEN && is_valid_hex(line, HEX_SHA1_LEN)) {
            memcpy(hex_out, line, HEX_SHA1_LEN);
            hex_out[HEX_SHA1_LEN] = '\0';
            return 0;
        }

        /* Content is neither symbolic nor valid hex */
        errno = EINVAL;
        return -1;
    }

    /* Exceeded max depth */
    errno = ELOOP;
    return -1;
}

int mygit_ref_update(const char *repo_path, const char *refname,
                     const char hex[41])
{
    (void)repo_path;
    (void)refname;
    (void)hex;
    errno = ENOSYS;
    return -1;
}

int mygit_ref_list(const char *repo_path, const char *prefix,
                   mygit_ref_callback cb, void *user_data)
{
    (void)repo_path;
    (void)prefix;
    (void)cb;
    (void)user_data;
    errno = ENOSYS;
    return -1;
}

int mygit_head_resolve(const char *repo_path, char hex_out[41])
{
    (void)repo_path;
    (void)hex_out;
    errno = ENOSYS;
    return -1;
}
