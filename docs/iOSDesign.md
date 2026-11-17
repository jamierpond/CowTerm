# CowTerm for iPhone: the design

Status: being built on jp/ios-app; see "Results of the risk tests" and "Decisions made while building" at the end. [iOSApp.md](iOSApp.md) is the technical plan
(ssh link, libraries, pairing, push). This page says how the app behaves under
a thumb, and it overrules that page where the two disagree; the last section
lists where.

The goal, in Jamie's words: "jump up from my desk, go out the door and continue
working without interruption", and "it must be delightful to use on mobile.
scrolling up, visual mode, all this stuff."

It rests on three pieces of evidence: three days of CowTerm's own usage logs
(14–17 November 2026, 10,017 lines), a reading of the Mac app's code for the
model it gives its user today, and Don Norman's essays on gestures, modes,
feedback and automation.

## The model

Your panes live on your machines. The Mac and the phone are two windows onto
the same panes, and whichever you touched last sets a pane's size.

Nothing moves to the phone. A pane is seen from two places, and each place says
so: the Mac shows "on iPhone, 49 columns" on a pane the phone holds, and the
phone shows which machine a pane lives on and whether the picture is live.

| | Mac today | iPhone |
| --- | --- | --- |
| What fills the screen | A session: a split tree of panes | One pane |
| Finding a session | `leader f` palette, 3 typed characters, row 0 picked 53% of the time | The overview: a list with the same ranking and the same search |
| Panes of a session | Splits, `leader h/j/k/l`, zoom | Pages. Sideways swipe on the bottom bar, dots show how many |
| "This pane, big" | `leader z`, 236 times in three days | Always |
| Size of a pane | The Mac's window | The phone's screen, while the phone holds it |
| Scrolling back | Wheel, whole lines, drifts while output arrives | Finger, per pixel, with momentum, and it holds still |
| Selecting | Mouse drag, or copy mode with vi keys | Long press and handles, or the same vi keys on a hardware keyboard |
| Who needs me | Notification, palette row detail | The waiting group at the top of the overview, and a push |
| Typing | Keys go straight to the shell | A composer first; raw keys one tap away |

## Leaving the desk

This is the one activity the app exists for, so it is designed as a sequence
and tested as one.

1. At the desk nothing is needed. Each daemon already records the pane that
   last received input and when.
2. He stands up and unlocks the phone. CowTerm opens on that pane: the screen it
   stored last time is drawn at once, dimmed, with its age ("12 s ago").
3. The link comes up in the background, about a second on cellular. The pane
   brightens, the phone takes the size, and Claude repaints at phone width.
4. He can type into the composer during step 2. What he wrote is sent when the
   link is up, and the composer shows it waiting until then.
5. At the door Wi-Fi gives way to cellular. Tailscale keeps the address, so the
   link stalls for a few seconds. The app reconnects on the network-change
   event, not after the 15-second keepalive, and the connection mark in the
   bottom bar shows the stall.
6. The phone goes in a pocket and iOS suspends the app. Nothing is lost: the
   panes are in the daemons.
7. A Claude asks a question. The daemon sees the Mac idle or locked for 60
   seconds and pushes. The lock screen shows the question with its options as
   buttons.
8. Forty minutes later he opens the app. The overview shows what is waiting
   first, each with one line saying what it wants.
9. He sits back down and presses a key on the Mac. The Mac takes every size
   back at once and the banners go.

Step 2 has to be instant fifty times a day. A two-second spinner on every
unlock sends him back to the laptop.

## The pane screen

One pane fills the screen. There are no splits, no zoom and no pane resizing on
the phone.

### Size

A 150-column Mac pane fitted to a 393-point screen has 2.6-point cells, and
panning shows a third of each line. So the phone owns the size of the pane it
is showing.

- The phone claims the size when the pane opens, not at the first keypress. A
  claim on the first key would reflow Claude at the moment he answers it.
- The row count is fixed at the keyboard-up height, about 49 × 25 at 13 points
  on a 393-point iPhone. Showing and hiding the keyboard then never resizes the
  PTY. With the keyboard down, the extra rows show scrollback.
- Only the pane on screen is adopted and resized. The other 90 stay as the Mac
  left them.
- The Mac reclaims its sizes when its window next receives input.

