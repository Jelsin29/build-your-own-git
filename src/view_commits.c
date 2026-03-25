#define _POSIX_C_SOURCE 200809L

#include "tui.h"
#include "object.h"
#include "commit.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#define HEADER_ROW  1
#define LIST_START  3
#define MAX_COMMITS 128

typedef struct {
    char hash[41];
    char message_line[80];
    char author[64];
    int64_t timestamp;
} commit_entry;

static commit_entry commit_list[MAX_COMMITS];
static size_t commit_count;
static int commit_selected;
static int commit_scroll;

static int compare_commits(const void *a, const void *b)
{
    const commit_entry *ca = (const commit_entry *)a;
    const commit_entry *cb = (const commit_entry *)b;
    /* Newest first */
    if (cb->timestamp > ca->timestamp) return 1;
    if (cb->timestamp < ca->timestamp) return -1;
    return 0;
}

void view_commits_load(tui_state *state)
{
    commit_count = 0;
    commit_selected = 0;
    commit_scroll = 0;

    char objects_dir[TUI_PATH_MAX + 16];
    snprintf(objects_dir, sizeof(objects_dir), "%s/.mygit/objects",
             state->repo_path);

    DIR *dp = opendir(objects_dir);
    if (dp == NULL) return;

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
            if (commit_count >= MAX_COMMITS) break;

            char hash[41];
            memcpy(hash, prefix_ent->d_name, 2);
            memcpy(hash + 2, obj_ent->d_name, 38);
            hash[40] = '\0';

            /* Check if it's a commit */
            unsigned char *raw = NULL;
            size_t raw_len = 0;
            if (mygit_object_read(state->repo_path, hash,
                                  &raw, &raw_len) != 0) {
                continue;
            }

            if (raw_len < 6 || memcmp(raw, "commit", 6) != 0) {
                free(raw);
                continue;
            }
            free(raw);

            /* Parse full commit */
            mygit_commit commit;
            if (mygit_commit_read(state->repo_path, hash, &commit) != 0)
                continue;

            commit_entry *entry = &commit_list[commit_count];
            memcpy(entry->hash, hash, 41);
            entry->timestamp = commit.author_time;

            /* First line of message */
            const char *nl = strchr(commit.message, '\n');
            size_t msg_len = nl ? (size_t)(nl - commit.message)
                                : strlen(commit.message);
            if (msg_len > sizeof(entry->message_line) - 1)
                msg_len = sizeof(entry->message_line) - 1;
            memcpy(entry->message_line, commit.message, msg_len);
            entry->message_line[msg_len] = '\0';

            snprintf(entry->author, sizeof(entry->author), "%.63s",
                     commit.author_name);

            commit_count++;
        }
        closedir(sub);
    }
    closedir(dp);

    if (commit_count > 1) {
        qsort(commit_list, commit_count, sizeof(commit_entry),
              compare_commits);
    }
}

void view_commits_render(tui_state *state, int max_y, int max_x)
{
    (void)state;
    clear();

    attron(COLOR_PAIR(PAIR_HEADER) | A_BOLD);
    mvprintw(HEADER_ROW, 2, "Commit Log");
    attroff(COLOR_PAIR(PAIR_HEADER) | A_BOLD);

    if (commit_count == 0) {
        attron(COLOR_PAIR(PAIR_DIM));
        mvprintw(LIST_START + 1, 4, "No commits found.");
        attroff(COLOR_PAIR(PAIR_DIM));
        tui_render_hint_bar(max_y, max_x, "[Escape back]  [q quit]");
        refresh();
        return;
    }

    attron(COLOR_PAIR(PAIR_DIM));
    mvprintw(HEADER_ROW, 15, "(%zu commits)", commit_count);
    attroff(COLOR_PAIR(PAIR_DIM));

    int visible = max_y - LIST_START - 2;
    if (visible < 1) visible = 1;

    for (int i = 0; i < visible && (size_t)(i + commit_scroll) < commit_count; i++) {
        int idx = i + commit_scroll;
        const commit_entry *e = &commit_list[idx];

        /* Format date */
        time_t t = (time_t)e->timestamp;
        struct tm *tm = localtime(&t);
        char datebuf[16];
        strftime(datebuf, sizeof(datebuf), "%Y-%m-%d", tm);

        int max_msg = max_x - 30;
        if (max_msg < 10) max_msg = 10;

        if (idx == commit_selected) {
            attron(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
            mvprintw(LIST_START + i, 2, " > ");
            attroff(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
        } else {
            mvprintw(LIST_START + i, 2, "   ");
        }

        attron(COLOR_PAIR(PAIR_ACCENT));
        printw("%.8s", e->hash);
        attroff(COLOR_PAIR(PAIR_ACCENT));

        printw("  ");
        attron(COLOR_PAIR(PAIR_DIM));
        printw("%s", datebuf);
        attroff(COLOR_PAIR(PAIR_DIM));

        printw("  %.*s", max_msg, e->message_line);
    }

    tui_render_hint_bar(max_y, max_x,
                        "[j/k navigate]  [Enter inspect]  [Escape back]  [q quit]");
    refresh();
}

int view_commits_input(tui_state *state, int ch)
{
    int max_y = getmaxy(stdscr);
    int visible = max_y - LIST_START - 2;
    if (visible < 1) visible = 1;

    switch (ch) {
    case 'j':
    case KEY_DOWN:
        if ((size_t)(commit_selected + 1) < commit_count) {
            commit_selected++;
            if (commit_selected >= commit_scroll + visible)
                commit_scroll++;
        }
        break;

    case 'k':
    case KEY_UP:
        if (commit_selected > 0) {
            commit_selected--;
            if (commit_selected < commit_scroll)
                commit_scroll = commit_selected;
        }
        break;

    case '\n':
    case KEY_ENTER:
        if (commit_count > 0) {
            view_detail_load(state, commit_list[commit_selected].hash);
            state->current_view = VIEW_OBJECT_DETAIL;
        }
        break;

    case 27: /* Escape */
        state->current_view = VIEW_DASHBOARD;
        break;
    }

    return 0;
}
