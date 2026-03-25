#define _POSIX_C_SOURCE 200809L

#include "tui.h"
#include "blob.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>

#define HEADER_ROW  1
#define LIST_START  3
#define MAX_FILES   256
#define FILENAME_MAX_LEN 128

typedef struct {
    char name[FILENAME_MAX_LEN];
    int is_dir;
} file_entry;

static file_entry file_list[MAX_FILES];
static size_t file_count;
static int file_selected;
static int file_scroll;
static char result_hash[41];
static int has_result;

static int compare_files(const void *a, const void *b)
{
    const file_entry *fa = (const file_entry *)a;
    const file_entry *fb = (const file_entry *)b;
    /* Directories first, then alphabetical */
    if (fa->is_dir != fb->is_dir)
        return fb->is_dir - fa->is_dir;
    return strcmp(fa->name, fb->name);
}

static void load_files(const char *repo_path)
{
    file_count = 0;
    file_selected = 0;
    file_scroll = 0;
    has_result = 0;
    result_hash[0] = '\0';

    DIR *dp = opendir(repo_path);
    if (dp == NULL) return;

    struct dirent *ent;
    while ((ent = readdir(dp)) != NULL) {
        if (ent->d_name[0] == '.') continue;
        if (file_count >= MAX_FILES) break;

        file_entry *fe = &file_list[file_count];
        snprintf(fe->name, sizeof(fe->name), "%.127s", ent->d_name);

        char full_path[TUI_PATH_MAX + FILENAME_MAX_LEN];
        snprintf(full_path, sizeof(full_path), "%s/%.127s",
                 repo_path, ent->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            fe->is_dir = 1;
        } else {
            fe->is_dir = 0;
        }

        file_count++;
    }
    closedir(dp);

    if (file_count > 1) {
        qsort(file_list, file_count, sizeof(file_entry), compare_files);
    }
}

void view_hashfile_render(tui_state *state, int max_y, int max_x)
{
    (void)state;
    (void)max_x;
    clear();

    attron(COLOR_PAIR(PAIR_HEADER) | A_BOLD);
    mvprintw(HEADER_ROW, 2, "Hash File as Blob");
    attroff(COLOR_PAIR(PAIR_HEADER) | A_BOLD);

    if (has_result) {
        attron(COLOR_PAIR(PAIR_STATUS) | A_BOLD);
        mvprintw(HEADER_ROW + 1, 2, "Stored: %s", result_hash);
        attroff(COLOR_PAIR(PAIR_STATUS) | A_BOLD);
    }

    if (file_count == 0) {
        attron(COLOR_PAIR(PAIR_DIM));
        mvprintw(LIST_START + 1, 4, "No files found in repository.");
        attroff(COLOR_PAIR(PAIR_DIM));
        tui_render_hint_bar(max_y, max_x, "[Escape back]  [q quit]");
        refresh();
        return;
    }

    attron(COLOR_PAIR(PAIR_DIM));
    mvprintw(HEADER_ROW, 22, "(%zu files)", file_count);
    attroff(COLOR_PAIR(PAIR_DIM));

    int visible = max_y - LIST_START - 2;
    if (visible < 1) visible = 1;

    for (int i = 0; i < visible && (size_t)(i + file_scroll) < file_count; i++) {
        int idx = i + file_scroll;
        const file_entry *fe = &file_list[idx];

        if (idx == file_selected) {
            attron(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
            mvprintw(LIST_START + i, 2, " > ");
            attroff(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
        } else {
            mvprintw(LIST_START + i, 2, "   ");
        }

        if (fe->is_dir) {
            attron(COLOR_PAIR(PAIR_ACCENT) | A_BOLD);
            printw("%s/", fe->name);
            attroff(COLOR_PAIR(PAIR_ACCENT) | A_BOLD);
        } else {
            printw("%s", fe->name);
        }
    }

    tui_render_hint_bar(max_y, max_x,
                        "[j/k navigate]  [Enter hash file]  [Escape back]  [q quit]");
    refresh();
}

int view_hashfile_input(tui_state *state, int ch)
{
    int max_y = getmaxy(stdscr);
    int visible = max_y - LIST_START - 2;
    if (visible < 1) visible = 1;

    switch (ch) {
    case 'j':
    case KEY_DOWN:
        if ((size_t)(file_selected + 1) < file_count) {
            file_selected++;
            if (file_selected >= file_scroll + visible)
                file_scroll++;
        }
        break;

    case 'k':
    case KEY_UP:
        if (file_selected > 0) {
            file_selected--;
            if (file_selected < file_scroll)
                file_scroll = file_selected;
        }
        break;

    case '\n':
    case KEY_ENTER:
        if (file_count > 0 && !file_list[file_selected].is_dir) {
            char file_path[TUI_PATH_MAX + FILENAME_MAX_LEN];
            snprintf(file_path, sizeof(file_path), "%s/%.127s",
                     state->repo_path, file_list[file_selected].name);

            if (mygit_blob_write(state->repo_path, file_path,
                                 result_hash) == 0) {
                has_result = 1;
            }
        }
        break;

    case 27: /* Escape */
        state->current_view = VIEW_DASHBOARD;
        break;
    }

    return 0;
}

void view_hashfile_load(tui_state *state)
{
    load_files(state->repo_path);
}
