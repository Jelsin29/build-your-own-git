#define _POSIX_C_SOURCE 200809L

#include "tui.h"
#include "commit.h"

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#define HEADER_ROW      1
#define FORM_START      3
#define LABEL_COL       4
#define INPUT_COL       20
#define FIELD_COUNT     5

/* Maximum lengths for each input field (excluding NUL) */
#define HASH_LEN        40
#define NAME_LEN        128
#define EMAIL_LEN       128
#define MESSAGE_LEN     512

typedef enum {
    FIELD_TREE_HASH,
    FIELD_PARENT_HASH,
    FIELD_AUTHOR_NAME,
    FIELD_AUTHOR_EMAIL,
    FIELD_MESSAGE,
} form_field;

static char field_tree[HASH_LEN + 1];
static char field_parent[HASH_LEN + 1];
static char field_name[NAME_LEN + 1];
static char field_email[EMAIL_LEN + 1];
static char field_message[MESSAGE_LEN + 1];

static int active_field;
static char result_msg[128];
static int result_color;

static const char *field_labels[FIELD_COUNT] = {
    "Tree hash:",
    "Parent hash:",
    "Author name:",
    "Author email:",
    "Message:",
};

static char *field_buffers[FIELD_COUNT];

static size_t field_max_lens[FIELD_COUNT] = {
    HASH_LEN,
    HASH_LEN,
    NAME_LEN,
    EMAIL_LEN,
    MESSAGE_LEN,
};

static void init_field_pointers(void)
{
    field_buffers[FIELD_TREE_HASH]   = field_tree;
    field_buffers[FIELD_PARENT_HASH] = field_parent;
    field_buffers[FIELD_AUTHOR_NAME] = field_name;
    field_buffers[FIELD_AUTHOR_EMAIL] = field_email;
    field_buffers[FIELD_MESSAGE]     = field_message;
}

static int is_valid_hex(const char *s, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (!isxdigit((unsigned char)s[i]))
            return 0;
    }
    return 1;
}

static int try_submit(tui_state *state)
{
    /* Validate tree hash */
    size_t tree_len = strlen(field_tree);
    if (tree_len != HASH_LEN || !is_valid_hex(field_tree, HASH_LEN)) {
        snprintf(result_msg, sizeof(result_msg),
                 "Error: tree hash must be exactly 40 hex characters");
        result_color = PAIR_ERROR;
        return -1;
    }

    /* Validate optional parent hash */
    size_t parent_len = strlen(field_parent);
    if (parent_len > 0 &&
        (parent_len != HASH_LEN || !is_valid_hex(field_parent, HASH_LEN))) {
        snprintf(result_msg, sizeof(result_msg),
                 "Error: parent hash must be 40 hex characters or empty");
        result_color = PAIR_ERROR;
        return -1;
    }

    /* Validate required fields */
    if (strlen(field_name) == 0) {
        snprintf(result_msg, sizeof(result_msg),
                 "Error: author name is required");
        result_color = PAIR_ERROR;
        return -1;
    }

    if (strlen(field_email) == 0) {
        snprintf(result_msg, sizeof(result_msg),
                 "Error: author email is required");
        result_color = PAIR_ERROR;
        return -1;
    }

    if (strlen(field_message) == 0) {
        snprintf(result_msg, sizeof(result_msg),
                 "Error: commit message is required");
        result_color = PAIR_ERROR;
        return -1;
    }

    /* Build commit struct */
    mygit_commit commit;
    memset(&commit, 0, sizeof(commit));

    memcpy(commit.tree_hex, field_tree, HASH_LEN + 1);

    if (parent_len == HASH_LEN) {
        memcpy(commit.parent_hex[0], field_parent, HASH_LEN + 1);
        commit.parent_count = 1;
    }

    snprintf(commit.author_name, sizeof(commit.author_name), "%s", field_name);
    snprintf(commit.author_email, sizeof(commit.author_email), "%s",
             field_email);

    /* Use current time */
    commit.author_time = (int64_t)time(NULL);
    snprintf(commit.author_tz, sizeof(commit.author_tz), "+0000");

    /* Committer = author */
    snprintf(commit.committer_name, sizeof(commit.committer_name), "%s",
             field_name);
    snprintf(commit.committer_email, sizeof(commit.committer_email), "%s",
             field_email);
    commit.committer_time = commit.author_time;
    snprintf(commit.committer_tz, sizeof(commit.committer_tz), "+0000");

    snprintf(commit.message, sizeof(commit.message), "%s", field_message);

    /* Write commit */
    char hex_out[41];
    if (mygit_commit_write(state->repo_path, &commit, hex_out) != 0) {
        snprintf(result_msg, sizeof(result_msg),
                 "Error: failed to write commit object");
        result_color = PAIR_ERROR;
        return -1;
    }

    snprintf(result_msg, sizeof(result_msg), "Created commit: %s", hex_out);
    result_color = PAIR_STATUS;
    return 0;
}

