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

void tui_render_hint_bar(int max_y, int max_x, const char *hints)
{
    attron(COLOR_PAIR(PAIR_DIM));
    mvprintw(max_y - 1, (max_x - (int)strlen(hints)) / 2, "%s", hints);
    attroff(COLOR_PAIR(PAIR_DIM));
}

static void render_placeholder(tui_state *state, int max_y, int max_x)
{
    clear();

    const char *names[] = {
        "Dashboard", "Object Explorer", "Tree Browser",
        "Commit Log", "Create Commit", "Hash File",
    };
    size_t name_count = sizeof(names) / sizeof(names[0]);

    const char *label = "Unknown View";
    if ((size_t)state->current_view < name_count) {
        label = names[state->current_view];
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "[ %s — not implemented yet ]", label);

    mvprintw(max_y / 2, (max_x - (int)strlen(buf)) / 2, "%s", buf);
    tui_render_hint_bar(max_y, max_x, "[Escape back to dashboard]  [q quit]");
    refresh();
}

int tui_run(const char *repo_path)
{
    initscr();
    raw();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    init_colors();

    tui_state state;
    memset(&state, 0, sizeof(state));
    state.current_view = VIEW_DASHBOARD;
    strncpy(state.repo_path, repo_path, TUI_PATH_MAX - 1);

    int running = 1;
    while (running) {
        int max_y, max_x;
        getmaxyx(stdscr, max_y, max_x);

        /* Render current view */
        switch (state.current_view) {
        case VIEW_DASHBOARD:
            view_dashboard_render(&state, max_y, max_x);
            break;
        default:
            render_placeholder(&state, max_y, max_x);
            break;
        }

        /* Get input */
        int ch = getch();

        if (ch == 'q') {
            running = 0;
            break;
        }

        /* Dispatch input to current view */
        switch (state.current_view) {
        case VIEW_DASHBOARD:
            if (view_dashboard_input(&state, ch) == -1) {
                running = 0;
            }
            break;
        default:
            /* Placeholder views: Escape goes back to dashboard */
            if (ch == 27) { /* Escape */
                state.current_view = VIEW_DASHBOARD;
            }
            break;
        }
    }

    endwin();
    return 0;
}
