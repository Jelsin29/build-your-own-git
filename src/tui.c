#include "tui.h"

#include <string.h>

static void init_colors(void)
{
    start_color();
    use_default_colors();

    init_pair(PAIR_NORMAL,    COLOR_WHITE,   -1);
    init_pair(PAIR_HEADER,    COLOR_CYAN,    -1);
    init_pair(PAIR_HIGHLIGHT, COLOR_BLACK,   COLOR_CYAN);
    init_pair(PAIR_STATUS,    COLOR_GREEN,   -1);
    init_pair(PAIR_ERROR,     COLOR_RED,     -1);
    init_pair(PAIR_DIM,       COLOR_BLUE,    -1);
    init_pair(PAIR_ACCENT,    COLOR_YELLOW,  -1);
}

int tui_run(const char *repo_path)
{
    /* Initialize ncurses */
    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    init_colors();

    /* Initialize state */
    tui_state state;
    memset(&state, 0, sizeof(state));
    state.current_view = VIEW_DASHBOARD;
    strncpy(state.repo_path, repo_path, TUI_PATH_MAX - 1);

    /* Get terminal dimensions */
    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);

    /* Render dashboard */
    view_dashboard_render(&state, max_y, max_x);

    /* Event loop */
    int ch;
    while ((ch = getch()) != 'q') {
        /* No input handling yet — just redraw */
        getmaxyx(stdscr, max_y, max_x);
        view_dashboard_render(&state, max_y, max_x);
    }

    endwin();
    return 0;
}
