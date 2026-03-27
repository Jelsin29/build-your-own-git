#define _POSIX_C_SOURCE 200809L

#include "tui.h"
#include "ref.h"
#include "commit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define HEADER_ROW  1
#define LIST_START  4

/* Branch list storage — name capped to 64 for display sanity */
#define BRANCH_NAME_MAX 64

typedef struct {
    char name[BRANCH_NAME_MAX];
    char hex[41];
    int is_current;
} branch_entry;

static branch_entry branches[TUI_MAX_BRANCHES];
static char current_branch[256];

static int collect_branch(const char *refname, const char hex[41],
                          void *user_data)
{
    tui_state *state = (tui_state *)user_data;
    if (state->branch_count >= TUI_MAX_BRANCHES) return 1;

    branch_entry *b = &branches[state->branch_count];
    memset(b, 0, sizeof(*b));

    /* Strip refs/heads/ prefix */
    const char *name = refname;
    if (strncmp(refname, "refs/heads/", 11) == 0) {
        name = refname + 11;
    }
    strncpy(b->name, name, BRANCH_NAME_MAX - 1);
    b->name[BRANCH_NAME_MAX - 1] = '\0';
    memcpy(b->hex, hex, 41);
    b->is_current = (strcmp(name, current_branch) == 0);

    state->branch_count++;
    return 0;
}

void view_branches_load(tui_state *state)
{
    state->branch_count = 0;
    state->branch_selected = 0;
    state->branch_scroll = 0;
    state->branch_input_mode = 0;
    state->branch_input[0] = '\0';
    state->branch_input_len = 0;

    /* Detect current branch from HEAD */
    current_branch[0] = '\0';
    char head_path[TUI_PATH_MAX + 16];
    snprintf(head_path, sizeof(head_path), "%s/.mygit/HEAD", state->repo_path);
    FILE *fp = fopen(head_path, "r");
    if (fp != NULL) {
        char line[256];
        if (fgets(line, (int)sizeof(line), fp) != NULL) {
            size_t len = strlen(line);
            if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';
            if (strncmp(line, "ref: refs/heads/", 16) == 0) {
                strncpy(current_branch, line + 16,
                        sizeof(current_branch) - 1);
            }
        }
        fclose(fp);
    }

    mygit_ref_list(state->repo_path, "refs/heads", collect_branch, state);
}

