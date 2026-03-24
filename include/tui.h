#ifndef MYGIT_TUI_H
#define MYGIT_TUI_H

/*
 * Launch the interactive TUI for mygit.
 *
 * Initializes ncurses, runs the main event loop,
 * and restores terminal state on exit.
 *
 * Returns 0 on success, -1 on error.
 */
int tui_run(const char *repo_path);

#endif /* MYGIT_TUI_H */
