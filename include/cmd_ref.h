#ifndef MYGIT_CMD_REF_H
#define MYGIT_CMD_REF_H

/* Ref, config, and log commands */

int cmd_log(int argc, char *argv[]);
int cmd_branch(int argc, char *argv[]);
int cmd_update_ref(int argc, char *argv[]);
int cmd_show_ref(void);
int cmd_config(int argc, char *argv[]);

#endif /* MYGIT_CMD_REF_H */
