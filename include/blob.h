#ifndef MYGIT_BLOB_H
#define MYGIT_BLOB_H

#include <stddef.h>

/*
 * Compute the blob SHA-1 without writing to disk.
 * Constructs "blob <size>\0<content>" and hashes it.
 *
 * Returns 0 on success, -1 on error.
 */
int mygit_blob_hash(const unsigned char *content, size_t content_len,
                    char hex_out[41]);

/*
 * Read a file from disk, format as a blob, and store in the object store.
 *
 * Returns 0 on success, -1 on error (errno set).
 */
int mygit_blob_write(const char *repo_path, const char *file_path,
                     char hex_out[41]);

/*
 * Read a blob from the object store and return the raw content.
 * Strips the "blob <size>\0" header.
 *
 * `content` is set to a malloc'd buffer — CALLER MUST FREE.
 *
 * Returns  0 on success
 *          1 if not found
 *         -1 on error
 */
int mygit_blob_read(const char *repo_path, const char hex[40],
                    unsigned char **content, size_t *content_len);

#endif /* MYGIT_BLOB_H */
