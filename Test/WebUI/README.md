# Web UI tests

The C++ suite (`just test`) covers the wire: framing, screen serialization,
command names, layout round-trips. It cannot cover the half of the web UI
that lives in the browser — key handling, focus, and CSS layout — and every
bug found in this area so far lived exactly there:

- shifted leader keys (`"` `%` `H J K L`) fell through to the shell, because
  `Shift`'s own keydown disarmed the leader before the real key arrived;
- a lone pane's `.leaf` collapsed to the 2px of its border, so the terminal
  painted over a box too small to click;
- clicking a pane moved focus to `<body>`, leaving the terminal unable to
  type.

None of those are visible from C++, and all of them are obvious the moment a
real browser drives the page. Hence this harness.

## Running

Needs node and Playwright's chromium:

    npm install playwright && npx playwright install chromium

Then, against a running CowTerm (see *Isolation* below):

    just test-webui              # against the test instance on :3697
    PORT=2697 node drive.js      # against your everyday instance

`drive.js` asserts behaviour: the leader table, and that repeated Ctrl+C both
reaches the shell and renders. `parity.js` is the stronger claim — it drives
keys through the browser and then diffs the page's rendered rows against
`/api/v1/panes/{id}/screen`, which is the owning CowTerm's own `TermScreen`.
Equal output there means the remote experience *is* the local one, rather
than merely resembling it.

## Isolation

CowTerm was a singleton by construction — one hardcoded config path, one
hardcoded daemon name — so a second copy silently shared the first's
sessions and lost the race for the web port. `$COWTERM_INSTANCE` gives a
build under test its own config file (`~/.config/cowterm.<name>.json`), its
own daemon (`cowtermd-<name>`) and its own session store, so it can run
beside the CowTerm you actually use without touching it:

    COWTERM_INSTANCE=test ./build/Terminal/CowTerm.app/Contents/MacOS/CowTerm

`restart.sh` brings such an instance up from scratch. It kills the daemon
too, which is easy to forget: the daemon outlives the GUI on purpose, so a
restart that leaves it running will restore *stale panes whose shells have
already exited* — input then goes nowhere and every test fails in a way that
looks like a regression in the app.

Tests that close panes will eventually close the last one, and CowTerm quits
when its last session ends. That is correct behaviour, not a crash; re-run
`restart.sh --wipe` between runs.
