#include "tui.h"

#include <string.h>

#define HEADER_START_Y    1
#define SUBTITLE_OFFSET   1
#define MENU_OFFSET       4
#define MENU_LEFT_COL     4
#define MENU_ITEM_WIDTH   30

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
    clear();

    /* Draw ASCII header in cyan */
    attron(COLOR_PAIR(PAIR_HEADER) | A_BOLD);
    for (int i = 0; i < header_lines; i++) {
        int x = (max_x - (int)strlen(ascii_header[i])) / 2;
        if (x < 0) x = 0;
        mvprintw(HEADER_START_Y + i, x, "%s", ascii_header[i]);
    }
    attroff(COLOR_PAIR(PAIR_HEADER) | A_BOLD);

    /* Subtitle */
    const char *sub = "< mygit 1.0 -- content-addressed storage >";
    int sub_y = HEADER_START_Y + header_lines + SUBTITLE_OFFSET;
    attron(COLOR_PAIR(PAIR_DIM));
    mvprintw(sub_y, (max_x - (int)strlen(sub)) / 2, "%s", sub);
    attroff(COLOR_PAIR(PAIR_DIM));

    /* Menu section */
    int menu_y = HEADER_START_Y + header_lines + MENU_OFFSET;

    attron(COLOR_PAIR(PAIR_ACCENT) | A_BOLD);
    mvprintw(menu_y, MENU_LEFT_COL, "Actions");
    attroff(COLOR_PAIR(PAIR_ACCENT) | A_BOLD);

    attron(COLOR_PAIR(PAIR_DIM));
    mvhline(menu_y + 1, MENU_LEFT_COL, ACS_HLINE, 20);
    attroff(COLOR_PAIR(PAIR_DIM));

    /* Menu items with selection highlight */
    int items_y = menu_y + 2;
    for (int i = 0; i < menu_count; i++) {
        if (i == state->menu_index) {
            attron(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
            mvprintw(items_y + i, MENU_LEFT_COL, " > %-*s", MENU_ITEM_WIDTH, menu_items[i]);
            attroff(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
        } else {
            mvprintw(items_y + i, MENU_LEFT_COL, "   %-*s", MENU_ITEM_WIDTH, menu_items[i]);
        }
    }

    /* Key hint bar */
    tui_render_hint_bar(max_y, max_x, "[j/k navigate]  [Enter select]  [q quit]");

    refresh();
}

int view_dashboard_input(tui_state *state, int ch)
{
    switch (ch) {
    case 'j':
    case KEY_DOWN:
        if (state->menu_index < menu_count - 1) {
            state->menu_index++;
        }
        break;

    case 'k':
    case KEY_UP:
        if (state->menu_index > 0) {
            state->menu_index--;
        }
        break;

    case '\n':
    case KEY_ENTER:
        switch (state->menu_index) {
        case 0: /* Init repository — handled later */
            break;
        case 1:
            state->current_view = VIEW_HASH_FILE;
            break;
        case 2:
            state->current_view = VIEW_OBJECTS;
            break;
        case 3:
            state->current_view = VIEW_TREE;
            break;
        case 4:
            state->current_view = VIEW_COMMIT_CREATE;
            break;
        case 5:
            state->current_view = VIEW_COMMIT_LOG;
            break;
        case 6: /* Quit */
            return -1;
        }
        break;
    }

    return 0;
}
