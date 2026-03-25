#define _POSIX_C_SOURCE 200809L

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- internal helpers ---- */

/*
 * Case-insensitive string comparison (like strcasecmp but portable
 * under strict POSIX).
 */
static int ci_streq(const char *a, const char *b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

/*
 * Strip leading and trailing whitespace in-place.
 * Returns pointer into the same buffer (possibly offset).
 */
static char *strip(char *s)
{
    while (*s && isspace((unsigned char)*s))
        s++;

    if (*s == '\0')
        return s;

    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end))
        *end-- = '\0';

    return s;
}

/*
 * Try to parse a section header like "[section]".
 * On success, writes the section name into `out` (up to out_size)
 * and returns 1.  Returns 0 otherwise.
 */
static int parse_section(const char *line, char *out, size_t out_size)
{
    const char *p = line;

    /* skip leading whitespace */
    while (*p && isspace((unsigned char)*p))
        p++;

    if (*p != '[')
        return 0;
    p++;

    const char *end = strchr(p, ']');
    if (end == NULL)
        return 0;

    size_t len = (size_t)(end - p);
    if (len == 0 || len >= out_size)
        return 0;

    memcpy(out, p, len);
    out[len] = '\0';

    /* trim whitespace inside the brackets */
    char *trimmed = strip(out);
    if (trimmed != out)
        memmove(out, trimmed, strlen(trimmed) + 1);

    return 1;
}

/*
 * Try to parse a key = value line.
 * On success writes key into key_out and value into val_out, returns 1.
 * Returns 0 otherwise.
 */
static int parse_kv(const char *line, char *key_out, size_t key_size,
                    char *val_out, size_t val_size)
{
    const char *eq = strchr(line, '=');
    if (eq == NULL)
        return 0;

    /* key part */
    size_t klen = (size_t)(eq - line);
    if (klen == 0 || klen >= key_size)
        return 0;

    char kbuf[MYGIT_MAX_KEY];
    memcpy(kbuf, line, klen);
    kbuf[klen] = '\0';
    char *k = strip(kbuf);
    if (*k == '\0')
        return 0;

    size_t final_klen = strlen(k);
    if (final_klen >= key_size)
        return 0;
    memcpy(key_out, k, final_klen + 1);

    /* value part */
    const char *vstart = eq + 1;
    size_t vlen = strlen(vstart);
    if (vlen >= val_size)
        return 0;

    char vbuf[MYGIT_MAX_VALUE];
    if (vlen >= sizeof(vbuf))
        return 0;
    memcpy(vbuf, vstart, vlen);
    vbuf[vlen] = '\0';
    char *v = strip(vbuf);

    size_t final_vlen = strlen(v);
    if (final_vlen >= val_size)
        return 0;
    memcpy(val_out, v, final_vlen + 1);

    return 1;
}

/* ---- public API ---- */

int mygit_config_get(const char *repo_path, const char *section,
                     const char *key, char *value_out, size_t value_size)
{
    if (repo_path == NULL || section == NULL || key == NULL ||
        value_out == NULL || value_size == 0) {
        return -1;
    }

    char config_path[512];
    int n = snprintf(config_path, sizeof(config_path),
                     "%s/.mygit/config", repo_path);
    if (n < 0 || (size_t)n >= sizeof(config_path))
        return -1;

    FILE *fp = fopen(config_path, "r");
    if (fp == NULL)
        return 1; /* missing file → not found */

    char current_section[MYGIT_MAX_SECTION] = "";
    char line[MYGIT_MAX_CONFIG_LINE];
    int found = 1; /* default: not found */

    while (fgets(line, (int)sizeof(line), fp) != NULL) {
        char *trimmed = strip(line);

        /* skip empty lines and comments */
        if (*trimmed == '\0' || *trimmed == '#' || *trimmed == ';')
            continue;

        /* section header? */
        char sec[MYGIT_MAX_SECTION];
        if (parse_section(trimmed, sec, sizeof(sec))) {
            memcpy(current_section, sec, strlen(sec) + 1);
            continue;
        }

        /* key = value? */
        char k[MYGIT_MAX_KEY];
        char v[MYGIT_MAX_VALUE];
        if (parse_kv(trimmed, k, sizeof(k), v, sizeof(v))) {
            if (ci_streq(current_section, section) && ci_streq(k, key)) {
                size_t vlen = strlen(v);
                if (vlen >= value_size) {
                    found = -1; /* buffer too small */
                    break;
                }
                memcpy(value_out, v, vlen + 1);
                found = 0;
                break;
            }
        }
    }

    fclose(fp);
    return found;
}

/*
 * Read entire file into a malloc'd buffer.  Sets *out_len.
 * Returns NULL on error (or if file does not exist, sets *out_len = 0
 * and returns a zero-length malloc'd string).
 */
static char *read_file(const char *path, size_t *out_len)
{
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        if (errno == ENOENT) {
            char *empty = calloc(1, 1);
            if (empty != NULL)
                *out_len = 0;
            return empty;
        }
        return NULL;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long fsize = ftell(fp);
    if (fsize < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);

    size_t len = (size_t)fsize;
    char *buf = malloc(len + 1);
    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }

    size_t nread = fread(buf, 1, len, fp);
    fclose(fp);

    buf[nread] = '\0';
    *out_len = nread;
    return buf;
}

/*
 * Write buffer to path atomically (write tmp + rename).
 */
