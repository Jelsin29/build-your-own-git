#ifndef MYGIT_TUI_H
#define MYGIT_TUI_H

#include <ncurses.h>
#include <stddef.h>

#define TUI_PATH_MAX 4096
#define TUI_MAX_OBJECTS 512

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

/* Object entry for the explorer */
typedef struct {
    char hash[41];
} tui_object_entry;

/* Application state */
typedef struct {
    tui_view_id current_view;
    char repo_path[TUI_PATH_MAX];
    int menu_index;

    /* Object explorer */
    tui_object_entry objects[TUI_MAX_OBJECTS];
    size_t object_count;
    int obj_selected;
    int obj_scroll;
} tui_state;

/* Dashboard view */
void view_dashboard_render(tui_state *state, int max_y, int max_x);
int  view_dashboard_input(tui_state *state, int ch);

/* Object explorer view */
void view_objects_render(tui_state *state, int max_y, int max_x);
int  view_objects_input(tui_state *state, int ch);
void view_objects_load(tui_state *state);

/* Shared rendering helpers */
void tui_render_hint_bar(int max_y, int max_x, const char *hints);

/*
 * Launch the interactive TUI for mygit.
 * Returns 0 on success, -1 on error.
 */
int tui_run(const char *repo_path);

#endif /* MYGIT_TUI_H */
