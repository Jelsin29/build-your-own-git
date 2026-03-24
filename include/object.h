#ifndef MYGIT_OBJECT_H
#define MYGIT_OBJECT_H

#include <stddef.h>

/*
 * Write a pre-formatted store buffer to .mygit/objects/.
 *
 * Computes SHA-1 of `store`, compresses with zlib, and writes to:
 *   <repo_path>/.mygit/objects/<xx>/<remaining-38-chars>
 *
 * Idempotent: if the object already exists, skips the write.
 *
 * Returns  0 on success
 *         -1 on error (errno set)
 */
int mygit_object_write(const char *repo_path,
                       const unsigned char *store, size_t store_len,
                       char hex_out[41]);

/*
 * Read and decompress an object from .mygit/objects/.
 *
 * `out` is set to a malloc'd buffer — CALLER MUST FREE.
 * `out_len` is set to the decompressed byte count.
 *
 * Returns  0 on success
 *          1 if object not found
 *         -1 on error (errno set)
 */
int mygit_object_read(const char *repo_path,
                      const char hex[40],
                      unsigned char **out, size_t *out_len);

#endif /* MYGIT_OBJECT_H */
