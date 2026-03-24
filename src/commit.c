#define _POSIX_C_SOURCE 200809L

#include "commit.h"
#include "hash.h"
#include "object.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int build_body(const mygit_commit *commit,
                      char **body_out, size_t *body_len_out)
{
    /* Calculate upper bound for body size */
    size_t max_size = 0;
    max_size += 46;  /* "tree " + 40 hex + "\n" */
    max_size += commit->parent_count * 48; /* "parent " + 40 hex + "\n" */
    max_size += 512; /* author line */
    max_size += 512; /* committer line */
    max_size += 1;   /* blank line */
    max_size += strlen(commit->message) + 2; /* message + "\n" */

    char *body = malloc(max_size);
    if (body == NULL) {
        return -1;
    }

    int offset = 0;

    /* tree line */
    int n = snprintf(body + offset, max_size - (size_t)offset,
                     "tree %s\n", commit->tree_hex);
    if (n < 0) { free(body); return -1; }
    offset += n;

    /* parent lines */
    for (size_t i = 0; i < commit->parent_count; i++) {
        n = snprintf(body + offset, max_size - (size_t)offset,
                     "parent %s\n", commit->parent_hex[i]);
        if (n < 0) { free(body); return -1; }
        offset += n;
    }

    /* author line */
    n = snprintf(body + offset, max_size - (size_t)offset,
                 "author %s <%s> %" PRId64 " %s\n",
                 commit->author_name, commit->author_email,
                 commit->author_time, commit->author_tz);
    if (n < 0) { free(body); return -1; }
    offset += n;

    /* committer line */
    n = snprintf(body + offset, max_size - (size_t)offset,
                 "committer %s <%s> %" PRId64 " %s\n",
                 commit->committer_name, commit->committer_email,
                 commit->committer_time, commit->committer_tz);
    if (n < 0) { free(body); return -1; }
    offset += n;

    /* blank line + message */
    n = snprintf(body + offset, max_size - (size_t)offset,
                 "\n%s\n", commit->message);
    if (n < 0) { free(body); return -1; }
    offset += n;

    *body_out = body;
    *body_len_out = (size_t)offset;
    return 0;
}

static int build_store(const mygit_commit *commit,
                       unsigned char **store_out, size_t *store_len_out)
{
    char *body = NULL;
    size_t body_len = 0;
    if (build_body(commit, &body, &body_len) == -1) {
        return -1;
    }

    char header[64];
    int hdr_len = snprintf(header, sizeof(header), "commit %zu", body_len);
    if (hdr_len < 0) {
        free(body);
        return -1;
    }

    size_t store_len = (size_t)hdr_len + 1 + body_len;
    unsigned char *store = malloc(store_len);
    if (store == NULL) {
        free(body);
        return -1;
    }

    memcpy(store, header, (size_t)hdr_len);
    store[hdr_len] = '\0';
    memcpy(store + hdr_len + 1, body, body_len);

    free(body);
    *store_out = store;
    *store_len_out = store_len;
    return 0;
}

