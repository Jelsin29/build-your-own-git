#define _POSIX_C_SOURCE 200809L

#include "tui.h"
#include "object.h"
#include "tree.h"
#include "commit.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

#define HEADER_ROW  1
#define CONTENT_START 4

static tui_obj_type detect_type(const unsigned char *data, size_t len)
{
    if (len >= 4 && memcmp(data, "blob", 4) == 0) return OBJ_BLOB;
    if (len >= 4 && memcmp(data, "tree", 4) == 0) return OBJ_TREE;
    if (len >= 6 && memcmp(data, "commit", 6) == 0) return OBJ_COMMIT;
    return OBJ_UNKNOWN;
}

void view_detail_load(tui_state *state, const char *hash)
{
    view_detail_free(state);
    strncpy(state->detail_hash, hash, 40);
    state->detail_hash[40] = '\0';
    state->detail_scroll = 0;

    unsigned char *raw = NULL;
    size_t raw_len = 0;
    if (mygit_object_read(state->repo_path, hash, &raw, &raw_len) != 0) {
        return;
    }

    state->detail_type = detect_type(raw, raw_len);
    state->detail_data = raw;
    state->detail_len = raw_len;
}

void view_detail_free(tui_state *state)
{
    free(state->detail_data);
    state->detail_data = NULL;
    state->detail_len = 0;
}

static const unsigned char *skip_header(const unsigned char *data, size_t len,
                                        size_t *content_len)
{
    /* Format: "type size\0content" */
    const unsigned char *nul = memchr(data, '\0', len);
    if (nul == NULL) {
        *content_len = 0;
        return data;
    }
    nul++;
    *content_len = len - (size_t)(nul - data);
    return nul;
}

static void render_blob(tui_state *state, int max_y, int max_x)
{
    size_t content_len = 0;
    const unsigned char *content = skip_header(state->detail_data,
                                               state->detail_len,
                                               &content_len);

    attron(COLOR_PAIR(PAIR_DIM));
    mvprintw(HEADER_ROW + 1, 2, "Size: %zu bytes", content_len);
    attroff(COLOR_PAIR(PAIR_DIM));

    int visible = max_y - CONTENT_START - 2;
    if (visible < 1) visible = 1;

    /* Render content line by line with scrolling */
    int line = 0;
    size_t pos = 0;
    while (pos < content_len) {
        const unsigned char *eol = memchr(content + pos, '\n',
                                          content_len - pos);
        size_t line_len;
        if (eol != NULL) {
            line_len = (size_t)(eol - (content + pos));
        } else {
            line_len = content_len - pos;
        }

        if (line >= state->detail_scroll &&
            line < state->detail_scroll + visible) {
            int row = CONTENT_START + (line - state->detail_scroll);
            int display_len = (int)line_len;
            if (display_len > max_x - 4) display_len = max_x - 4;
            mvprintw(row, 2, "%.*s", display_len, content + pos);
        }

        pos += line_len + 1;
        line++;
    }
}

static void render_tree(tui_state *state, int max_y, int max_x)
{
    (void)max_x;

    mygit_tree_entry *entries = NULL;
    size_t count = 0;
    if (mygit_tree_read(state->repo_path, state->detail_hash,
                        &entries, &count) != 0) {
        mvprintw(CONTENT_START, 2, "Failed to parse tree.");
        return;
    }

    attron(COLOR_PAIR(PAIR_DIM));
    mvprintw(HEADER_ROW + 1, 2, "%zu entries", count);
    attroff(COLOR_PAIR(PAIR_DIM));

    int visible = max_y - CONTENT_START - 2;
    if (visible < 1) visible = 1;

    for (int i = 0; i < visible && (size_t)(i + state->detail_scroll) < count; i++) {
        int idx = i + state->detail_scroll;
        const mygit_tree_entry *e = &entries[idx];

        /* Convert binary hash to hex */
        char hex[41];
        for (int j = 0; j < 20; j++) {
            snprintf(hex + j * 2, 3, "%02x", e->hash[j]);
        }

        const char *kind = (e->mode == MYGIT_MODE_TREE) ? "tree" : "blob";
        int type_color = (e->mode == MYGIT_MODE_TREE) ? PAIR_ACCENT : PAIR_STATUS;

        mvprintw(CONTENT_START + i, 2, "%06o ", e->mode);

        attron(COLOR_PAIR(type_color) | A_BOLD);
        printw("%-4s ", kind);
        attroff(COLOR_PAIR(type_color) | A_BOLD);

        printw("%.8s  %s", hex, e->name);
    }

    free(entries);
}

