#ifndef MYGIT_REPO_H
#define MYGIT_REPO_H

/*
 * Initialize a new mygit repository at the given path.
 *
 * Creates .mygit/ with the required internal structure:
 *   .mygit/HEAD
 *   .mygit/objects/
 *   .mygit/objects/info/
 *   .mygit/objects/pack/
 *   .mygit/refs/
 *   .mygit/refs/heads/
 *   .mygit/refs/tags/
 *
 * Returns:  0 on success
 *           1 if .mygit already exists
 *          -1 on fatal error (errno set)
 */
int mygit_init(const char *path);

#endif /* MYGIT_REPO_H */