The cost is two reflows per pane per trip. Whether Claude Code leaves duplicate
frames in scrollback after 150 → 49 → 150 is the first thing to test, before
any interface is built.

The renderer rounds cells to whole points today, which leaves a phone four
possible column counts. Fractional cell widths are needed to fit a pane exactly
and to make pinch feel continuous.

### Scrolling

Dragging the pane scrolls the scrollback. No mode is entered to scroll.

- The content tracks the finger per pixel and carries on with momentum and
  friction like any iOS list. Today's scrolling moves whole lines with no
  momentum (`takeWholeWheelLines`), so this is new work in `TerminalView`, and
  the Mac's trackpad gets it too.
- The view holds still while output arrives. Today the position is counted
  from the bottom, so every new line pushes the text up under the reader
  (`TerminalView.cpp:239`). The position becomes an absolute row.
- A scroll indicator on the right edge shows where he is in the 10,000 lines.
- Once scrolled up, a "↓ 214 new" pill sits above the bottom bar. Tapping it
  returns to the live bottom. So does sending anything.
- On the alternate screen (vim, lazygit, Claude's full-screen mode) a drag
  sends wheel steps to the program instead, one per line moved, with no
  momentum tail, because the program owns that scrolling.

### Selecting, and visual mode

Selection follows iOS, because a convention already in his hand beats a better
idea he has to learn.

- Long press selects the word under the finger and shows two handles and a
  magnifier. Dragging a handle extends the selection, and dragging it past the
  top or bottom edge scrolls.
- While a selection is open the whole pane is marked: a tinted border, and the
  bottom bar is replaced by Copy, Line, Search and Done. Typing leaves the mode
  and says so; it never falls through to the shell.
- With a hardware keyboard the Mac's copy mode works unchanged: `v`, `V`,
  `h j k l`, `w`, `b`, `y`. Its motions are pure functions (`CopyMode.h`), so
  touch and keys drive the same code.
- Search is new, on both platforms. Copy mode has no `/` today, and on a phone
  search is the main way back to something. Search opens from the selection bar
  and from the menu; matches are marked on the scroll indicator.
- Links open on a tap. File paths are not detected today and stay out.

### Typing

A composer is the main input, for every pane and not only Claude's. It is a
real iOS text view sitting above the keyboard, so it has dictation,
autocorrect, the space-bar cursor and correction by tapping a word. It sends as
a bracketed paste followed by Enter. There is no local echo over ssh and a
round trip on cellular is 60 to 300 ms, so composing on the phone and sending
whole lines is both faster and safer than typing into the grid.

- Up-arrow in an empty composer recalls earlier messages, since dictation will
  mangle `eacp` and `jp/develop` and fixing a recalled line beats saying it
  again.
- The keyboard opens from a button on the bottom bar, not from a tap on the
  pane. A tap already means click or open link, and a stray touch would cut 39
  rows to 25.
- A key row above the keyboard holds what the iOS keyboard lacks. A 393-point
  row fits seven keys at a walking size of 48 points: `Esc`, `Tab`, `Ctrl`,
  `↑`, `↓`, `←`, `→`. They are grouped by job with a gap between groups, and
  `Esc` sits at the far end from the arrows, because `Esc` interrupts Claude.
  Symbols (`| ~ / -`) are on a second page of the row.
- `Ctrl` latches for one key. While it is latched the keyboard's own keys show
  `^C`, `^R`, `^D`, so the mode is on the keys and not on a tinted button.
- Raw mode is a switch on the key row: keys go straight to the pane, for vim
  and for shells. The composer's field is gone while raw mode is on, so the two
  cannot be confused.
- A paste of several lines into a shell shows its first lines and asks, as the
  Mac does. A keystroke sent to a remote shell cannot be undone, so the check
  belongs before sending.

### Claude panes

- When the screen shows a permission prompt, its options become buttons above
  the composer. Each button is tied to the exact prompt it was drawn for; if
  the prompt changed before the tap landed, the tap is dropped and the new
  prompt is shown. A late tap must never approve a different command.
- A stop button sends `Esc`. It is on the left, away from the send button.
- The auto-lock is held off while the pane on screen is producing output.

### The bottom bar

Everything he needs while walking sits in the bottom 40% of the screen, where a
thumb reaches.

```
┌──────────────────────────────────────────┐
│ tamby/Grazer                    ● live   │  session, host colour, link state
│                                          │
│                 the pane                 │
│                                          │
│                         [ ↓ 214 new ]    │
├──────────────────────────────────────────┤
│ ‹ CowTerm    • ○ ○     ⌨     git    ☰   │
└──────────────────────────────────────────┘
```

- `‹ CowTerm` names the session he was in before this one, and a tap goes back
  to it. 34% of his switches return to the previous session, so it gets a
  control of its own with the destination written on it.
- The dots are the panes of this session. Swiping sideways along the bar moves
  to the next one, and tapping a dot does the same. 64% of his sessions have
  one pane, and then there are no dots.
- `⌨` opens the keyboard and composer.
- `git` shows this pane's repository (next section).
- `☰` opens the overview. Swiping up on the bar does the same.

The bar sits above the home indicator, as Safari's does, so its swipes do not
trigger the system's.

### lazygit

He opens lazygit in 23% of session visits and closes it after a median of 3.8
seconds: it is a glance. iOS cannot run a local popup. The `git` button asks
the daemon to run lazygit in a temporary pane beside the current one, and
closing it returns to where he was. The branch and the count of changed files
are also shown in the overview row, which may answer most of those glances
without opening anything.

## The overview

The overview is a vertical list, about ten rows to a screen. A carousel of
cards shows one card and two edges, and his ten most used sessions cover only
63% of his switches, so cards cannot be the way to reach 64 sessions.

- Waiting sessions come first, as cards, because there are few of them and
  their text matters. Each card carries the session name, the host colour and
  the question itself with its answer buttons. He can answer from the card
  without opening the pane.
- Below them, every session by most recent use, with the same ranking the
  Mac's palette uses. Each row has the session name, a host-colour stripe, `✳`
  for Claude, the branch and changed-file count, and one line of state:
  "working, 4 min", "finished 12 min ago", or Claude's last line.
- The list is anchored at the bottom. Row 0 is nearest the thumb, since he
  picks row 0 53% of the time and rows 0 to 2 89% of the time.
- A search field sits under the list, by the thumb, with the Mac's fuzzy match.
  His median pick is three typed characters.
- Thumbnails are the stored last screen, shown small on the waiting cards only.
  At card size a terminal is texture, not text, so the words carry the card.
- An unreachable host is one row with the reason. If nothing on the tailnet
  answers, one banner says so instead of every host failing on its own.

A live thumbnail for every session would mean adopting every pane, moving
several megabytes and making every Claude repaint on each unlock. The list is
fed by the daemons' `info` instead, which has to grow the fields below.

## Nothing is only a gesture

Norman's objection to touch interfaces is that nobody can know a gesture
exists. Every gesture here is a shortcut for a control that can be seen, and
one menu (long press on the bottom bar) lists every action with its gesture
beside it.

| Gesture | What it does | The visible control |
| --- | --- | --- |
| Drag on the pane | Scrolls back | Scroll indicator |
| Long press on the pane | Selects a word | Menu → Select |
| Tap on the pane | Opens a link, or clicks in a mouse program | |
| Pinch | Font size | Menu → Text size |
| Sideways swipe on the bar | Next or previous pane | The dots |
| Swipe up on the bar | Overview | `☰` |
| Swipe from the left edge | Back, as everywhere in iOS | `‹` |

No gesture needs two hands except pinch, and nothing destructive is a gesture.
Closing a pane is a menu item; the pane then disappears from the phone and its
shell lives on for a minute with an Undo bar, instead of an "are you sure?" he
would learn to tap through.

## Modes

Each mode is marked over the whole pane or on the keys themselves, and typing
in the wrong one is harmless.

| Mode | How it shows | Typing does |
| --- | --- | --- |
| Composer | Text field above the keyboard | Edits the field |
| Raw keys | No field; the key row's switch is lit; a cursor shows in the pane | Goes to the pane |
| Ctrl latched | The keyboard's keys read `^C`, `^R` … | Sends that control key, then unlatches |
| Selecting | Tinted border; the bar reads Copy, Line, Search, Done | Leaves the mode, and says so |
| Scrolled back | "↓ n new" pill, scroll indicator | Sends, and returns to the bottom |
| Not live | Pane dimmed, with the age of the picture | Composer text waits and is marked unsent |
| Held by the Mac | "on Mac, 150 columns · take over" | Take over claims the size |

## What the phone leaves out

From the logs and from the size of the screen:

- Splits, split direction, pane resizing, zoom and directional focus. One pane
  is on screen.
- The leader key. Navigation is the bar and the overview.
- Session by number, pane cycling and "previous session" as keys. The `‹` chip
  replaces the last one.
- The Ctrl+Tab switcher. It depends on releasing a hardware key.
- Peeking at a session while moving through a list. 241 of his 609 session
  stays are previews under five seconds, and on a phone each would cost a
  connection and a reflow. The row's one line of state does that job.
- The palette's normal mode. He changed mode 17 times in three days.

## What has to be true before any of this works

These are daemon and library changes. The Mac benefits from each, and the
first screen of the phone depends on all of them.

1. Local sessions are known to the daemon. Today local panes are spawned with
   an empty session name and layouts are published for remote sessions only
   (`DaemonClient.cpp:403`, `Session.cpp:490`), so the Mac's own daemon cannot
   say which panes form a session.
2. The daemon records which pane last received input, and when. The phone
   opens there. Today the focused pane lives only in the Mac app's memory.
3. The daemon knows "needs you". `info` has no such field. Claude Code's hooks
   calling `ct` are steadier than reading the screen.
4. A pane has a size owner, and every watcher is told when it changes
   (`claim-size`, `size-owner`). Today the last `resize` wins silently
   (`Daemon/Main.cpp:386`).
5. Asks go through the daemon and reach every client; the first answer wins.
   Today a local ask goes only to the Mac app, and on a remote host only the
   first bridge receives it.
6. The daemon can run lazygit as a temporary pane.
7. The daemon answers a client that is one protocol version older or newer.
   The Mac and daemons are rebuilt many times a day and the phone will lag.
8. `TerminalView` scrolls per pixel from an absolute row, and draws fractional
   cell widths.
9. The ssh key is bound to the device and usable after the first unlock. A key
   gated by Face ID on each use cannot answer from the lock screen, because a
   background wake cannot show Face ID.

## Build order

The plan's first phase was a read-only view of the desktop grid fitted to the
phone, which is the one arrangement nobody can read. The order below builds
the pane screen properly before anything else.

1. Tests of the three risks, with no interface: resize a live Claude pane
   150 → 49 → 150 and look at the scrollback; reach a daemon through libssh2
   from a command-line tool; build CowTerm's libraries for the simulator.
2. The daemon changes 1, 2, 4 and 7 above, with the Mac app using them
   ("on iPhone" banner, reclaim on input).
3. The pane screen: stored last screen, phone-owned size, per-pixel scrolling
   with momentum, reconnect on network change, composer, key row, bottom bar.
   This is the part to get right; it is tried on a real phone before the next
   step starts.
4. The overview, with "needs you" (daemon change 3) and search.
5. Selection, handles and search in scrollback.
6. Asks through the daemon, push, and answers from the lock screen.
7. lazygit as a pane, pairing by QR code, the Secure Enclave key.

## For Jamie

Answers that change the design:

1. Does the desk Mac stay awake when he leaves? It ran on battery tonight, so
   it is a laptop, and 59% of his session switches are to sessions on it. A
   sleeping Mac stops those Claudes and the phone cannot reach them. Either
   that work moves to tamby and macprorack, or the Mac stays open on power and
   the daemon holds it awake while a pane is busy.
2. Does he run Claude Code in its full-screen mode? That decides whether
   scrolling Claude's transcript is local and smooth or goes over the network.
   Answered while building: yes (`"tui": "fullscreen"`), so it goes over the
   network. See "Results of the risk tests".
3. Should the phone start new sessions and panes, or only steer existing ones?
4. Which iPhone?

Things only a finger can judge:

- 13-point against 15-point JetBrains Mono while walking (49 against 43
  columns).
- The sideways swipe on the bottom bar against the home indicator below it.
- The friction of the scroll fling.

Mac features with no use, or one use, in three days of logs. They are listed
for a decision and nothing has been removed: `leader H J K L` (resize by a
cell), `leader o` (cycle panes), `leader ^` (previous session), `leader 1–9`
(session by number), `leader ]` (paste), `leader w` (a second key for the
palette), and the palette's normal mode. Click to focus stays. Cmd chords are
not logged, so nothing is known about them.

To let the daemon know which Claude is waiting (daemon change 3), Claude Code's
hooks report it with `ct pane-state`. Nothing was added to his settings; these
go in the `hooks` of `~/.claude/settings.json` on each machine:

```json
"Notification": [{"hooks": [{"type": "command",
  "command": "ct pane-state waiting --message \"$(jq -r .message)\" 2>/dev/null || true"}]}],
"UserPromptSubmit": [{"hooks": [{"type": "command",
  "command": "ct pane-state working 2>/dev/null || true"}]}],
"Stop": [{"hooks": [{"type": "command",
  "command": "ct pane-state finished 2>/dev/null || true"}]}]
```

Found in the Mac app while reading it, and not changed: the leader key shows
nothing while it is pending and never times out; the terminal bell is not
connected to anything; the palette paints twelve rows and cannot scroll;
`leader x` kills a shell with no way back.

## Results of the risk tests

### Resizing a Claude pane 150 → 49 → 150

Jamie's Claude Code runs full-screen (`"tui": "fullscreen"` in his settings),
and full-screen Claude leaves the scrollback clean: it draws on the alternate
screen, writes nothing to history, and repaints at each width. Its transcript
is scrolled by Claude itself, so on the phone a drag over a Claude pane sends
wheel steps over the network, as the design's alternate-screen rule says. That
answers the second question for Jamie below.

Inline Claude (the default `tui`) does leave duplicates. Each width change
reprints the lines that were on screen, and the old copy stays in history:
after 150 → 49 → 150, 9 of 60 reply lines appear twice in scrollback (11 after
the first resize alone). The same snapshots without resizing, which still
nudge the height by one row on each adopt, leave none, so the column change is
the cause. vim on the alternate screen is unaffected.

`scripts/resize-scrollback-test.py --program claude|claude-inline|vim
[--control] --directory <a folder Claude trusts>` reruns it. It starts its own
daemon under `COWTERM_INSTANCE=resize-scrollback-test`, so Jamie's daemon and
panes are never touched, and it quits that daemon at the end.

### Reaching a daemon through libssh2

An ssh client inside the app reaches cowtermd on tamby and macprorack, opens a
pane, reads its screen and closes it. `scripts/probe-embedded-ssh.sh
<user@host>` runs it. libssh2 1.11.1 on mbedTLS 3.6.8 is pinned in lockstep,
and key exchange and host keys work with stock OpenSSH 10.2.

mbedTLS has no ed25519, so libssh2 cannot sign with an ed25519 key file. tamby
accepts only Jamie's ed25519 key, so the probe signs through a private
ssh-agent for that run. The phone's planned Secure Enclave key is P-256, which
works.

### CowTerm's libraries on the iOS Simulator

The four libraries build for the simulator with eacp's own iOS CI flags
(`scripts/build-ios-simulator.sh`). The PTY is a stub on iOS
(`Lib/Shell/Pty-iOS.cpp`). emberstore does not build for iOS (it watches files
with FSEvents), so `MruStore` stays out of the phone; the overview's ranking
will need an iOS file watcher in emberstore. RudderClient and Crashout's
handler are Mac-only and stay out too.

simctl cannot rotate a simulator. The app turns itself to landscape when
launched with `--landscape`, which is how landscape screenshots are taken.

## Decisions made while building

- The resize test runs Claude with no tools (`--tools ''`) and Haiku, so the
  spawned Claude cannot act on Jamie's machine; how Claude draws does not
  depend on the model.
- When the client holding a pane's size goes away (the phone is locked or
  loses signal), the client that held it before takes it back at once, rather
  than leaving the pane at phone width until someone types on the Mac.
