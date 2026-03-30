#ifndef MYGIT_CMD_PORCELAIN_H
#define MYGIT_CMD_PORCELAIN_H

/* Porcelain commands — high-level user-facing operations */

int cmd_add(int argc, char *argv[]);
int cmd_commit_porcelain(int argc, char *argv[]);
int cmd_status(void);
int cmd_checkout(int argc, char *argv[]);

#endif /* MYGIT_CMD_PORCELAIN_H */
