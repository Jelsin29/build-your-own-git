#ifndef MYGIT_HASH_H
#define MYGIT_HASH_H

#include <stddef.h>

/*
 * Compute SHA-1 of `len` bytes at `data`.
 * Writes 20 raw bytes into `out`.
 * Returns 0 on success, -1 on error.
 */
int mygit_sha1(const unsigned char *data, size_t len,
               unsigned char out[20]);

/*
 * Convert 20 raw SHA-1 bytes to a 40-char lowercase hex string.
 * `out` must be at least 41 bytes (includes NUL terminator).
 */
void mygit_hex(const unsigned char hash[20], char out[41]);

#endif /* MYGIT_HASH_H */
