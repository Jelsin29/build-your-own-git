#define _POSIX_C_SOURCE 200809L

#include "tui.h"
#include "tree.h"
#include "object.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <dirent.h>

#define HEADER_ROW  1
#define LIST_START  4

static char cached_entries_hash[41];
static mygit_tree_entry *cached_entries;
static size_t cached_count;

static void load_tree_entries(tui_state *state)
{
    /* Avoid re-reading if same hash */
    if (strcmp(cached_entries_hash, state->tree_hash) == 0 &&
        cached_entries != NULL) {
        state->tree_entry_count = cached_count;
        return;
    }

    free(cached_entries);
    cached_entries = NULL;
    cached_count = 0;
    cached_entries_hash[0] = '\0';

    if (mygit_tree_read(state->repo_path, state->tree_hash,
                        &cached_entries, &cached_count) != 0) {
        state->tree_entry_count = 0;
        return;
    }

    strncpy(cached_entries_hash, state->tree_hash, 40);
    cached_entries_hash[40] = '\0';
    state->tree_entry_count = cached_count;
}

static const char *find_first_tree(const char *repo_path)
{
    static char first_tree[41];
    char objects_dir[TUI_PATH_MAX + 16];
    snprintf(objects_dir, sizeof(objects_dir), "%s/.mygit/objects", repo_path);

    DIR *dp = opendir(objects_dir);
    if (dp == NULL) return NULL;

    struct dirent *prefix_ent;
    while ((prefix_ent = readdir(dp)) != NULL) {
        if (strlen(prefix_ent->d_name) != 2 || prefix_ent->d_name[0] == '.')
            continue;

        char prefix_path[TUI_PATH_MAX + 20];
        int n = snprintf(prefix_path, sizeof(prefix_path), "%s/%.2s",
                         objects_dir, prefix_ent->d_name);
        if (n < 0 || (size_t)n >= sizeof(prefix_path)) continue;

        DIR *sub = opendir(prefix_path);
        if (sub == NULL) continue;

        struct dirent *obj_ent;
        while ((obj_ent = readdir(sub)) != NULL) {
            if (obj_ent->d_name[0] == '.' || strlen(obj_ent->d_name) != 38)
                continue;

            char hash[41];
            memcpy(hash, prefix_ent->d_name, 2);
            memcpy(hash + 2, obj_ent->d_name, 38);
            hash[40] = '\0';

            unsigned char *raw = NULL;
            size_t raw_len = 0;
            if (mygit_object_read(repo_path, hash, &raw, &raw_len) == 0) {
                int is_tree = (raw_len >= 4 && memcmp(raw, "tree", 4) == 0);
                free(raw);
                if (is_tree) {
                    memcpy(first_tree, hash, 41);
                    closedir(sub);
                    closedir(dp);
                    return first_tree;
                }
            }
        }
        closedir(sub);
    }
    closedir(dp);
    return NULL;
}

void view_tree_load(tui_state *state, const char *tree_hash)
{
    if (tree_hash != NULL) {
        strncpy(state->tree_hash, tree_hash, 40);
        state->tree_hash[40] = '\0';
    } else {
        const char *found = find_first_tree(state->repo_path);
        if (found != NULL) {
            memcpy(state->tree_hash, found, 41);
        } else {
            state->tree_hash[0] = '\0';
        }
    }

    state->tree_depth = 0;
    state->tree_selected = 0;
    state->tree_scroll = 0;

    /* Invalidate cache */
    cached_entries_hash[0] = '\0';
    free(cached_entries);
    cached_entries = NULL;
    cached_count = 0;

    load_tree_entries(state);
}

