#define _POSIX_C_SOURCE 200809L

#include "ref.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

/*
 * Validate a refname per git conventions.
 * Returns 0 if valid, -1 with errno=EINVAL if invalid.
 */
static int validate_refname(const char *refname)
{
    if (refname == NULL || refname[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    size_t len = strlen(refname);

    /* Must not start with '/' */
    if (refname[0] == '/') {
        errno = EINVAL;
        return -1;
    }

    /* Must not end with '/' or '.lock' */
    if (refname[len - 1] == '/') {
        errno = EINVAL;
        return -1;
    }
    if (len >= 5 && strcmp(refname + len - 5, ".lock") == 0) {
        errno = EINVAL;
        return -1;
    }

    /* Walk the string checking for forbidden patterns */
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)refname[i];

        /* Control chars (0x00-0x1F, 0x7F) */
        if (c <= 0x1F || c == 0x7F) {
            errno = EINVAL;
            return -1;
        }

        /* Forbidden single chars */
        if (c == ':' || c == '?' || c == '*' || c == '[' ||
            c == '\\' || c == '^' || c == '~' || c == ' ' || c == '\t') {
            errno = EINVAL;
            return -1;
        }

        /* '..' sequence */
        if (c == '.' && i + 1 < len && refname[i + 1] == '.') {
            errno = EINVAL;
            return -1;
        }

        /* '//' sequence */
        if (c == '/' && i + 1 < len && refname[i + 1] == '/') {
            errno = EINVAL;
            return -1;
        }

        /* '@{' sequence */
        if (c == '@' && i + 1 < len && refname[i + 1] == '{') {
            errno = EINVAL;
            return -1;
        }
    }

    return 0;
}

/*
 * Create parent directories for a ref path under .mygit/.
 * E.g., for "refs/heads/feature/foo", creates refs/, refs/heads/, refs/heads/feature/.
 */
static int ensure_ref_dirs(const char *repo_path, const char *refname)
{
    char dir_path[PATH_MAX];
    int written = snprintf(dir_path, sizeof(dir_path), "%s/.mygit/%s",
                           repo_path, refname);
    if (written < 0 || (size_t)written >= sizeof(dir_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    /* Walk backwards to find the last '/' — everything before it is directories */
    char *last_slash = strrchr(dir_path, '/');
    if (last_slash == NULL) {
        return 0; /* No directory component */
    }
    *last_slash = '\0'; /* Temporarily truncate to get the directory part */

    /* Create each directory component */
    for (char *p = dir_path + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(dir_path, 0755) == -1 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    /* Create the final directory */
    if (mkdir(dir_path, 0755) == -1 && errno != EEXIST) {
        return -1;
    }

    return 0;
}

/*
 * Recursive directory walker for ref listing.
 * prefix: refname prefix being built (e.g., "refs/heads/")
 * dir_path: filesystem path to scan
 */
static int ref_list_walk(const char *repo_path, const char *dir_path,
                         const char *prefix, mygit_ref_callback cb,
                         void *user_data)
{
    DIR *dp = opendir(dir_path);
    if (dp == NULL) {
        if (errno == ENOENT) {
            return 0; /* Directory doesn't exist — no refs here */
        }
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 ||
            strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        char child_path[PATH_MAX];
        int w = snprintf(child_path, sizeof(child_path), "%s/%s",
                         dir_path, entry->d_name);
        if (w < 0 || (size_t)w >= sizeof(child_path)) {
            continue;
        }

        char child_ref[MYGIT_MAX_REFNAME];
        w = snprintf(child_ref, sizeof(child_ref), "%s%s",
                     prefix, entry->d_name);
        if (w < 0 || (size_t)w >= (int)sizeof(child_ref)) {
            continue;
        }

        struct stat st;
        if (stat(child_path, &st) == -1) {
            continue;
        }

        if (S_ISDIR(st.st_mode)) {
            /* Recurse with trailing slash */
            char sub_prefix[MYGIT_MAX_REFNAME + 1];
            snprintf(sub_prefix, sizeof(sub_prefix), "%s/", child_ref);
            int ret = ref_list_walk(repo_path, child_path, sub_prefix,
                                    cb, user_data);
            if (ret != 0) {
                closedir(dp);
                return ret;
            }
        } else if (S_ISREG(st.st_mode)) {
            /* Try to resolve this ref */
            char hex[41];
            int ret = mygit_ref_resolve(repo_path, child_ref, hex);
            if (ret == 0) {
                if (cb(child_ref, hex, user_data) != 0) {
                    closedir(dp);
                    return 0; /* Callback stopped iteration */
                }
            }
            /* Skip unresolvable refs silently */
        }
    }

    closedir(dp);
    return 0;
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
    if (repo_path == NULL || refname == NULL || hex == NULL) {
        errno = EINVAL;
        return -1;
    }

    /* Validate hex is exactly 40 hex chars */
    if (strlen(hex) != HEX_SHA1_LEN || !is_valid_hex(hex, HEX_SHA1_LEN)) {
        errno = EINVAL;
        return -1;
    }

    /* Validate refname */
    if (validate_refname(refname) == -1) {
        return -1;
    }

    /* Create parent directories */
    if (ensure_ref_dirs(repo_path, refname) == -1) {
        return -1;
    }

    /* Build temp path for atomic write */
    char tmp_path[PATH_MAX];
    int w = snprintf(tmp_path, sizeof(tmp_path), "%s/.mygit/%s.tmp",
                     repo_path, refname);
    if (w < 0 || (size_t)w >= sizeof(tmp_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    /* Build final path */
    char final_path[PATH_MAX];
    w = snprintf(final_path, sizeof(final_path), "%s/.mygit/%s",
                 repo_path, refname);
    if (w < 0 || (size_t)w >= sizeof(final_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    /* Write to temp file */
    FILE *fp = fopen(tmp_path, "w");
    if (fp == NULL) {
        return -1;
    }

    if (fprintf(fp, "%s\n", hex) < 0) {
        fclose(fp);
        unlink(tmp_path);
        return -1;
    }

    if (fflush(fp) != 0) {
        fclose(fp);
        unlink(tmp_path);
        return -1;
    }
    fclose(fp);

    /* Atomic rename */
    if (rename(tmp_path, final_path) == -1) {
        unlink(tmp_path);
        return -1;
    }

    return 0;
}

int mygit_ref_list(const char *repo_path, const char *prefix,
                   mygit_ref_callback cb, void *user_data)
{
    if (repo_path == NULL || prefix == NULL || cb == NULL) {
        errno = EINVAL;
        return -1;
    }

    char dir_path[PATH_MAX];
    int w = snprintf(dir_path, sizeof(dir_path), "%s/.mygit/%s",
                     repo_path, prefix);
    if (w < 0 || (size_t)w >= sizeof(dir_path)) {
        errno = ENAMETOOLONG;
        return -1;
    }

    /* Build the prefix string for refnames (ensure trailing slash) */
    char ref_prefix[MYGIT_MAX_REFNAME];
    size_t plen = strlen(prefix);
    if (plen > 0 && prefix[plen - 1] == '/') {
        snprintf(ref_prefix, sizeof(ref_prefix), "%s", prefix);
    } else {
        snprintf(ref_prefix, sizeof(ref_prefix), "%s/", prefix);
    }

    return ref_list_walk(repo_path, dir_path, ref_prefix, cb, user_data);
}

int mygit_head_resolve(const char *repo_path, char hex_out[41])
{
    return mygit_ref_resolve(repo_path, "HEAD", hex_out);
}
