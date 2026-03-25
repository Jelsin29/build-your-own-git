#include "tui.h"
#include "object.h"
#include "repo.h"

#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define HEADER_START_Y    1
#define SUBTITLE_OFFSET   1
#define MENU_OFFSET       4
#define MENU_LEFT_COL     4
#define MENU_ITEM_WIDTH   30

static const char *ascii_header[] = {
    "     ______  _______    _____      _____             _____     ____  _________________ ",
    "    |      \\/       \\  |\\    \\    /    /|        ___|\\    \\   |    |/                 \\",
    "   /          /\\     \\ | \\    \\  /    / |       /    /\\    \\  |    |\\______     ______/",
    "  /     /\\   / /\\     ||  \\____\\/    /  /      |    |  |____| |    |   \\( /    /  )/   ",
    " /     /\\ \\_/ / /    /| \\ |    /    /  /       |    |    ____ |    |    ' |   |   '    ",
    "|     |  \\|_|/ /    / |  \\|___/    /  /        |    |   |    ||    |      |   |        ",
    "|     |       |    |  |      /    /  /         |    |   |_,  ||    |     /   //        ",
    "|\\____\\       |____|  /     /____/  /          |\\ ___\\___/  /||____|    /___//         ",
    "| |    |      |    | /     |`    | /           | |   /____ / ||    |   |`   |          ",
    " \\|____|      |____|/      |_____|/             \\|___|    | / |____|   |____|          ",
    "    \\(          )/            )/                  \\( |____|/    \\(       \\(            ",
    "     '          '             '                    '   )/        '        '            ",
};

static const int header_lines = 12;

static const char *menu_items[] = {
    "Init repository",
    "Hash a file",
    "Browse objects",
    "Explore trees",
    "Create commit",
    "View commit log",
    "Quit",
};

static const int menu_count = 7;

#define STATS_COL 46

static char status_msg[128];
static int status_color = PAIR_STATUS;

typedef struct {
    int blobs;
    int trees;
    int commits;
    int total;
} repo_stats;

static repo_stats scan_repo_stats(const char *repo_path)
{
    repo_stats stats = {0, 0, 0, 0};
    char objects_dir[TUI_PATH_MAX + 16];
    snprintf(objects_dir, sizeof(objects_dir), "%s/.mygit/objects", repo_path);

    DIR *dp = opendir(objects_dir);
    if (dp == NULL) return stats;

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
                if (raw_len >= 4 && memcmp(raw, "blob", 4) == 0)
                    stats.blobs++;
                else if (raw_len >= 4 && memcmp(raw, "tree", 4) == 0)
                    stats.trees++;
                else if (raw_len >= 6 && memcmp(raw, "commit", 6) == 0)
                    stats.commits++;
                stats.total++;
                free(raw);
            }
        }
        closedir(sub);
    }
    closedir(dp);
    return stats;
}

