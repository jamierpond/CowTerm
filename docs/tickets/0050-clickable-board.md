# 50 Clickable board

state: in-human
source: ask
owner: cowterm-task-board

Change: a click selects a card; a click on the selected card, or a
double-click, goes to its pane like Enter. Dragging a card (past 4 points)
highlights the column under the pointer, and dropping it there moves it with
the same rules and pane message as `m` (no drops on needs you). Clicking a
picker row picks that project. Hit-testing reads the card and column rects
recorded by the last paint.
Check: CowTermBoardViewTests drive the component's own mouse handlers on an
offscreen render; `mid-drag-*.png` in docs/screenshots/board.

Result: 6/6 view tests, 71/71 ctest. In the narrow single-list layout only
filled sections are drop targets, so a card can't be dragged to an empty
zone there (m still works). No real-mouse check: needs the relaunched app.
