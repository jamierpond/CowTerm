# 40 n: project picker, worktree, claude in a new pane

state: in-human
source: ask
owner: cowterm-task-board

Change: `n` on the board opens a picker over ~/projects checkouts plus
`gh repo list` for Jamie and each of his organisations (gh api user/orgs),
filtered with the palette's fuzzyScore. Enter asks for the task's title;
Enter again clones if needed, fetches, and adds
`~/projects/<repo>-jp-<title-slug>` on branch `jp/<title-slug>` off
origin/<default>, then opens a session there and types `claude` into it.
Check: TaskBoard/preparesWorktreeOffOriginDefault (scratch repo with a bare
origin) passes; picker renders at five shapes.

Result: 12/12 board tests. Not proven: a real clone from gh and claude
starting in the new pane, which need the relaunched app.
