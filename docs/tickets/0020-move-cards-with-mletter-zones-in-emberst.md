# 20 Move cards with m+letter, zones in emberstore, tell the agent

state: in-human
source: ask
owner: cowterm-task-board

Change: on the board, `m` then `w`/`r`/`d` moves the selected card to working,
review or done. The zone is stored in emberstore
(~/Library/Application Support/tamber/cowterm-board/board.json), and the
card's pane gets "Jamie moved this to <zone>. <what it means>" typed in, then
Enter 150 ms later (claude reads text and newline in one burst as a paste).
A card claude reports as needing Jamie stays in needs you whatever is stored.
Check: CowTermBoardTests (storedZoneShownUnlessNeedsYou, moveMessages...) pass.

Result: 9/9 tests pass. Not proven: the typed message reaching a live
claude, which needs the relaunched app and a real m keypress.