- A client that does not say its protocol version in `hello` is treated as
  version 4, and its plain resize still takes the pane, as before. Without
  that, an old Mac app could never get a pane back from the phone.
- `ct bridge` keeps a daemon one protocol version older running instead of
  restarting it. Restarting ends every shell in it, Claudes included, and a
  neighbouring version can serve both. A daemon two versions old is still
  restarted.
- The phone sends every size claim as a `resize` followed by `claim-size`, so
  daemons from before size owners (tamby and macprorack run one tonight)
  still resize the pane it opens.
- The pane last typed into is its own message (`last-input`) instead of a
  fifth column in `info`, because older clients read the fourth column to the
  end of the line and would take the time into the session name.
- Typing is marked as such (`write <id> input`) at TerminalView's typing and
  paste path, so a terminal's automatic replies never count as input.
- The Mac calls itself "Mac" to other clients ("Windows" and "Linux"
  elsewhere). Its "on iPhone, 49 columns; type here to take it back" banner
  uses the existing note over a pane (the one that says a connection was
  lost). Typing in any pane takes back every pane on every machine that
  another client holds.
- The ssh link's hello and failure wording moved from `SshBridgeLink` into a
  `BridgeLink` base both links share, and `RemoteHost` takes a link factory.
  libssh2 is its own library (`CowTerm::EmbeddedSsh`), so ct, the daemon and
  the Mac app never link it.
