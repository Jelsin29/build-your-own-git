#ifndef MYGIT_TUI_H
#define MYGIT_TUI_H

#include <ncurses.h>

#define TUI_PATH_MAX 4096

/* Color pairs */
enum {
    PAIR_NORMAL = 1,
    PAIR_HEADER,
    PAIR_HIGHLIGHT,
    PAIR_STATUS,
    PAIR_ERROR,
    PAIR_DIM,
    PAIR_ACCENT,
};

/* View identifiers */
typedef enum {
    VIEW_DASHBOARD,
    VIEW_OBJECTS,
    VIEW_TREE,
    VIEW_COMMIT_LOG,
    VIEW_COMMIT_CREATE,
    VIEW_HASH_FILE,
} tui_view_id;

/* Application state */
typedef struct {
    tui_view_id current_view;
    char repo_path[TUI_PATH_MAX];
    int menu_index;
} tui_state;

/* Dashboard view */
void view_dashboard_render(tui_state *state, int max_y, int max_x);
int  view_dashboard_input(tui_state *state, int ch);

/* Shared rendering helpers */
void tui_render_hint_bar(int max_y, int max_x, const char *hints);

/*
 * Launch the interactive TUI for mygit.
 * Returns 0 on success, -1 on error.
 */
int tui_run(const char *repo_path);

#endif /* MYGIT_TUI_H */
