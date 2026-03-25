#ifndef MYGIT_REF_H
#define MYGIT_REF_H

#include <stddef.h>

#define MYGIT_MAX_REF_DEPTH  8   /* Max symbolic ref chain length */
#define MYGIT_MAX_REFNAME  256   /* Max refname length including NUL */

/*
 * Resolve a ref to a 40-char hex SHA-1.
 *
 * Handles both direct refs ("abc123...\n") and symbolic refs
 * ("ref: refs/heads/master\n"). Follows symbolic chains up to
 * MYGIT_MAX_REF_DEPTH hops.
 *
 * refname is relative to .mygit/ -- e.g., "HEAD", "refs/heads/master".
 *
 * Returns  0  on success (hex_out filled with 40-char + NUL)
 *          1  if ref does not exist (dangling ref or missing file)
 *         -1  on error (circular ref, path traversal, I/O error; errno set)
 */
int mygit_ref_resolve(const char *repo_path, const char *refname,
                      char hex_out[41]);

/*
 * Write a direct ref (SHA-1) to disk.
 *
 * Creates parent directories if needed (e.g., refs/heads/ for a new branch).
 * Uses write-to-temp + rename for crash safety.
 *
 * Returns  0  on success
 *         -1  on error (errno set: EINVAL for bad refname, others for I/O)
 */
int mygit_ref_update(const char *repo_path, const char *refname,
                     const char hex[41]);

/*
 * Callback for mygit_ref_list. Called once per ref found.
 *
 * refname: full ref path relative to .mygit/ (e.g., "refs/heads/master")
 * hex: resolved 40-char SHA-1 (NUL-terminated)
 * user_data: opaque pointer passed through from caller
 *
 * Return 0 to continue iteration, non-zero to stop early.
 */
typedef int (*mygit_ref_callback)(const char *refname, const char hex[41],
                                  void *user_data);

/*
 * List all refs under a prefix, calling cb for each.
 *
 * prefix is relative to .mygit/ -- e.g., "refs/heads/" lists branches,
 * "refs/tags/" lists tags, "refs/" lists everything.
 *
 * Returns  0  on success (all refs visited or callback stopped early)
 *         -1  on error (errno set)
 */
int mygit_ref_list(const char *repo_path, const char *prefix,
                   mygit_ref_callback cb, void *user_data);

/*
 * Convenience: resolve HEAD to a commit SHA-1.
 *
 * Equivalent to mygit_ref_resolve(repo_path, "HEAD", hex_out).
 *
 * Returns  0  on success
 *          1  if HEAD is unresolvable (new repo, no commits yet)
 *         -1  on error
 */
int mygit_head_resolve(const char *repo_path, char hex_out[41]);

#endif /* MYGIT_REF_H */
