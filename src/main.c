#include "repo.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "usage: mygit <command> [<args>]\n");
        return EXIT_FAILURE;
    }

    if (strcmp(argv[1], "init") == 0) {
        const char *path = (argc >= 3) ? argv[2] : ".";
        int result = mygit_init(path);

        switch (result) {
        case 0:
            fprintf(stdout, "Initialized empty mygit repository in %s/.mygit/\n", path);
            return EXIT_SUCCESS;
        case 1:
            fprintf(stdout, "Reinitialized existing mygit repository in %s/.mygit/\n", path);
            return EXIT_SUCCESS;
        default:
            fprintf(stderr, "error: failed to initialize repository: %s\n",
                    strerror(errno));
            return EXIT_FAILURE;
        }
    }

    fprintf(stderr, "mygit: unknown command '%s'\n", argv[1]);
    return EXIT_FAILURE;
}