int mygit_commit_write(const char *repo_path, const mygit_commit *commit,
                       char hex_out[41])
{
    if (repo_path == NULL || commit == NULL || hex_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (strlen(commit->tree_hex) != 40) {
        errno = EINVAL;
        return -1;
    }

    for (size_t i = 0; i < commit->parent_count; i++) {
        if (strlen(commit->parent_hex[i]) != 40) {
            errno = EINVAL;
            return -1;
        }
    }

    unsigned char *store = NULL;
    size_t store_len = 0;
    if (build_store(commit, &store, &store_len) == -1) {
        return -1;
    }

    int ret = mygit_object_write(repo_path, store, store_len, hex_out);
    free(store);
    return ret;
}

static int parse_identity(const char *line, size_t line_len,
                          char *name, size_t name_size,
                          char *email, size_t email_size,
                          int64_t *timestamp, char *tz, size_t tz_size)
{
    /* Format: "Name <email> timestamp tz" */
    const char *lt = memchr(line, '<', line_len);
    if (lt == NULL) return -1;

    const char *gt = memchr(lt, '>', line_len - (size_t)(lt - line));
    if (gt == NULL) return -1;

    /* Name: everything before " <" */
    size_t name_len = (size_t)(lt - line);
    if (name_len > 0 && line[name_len - 1] == ' ') {
        name_len--;
    }
    if (name_len >= name_size) return -1;
    memcpy(name, line, name_len);
    name[name_len] = '\0';

    /* Email: between < and > */
    size_t email_len = (size_t)(gt - lt - 1);
    if (email_len >= email_size) return -1;
    memcpy(email, lt + 1, email_len);
    email[email_len] = '\0';

    /* Timestamp and timezone after "> " */
    const char *after_gt = gt + 1;
    size_t remaining = line_len - (size_t)(after_gt - line);
    if (remaining < 2 || *after_gt != ' ') return -1;
    after_gt++;
    remaining--;

    /* Parse timestamp */
    char *endptr = NULL;
    *timestamp = strtoll(after_gt, &endptr, 10);
    if (endptr == after_gt || *endptr != ' ') return -1;

    /* Parse timezone */
    const char *tz_start = endptr + 1;
    size_t tz_len = line_len - (size_t)(tz_start - line);
    if (tz_len >= tz_size) return -1;
    memcpy(tz, tz_start, tz_len);
    tz[tz_len] = '\0';

    return 0;
}

int mygit_commit_read(const char *repo_path, const char hex[40],
                      mygit_commit *commit)
{
    if (repo_path == NULL || hex == NULL || commit == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(commit, 0, sizeof(*commit));

    /* Read raw object */
    unsigned char *store = NULL;
    size_t store_len = 0;
    int ret = mygit_object_read(repo_path, hex, &store, &store_len);
    if (ret != 0) {
        return ret;
    }

    /* Find NUL after header */
    unsigned char *nul = memchr(store, '\0', store_len);
    if (nul == NULL) {
        free(store);
        errno = EINVAL;
        return -1;
    }

    /* Validate "commit " header */
    size_t hdr_len = (size_t)(nul - store);
    if (hdr_len < 8 || memcmp(store, "commit ", 7) != 0) {
        free(store);
        errno = EINVAL;
        return -1;
    }

    /* Parse body line by line */
    const char *body = (const char *)(nul + 1);
    size_t body_len = store_len - hdr_len - 1;
    const char *end = body + body_len;
    const char *pos = body;
    int in_message = 0;
    size_t msg_offset = 0;

    while (pos < end) {
        const char *newline = memchr(pos, '\n', (size_t)(end - pos));
        size_t line_len = (newline != NULL)
                              ? (size_t)(newline - pos)
                              : (size_t)(end - pos);

        if (in_message) {
            /* Append to message */
            if (msg_offset + line_len + 1 < sizeof(commit->message)) {
                memcpy(commit->message + msg_offset, pos, line_len);
                msg_offset += line_len;
                if (newline != NULL) {
                    commit->message[msg_offset++] = '\n';
                }
            }
        } else if (line_len == 0) {
            /* Blank line marks start of message */
            in_message = 1;
        } else if (line_len > 5 && memcmp(pos, "tree ", 5) == 0) {
            if (line_len - 5 != 40) {
                free(store);
                errno = EINVAL;
                return -1;
            }
            memcpy(commit->tree_hex, pos + 5, 40);
            commit->tree_hex[40] = '\0';
        } else if (line_len > 7 && memcmp(pos, "parent ", 7) == 0) {
            if (commit->parent_count >= MYGIT_MAX_PARENTS) {
                free(store);
                errno = EOVERFLOW;
                return -1;
            }
            if (line_len - 7 != 40) {
                free(store);
                errno = EINVAL;
                return -1;
            }
            memcpy(commit->parent_hex[commit->parent_count], pos + 7, 40);
            commit->parent_hex[commit->parent_count][40] = '\0';
            commit->parent_count++;
        } else if (line_len > 7 && memcmp(pos, "author ", 7) == 0) {
            if (parse_identity(pos + 7, line_len - 7,
                               commit->author_name,
                               sizeof(commit->author_name),
                               commit->author_email,
                               sizeof(commit->author_email),
                               &commit->author_time,
                               commit->author_tz,
                               sizeof(commit->author_tz)) == -1) {
                free(store);
                errno = EINVAL;
                return -1;
            }
        } else if (line_len > 10 && memcmp(pos, "committer ", 10) == 0) {
            if (parse_identity(pos + 10, line_len - 10,
                               commit->committer_name,
                               sizeof(commit->committer_name),
                               commit->committer_email,
                               sizeof(commit->committer_email),
                               &commit->committer_time,
                               commit->committer_tz,
                               sizeof(commit->committer_tz)) == -1) {
                free(store);
                errno = EINVAL;
                return -1;
            }
        }

        pos = (newline != NULL) ? newline + 1 : end;
    }

    /* Strip trailing newline from message */
    while (msg_offset > 0 && commit->message[msg_offset - 1] == '\n') {
        msg_offset--;
    }
    commit->message[msg_offset] = '\0';

    free(store);
    return 0;
}