- The phone's app is `CowTermMobile`, bundle id `audio.pond.cowterm.mobile`,
  named "CowTerm" on the home screen, iOS 15 or later.
- Scrolling has no rubber band: it stops hard at the top and bottom. The
  scroll position keeps the hidden part of the top row as a fraction of a row,
  so a font size change needs no correction.
- The scroll indicator shows on the Mac too, with no setting, since it behaves
  like the scroll bars macOS draws over content.
- The phone fixes the pane's grid at the keyboard-up height
  (`TerminalView::setFixedGridSize`); a taller view shows scrollback above the
  grid, and the alternate screen shows blank rows there.
- Fractional cell widths (item 8) are not built. Cells are rounded up to whole
  points today. Rounding them to whole screen pixels is small but changes the
  Mac's column count at some font sizes, so it waits for Jamie's yes. The glyph
  atlas drew at 2x on every screen, which made a 3x iPhone's text soft; it now
  draws at the screen's own scale.
- "Needs you" (daemon change 3) is `ct pane-state`, run from Claude Code's
  hooks; reading the screen was not tried. The daemon's `activity` answer
  carries each pane's state, last typing and last output, and
  `SessionOverview` turns it into the overview's rows.

## What eacp lacks on iOS

Each gap is a local branch of eacp off Eyal's develop, in its own worktree,
not pushed and with no pull request. `jp/ios-integration`
(`~/projects/eacp-ios-integration`) is jp/develop with the three commits
cherry-picked on top; the phone app builds against it with
`EACP_SOURCE=~/projects/eacp-ios-integration scripts/build-ios-simulator.sh`,
and CowTerm's pin is unchanged.

