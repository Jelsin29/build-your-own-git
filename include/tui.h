#ifndef MYGIT_TUI_H
#define MYGIT_TUI_H

#include <ncurses.h>
#include <stddef.h>

#define TUI_PATH_MAX 4096
#define TUI_MAX_OBJECTS 512
#define TUI_MAX_TREE_ENTRIES 256
#define TUI_TREE_STACK_DEPTH 16

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
    VIEW_OBJECT_DETAIL,
} tui_view_id;

/* Object type identifiers */
typedef enum {
    OBJ_UNKNOWN,
    OBJ_BLOB,
    OBJ_TREE,
    OBJ_COMMIT,
} tui_obj_type;

/* Object entry for the explorer */
typedef struct {
    char hash[41];
    tui_obj_type type;
    size_t size;
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

    /* Tree browser */
    char tree_hash[41];
    char tree_stack[TUI_TREE_STACK_DEPTH][41];
    int tree_depth;
    int tree_selected;
    int tree_scroll;
    size_t tree_entry_count;

    /* Object detail view */
    char detail_hash[41];
    unsigned char *detail_data;
    size_t detail_len;
    tui_obj_type detail_type;
    int detail_scroll;
} tui_state;

/* Dashboard view */
void view_dashboard_render(tui_state *state, int max_y, int max_x);
int  view_dashboard_input(tui_state *state, int ch);

/* Object explorer view */
void view_objects_render(tui_state *state, int max_y, int max_x);
int  view_objects_input(tui_state *state, int ch);
void view_objects_load(tui_state *state);

/* Tree browser view */
void view_tree_render(tui_state *state, int max_y, int max_x);
int  view_tree_input(tui_state *state, int ch);
void view_tree_load(tui_state *state, const char *tree_hash);

/* Object detail view */
void view_detail_render(tui_state *state, int max_y, int max_x);
int  view_detail_input(tui_state *state, int ch);
void view_detail_load(tui_state *state, const char *hash);
void view_detail_free(tui_state *state);

/* Shared rendering helpers */
void tui_render_hint_bar(int max_y, int max_x, const char *hints);

/*
 * Launch the interactive TUI for mygit.
 * Returns 0 on success, -1 on error.
 */
int tui_run(const char *repo_path);

#endif /* MYGIT_TUI_H */
