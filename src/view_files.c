#define _POSIX_C_SOURCE 200809L

#include "tui.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define HEADER_ROW  1
#define LIST_START  4

typedef struct {
    char name[256];
    int is_dir;
    off_t size;
} file_entry;

static file_entry files[TUI_MAX_FILES];

static int file_cmp(const void *a, const void *b)
{
    const file_entry *fa = (const file_entry *)a;
    const file_entry *fb = (const file_entry *)b;

    /* Directories first, then alphabetical */
    if (fa->is_dir && !fb->is_dir) return -1;
    if (!fa->is_dir && fb->is_dir) return 1;
    return strcmp(fa->name, fb->name);
}

void view_files_load(tui_state *state, const char *dir_path)
{
    state->file_count = 0;
    state->file_selected = 0;
    state->file_scroll = 0;

    if (dir_path != NULL) {
        strncpy(state->file_cwd, dir_path, TUI_PATH_MAX - 1);
        state->file_cwd[TUI_PATH_MAX - 1] = '\0';
    } else if (state->file_cwd[0] == '\0') {
        strncpy(state->file_cwd, state->repo_path, TUI_PATH_MAX - 1);
        state->file_cwd[TUI_PATH_MAX - 1] = '\0';
    }

    DIR *dp = opendir(state->file_cwd);
    if (dp == NULL) return;

    struct dirent *ent;
    while ((ent = readdir(dp)) != NULL && state->file_count < TUI_MAX_FILES) {
        /* Skip . and hidden files (but allow ..) */
        if (strcmp(ent->d_name, ".") == 0) continue;
        if (ent->d_name[0] == '.' && strcmp(ent->d_name, "..") != 0) continue;

        file_entry *fe = &files[state->file_count];
        memset(fe, 0, sizeof(*fe));
        strncpy(fe->name, ent->d_name, sizeof(fe->name) - 1);

        char full_path[TUI_PATH_MAX + 256];
        snprintf(full_path, sizeof(full_path), "%s/%s",
                 state->file_cwd, ent->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            fe->is_dir = S_ISDIR(st.st_mode);
            fe->size = st.st_size;
        }

        state->file_count++;
    }
    closedir(dp);

    /* Sort: directories first, then alphabetical */
    if (state->file_count > 0) {
        qsort(files, state->file_count, sizeof(file_entry), file_cmp);
    }
}

static const char *format_size(off_t size)
{
    static char buf[32];
    if (size < 1024) {
        snprintf(buf, sizeof(buf), "%ldB", (long)size);
    } else if (size < 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.1fK", (double)size / 1024);
    } else {
        snprintf(buf, sizeof(buf), "%.1fM", (double)size / (1024 * 1024));
    }
    return buf;
}

/* Compute a relative display path from repo root */
static const char *display_path(tui_state *state)
{
    size_t repo_len = strlen(state->repo_path);
    if (strncmp(state->file_cwd, state->repo_path, repo_len) == 0) {
        const char *rel = state->file_cwd + repo_len;
        if (rel[0] == '/') rel++;
        if (rel[0] == '\0') return "./";
        return rel;
    }
    return state->file_cwd;
}

void view_files_render(tui_state *state, int max_y, int max_x)
{
    (void)max_x;
    clear();

    attron(COLOR_PAIR(PAIR_HEADER) | A_BOLD);
    mvprintw(HEADER_ROW, 2, "File Browser");
    attroff(COLOR_PAIR(PAIR_HEADER) | A_BOLD);

    attron(COLOR_PAIR(PAIR_DIM));
    mvprintw(HEADER_ROW + 1, 2, "%s  (%zu items)",
             display_path(state), state->file_count);
    attroff(COLOR_PAIR(PAIR_DIM));

    if (state->file_count == 0) {
        attron(COLOR_PAIR(PAIR_DIM));
        mvprintw(LIST_START, 4, "Empty directory.");
        attroff(COLOR_PAIR(PAIR_DIM));
        tui_render_hint_bar(max_y, max_x, "[Escape back]  [q quit]");
        refresh();
        return;
    }

    int visible = max_y - LIST_START - 2;
    if (visible < 1) visible = 1;

    for (int i = 0; i < visible && (size_t)(i + state->file_scroll) < state->file_count; i++) {
        int idx = i + state->file_scroll;
        const file_entry *fe = &files[idx];

        if (idx == state->file_selected) {
            attron(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
            mvprintw(LIST_START + i, 2, " > ");
            attroff(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
        } else {
            mvprintw(LIST_START + i, 2, "   ");
        }

        if (fe->is_dir) {
            attron(COLOR_PAIR(PAIR_ACCENT) | A_BOLD);
            printw("%-35s", fe->name);
            attroff(COLOR_PAIR(PAIR_ACCENT) | A_BOLD);
            attron(COLOR_PAIR(PAIR_DIM));
            printw("  <DIR>");
            attroff(COLOR_PAIR(PAIR_DIM));
        } else {
            printw("%-35s", fe->name);
            attron(COLOR_PAIR(PAIR_DIM));
            printw("  %s", format_size(fe->size));
            attroff(COLOR_PAIR(PAIR_DIM));
        }
    }

    tui_render_hint_bar(max_y, max_x,
        "[j/k navigate]  [Enter open dir]  [Escape back/up]  [q quit]");
    refresh();
}

int view_files_input(tui_state *state, int ch)
{
    int max_y = getmaxy(stdscr);
    int visible = max_y - LIST_START - 2;
    if (visible < 1) visible = 1;

    switch (ch) {
    case 'j':
    case KEY_DOWN:
        if ((size_t)(state->file_selected + 1) < state->file_count) {
            state->file_selected++;
            if (state->file_selected >= state->file_scroll + visible)
                state->file_scroll++;
        }
        break;

    case 'k':
    case KEY_UP:
        if (state->file_selected > 0) {
            state->file_selected--;
            if (state->file_selected < state->file_scroll)
                state->file_scroll = state->file_selected;
        }
        break;

    case '\n':
    case KEY_ENTER:
        if (state->file_count > 0) {
            const file_entry *fe = &files[state->file_selected];

            if (fe->is_dir) {
                if (strcmp(fe->name, "..") == 0) {
                    /* Go up — pop from dir stack */
                    if (state->file_dir_depth > 0) {
                        state->file_dir_depth--;
                        strncpy(state->file_cwd,
                                state->file_dir_stack[state->file_dir_depth],
                                TUI_PATH_MAX - 1);
                        view_files_load(state, NULL);
                    }
                } else {
                    /* Push current dir and navigate into child */
                    if (state->file_dir_depth < TUI_DIR_STACK_DEPTH) {
                        strncpy(state->file_dir_stack[state->file_dir_depth],
                                state->file_cwd, TUI_PATH_MAX - 1);
                        state->file_dir_depth++;
                    }

                    char new_path[TUI_PATH_MAX + 256];
                    snprintf(new_path, sizeof(new_path), "%s/%s",
                             state->file_cwd, fe->name);
                    view_files_load(state, new_path);
                }
            }
            /* Non-directory files: just show selection, no action for now */
        }
        break;

    case 27: /* Escape */
        if (state->file_dir_depth > 0) {
            /* Go up one level */
            state->file_dir_depth--;
            strncpy(state->file_cwd,
                    state->file_dir_stack[state->file_dir_depth],
                    TUI_PATH_MAX - 1);
            view_files_load(state, NULL);
        } else {
            state->current_view = VIEW_DASHBOARD;
        }
        break;
    }

    return 0;
}
