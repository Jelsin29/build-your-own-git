#ifndef MYGIT_COMMIT_H
#define MYGIT_COMMIT_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#define MYGIT_MAX_PARENTS 16

typedef struct {
    char     tree_hex[41];
    char     parent_hex[MYGIT_MAX_PARENTS][41];
    size_t   parent_count;
    char     author_name[256];
    char     author_email[256];
    int64_t  author_time;
    char     author_tz[8];
    char     committer_name[256];
    char     committer_email[256];
    int64_t  committer_time;
    char     committer_tz[8];
    char     message[4096];
} mygit_commit;

/*
 * Create a commit object and store it in the object store.
 *
 * Returns 0 on success, -1 on error (errno set).
 */
int mygit_commit_write(const char *repo_path, const mygit_commit *commit,
                       char hex_out[41]);

/*
 * Read a commit object from the object store.
 *
 * Returns  0 on success
 *          1 if not found
 *         -1 on error
 */
int mygit_commit_read(const char *repo_path, const char hex[40],
                      mygit_commit *commit);

#endif /* MYGIT_COMMIT_H */