void view_commitform_render(tui_state *state, int max_y, int max_x)
{
    (void)state;
    clear();

    attron(COLOR_PAIR(PAIR_HEADER) | A_BOLD);
    mvprintw(HEADER_ROW, 2, "Create Commit");
    attroff(COLOR_PAIR(PAIR_HEADER) | A_BOLD);

    /* Result message */
    if (result_msg[0] != '\0') {
        attron(COLOR_PAIR(result_color) | A_BOLD);
        mvprintw(HEADER_ROW + 1, 2, "%s", result_msg);
        attroff(COLOR_PAIR(result_color) | A_BOLD);
    }

    int input_width = max_x - INPUT_COL - 4;
    if (input_width < 10)
        input_width = 10;

    for (int i = 0; i < FIELD_COUNT; i++) {
        int row = FORM_START + i * 2;

        /* Label */
        attron(COLOR_PAIR(PAIR_ACCENT));
        mvprintw(row, LABEL_COL, "%-15s", field_labels[i]);
        attroff(COLOR_PAIR(PAIR_ACCENT));

        /* Input field */
        if (i == active_field) {
            attron(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
        }

        /* Draw input box background */
        mvprintw(row, INPUT_COL, "[");
        size_t buf_len = strlen(field_buffers[i]);
        size_t display_max = (size_t)(input_width - 2);
        if (display_max > field_max_lens[i])
            display_max = field_max_lens[i];

        /* Show the tail of the string if it overflows the visible area */
        const char *display_str = field_buffers[i];
        if (buf_len > display_max) {
            display_str = field_buffers[i] + (buf_len - display_max);
            buf_len = display_max;
        }

        printw("%-*.*s", (int)display_max, (int)display_max, display_str);
        printw("]");

        if (i == active_field) {
            attroff(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
        }

        /* Show hint for optional field */
        if (i == FIELD_PARENT_HASH) {
            attron(COLOR_PAIR(PAIR_DIM));
            printw("  (optional)");
            attroff(COLOR_PAIR(PAIR_DIM));
        }
    }

    /* Submit button */
    int submit_row = FORM_START + FIELD_COUNT * 2 + 1;
    if (active_field == FIELD_COUNT) {
        /* Not reachable in current design, but defensive */
        attron(COLOR_PAIR(PAIR_HIGHLIGHT) | A_BOLD);
    }
    attron(COLOR_PAIR(PAIR_STATUS) | A_BOLD);
    mvprintw(submit_row, INPUT_COL, "[ Ctrl-S: Submit ]");
    attroff(COLOR_PAIR(PAIR_STATUS) | A_BOLD);

    tui_render_hint_bar(max_y, max_x,
                        "[Tab next field]  [Ctrl-S submit]  [Escape back]  [q quit]");
    refresh();
}

int view_commitform_input(tui_state *state, int ch)
{
    switch (ch) {
    case '\t':
        /* Cycle to next field */
        active_field = (active_field + 1) % FIELD_COUNT;
        break;

    case KEY_BTAB:
        /* Shift-Tab: cycle backwards */
        active_field = (active_field + FIELD_COUNT - 1) % FIELD_COUNT;
        break;

    case 19: /* Ctrl-S */
        try_submit(state);
        break;

    case 27: /* Escape */
        state->current_view = VIEW_DASHBOARD;
        break;

    case KEY_BACKSPACE:
    case 127:
    case 8: {
        char *buf = field_buffers[active_field];
        size_t len = strlen(buf);
        if (len > 0) {
            buf[len - 1] = '\0';
        }
        break;
    }

    default:
        /* Printable character input */
        if (ch >= 32 && ch <= 126) {
            char *buf = field_buffers[active_field];
            size_t len = strlen(buf);
            if (len < field_max_lens[active_field]) {
                buf[len] = (char)ch;
                buf[len + 1] = '\0';
            }
        }
        break;
    }

    return 0;
}

void view_commitform_load(tui_state *state)
{
    (void)state;

    init_field_pointers();

    field_tree[0]    = '\0';
    field_parent[0]  = '\0';
    field_name[0]    = '\0';
    field_email[0]   = '\0';
    field_message[0] = '\0';

    active_field = 0;
    result_msg[0] = '\0';
    result_color = PAIR_NORMAL;
}