| Gap | Branch | Commit |
| --- | --- | --- |
| Typing: no `UIKeyInput`, so no keyboard and no text; hardware keys reported nothing | `jp/ios-text-input` (`~/projects/eacp-ios-text-input`) | 176ab5a4 |
| The software keyboard's height and animation | `jp/ios-keyboard-frame` (`~/projects/eacp-ios-keyboard-frame`) | 2b287d8f |
| The status bar was always hidden | `jp/ios-status-bar` (`~/projects/eacp-ios-status-bar`) | c5ce3388 |

Lifecycle events, touch timestamps and safe-area insets already exist. The
keyboard-frame and status-bar branches both change the iOS window's
constructor, so whichever merges second needs a one-line fix.

## Where this overrules iOSApp.md

- "View mode: the phone never resizes, pinch and pan" is replaced by the phone
  owning the size from the moment a pane opens.
- "Take over when you start typing" is replaced by claiming on open.
- "Tap brings up the keyboard" is replaced by the `⌨` button.
- "Two-finger swipe moves between panes" is replaced by the bar swipe and the
  dots.
- "Gated by Face ID" is replaced by a key usable after first unlock.
- The twelve-key bar is replaced by seven keys and a second page.
- "A composer for Claude panes" becomes the main input for every pane, and it
  is a hosted iOS text view inside the eacp app.
- "iOS reports the VPN state" is untested; there is no public call for another
  app's VPN.
- Phase 1 "read-only at the daemon's size" is dropped.
