#include "tui.h"

#include <ncurses.h>
#include <string.h>

int tui_run(const char *repo_path)
{
    (void)repo_path;

    /* Initialize ncurses */
    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    /* Get terminal dimensions */
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    /* Show a centered title */
    const char *title = "mygit TUI";
    const char *subtitle = "press q to quit";

    mvprintw(max_y / 2 - 1, (max_x - (int)strlen(title)) / 2, "%s", title);
    mvprintw(max_y / 2 + 1, (max_x - (int)strlen(subtitle)) / 2, "%s", subtitle);
    refresh();

    /* Event loop — just wait for 'q' */
    int ch;
    while ((ch = getch()) != 'q') {
        /* do nothing yet */
    }

    /* Cleanup */
    endwin();
    return 0;
}
