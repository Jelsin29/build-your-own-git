#define _POSIX_C_SOURCE 200809L

#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

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

int mygit_config_set(const char *repo_path, const char *section,
                     const char *key, const char *value)
{
    /* TODO: implement in Phase 6 */
    (void)repo_path;
    (void)section;
    (void)key;
    (void)value;
    return -1;
}

int mygit_config_init(const char *repo_path)
{
    /* TODO: implement in Phase 6 */
    (void)repo_path;
    return -1;
}
