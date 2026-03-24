#define _POSIX_C_SOURCE 200809L

#include "tui.h"

#include <dirent.h>
#include <string.h>
#include <stdio.h>

#define HEADER_ROW  1
#define LIST_START  3

void view_objects_load(tui_state *state)
{
    state->object_count = 0;
    state->obj_selected = 0;
    state->obj_scroll = 0;

    /* Scan .mygit/objects/ for object files */
    char objects_dir[TUI_PATH_MAX + 16];
    snprintf(objects_dir, sizeof(objects_dir), "%s/.mygit/objects", state->repo_path);

    DIR *dp = opendir(objects_dir);
    if (dp == NULL) {
        return;
    }

    struct dirent *prefix_ent;
    while ((prefix_ent = readdir(dp)) != NULL) {
        /* Skip non-2-char prefix directories */
        if (strlen(prefix_ent->d_name) != 2) {
            continue;
        }
        if (prefix_ent->d_name[0] == '.') {
            continue;
        }

        /* Open the prefix directory: objects_dir + "/" + 2-char prefix */
        char prefix_path[TUI_PATH_MAX + 20];
        int n = snprintf(prefix_path, sizeof(prefix_path), "%s/%.2s",
                         objects_dir, prefix_ent->d_name);
        if (n < 0 || (size_t)n >= sizeof(prefix_path)) {
            continue;
        }

        DIR *sub = opendir(prefix_path);
        if (sub == NULL) {
            continue;
        }

        struct dirent *obj_ent;
        while ((obj_ent = readdir(sub)) != NULL) {
            if (obj_ent->d_name[0] == '.') {
                continue;
            }
            if (strlen(obj_ent->d_name) != 38) {
                continue;
            }

            if (state->object_count >= TUI_MAX_OBJECTS) {
                closedir(sub);
                closedir(dp);
                return;
            }

            /* Reconstruct full 40-char hash: 2-char prefix + 38-char filename */
            tui_object_entry *entry = &state->objects[state->object_count];
            memcpy(entry->hash, prefix_ent->d_name, 2);
            memcpy(entry->hash + 2, obj_ent->d_name, 38);
            entry->hash[40] = '\0';

            state->object_count++;
        }

        closedir(sub);
    }

    closedir(dp);
}

void view_objects_render(tui_state *state, int max_y, int max_x)
{
    (void)max_x;

    clear();

    attron(COLOR_PAIR(PAIR_HEADER) | A_BOLD);
    mvprintw(HEADER_ROW, 2, "Object Explorer");
    attroff(COLOR_PAIR(PAIR_HEADER) | A_BOLD);

    if (state->object_count == 0) {
        attron(COLOR_PAIR(PAIR_DIM));
        mvprintw(LIST_START + 1, 4, "No objects found.");
        attroff(COLOR_PAIR(PAIR_DIM));
        tui_render_hint_bar(max_y, max_x, "[Escape back]  [q quit]");
        refresh();
        return;
    }

    /* How many items fit on screen */
    int visible = max_y - LIST_START - 2;
    if (visible < 1) visible = 1;

    attron(COLOR_PAIR(PAIR_DIM));
    mvprintw(HEADER_ROW, 20, "(%zu objects)", state->object_count);
    attroff(COLOR_PAIR(PAIR_DIM));

    for (int i = 0; i < visible && (size_t)(i + state->obj_scroll) < state->object_count; i++) {
        int idx = i + state->obj_scroll;
        const char *hash = state->objects[idx].hash;

        if (idx == state->obj_selected) {
            attron(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
            mvprintw(LIST_START + i, 2, " > %.8s...  %s", hash, hash);
            attroff(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
        } else {
            mvprintw(LIST_START + i, 2, "   %.8s...  %s", hash, hash);
        }
    }

    tui_render_hint_bar(max_y, max_x, "[j/k navigate]  [Escape back]  [q quit]");
    refresh();
}

int view_objects_input(tui_state *state, int ch)
{
    int max_y = getmaxy(stdscr);
    int visible = max_y - LIST_START - 2;
    if (visible < 1) visible = 1;

    switch (ch) {
    case 'j':
    case KEY_DOWN:
        if ((size_t)(state->obj_selected + 1) < state->object_count) {
            state->obj_selected++;
            /* Scroll down if needed */
            if (state->obj_selected >= state->obj_scroll + visible) {
                state->obj_scroll++;
            }
        }
        break;

    case 'k':
    case KEY_UP:
        if (state->obj_selected > 0) {
            state->obj_selected--;
            if (state->obj_selected < state->obj_scroll) {
                state->obj_scroll = state->obj_selected;
            }
        }
        break;

    case 27: /* Escape */
        state->current_view = VIEW_DASHBOARD;
        break;
    }

    return 0;
}
