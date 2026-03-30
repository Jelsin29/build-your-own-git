#ifndef MYGIT_CMD_OBJECT_H
#define MYGIT_CMD_OBJECT_H

/* Plumbing commands — operate on raw git objects */

int cmd_init(int argc, char *argv[]);
int cmd_hash_object(int argc, char *argv[]);
int cmd_cat_file(int argc, char *argv[]);
int cmd_ls_tree(int argc, char *argv[]);
int cmd_commit_tree(int argc, char *argv[]);
int cmd_write_tree(void);

#endif /* MYGIT_CMD_OBJECT_H */