static int atomic_write(const char *path, const char *data, size_t len)
{
    char tmp[512];
    int n = snprintf(tmp, sizeof(tmp), "%s.tmp.XXXXXX", path);
    if (n < 0 || (size_t)n >= sizeof(tmp))
        return -1;

    int fd = mkstemp(tmp);
    if (fd == -1)
        return -1;

    FILE *fp = fdopen(fd, "w");
    if (fp == NULL) {
        close(fd);
        unlink(tmp);
        return -1;
    }

    if (fwrite(data, 1, len, fp) != len) {
        fclose(fp);
        unlink(tmp);
        return -1;
    }
    fclose(fp);

    if (rename(tmp, path) != 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

/*
 * Append formatted text to a dynamically growing buffer.
 * *buf / *cap / *len track the allocation.
 * Returns 0 on success, -1 on alloc failure.
 */
static int buf_append(char **buf, size_t *cap, size_t *len,
                      const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));

static int buf_append(char **buf, size_t *cap, size_t *len,
                      const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int need = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (need < 0)
        return -1;

    while (*len + (size_t)need + 1 > *cap) {
        size_t newcap = (*cap == 0) ? 256 : (*cap * 2);
        char *tmp = realloc(*buf, newcap);
        if (tmp == NULL)
            return -1;
        *buf = tmp;
        *cap = newcap;
    }

    va_start(ap, fmt);
    vsnprintf(*buf + *len, *cap - *len, fmt, ap);
    va_end(ap);
    *len += (size_t)need;
    return 0;
}

int mygit_config_set(const char *repo_path, const char *section,
                     const char *key, const char *value)
{
    if (repo_path == NULL || section == NULL || key == NULL || value == NULL)
        return -1;

    char config_path[512];
    int n = snprintf(config_path, sizeof(config_path),
                     "%s/.mygit/config", repo_path);
    if (n < 0 || (size_t)n >= sizeof(config_path))
        return -1;

    /* Read existing content */
    size_t file_len = 0;
    char *content = read_file(config_path, &file_len);
    if (content == NULL)
        return -1;

    /* Build new content by scanning line-by-line */
    char *out = NULL;
    size_t out_cap = 0;
    size_t out_len = 0;

    char current_sec[MYGIT_MAX_SECTION] = "";
    int in_target_section = 0;
    int key_replaced = 0;
    int section_found = 0;

    char *cursor = content;
    while (*cursor != '\0') {
        /* Extract one line */
        char *eol = strchr(cursor, '\n');
        size_t line_len = (eol != NULL) ? (size_t)(eol - cursor + 1)
                                        : strlen(cursor);
        char line[MYGIT_MAX_CONFIG_LINE];
        if (line_len >= sizeof(line))
            line_len = sizeof(line) - 1;
        memcpy(line, cursor, line_len);
        line[line_len] = '\0';

        /* Make a stripped copy for parsing */
        char stripped[MYGIT_MAX_CONFIG_LINE];
        memcpy(stripped, line, line_len + 1);
        char *trimmed = strip(stripped);

        /* Check for section header */
        char sec[MYGIT_MAX_SECTION];
        if (parse_section(trimmed, sec, sizeof(sec))) {
            /* If we were in the target section and didn't replace,
             * insert the new key before this new section header */
            if (in_target_section && !key_replaced) {
                if (buf_append(&out, &out_cap, &out_len,
                               "\t%s = %s\n", key, value) != 0) {
                    free(content);
                    free(out);
                    return -1;
                }
                key_replaced = 1;
            }
            memcpy(current_sec, sec, strlen(sec) + 1);
            in_target_section = ci_streq(current_sec, section);
            if (in_target_section)
                section_found = 1;
        }

        /* Check if this line has the key we want to replace */
        char k[MYGIT_MAX_KEY];
        char v[MYGIT_MAX_VALUE];
        if (in_target_section && !key_replaced &&
            parse_kv(trimmed, k, sizeof(k), v, sizeof(v)) &&
            ci_streq(k, key)) {
            /* Replace this line */
            if (buf_append(&out, &out_cap, &out_len,
                           "\t%s = %s\n", key, value) != 0) {
                free(content);
                free(out);
                return -1;
            }
            key_replaced = 1;
            cursor += line_len;
            continue;
        }

        /* Copy line as-is */
        if (buf_append(&out, &out_cap, &out_len, "%.*s",
                       (int)line_len, cursor) != 0) {
            free(content);
            free(out);
            return -1;
        }

        cursor += line_len;
    }

    /* If we were in the target section at EOF and didn't replace */
    if (in_target_section && !key_replaced) {
        if (buf_append(&out, &out_cap, &out_len,
                       "\t%s = %s\n", key, value) != 0) {
            free(content);
            free(out);
            return -1;
        }
        key_replaced = 1;
    }

    /* If the section was never found, append it */
    if (!section_found) {
        /* Ensure there's a newline before the new section if file is non-empty */
        if (out_len > 0 && out[out_len - 1] != '\n') {
            if (buf_append(&out, &out_cap, &out_len, "\n") != 0) {
                free(content);
                free(out);
                return -1;
            }
        }
        if (buf_append(&out, &out_cap, &out_len,
                       "[%s]\n\t%s = %s\n", section, key, value) != 0) {
            free(content);
            free(out);
            return -1;
        }
        key_replaced = 1;
    }

    free(content);

    /* Write atomically */
    int rc = -1;
    if (key_replaced && out != NULL) {
        rc = atomic_write(config_path, out, out_len);
    }
    free(out);
    return rc;
}

int mygit_config_init(const char *repo_path)
{
    if (repo_path == NULL)
        return -1;

    char config_path[512];
    int n = snprintf(config_path, sizeof(config_path),
                     "%s/.mygit/config", repo_path);
    if (n < 0 || (size_t)n >= sizeof(config_path))
        return -1;

    static const char default_config[] =
        "[core]\n"
        "\trepositoryformatversion = 0\n"
        "\tfilemode = true\n"
        "\tbare = false\n";

    return atomic_write(config_path, default_config,
                        sizeof(default_config) - 1);
}
