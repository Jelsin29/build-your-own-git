#include "cmd_diff.h"
#include "cmd_object.h"
#include "cmd_porcelain.h"
#include "cmd_ref.h"
#include "tui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: mygit <command> [<args>]\n");
        return EXIT_FAILURE;
    }

    /* Plumbing — raw object operations */
    if (strcmp(argv[1], "init") == 0)        return cmd_init(argc, argv);
    if (strcmp(argv[1], "hash-object") == 0) return cmd_hash_object(argc, argv);
    if (strcmp(argv[1], "cat-file") == 0)    return cmd_cat_file(argc, argv);
    if (strcmp(argv[1], "ls-tree") == 0)     return cmd_ls_tree(argc, argv);
    if (strcmp(argv[1], "commit-tree") == 0) return cmd_commit_tree(argc, argv);
    if (strcmp(argv[1], "write-tree") == 0)  return cmd_write_tree();

    /* Porcelain — high-level workflow */
    if (strcmp(argv[1], "add") == 0)      return cmd_add(argc, argv);
    if (strcmp(argv[1], "commit") == 0)   return cmd_commit_porcelain(argc, argv);
    if (strcmp(argv[1], "status") == 0)   return cmd_status();
    if (strcmp(argv[1], "checkout") == 0) return cmd_checkout(argc, argv);

    /* Refs, config, log */
    if (strcmp(argv[1], "log") == 0)        return cmd_log(argc, argv);
    if (strcmp(argv[1], "branch") == 0)     return cmd_branch(argc, argv);
    if (strcmp(argv[1], "update-ref") == 0) return cmd_update_ref(argc, argv);
    if (strcmp(argv[1], "show-ref") == 0)   return cmd_show_ref();
    if (strcmp(argv[1], "config") == 0)     return cmd_config(argc, argv);

    /* Diff */
    if (strcmp(argv[1], "diff") == 0) return cmd_diff(argc, argv);

    /* TUI */
    if (strcmp(argv[1], "tui") == 0)
        return tui_run(".") == 0 ? EXIT_SUCCESS : EXIT_FAILURE;

    fprintf(stderr, "mygit: unknown command '%s'\n", argv[1]);
    return EXIT_FAILURE;
}
