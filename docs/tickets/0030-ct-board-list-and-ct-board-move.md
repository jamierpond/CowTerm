# 30 ct board list and ct board move

state: in-human
source: ask
owner: cowterm-task-board

Change: `ct board list` prints every card (zone, key, title, status) with
stored zones applied. `ct board move <review|done> [--card <key>]` moves the
card of the pane it runs in (found by COWTERM_PANE, else directory + branch);
other zones are refused for agents (exit 1). The open board picks the change
up on its next refresh (2 s).
Check: run both against the real store with a scratch key.

Result: list printed the 3 live pane cards; `move working` refused (exit 1),
`move review --card /tmp/scratch@x` stored it; scratch store deleted after.
