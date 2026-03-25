#ifndef MYGIT_CONFIG_H
#define MYGIT_CONFIG_H

#include <stddef.h>

/* Maximum sizes for config parsing */
#define MYGIT_MAX_CONFIG_LINE 1024
#define MYGIT_MAX_CONFIG_SIZE 8192
#define MYGIT_MAX_SECTION     64
#define MYGIT_MAX_KEY         64
#define MYGIT_MAX_VALUE      512

/*
 * Read a configuration value from .mygit/config.
 *
 * Searches for [section] / key in the INI-style config file.
 * Section and key comparisons are case-insensitive.
 * Lines starting with # or ; are treated as comments.
 *
 * Returns:  0  value found (copied into value_out)
 *           1  key or section not found, or config file missing
 *          -1  error (I/O failure, buffer overflow, etc.)
 */
int mygit_config_get(const char *repo_path, const char *section,
                     const char *key, char *value_out, size_t value_size);

/*
 * Set a configuration value in .mygit/config.
 *
 * Returns:  0 on success
 *          -1 on error
 */
int mygit_config_set(const char *repo_path, const char *section,
                     const char *key, const char *value);

/*
 * Create a default .mygit/config file for a new repository.
 *
 * Returns:  0 on success
 *          -1 on error
 */
int mygit_config_init(const char *repo_path);

#endif /* MYGIT_CONFIG_H */
