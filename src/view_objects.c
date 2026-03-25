#define _POSIX_C_SOURCE 200809L

#include "tui.h"
#include "object.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static tui_obj_type parse_object_type(const unsigned char *data, size_t len)
{
    if (len >= 4 && memcmp(data, "blob", 4) == 0) return OBJ_BLOB;
    if (len >= 4 && memcmp(data, "tree", 4) == 0) return OBJ_TREE;
    if (len >= 6 && memcmp(data, "commit", 6) == 0) return OBJ_COMMIT;
    return OBJ_UNKNOWN;
}

static size_t parse_object_size(const unsigned char *data, size_t len)
{
    /* Format: "type size\0content" — find space, parse number */
    const unsigned char *space = memchr(data, ' ', len);
    if (space == NULL) return 0;
    return (size_t)strtoul((const char *)(space + 1), NULL, 10);
}

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

            /* Detect object type by reading and parsing the header */
            unsigned char *raw = NULL;
            size_t raw_len = 0;
            if (mygit_object_read(state->repo_path, entry->hash,
                                  &raw, &raw_len) == 0) {
                entry->type = parse_object_type(raw, raw_len);
                entry->size = parse_object_size(raw, raw_len);
                free(raw);
            } else {
                entry->type = OBJ_UNKNOWN;
                entry->size = 0;
            }

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
        const tui_object_entry *entry = &state->objects[idx];
        const char *type_str = "???";
        int type_color = PAIR_DIM;
        switch (entry->type) {
        case OBJ_BLOB:   type_str = "blob";   type_color = PAIR_STATUS;  break;
        case OBJ_TREE:   type_str = "tree";   type_color = PAIR_ACCENT;  break;
        case OBJ_COMMIT: type_str = "commit"; type_color = PAIR_HEADER;  break;
        case OBJ_UNKNOWN: break;
        }

        if (idx == state->obj_selected) {
            attron(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
            mvprintw(LIST_START + i, 2, " > %.8s ", entry->hash);
            attroff(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
        } else {
            mvprintw(LIST_START + i, 2, "   %.8s ", entry->hash);
        }

        attron(COLOR_PAIR(type_color) | A_BOLD);
        printw("%-6s", type_str);
        attroff(COLOR_PAIR(type_color) | A_BOLD);

        attron(COLOR_PAIR(PAIR_DIM));
        printw("  %zu bytes", entry->size);
        attroff(COLOR_PAIR(PAIR_DIM));
    }

    tui_render_hint_bar(max_y, max_x, "[j/k navigate]  [Enter inspect]  [Escape back]  [q quit]");
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

    case '\n':
    case KEY_ENTER:
        if (state->object_count > 0) {
            const char *hash = state->objects[state->obj_selected].hash;
            view_detail_load(state, hash);
            state->current_view = VIEW_OBJECT_DETAIL;
        }
        break;

    case 27: /* Escape */
        state->current_view = VIEW_DASHBOARD;
        break;
    }

    return 0;
}
