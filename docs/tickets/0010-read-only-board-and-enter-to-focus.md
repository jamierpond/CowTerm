# 10 Read-only board and Enter to focus

state: in-human
source: ask
owner: cowterm-task-board

Change: Ctrl+A b toggles a board over the window. Each claude running in a
CowTerm pane is a card, grouped by directory + git branch, in needs you ·
working · review · done, with its status as a badge. j/k, h/l move; Enter
focuses the card's pane and closes the board.
Check: `just board-snapshots` renders fixture and live scenes at five shapes;
the live scene shows the claude sessions running in Jamie's panes.

Result: live scene showed 3 real panes (tamber-web-eacp-crash-reporting,
QueenDash, eacp-thinking). Panes are matched by the COWTERM_PANE each claude
inherited, else by directory. Review and done stay empty until ticket 20
stores zones. Enter-to-focus is unproven in the running app: it needs
CowTerm relaunched on this build and a real keypress.