static void render_commit(tui_state *state, int max_y, int max_x)
{
    (void)max_y;
    (void)max_x;

    mygit_commit commit;
    if (mygit_commit_read(state->repo_path, state->detail_hash,
                          &commit) != 0) {
        mvprintw(CONTENT_START, 2, "Failed to parse commit.");
        return;
    }

    int row = CONTENT_START;

    attron(COLOR_PAIR(PAIR_ACCENT) | A_BOLD);
    mvprintw(row++, 2, "tree    %.8s...", commit.tree_hex);
    attroff(COLOR_PAIR(PAIR_ACCENT) | A_BOLD);

    for (size_t i = 0; i < commit.parent_count; i++) {
        attron(COLOR_PAIR(PAIR_DIM));
        mvprintw(row++, 2, "parent  %.8s...", commit.parent_hex[i]);
        attroff(COLOR_PAIR(PAIR_DIM));
    }

    row++;

    attron(COLOR_PAIR(PAIR_STATUS));
    mvprintw(row++, 2, "Author:    %s <%s>", commit.author_name,
             commit.author_email);
    attroff(COLOR_PAIR(PAIR_STATUS));

    /* Format timestamp */
    time_t t = (time_t)commit.author_time;
    struct tm *tm = localtime(&t);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", tm);
    attron(COLOR_PAIR(PAIR_DIM));
    mvprintw(row++, 2, "Date:      %s %s", timebuf, commit.author_tz);
    attroff(COLOR_PAIR(PAIR_DIM));

    row++;
    mvprintw(row++, 4, "%s", commit.message);
}

void view_detail_render(tui_state *state, int max_y, int max_x)
{
    clear();

    const char *type_str = "unknown";
    switch (state->detail_type) {
    case OBJ_BLOB:   type_str = "blob";   break;
    case OBJ_TREE:   type_str = "tree";   break;
    case OBJ_COMMIT: type_str = "commit"; break;
    case OBJ_UNKNOWN: break;
    }

    attron(COLOR_PAIR(PAIR_HEADER) | A_BOLD);
    mvprintw(HEADER_ROW, 2, "%s %.12s...", type_str, state->detail_hash);
    attroff(COLOR_PAIR(PAIR_HEADER) | A_BOLD);

    if (state->detail_data == NULL) {
        attron(COLOR_PAIR(PAIR_ERROR));
        mvprintw(CONTENT_START, 4, "Failed to read object.");
        attroff(COLOR_PAIR(PAIR_ERROR));
    } else {
        switch (state->detail_type) {
        case OBJ_BLOB:   render_blob(state, max_y, max_x);   break;
        case OBJ_TREE:   render_tree(state, max_y, max_x);   break;
        case OBJ_COMMIT: render_commit(state, max_y, max_x); break;
        case OBJ_UNKNOWN:
            mvprintw(CONTENT_START, 4, "Unknown object type.");
            break;
        }
    }

    tui_render_hint_bar(max_y, max_x, "[j/k scroll]  [Escape back]  [q quit]");
    refresh();
}

int view_detail_input(tui_state *state, int ch)
{
    switch (ch) {
    case 'j':
    case KEY_DOWN:
        state->detail_scroll++;
        break;
    case 'k':
    case KEY_UP:
        if (state->detail_scroll > 0)
            state->detail_scroll--;
        break;
    case 27: /* Escape */
        view_detail_free(state);
        state->current_view = VIEW_OBJECTS;
        break;
    }
    return 0;
}
