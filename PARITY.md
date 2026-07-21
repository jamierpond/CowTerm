# Parity with the ~/.config workflow

An audit of `~/.config` (dotfiles repo) against CowTerm as of 2026-07-20:
what the tmux/iTerm2 setup does that CowTerm doesn't yet, and what's worth
building beyond parity. Source material: `tmux/tmux.conf`, `bin/scripts/*`,
`zshrc`/`shellrc`/`vi-mode.zsh`, `skhd`/`aerospace`, `iterm2/`.

## Already covered (no action)

- Prefix `Ctrl+A`, splits in the pane's cwd, vi pane focus/resize, zoom,
  last-session toggle `^`, index switching, `u`/`n` quick actions.
- Sessionizer → palette (fuzzy dirs, MRU, live peek); window switcher →
  `Ctrl+Tab` MRU switcher.
- Lazygit popup (`Ctrl+A i`), worktree create/remove from the palette
  (replaces `wt`/`wt-rm`).
- Copy mode with vi keys (`Ctrl+A [`), `]` paste, native clipboard (the
  tmux config needed `copy-pipe-and-cancel 'xclip …'`).
- Rose-pine theme, pane border styling, active-border emphasis.
- Cursor shape changes (`vi-mode.zsh` DECSCUSR escapes), truecolor,
  escape-time 0 (native), OSC 7 cwd, OSC 9/777 notifications with
  ring-back, session persistence + daemon-owned shells.

## Config gaps (minutes each)

- [ ] `searchDirs` is missing entries the sessionizer scans:
      `~/Music/NME/Project Files` (and `/tmp/time` if still used).
- [ ] Stale `~/.config/wim.json` still sits next to `cowterm.json`; delete
      once nothing else reads it.
- [ ] Port the tmux popup bindings that already fit `bindings[].popup`:
      `m` shell popup, `E` edit config in nvim, `b` fzf branch checkout,
      `g` google-search, `o` quick-url, `r` create-pull-request,
      `N` open-todays-journal, cht.sh popup.

## Missing features, roughly by value

### 1. Status bar

The tmux status line is the biggest visible gap: session name badge,
centred window list with indices, battery (`tmux-status-battery`),
hostname, clock; also unused-but-present `tmux-status-git`/`-path`.
CowTerm only sets the OS window title. Needs: a one-line CPU-painted bar,
session list with active highlight, right-side widgets (clock, battery),
maybe `#(command)`-style user segments later.

### 2. A CLI control surface

Nearly every power script talks to tmux: `tmux-pr-dashboard` (jump to the
session owning a PR's worktree), sessionizer from a shell, `tmux neww`,
`open-adhoc-terminal`, `display-popup` from scripts. CowTerm has no
scripting interface, so none of these can target it. A `cowterm` CLI (or
socket protocol on the daemon) with verbs like `open-session <dir>`,
`switch <name>`, `popup <cmd>`, `send-keys` unlocks the whole scripts/
directory as-is.

### 3. Binding action types

`bindings[]` supports `send` and `popup`. The tmux config also has:
- Pinned-project keys (`F`/`J`/`T`/`P`/`C`/`V`/`M`): prefix+key →
  open/switch to a session on a fixed directory. Needs an `open` (dir)
  action.
- `R` (`gh repo view --web`), `G`-style fire-and-forget: needs a `run`
  action (no popup, no typing into the pane).
- Repeatable bindings (tmux `-r`): held `Ctrl+A Ctrl+h h h…` resize
  without re-prefixing (repeat-time 50ms).

### 4. OSC 52 clipboard

The tmux config wires `set-clipboard` + OSC 52 passthrough for nvim.
CowTerm's parser (`TermParser::dispatchOsc`) has no case 52, so clipboard
writes from nvim or from inside an ssh session are dropped. Write support
(`\033]52;c;<base64>`) is small and high-leverage; ignore read requests.

### 5. Config hot-reload

`prefix r` re-sources tmux.conf (alias `tso`). CowTerm reads
`cowterm.json` once at startup; font/theme/binding changes need a
relaunch. Watch the file or add a reload key.

### 6. Global summon

skhd binds `ctrl-7` to focus iTerm2; iTerm2 also has its hotkey window.
CowTerm has the tray, but no global show/hide hotkey. (Interim: point
skhd/aerospace at CowTerm's bundle id.)

### 7. Copy-mode search

tmux copy-mode `/` and `?` incremental search — the missing half of the
scrollback story now that vi navigation is in.

### 8. Notification sound

`bell` alias + `bell.mp3` + `say-notify` are load-bearing (Claude runs
ring the bell). CowTerm notifications are visual-only; an optional sound
per notification would replace the shell-side hack.

## Beyond parity (the "make it better" list)

- **PR dashboard, native** — ✅ built (`PrDashboard`, `Ctrl+A p`): open
  PRs × CI status × owning session, fed by one `gh api graphql` call on a
  worker thread with an on-disk cache for instant opens. Enter lands in a
  session on the PR's branch — one worktree per PR, cloning the repo if
  it isn't on disk — `^o` browser, `^s` session, `^y` copy, `^r`
  refresh; typing filters fuzzily. Still missing vs the script: `ctrl-x`
  close-PR and the rich per-PR preview pane.
- **Claude awareness** — ✅ built (`ClaudeHud`, `Ctrl+A a`): every Claude
  pane across all sessions in one list — conversation title, cwd, last
  notification with age, "waiting on you" (unseen-notify) rows sorted
  first and counted in the header. Refreshes live once a second; Enter
  jumps to the session and focuses the exact pane. Sessions now track
  when a notification arrived and whether it's been seen
  (`TermSession::lastNotifyAt`/`notifyUnseen`).
- **quick-url / cht.sh style pickers** as palette providers rather than
  fzf popups: one fuzzy UI, many sources.
- **Windows/psmux parity**: the dotfiles carry a whole
  `windows/psmux.conf` fork; eacp is cross-platform, so CowTerm could
  eventually delete that fork.