void view_dashboard_render(tui_state *state, int max_y, int max_x)
{
    clear();

    /* Draw ASCII header in cyan */
    attron(COLOR_PAIR(PAIR_HEADER) | A_BOLD);
    for (int i = 0; i < header_lines; i++) {
        int x = (max_x - (int)strlen(ascii_header[i])) / 2;
        if (x < 0) x = 0;
        mvprintw(HEADER_START_Y + i, x, "%s", ascii_header[i]);
    }
    attroff(COLOR_PAIR(PAIR_HEADER) | A_BOLD);

    /* Subtitle */
    const char *sub = "< mygit 1.0 -- content-addressed storage >";
    int sub_y = HEADER_START_Y + header_lines + SUBTITLE_OFFSET;
    attron(COLOR_PAIR(PAIR_DIM));
    mvprintw(sub_y, (max_x - (int)strlen(sub)) / 2, "%s", sub);
    attroff(COLOR_PAIR(PAIR_DIM));

    /* Menu section */
    int menu_y = HEADER_START_Y + header_lines + MENU_OFFSET;

    attron(COLOR_PAIR(PAIR_ACCENT) | A_BOLD);
    mvprintw(menu_y, MENU_LEFT_COL, "Actions");
    attroff(COLOR_PAIR(PAIR_ACCENT) | A_BOLD);

    attron(COLOR_PAIR(PAIR_DIM));
    mvhline(menu_y + 1, MENU_LEFT_COL, ACS_HLINE, 20);
    attroff(COLOR_PAIR(PAIR_DIM));

    /* Menu items with selection highlight */
    int items_y = menu_y + 2;
    for (int i = 0; i < menu_count; i++) {
        if (i == state->menu_index) {
            attron(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
            mvprintw(items_y + i, MENU_LEFT_COL, " > %-*s", MENU_ITEM_WIDTH, menu_items[i]);
            attroff(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
        } else {
            mvprintw(items_y + i, MENU_LEFT_COL, "   %-*s", MENU_ITEM_WIDTH, menu_items[i]);
        }
    }

    /* Status message */
    if (status_msg[0] != '\0') {
        attron(COLOR_PAIR(status_color) | A_BOLD);
        mvprintw(items_y + menu_count + 1, MENU_LEFT_COL, "%s", status_msg);
        attroff(COLOR_PAIR(status_color) | A_BOLD);
    }

    /* Stats panel on the right */
    repo_stats stats = scan_repo_stats(state->repo_path);
    int stats_y = menu_y;

    attron(COLOR_PAIR(PAIR_ACCENT) | A_BOLD);
    mvprintw(stats_y, STATS_COL, "Repository");
    attroff(COLOR_PAIR(PAIR_ACCENT) | A_BOLD);

    attron(COLOR_PAIR(PAIR_DIM));
    mvhline(stats_y + 1, STATS_COL, ACS_HLINE, 20);
    attroff(COLOR_PAIR(PAIR_DIM));

    mvprintw(stats_y + 2, STATS_COL, "   Objects: ");
    attron(A_BOLD);
    printw("%d", stats.total);
    attroff(A_BOLD);

    mvprintw(stats_y + 3, STATS_COL, "   ");
    attron(COLOR_PAIR(PAIR_STATUS));
    printw("blobs:   %d", stats.blobs);
    attroff(COLOR_PAIR(PAIR_STATUS));

    mvprintw(stats_y + 4, STATS_COL, "   ");
    attron(COLOR_PAIR(PAIR_ACCENT));
    printw("trees:   %d", stats.trees);
    attroff(COLOR_PAIR(PAIR_ACCENT));

    mvprintw(stats_y + 5, STATS_COL, "   ");
    attron(COLOR_PAIR(PAIR_HEADER));
    printw("commits: %d", stats.commits);
    attroff(COLOR_PAIR(PAIR_HEADER));

    /* Key hint bar */
    tui_render_hint_bar(max_y, max_x, "[j/k navigate]  [Enter select]  [q quit]");

    refresh();
}

int view_dashboard_input(tui_state *state, int ch)
{
    switch (ch) {
    case 'j':
    case KEY_DOWN:
        if (state->menu_index < menu_count - 1) {
            state->menu_index++;
        }
        break;

    case 'k':
    case KEY_UP:
        if (state->menu_index > 0) {
            state->menu_index--;
        }
        break;

    case '\n':
    case KEY_ENTER:
        switch (state->menu_index) {
        case 0: {
            int rc = mygit_init(state->repo_path);
            if (rc == 0) {
                snprintf(status_msg, sizeof(status_msg),
                         "Initialized .mygit/ repository");
                status_color = PAIR_STATUS;
            } else if (rc == 1) {
                snprintf(status_msg, sizeof(status_msg),
                         ".mygit/ already exists");
                status_color = PAIR_ACCENT;
            } else {
                snprintf(status_msg, sizeof(status_msg),
                         "Failed to initialize repository");
                status_color = PAIR_ERROR;
            }
            break;
        }
        case 1:
            state->current_view = VIEW_HASH_FILE;
            break;
        case 2:
            state->current_view = VIEW_OBJECTS;
            break;
        case 3:
            state->current_view = VIEW_TREE;
            break;
        case 4:
            state->current_view = VIEW_COMMIT_CREATE;
            break;
        case 5:
            state->current_view = VIEW_COMMIT_LOG;
            break;
        case 6: /* Quit */
            return -1;
        }
        break;
    }

    return 0;
}