void view_tree_render(tui_state *state, int max_y, int max_x)
{
    (void)max_x;
    clear();

    attron(COLOR_PAIR(PAIR_HEADER) | A_BOLD);
    mvprintw(HEADER_ROW, 2, "Tree Browser");
    attroff(COLOR_PAIR(PAIR_HEADER) | A_BOLD);

    if (state->tree_hash[0] == '\0') {
        attron(COLOR_PAIR(PAIR_DIM));
        mvprintw(LIST_START, 4, "No tree objects found.");
        attroff(COLOR_PAIR(PAIR_DIM));
        tui_render_hint_bar(max_y, max_x, "[Escape back]  [q quit]");
        refresh();
        return;
    }

    attron(COLOR_PAIR(PAIR_DIM));
    mvprintw(HEADER_ROW + 1, 2, "%.12s...  depth: %d  (%zu entries)",
             state->tree_hash, state->tree_depth, state->tree_entry_count);
    attroff(COLOR_PAIR(PAIR_DIM));

    load_tree_entries(state);

    int visible = max_y - LIST_START - 2;
    if (visible < 1) visible = 1;

    for (int i = 0; i < visible && (size_t)(i + state->tree_scroll) < state->tree_entry_count; i++) {
        int idx = i + state->tree_scroll;
        const mygit_tree_entry *e = &cached_entries[idx];

        char hex[41];
        for (int j = 0; j < 20; j++) {
            snprintf(hex + j * 2, 3, "%02x", e->hash[j]);
        }

        int is_tree = (e->mode == MYGIT_MODE_TREE);
        const char *kind = is_tree ? "tree/" : "blob ";
        int type_color = is_tree ? PAIR_ACCENT : PAIR_STATUS;

        if (idx == state->tree_selected) {
            attron(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
            mvprintw(LIST_START + i, 2, " > ");
            attroff(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
        } else {
            mvprintw(LIST_START + i, 2, "   ");
        }

        mvprintw(LIST_START + i, 5, "%06o ", e->mode);

        attron(COLOR_PAIR(type_color) | A_BOLD);
        printw("%s", kind);
        attroff(COLOR_PAIR(type_color) | A_BOLD);

        printw("%.8s  %s", hex, e->name);
    }

    const char *hints = state->tree_depth > 0
        ? "[j/k navigate]  [Enter open]  [Escape up/back]  [q quit]"
        : "[j/k navigate]  [Enter open]  [Escape back]  [q quit]";
    tui_render_hint_bar(max_y, max_x, hints);
    refresh();
}

int view_tree_input(tui_state *state, int ch)
{
    int max_y = getmaxy(stdscr);
    int visible = max_y - LIST_START - 2;
    if (visible < 1) visible = 1;

    switch (ch) {
    case 'j':
    case KEY_DOWN:
        if ((size_t)(state->tree_selected + 1) < state->tree_entry_count) {
            state->tree_selected++;
            if (state->tree_selected >= state->tree_scroll + visible)
                state->tree_scroll++;
        }
        break;

    case 'k':
    case KEY_UP:
        if (state->tree_selected > 0) {
            state->tree_selected--;
            if (state->tree_selected < state->tree_scroll)
                state->tree_scroll = state->tree_selected;
        }
        break;

    case '\n':
    case KEY_ENTER:
        if (state->tree_entry_count > 0 && cached_entries != NULL) {
            const mygit_tree_entry *e = &cached_entries[state->tree_selected];
            char hex[41];
            for (int j = 0; j < 20; j++) {
                snprintf(hex + j * 2, 3, "%02x", e->hash[j]);
            }

            if (e->mode == MYGIT_MODE_TREE) {
                /* Push current tree onto stack and navigate into subtree */
                if (state->tree_depth < TUI_TREE_STACK_DEPTH) {
                    memcpy(state->tree_stack[state->tree_depth],
                           state->tree_hash, 41);
                    state->tree_depth++;
                }
                strncpy(state->tree_hash, hex, 40);
                state->tree_hash[40] = '\0';
                state->tree_selected = 0;
                state->tree_scroll = 0;
                cached_entries_hash[0] = '\0';
                free(cached_entries);
                cached_entries = NULL;
                cached_count = 0;
            } else {
                /* Open blob in detail view */
                view_detail_load(state, hex);
                state->current_view = VIEW_OBJECT_DETAIL;
            }
        }
        break;

    case 27: /* Escape */
        if (state->tree_depth > 0) {
            /* Pop parent tree from stack */
            state->tree_depth--;
            memcpy(state->tree_hash,
                   state->tree_stack[state->tree_depth], 41);
            state->tree_selected = 0;
            state->tree_scroll = 0;
            cached_entries_hash[0] = '\0';
            free(cached_entries);
            cached_entries = NULL;
            cached_count = 0;
        } else {
            state->current_view = VIEW_DASHBOARD;
        }
        break;
    }

    return 0;
}
