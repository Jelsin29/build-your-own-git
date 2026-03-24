#include "tui.h"

#include <string.h>

static const char *ascii_header[] = {
    " ███╗   ███╗██╗   ██╗ ██████╗ ██╗████████╗",
    " ████╗ ████║╚██╗ ██╔╝██╔════╝ ██║╚══██╔══╝",
    " ██╔████╔██║ ╚████╔╝ ██║  ███╗██║   ██║   ",
    " ██║╚██╔╝██║  ╚██╔╝  ██║   ██║██║   ██║   ",
    " ██║ ╚═╝ ██║   ██║   ╚██████╔╝██║   ██║   ",
    " ╚═╝     ╚═╝   ╚═╝    ╚═════╝ ╚═╝   ╚═╝   ",
};

static const int header_lines = 6;

static const char *menu_items[] = {
    "Init repository",
    "Hash a file",
    "Browse objects",
    "Explore trees",
    "Create commit",
    "View commit log",
    "Quit",
};

static const int menu_count = 7;

void view_dashboard_render(tui_state *state, int max_y, int max_x)
{
    (void)state;
    (void)max_y;

    clear();

    /* Draw ASCII header in cyan */
    attron(COLOR_PAIR(PAIR_HEADER) | A_BOLD);
    for (int i = 0; i < header_lines; i++) {
        int x = (max_x - (int)strlen(ascii_header[i])) / 2;
        if (x < 0) x = 0;
        mvprintw(1 + i, x, "%s", ascii_header[i]);
    }
    attroff(COLOR_PAIR(PAIR_HEADER) | A_BOLD);

    /* Subtitle */
    const char *sub = "< mygit 1.0 -- content-addressed storage >";
    attron(COLOR_PAIR(PAIR_DIM));
    mvprintw(1 + header_lines + 1, (max_x - (int)strlen(sub)) / 2, "%s", sub);
    attroff(COLOR_PAIR(PAIR_DIM));

    /* Menu section */
    int menu_y = 1 + header_lines + 4;

    attron(COLOR_PAIR(PAIR_ACCENT) | A_BOLD);
    mvprintw(menu_y, 4, "Actions");
    attroff(COLOR_PAIR(PAIR_ACCENT) | A_BOLD);

    attron(COLOR_PAIR(PAIR_DIM));
    mvhline(menu_y + 1, 4, ACS_HLINE, 20);
    attroff(COLOR_PAIR(PAIR_DIM));

    /* Menu items — static for now, no selection highlight */
    for (int i = 0; i < menu_count; i++) {
        mvprintw(menu_y + 2 + i, 6, "  %s", menu_items[i]);
    }

    refresh();
}