void view_branches_render(tui_state *state, int max_y, int max_x)
{
    (void)max_x;
    clear();

    attron(COLOR_PAIR(PAIR_HEADER) | A_BOLD);
    mvprintw(HEADER_ROW, 2, "Branch Manager");
    attroff(COLOR_PAIR(PAIR_HEADER) | A_BOLD);

    attron(COLOR_PAIR(PAIR_DIM));
    mvprintw(HEADER_ROW + 1, 2, "%zu branches  |  current: %s",
             state->branch_count,
             current_branch[0] ? current_branch : "(detached)");
    attroff(COLOR_PAIR(PAIR_DIM));

    if (state->branch_count == 0) {
        attron(COLOR_PAIR(PAIR_DIM));
        mvprintw(LIST_START, 4, "No branches yet. Press 'n' to create one.");
        attroff(COLOR_PAIR(PAIR_DIM));
    }

    int visible = max_y - LIST_START - 3;
    if (visible < 1) visible = 1;

    for (int i = 0; i < visible && (size_t)(i + state->branch_scroll) < state->branch_count; i++) {
        int idx = i + state->branch_scroll;
        const branch_entry *b = &branches[idx];

        if (idx == state->branch_selected) {
            attron(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
            mvprintw(LIST_START + i, 2, " > ");
            attroff(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
        } else {
            mvprintw(LIST_START + i, 2, "   ");
        }

        if (b->is_current) {
            attron(COLOR_PAIR(PAIR_STATUS) | A_BOLD);
            printw("* %-20s", b->name);
            attroff(COLOR_PAIR(PAIR_STATUS) | A_BOLD);
        } else {
            printw("  %-20s", b->name);
        }

        attron(COLOR_PAIR(PAIR_DIM));
        printw("  %.7s", b->hex);
        attroff(COLOR_PAIR(PAIR_DIM));

        /* Show commit message preview */
        mygit_commit c;
        if (mygit_commit_read(state->repo_path, b->hex, &c) == 0) {
            attron(COLOR_PAIR(PAIR_DIM));
            printw("  %.40s", c.message);
            attroff(COLOR_PAIR(PAIR_DIM));
        }
    }

    /* Input mode: show text field for new branch name */
    if (state->branch_input_mode) {
        int input_y = max_y - 3;
        attron(COLOR_PAIR(PAIR_ACCENT) | A_BOLD);
        mvprintw(input_y, 2, "New branch name: ");
        attroff(COLOR_PAIR(PAIR_ACCENT) | A_BOLD);
        attron(A_UNDERLINE);
        printw("%-30s", state->branch_input);
        attroff(A_UNDERLINE);
    }

    /* Status message */
    if (state->branch_status[0] != '\0') {
        attron(COLOR_PAIR(state->branch_status_color) | A_BOLD);
        mvprintw(max_y - 4, 2, "%s", state->branch_status);
        attroff(COLOR_PAIR(state->branch_status_color) | A_BOLD);
    }

    const char *hints = state->branch_input_mode
        ? "[Enter confirm]  [Escape cancel]"
        : "[j/k nav]  [n new]  [Enter checkout]  [d delete]  [Escape back]";
    tui_render_hint_bar(max_y, max_x, hints);
    refresh();
}

int view_branches_input(tui_state *state, int ch)
{
    /* Input mode — capturing branch name */
    if (state->branch_input_mode) {
        if (ch == 27) { /* Escape — cancel */
            state->branch_input_mode = 0;
            state->branch_input[0] = '\0';
            state->branch_input_len = 0;
            return 0;
        }

        if (ch == '\n' || ch == KEY_ENTER) {
            /* Create the branch */
            if (state->branch_input_len == 0) {
                state->branch_input_mode = 0;
                return 0;
            }

            char head_hex[41];
            int ret = mygit_head_resolve(state->repo_path, head_hex);
            if (ret != 0) {
                snprintf(state->branch_status, sizeof(state->branch_status),
                         "Error: no commits yet — cannot create branch");
                state->branch_status_color = PAIR_ERROR;
                state->branch_input_mode = 0;
                return 0;
            }

            char refname[MYGIT_MAX_REFNAME];
            snprintf(refname, sizeof(refname), "refs/heads/%s",
                     state->branch_input);

            /* Check if branch already exists */
            char existing[41];
            if (mygit_ref_resolve(state->repo_path, refname, existing) == 0) {
                snprintf(state->branch_status, sizeof(state->branch_status),
                         "Branch '%.30s' already exists", state->branch_input);
                state->branch_status_color = PAIR_ERROR;
                state->branch_input_mode = 0;
                return 0;
            }

            if (mygit_ref_update(state->repo_path, refname, head_hex) == -1) {
                snprintf(state->branch_status, sizeof(state->branch_status),
                         "Failed to create branch '%.30s'", state->branch_input);
                state->branch_status_color = PAIR_ERROR;
            } else {
                snprintf(state->branch_status, sizeof(state->branch_status),
                         "Created branch '%.30s' at %.7s",
                         state->branch_input, head_hex);
                state->branch_status_color = PAIR_STATUS;
            }

            state->branch_input_mode = 0;
            state->branch_input[0] = '\0';
            state->branch_input_len = 0;

            /* Reload branch list */
            view_branches_load(state);
            return 0;
        }

        if (ch == KEY_BACKSPACE || ch == 127 || ch == '\b') {
            if (state->branch_input_len > 0) {
                state->branch_input_len--;
                state->branch_input[state->branch_input_len] = '\0';
            }
            return 0;
        }

        /* Regular character input */
        if (ch >= 32 && ch < 127 && state->branch_input_len < sizeof(state->branch_input) - 1) {
            state->branch_input[state->branch_input_len++] = (char)ch;
            state->branch_input[state->branch_input_len] = '\0';
        }

        return 0;
    }

    /* Browse mode */
    int max_y = getmaxy(stdscr);
    int visible = max_y - LIST_START - 3;
    if (visible < 1) visible = 1;

    switch (ch) {
    case 'j':
    case KEY_DOWN:
        if ((size_t)(state->branch_selected + 1) < state->branch_count) {
            state->branch_selected++;
            if (state->branch_selected >= state->branch_scroll + visible)
                state->branch_scroll++;
        }
        break;

    case 'k':
    case KEY_UP:
        if (state->branch_selected > 0) {
            state->branch_selected--;
            if (state->branch_selected < state->branch_scroll)
                state->branch_scroll = state->branch_selected;
        }
        break;

    case 'n': /* New branch */
        state->branch_input_mode = 1;
        state->branch_input[0] = '\0';
        state->branch_input_len = 0;
        state->branch_status[0] = '\0';
        break;

    case 'd': /* Delete branch */
        if (state->branch_count > 0) {
            const branch_entry *b = &branches[state->branch_selected];
            if (b->is_current) {
                snprintf(state->branch_status, sizeof(state->branch_status),
                         "Cannot delete current branch '%.30s'", b->name);
                state->branch_status_color = PAIR_ERROR;
            } else {
                char ref_path[TUI_PATH_MAX + 96];
                snprintf(ref_path, sizeof(ref_path),
                         "%s/.mygit/refs/heads/%s",
                         state->repo_path, b->name);
                if (unlink(ref_path) == 0) {
                    snprintf(state->branch_status,
                             sizeof(state->branch_status),
                             "Deleted branch '%.30s'", b->name);
                    state->branch_status_color = PAIR_STATUS;
                    view_branches_load(state);
                } else {
                    snprintf(state->branch_status,
                             sizeof(state->branch_status),
                             "Failed to delete '%.30s'", b->name);
                    state->branch_status_color = PAIR_ERROR;
                }
            }
        }
        break;

    case '\n':
    case KEY_ENTER:
        /* Checkout selected branch */
        if (state->branch_count > 0) {
            const branch_entry *b = &branches[state->branch_selected];
            if (b->is_current) {
                snprintf(state->branch_status, sizeof(state->branch_status),
                         "Already on '%.30s'", b->name);
                state->branch_status_color = PAIR_ACCENT;
            } else {
                /* Update HEAD to point to this branch */
                char head_path2[TUI_PATH_MAX + 16];
                snprintf(head_path2, sizeof(head_path2),
                         "%s/.mygit/HEAD", state->repo_path);
                FILE *fp = fopen(head_path2, "w");
                if (fp != NULL) {
                    fprintf(fp, "ref: refs/heads/%s\n", b->name);
                    fclose(fp);
                    snprintf(state->branch_status,
                             sizeof(state->branch_status),
                             "Switched to branch '%.30s'", b->name);
                    state->branch_status_color = PAIR_STATUS;
                    /* Reload to update current marker */
                    view_branches_load(state);
                } else {
                    snprintf(state->branch_status,
                             sizeof(state->branch_status),
                             "Failed to switch branch");
                    state->branch_status_color = PAIR_ERROR;
                }
            }
        }
        break;

    case 27: /* Escape */
        state->current_view = VIEW_DASHBOARD;
        break;
    }

    return 0;
}
