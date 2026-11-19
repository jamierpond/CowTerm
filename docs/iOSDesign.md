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
- While the phone has the pane open on screen, every client keeps the phone's
  size: the Mac draws the pane at that grid, narrow, at its normal font, with
  the "on iPhone" note. Typing on the Mac does not take it back. The Mac gets
  its size back when the phone leaves the pane or goes to the background, or
  when he clicks the note.

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

Keys go straight to the pane as they are typed, like any terminal. This was
first designed the other way round, with a composer as the main input, and
changed on 17 November after Jamie used it: Claude Code already has its own
input box, so a second field below it that sends on return was confusing.

The composer stays, behind the key row's "compose" switch, off by default and
lit while it is on. It is a real iOS text view above the keyboard, so it has
dictation, autocorrect, the space-bar cursor and correction by tapping a word,
and it sends as a bracketed paste followed by Enter.

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
- The composer's field is there only while its switch is on, so the two ways
  of typing cannot be confused.
- The key row's keys are the pane's own: pressing one keeps the keyboard up.
  An eacp ComponentHost takes focus on a press, which took it from the pane and
  closed the keyboard, so the key row's host no longer does.
- "compose" sits on the key row's second page and is two keys wide, so its
  word fits on a 375-point iPhone 12 mini; a symbol label was the other choice.
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
- `diff` opens dv on this pane's folder ("dv from the bar", under
  "Decisions made while building"; it replaced lazygit, next section).
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
  Mac's palette uses. Each row is filled with its host's colour and has the
  session name, the host's name, `✳` for Claude, the branch and changed-file
  count, and one line of state:
  "working, 4 min", "finished 12 min ago", or Claude's last line.
- The list is anchored at the bottom. Row 0 is nearest the thumb, since he
  picks row 0 53% of the time and rows 0 to 2 89% of the time.
- A search field sits under the list, by the thumb, with the Mac's fuzzy match.
  His median pick is three typed characters. While the keyboard is up the field
  sits directly on top of it, the list ends above the field with the best match
  nearest it, and a "hide" button beside the field lowers the keyboard. Return
  opens the top match. Coming back to the list always empties the field and
  shows the whole list.
- The list is ordered by frecency on the phone, searched or not: how often
  and how recently each session was opened from the phone's list, decayed
  with FrecencySearch's `calculateDecayedUsage` (an opening weighs 1, as a
  palette pick does on the Mac), plus the session's last typing as one use.
  The Mac's usage store needs emberstore, which does not build for iOS, so the
  phone keeps its own record in `session-openings.json` beside `last-screen.json`
  (`Library/Application Support/pond/CowTerm` in the container). A typed query
  ranks with the Mac's `rankPaletteItems`; an empty one sorts by the same
  usage. On a first launch with no openings the order is the daemons' last
  typing, where they report it; Jamie's protocol 4 daemons do not, so it is by
  host, then session name. Waiting sessions still come first as cards.
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
| Typing (the default) | No field; a cursor shows in the pane | Goes to the pane |
| Composer | Text field above the keyboard; the key row's "compose" is lit | Edits the field |
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
   ("on iPhone" banner; click it to take the pane back).
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
   Answered while building: it starts a session for a project that has none
   ("Projects with no session" under "Decisions made while building"), and
   otherwise only steers.
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

### The phone holding a pane, end to end

`scripts/run-ios-size-owner-test.sh jamiepond@macprorack
'$HOME/tmp/cowterm-ios-phone-test' <png>` builds this branch's headless ct and
daemon in that folder on the host, runs them as a separate instance, and has a
desk stand-in hold a 150 × 40 pane. The phone opens it in the simulator and
takes it at 48 × 21 (keyboard-up rows), and when the phone quits the desk gets
150 × 40 back. Jamie's own daemons run protocol 4 and were not used.

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
  elsewhere). Its "on iPhone, 49 columns; click here to take it back" note
  uses the existing note over a pane (the one that says a connection was
  lost). Jamie agreed the rule on 17 November, replacing "typing on the Mac
  takes every pane back": while the phone holds a pane, the Mac draws it at
  the phone's grid (`TerminalView::setFixedGridSize`, driven by the
  `size-owner` notice through `Shell::onGridSetElsewhere`) and sends no resize
  of its own for it, so the size it gets back is the one it had. A click on
  the note sends `reclaim-sizes`. The phone sends `yield-size <id>` (new in
  protocol 5; older daemons ignore it) when it goes to the background or its
  link stalls, so the client that held the pane before takes it back while
  the phone still watches it; it claims again on coming back.
- That rule needs its other half, found when the Mac showed "on iPhone, 102
  columns" on a pane with no phone connected: a pane the Mac opened while the
  phone held it never had the Mac as an owner, so when the phone left nobody
  took it back, and the Mac, which sends no size while a pane is held
  elsewhere, stayed narrow. Now a pane whose holder leaves goes to a client
  still showing it, and the daemon takes the panes of a client that has said
  nothing for 5 seconds (a phone iOS suspended with its link still open; a
  client showing panes asks for info every second). The phone claims every
  pane it shows again when it comes back to the foreground.
- A launch that names its host itself (a simulator started by a script) never
  opens a pane on its own, so no test takes the size of a pane Jamie typed in;
  only the paired app does.
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
- The pane's rows follow the visible area, like any terminal: with the
  keyboard down a full-screen program fills the screen, and the shell is
  resized when the keyboard comes and goes. Fixing the rows at the keyboard-up
  height (17 November, replaced the same day) left the top half of the screen
  empty under Claude and tmux on Jamie's phone, since the alternate screen has
  no scrollback to show there. That rule spared inline Claude a reflow; Jamie
  runs Claude full-screen, which repaints at each size.
  `TerminalView::setFixedGridSize` is no longer used by the app.
- Fractional cell widths (item 8) are not built. Cells are rounded up to whole
  points today. Rounding them to whole screen pixels is small but changes the
  Mac's column count at some font sizes, so it waits for Jamie's yes. The glyph
  atlas drew at 2x on every screen, which made a 3x iPhone's text soft; it now
  draws at the screen's own scale.
- "Needs you" (daemon change 3) is `ct pane-state`, run from Claude Code's
  hooks; reading the screen was not tried. The daemon's `activity` answer
  carries each pane's state, last typing and last output, and
  `SessionOverview` turns it into the overview's rows.
- The phone never starts a shell. Before opening a pane, its daemon client
  asks `info` and opens only a listed pane; otherwise the pane says it no longer
  exists. A pane that dies between that answer and the open would still get a
  fresh shell; only a daemon-side "adopt, never create" message would close
  that, and it is not built.
- The phone chooses its pane by an explicit id, else the latest `last-input`
  of any host, else the pane it showed last time if still listed, else none.
  It waits 2 s for `last-input`, which older daemons never answer.
- Link state: stalled after 3 s with no bytes (the daemon answers `info` every
  second), dialled again after 8 s, and at once on returning to the foreground
  or a network change if the link stays quiet for 2 s. eacp has no
  network-change event, so `iOS/NetworkPathWatch-iOS.mm` wraps NWPathMonitor in
  CowTerm.
- The stored last screen is the daemon's own terminal-state JSON, saved every
  10 s while live and on going to the background.
- The phone's search counts a session's last typing as one pick; it has no
  learned picks, because emberstore, where the Mac keeps them, does not build
  for iOS. The palette's ranking moved into `CowTerm::Multiplexer` with
  `git mv`.
- A tap on a link opens it on the phone without a modifier key.
- The key row's keys are 48 points wide where they fit; on a 390-point phone
  they get 43.
- iOS does not let an app relabel its own keyboard's keys, so a latched Ctrl
  shows on the lit Ctrl key of the key row instead of as `^C` on the keyboard.
  This is where the design could not be followed.
- The composer sends through `TerminalView::pasteThenPressEnter`: a bracketed
  paste then Enter, counted as typing, and back to the live bottom.
- The overview shows a waiting session once, as a card nearest the thumb, with
  the other sessions above it. While searching there are no cards. Branch,
  changed-file count, thumbnails and answer buttons are not shown: the daemon
  does not report the first two, and asks through the daemon are not built.
- A Claude prompt's buttons answer only the prompt they were drawn for: a tap
  reads the screen again and sends the option's number only if the question,
  the options and the command above them are unchanged; otherwise the tap is
  dropped and the buttons redraw. The stop button sends Esc and shows when the
  pane's program is `claude` or a prompt is on screen.
- The auto-lock is held off for 30 s after the shown pane's last output. eacp
  has no call for it, so it is CowTerm glue (`iOS/ScreenAwake-iOS.mm`).
- The multi-line send check reuses the Mac's `PasteConfirmation` and
  `NotifyUserPrompt` as they are; on the phone its "Show pane" button does
  nothing useful.
- With several hosts (`--destination a,b`), each host has its own link watch;
  only the shown pane's host counts silence as a stall.
- Every row and card in the list is filled with its host's colour, so the list
  reads as blocks by machine; the host's name stays in text on every row. Jamie
  is mildly red-green colour blind, so the phone's host colours come from the
  Okabe-Ito palette (`getColourBlindSafeHostRowColours`), each darkened in
  linear light to its own lightness, CIE L* 18 to 48 in steps of 5, so hosts
  also differ in greyscale and white text keeps 4.5:1 or better. The host name
  picks the entry by FNV-1a, as on the Mac: big-cow gets orange, macprorack
  bluish green, tamby blue. A `hosts.<name>.color` in cowterm.json still wins,
  darkened only if white text would not read on it; the phone has no
  cowterm.json today. The pane screen's host dot uses the same colour.
- The Mac uses the same host colours, from the same function, so a machine is
  one colour on both. Its palette rows and switcher cards are filled with the
  host's colour as the phone's rows are, with white text, in place of the
  faint wash and thin stripe it had. The highlighted row or card gets a white
  outline, so it is not told by colour alone. Local sessions and projects take
  this Mac's own colour, picked by its short host name, because the app has
  no other name for itself. On Jamie's Mac that name is `daily-driver`, while
  the phone dials it as `big-cow`, so the two differ (reddish purple against
  orange) unless `hosts.daily-driver.color` in cowterm.json says otherwise;
  `daily-driver` also shares its entry with `tamby-windows`.
- The window's machine is shown in a strip across its top, not round the
  terminal: a remote session's strip is filled with its host's colour and its
  name starts with the host ("tamby/tamber-web"), so the text says the machine
  without telling hues apart; a local session's strip is the theme's
  background with its name. On macOS the strip is the title bar: the window
  has no title-bar background or title, and the strip sits beside the real
  traffic lights, 32 points tall to cover macOS 26's title bar. On Windows and
  Linux eacp has no way yet to drag a window by a native view or to keep the
  caption buttons without the frame, so the strip sits under the native title
  bar. The panes, their gutters and the active pane's ring are in the theme's
  own colours for every session.
  `scripts/render-host-colours.sh` renders the palette, the switcher and a
  session under the strip at minimum, tall narrow, default and ultrawide
  sizes, with protanopia, deuteranopia and greyscale views of each, into
  `build/renders/host-colours`.
- `ColourBlindHostColoursTests` checks the palette as protanopia and
  deuteranopia (Machado 2009, severity 1.0) and greyscale see it: every pair
  at least CIE76 ΔE 10 apart in each simulation and 4.5 L* apart in greyscale,
  and Jamie's three hosts at least ΔE 40 and 9.5 L* apart. For tritanopia only
  Jamie's three hosts are checked, at ΔE 10 or more: two entries of the
  palette come within ΔE 8.2 of each other there.
  `just simulate-colour-vision <png>` writes the same three views of a
  screenshot.
- A pressed row darkens and gets a white outline, so a press shows without
  telling one hue from another.
- Each row shows when Jamie last used the session: "just now" under a minute,
  "2 min ago", "3 h ago", "yesterday" (one to two days), "4 d ago". The time is
  the daemon's last typing in the session's panes (protocol 5 `activity`), else
  the last time he opened it from the phone's list (`session-openings.json`),
  else nothing. His protocol 4 daemons report no typing, so today every age is
  the phone's own record, and a session never opened on the phone shows none.
- Nothing on the phone signals by colour alone. The link state on the pane
  screen's top line is a word ("live", "connecting", "stalled", "unreachable");
  a waiting card says "waiting:" and is taller than a row; a latched Ctrl and
  a lit "compose" key also get a bar under the label.

### dv from the bar

The bar's `git` button was greyed out and did nothing. It is now `diff`, and
opens dv (DiffView) on the folder of the pane on screen, full screen at the
phone's size.

- It is the Mac's popup path: a TerminalView with a command, whose command
  shell is `makeRemoteShell`, so DaemonClient sends the daemon a `spawn` with
  a command payload. No second way to start a command pane was added.
- The phone otherwise never starts a shell. DaemonClient now also takes the
  one command such a client may start (`setOnlyCommandItMayStart`, `dv` on the
  phone); a spawn of any other command is refused like an unlisted pane. The
  pane's shell (`DiffViewShell`) starts only `dv`, only in the folder the
  daemon's `info` gave for the shown pane, and the phone builds that request in
  one place (`makeDiffViewPane`).
- The dv pane is the phone's alone, so it claims its size like any pane it
  shows. Its id starts `phone-dv-`, and the overview and the session's dots
  leave such panes out. The Mac app never lists panes it did not open.
- dv quitting, the "‹ Back to the pane" bar, or a swipe from the left edge
  returns to the pane he was in, and the phone sends `kill` for the dv pane.
- Never left behind: before the spawn is sent the phone adds the pane's host
  and id to `temporary-panes.json` beside `held-pane.json`, and removes it
  when it kills the pane. If the app is killed with dv open (a reinstall, a
  crash), the next launch sends `kill` for every pane in that file as soon as
  its host's daemon answers `daemon-hello`, and forgets it. If the record
  cannot be written, dv does not start. The pane stays alive between the kill
  and the next launch, and forever if the phone is never launched again or
  that host leaves its host list.
- When dv cannot start, a sentence says why in place of the pane: "dv is not
  installed on macprorack." (the daemon's `missing-command`, now also passed
  to `Shell::onCommandNotInstalled`), or "/tmp/x is not in a git repository."
  (dv prints "not inside a git work tree" and exits 1). Any other text dv
  leaves when it ends is shown as "dv stopped: " and its last line. A blank
  screen at exit is dv quitting.
- A tap reaches dv as a left click and a drag as wheel through the pane
  screen's `PaneTouchSurface`, unchanged.
- `just ios-test-diff-view <user@host> <screenshot-prefix>` checks it on
  throwaway panes in throwaway `/tmp` folders: dv open, the "not in a git
  repository" sentence, the pane gone after closing, and a pane left by a
  killed app gone after the next launch. The probe gained `--list-panes` and a
  folder for `--leave-pane`. On 17 November it passed on macprorack. big-cow
  and tamby refused the simulator's ssh key, so neither was checked there.

### Typing in dv

Jamie could open dv's comment box from the phone but not type in it: dv's
screen had only the close bar. Its bar now has the keyboard button at its
right, and typing works as on the pane screen.

- Keys go straight into dv. The key row gives Esc (dv's normal mode), Tab,
  the arrows and Ctrl (Ctrl then s saves and closes the comment); its
  compose switch brings up the composer for dictation. The composer sends a
  bracketed paste then Enter, as on the pane screen; dv trims the trailing
  newline from a comment.
- The pane screen's `TypingControls`, key row and `calculateTypingRects` are
  used as they are. `TypingControls` made `lowerKeyboard` and
  `pressKeyRowKey` public; the keyboard goes down with dv's pane, so no key
  reaches a pane that is gone.
- `--diff-view-steps=` plays taps on text on dv's screen, typed text, key
  row keys and composed text (iOS/DiffViewLaunchSteps.h), so the simulator
  can do the review a finger does. `just ios-test-diff-view-review` types one
  comment, composes another, submits, and prints the round dv wrote to
  `.diffview/latest.md` in a throwaway repository on the host.

### Projects with no session

Jamie could not find macprorack's `~/projects/iac` on the phone: the list
showed only sessions that had panes on a daemon. The Mac's palette also lists
each host's projects that are not open, and the phone now does too.

- Each host's project scan (`scan`, the bridge's `answerLocally`) is the same
  one the Mac's palette uses. The phone has no cowterm.json, so it searches
  each host where the Mac searches with none: `~/projects` and `~`. It asks
  again each time the list comes on screen.
- Below every session come the projects with no session open, as rows filled
  with their host's colour. A project counts as open when a session on its
  host has the name the Mac gives it (`macprorack/iac`) or its folder's name
  alone (a protocol 4 daemon's own panes).
- Every row starts with the palette's mark, so the two kinds differ without
  colour: `●` a session, `✳` a Claude session, `▸` a project. A project's
  second line says "not open · ~/projects/iac".
- Search covers both kinds with the palette's `rankPaletteItems`. With nothing
  typed, sessions come first and projects after, each by frecency. A project's
  openings are recorded under the key of the session it opens, so they carry
  over once it is open.
- A tap on a project starts a new session there and shows its pane: one login
  shell in that folder, under a new 16-hex-digit pane id, named as the Mac
  names that project's session (`getSessionNameForProject`, moved from
  `Terminal/Session.cpp` into `Projects`), so the daemon lists it as
  `macprorack/iac`.
- This is the second thing the phone starts, after dv, with the same care.
  `makeProjectSession` makes a session only for a folder in that host's last
  scan. DaemonClient sends the spawn of an unlisted pane only after
  `allowLoginShellInNewPane` named that pane and folder, only with no command,
  and only once. The pane's `ProjectShell` starts nothing but the login shell
  in that folder. Only a tap on the row (or `--open-project`, which takes the
  same path) calls `PhoneConnection::openProject`.
- The Mac app shows the new session as that project's session (see "One name
  for each host" below).
- `just ios-test-project-opening <user@host>` makes a throwaway folder in
  `~/projects` on the host, screenshots the list showing its row, opens it
  with `--open-project=<user@host>/<folder name>` (ten seconds after launch),
  screenshots the pane, and lists the host's panes in that folder before,
  while open, and after it kills the pane and removes the folder. On 17
  November it passed on macprorack.

### One name for each host

A session the phone started on macprorack did not show on the Mac: the phone
named the host `jamiepond@macprorack`, the Mac and the daemons `macprorack`.
The pane title showed the host twice (`macprorack/macprorack/iac`) and ran into
the link state. The phone now names each host as the Mac does, and the Mac
picks up the session the phone started.

- A host's name is its host name without the user (`getHostName`): the user
  is part of how to connect, not part of the name. The pairing file stays one
  `user@host` a line. The other way, a `name destination` pairing file, would
  let a name differ from the host dialled, but the phone dials the same
  tailnet names the Mac's ssh config uses as aliases (`big-cow`, `tamby`,
  `macprorack`), so it would only repeat each name. If those ever differ, the
  pairing file can gain the name then.
- Records the phone keeps under the old name are read under the new one:
  frecency in `session-openings.json` (row keys `jamiepond@macprorack/...`),
  `held-pane.json`, `temporary-panes.json` and `last-screen.json`. A session
  with openings under both names keeps the record opened last. The next save
  writes the new names.
- After a project session's login shell starts, the phone leaves its layout
  with the host's daemon (`layout`, `makeLayoutMessage`, shared with the Mac),
  as a Mac does for its own sessions: one pane, filed under
  `macprorack:<folder>`, the path the Mac's palette lists the project under.
  The Mac adopts it (`adoptRemoteSession`) and the palette shows one session
  where the project row was. `SavedPane` and `SavedSession` moved from
  `Terminal/` into `Lib/Multiplexer/SessionLayout.h` so the phone writes the
  same layout.
- The Mac asked a host for layouts only when it connected. It now also asks
  each time the palette opens, so a session the phone started since shows on
  the next open.
- The pane title shows a session named after its host once
  (`macprorack/iac`), and is cut short with an ellipsis before the link state.
- A session the phone starts on `big-cow` is named `big-cow/<folder>` and is
  not adopted: the Mac adopts layouts only from remote hosts, and names its own
  projects without a host.
- `just ios-test-project-opening <user@host>` now also lists the daemon's
  layouts with the probe (`--list-layouts`, which prints the name the Mac's
  `getSessionNameForProject` gives each project directory) and passes only when
  the phone's layout is filed under `<host>:<folder>`, its name is the Mac's
  name for that directory, and the pane is in that session.

### A host's name and the address the phone dials

On 18 November tamby moved to another tailnet, its short name stopped
resolving, and the phone could no longer reach it, though the Mac could
through an alias in its ssh config. A paired host now has a name and,
separately, an address to dial: the hosts file says
`jamiepond@tamby=jamies-mac-mini.tail8acf23.ts.net`. Sessions, host colours,
frecency and the Mac adopting phone sessions all key on the name (`tamby`);
the link dials the address and checks the host key under it, so the phone's
known_hosts holds the key under the address. `just ios-pair` takes the address,
user and port from this Mac's `ssh -G <host>`, so nothing is written into the
code and the next rename needs only a new pairing. A pairing from before has no
address and dials the name, as it did.

### Pairing a phone

The app on a real phone showed the sample pane, labelled live, because a phone
has no ssh key, host list or known hosts to reach anything with. `just
ios-pair` now gives it all three, and the sample needs `--sample`.

- The phone gets its own ECDSA P-256 key with no passphrase, since the app's
  mbedTLS cannot sign with ed25519. openssl writes it as SEC1 PEM on the named
  curve. macOS's `ssh-keygen -m PEM` writes the curve's parameters out in full,
  which mbedTLS refuses ("PK - Invalid key tag or value"); a key read back from
  the phone in that form is rewritten as the same key on the named curve.
- libssh2 on mbedTLS can work out a key file's public half only for RSA, so
  the phone also gets `phone-key.pub`, and `SshCredentials::publicKeyPath`
  hands it to libssh2. Without it libssh2 answers "Key type not supported" and
  the host refuses the key.
- Its public key goes into `~/.ssh/authorized_keys` on this Mac (`big-cow`),
  tamby and macprorack, and on tamby-windows (approved on 18 November) into
  `C:\ProgramData\ssh\administrators_authorized_keys`, the only file Windows'
  ssh server reads an administrator's keys from; it is appended to, never
  replaced, so it keeps its Administrators-and-SYSTEM-only access. Each gets
  the key once, with the comment `cowterm-device:pond`. Running
  the script again reads the key back from the phone and adds nothing. `just
  ios-unpair` removes exactly the lines with that comment.
- The phone's known hosts are the lines this Mac's `~/.ssh/known_hosts`
  already has for the names it dials. A name missing there is scanned with
  `ssh-keyscan` and kept only if the key equals one known_hosts holds under
  another name. All three names were already there, so none was added.
- Before copying, the script signs in with the phone's files through the app's
  own ssh client (the probe), on each host, so a key OpenSSH takes but libssh2
  does not is caught on the Mac.
- The script copies `hosts` (one `user@host` a line; the first is where an
  explicit `--pane` opens), `known_hosts`, `phone-key` and `phone-key.pub` into
  `Library/Application Support/pond/CowTerm/pairing` in the app's container
  with `devicectl device copy to`, and deletes the private key from the Mac.
  The key is a file in the container, not in the keychain or the Secure
  Enclave yet.
- The app reads that folder at its edge (`PhoneLaunch::readSettings`) only when
  the launch arguments and environment name no host, so the simulator scripts
  work as before. With no host anywhere the overview says "No hosts set up.
  Run just ios-pair on the Mac." and shows nothing else.
- On the phone the app writes its log to `logs/CowTermMobile.log` in the same
  folder, keeping the launch before as `CowTermMobile.previous.log`; `just
  ios-device-log` prints it.
- The phone dials the bare tailnet names (`big-cow`, `tamby`, `macprorack`).
  They resolve only while Tailscale on the phone is connected; otherwise each
  host shows "Could not resolve hostname".
- While the overview is on screen no host is asked anything every second, so
  silence is not a stall then; only the shown pane's host stalls and is
  redialled for being quiet.

### Push notifications

When a Claude in one of Jamie's panes finishes its turn or asks him something
while he is away from the desk, his phone shows it on the lock screen with a
sound, and a tap opens that pane.

- Claude Code's hooks run `ct pane-state --claude-hook` on UserPromptSubmit
  (working), Notification for permission and input questions (waiting, with
  the question) and Stop (finished, with Claude's last line). `just
  claude-hooks-add [user@host]` adds exactly those entries to
  `~/.claude/settings.json` and `just claude-hooks-remove` takes them out; Jamie
  agreed them for big-cow, tamby and macprorack on 17 November.
- A pane is unread when a Claude there finished or asked while no client was
  showing it to Jamie, or a shell there (not Claude) rang its bell or sent an
  OSC 9/777 notification. It keeps the event, one line of text and the time.
  It is read once any client sends `viewing <id>` for it (the phone shows the
  pane in the foreground; the Mac has it as the focused pane of its focused
  window and is not idle), or when Claude is given a prompt there. While a
  client is viewing a pane, what happens there is seen as it happens.
  `not-viewing <id>` or a disconnect ends that. The state lives in the pane's
  daemon (`PaneReadState`, `Daemon/UnreadPanes`), so seeing it on one client
  clears it on all, and it is kept in the daemon's pane store, so it survives
  a daemon restart. `activity` carries it in three more columns: the event,
  since when, and the text.
- A push is exactly a new unread event: the daemon posts it to the relay in
  `cowterm.json`'s `pushRelay`, titled with `pushHostName` (both Macs' host
  names read "daily-driver", so the name is set, not looked up; `just
  push-relay-set-host`). The same event again while still unread is not
  posted. This replaced an earlier rule (the phone holding the pane's size, or
  a desktop typing into it in the last minute), so nothing decides pushes but
  unread.
- `ct push-relay` runs on macprorack as a systemd user service (`just
  push-relay-install`), listening on its tailnet address only (eacp's
  `ServerOptions::bindAddress`). Apple's key is in Jamie's 1Password, vault
  CowTerm Push, item APNs Key (`key id`, `team id`, `private key`); a read-only
  service account for that vault alone has its token in
  `~/.config/cowterm-push-relay/op-service-account-token` (mode 600, `just
  push-relay-store-token`), and `op run` hands the key to the relay at start,
  so it is never on disk. The relay signs ES256 tokens with mbedTLS and renews
  them every 40 minutes or when Apple says they expired; it forgets a phone
  Apple calls unknown or unregistered, retries rate limits and Apple's server
  errors twice with a growing wait, and logs every answer (`just push-relay-log`).
- The phone asks once, at launch with a relay set up, with its own alert
  explaining what the notifications are for before iOS asks; "Not now" asks
  again on the next launch. It registers its device token with the relay on
  every launch. It learns the relay from `push-relay` and `device-name` in its
  pairing folder (`just ios-set-push-relay`).
- The notification's title is `host/session`, its body Claude's line,
  grouped by that title, with `ba-ding.caf`: two notes made from sine waves by
  `scripts/make-ba-ding-sound.py`, so no one else's sound ships. A tap opens
  the overview, then the session holding the pane, then the pane.
- Push needs a paid team's profile, so the entitlement is off unless the
  build sets `COWTERM_IOS_PUSH_NOTIFICATIONS`; `COWTERM_IOS_APNS_ENVIRONMENT`
  says whether the device token is the sandbox's (development) or production's
  (TestFlight).

### The unread mark

When a Claude in one of Jamie's panes finished or asked while he was not
looking, nothing told him which session needed him. Every list of sessions now
puts those sessions first with a yellow pill that names what happened.

- The mark is a pill filled with Okabe-Ito yellow (#f0e442) with the event in
  black words: "finished", "waiting", "bell" or "notified". The yellow is
  lighter than every host's row fill, and the word and the pill's shape say it
  without colour, since Jamie is mildly red-green colour blind
  (`UnreadMark.h`, `getUnreadMarkFillColour`).
- With nothing typed, unread sessions come first, the newest unread first;
  the rest keep the order they had (by use, then projects). A typed query
  ranks by the match alone: the pill shows but does not move a row, so typing
  stays predictable (`moveUnreadRowsFirst`). On the phone the waiting cards
  still sit nearest the thumb, unread ones first among them, then the unread
  rows.
- The Mac's Ctrl+Tab switcher keeps its most-recently-used order and shows the
  pill in place of the card's icon: re-ordering it would break the quick flip
  to the previous session.
- Opening an unread session on the phone opens its newest unread pane.
- The pane screen's ☰ carries a count of the other unread sessions, and a pane
  dot of an unread pane is a yellow square instead of a circle. While the pane
  screen is up the phone keeps asking for `activity` every three seconds, as
  the overview does.
- The phone sends `viewing <id>` for the chosen pane while the pane screen is
  up and the app is in the foreground, and `not-viewing` when the overview or
  dv's screen replaces it, when it moves to another pane, or when the app goes
  to the background. dv's screen views nothing: dv is not Claude's pane.
- The phone's overview row shows the pill at the start of its state line.
- The Mac views the focused pane of its focused window. A minute with no key
  typed in the app ends that (`desktopIdleMilliseconds`); the next key, or the
  window coming to the front, views it again. Only keys count: clicks and
  scrolling in a pane do not reach the app shell. A timer checks once a second.
- Both send only when the viewed pane changes, and send `viewing` again when a
  host's link comes back, since the daemon forgets a client that disconnects
  (`PaneViewingReporter`).
- The Mac asks every daemon for `activity` when the palette or the switcher
  opens and redraws when the answers arrive (`UnreadReports`).

### Touch on a real phone

On Jamie's phone no touch reached the pane screen: no tap on the bar, no
keyboard, no way back to the list, no scrolling, and a tap on the pane opened
another session. eacp on iOS hands every touch to the window's root view and
picks the target with `View::hitTest`, which never looked at visibility. The
session list, hidden but still laid over the pane screen, took every touch:
the phone's log showed "overview list: finger down" for taps on the pane, and
the keyboard Jamie raised typed into the list's hidden search field. The hidden
action menu, which covers the whole pane, did the same to drags: they became
mouse events and selected text instead of scrolling. Nothing had ever tapped
the app before: the simulator checks drove it with launch flags.

A view the phone shows only some of the time now exists only while it is
shown. It was first fixed in eacp, by making a hidden view take no touch, but
Jamie closed that pull request: eacp's hit-testing stays as it is, and the app
must not depend on it.

- The overview and dv's screen are made when they open and destroyed when they
  close (`ScreenStack`, `iOS/ScreenStack.h`). The pane screen is always there,
  under them, since it holds the pane's link. A new overview starts with an
  empty search, as before; the app keeps its scroll position and hands it to
  the next one. The overview binding attaches to each new overview
  (`PhoneOverviewBinding::showOn`) and dv's binding gets each new dv screen.
- The action menu and the "↓ n new" pill are made when shown and destroyed when
  hidden (`HostedComponent`, `iOS/ViewsShownSometimes.h`); dv's touch surface
  comes and goes with dv's pane. A closed view leaves the view tree at once and
  is destroyed on the next loop tick, because it usually closes from inside one
  of its own callbacks.
- The rest stay alive while hidden, are made invisible and ignore mouse events
  (`showOrHideLettingTouchesThrough`): the composer, which holds unsent text,
  its stop button and the key row; the bottom bar while the keyboard is up; the
  prompt buttons, which take no height when there is no prompt; and the pane
  screen under the overview or dv. That is only safe where no view that takes
  touches lies beneath: eacp picks a finger's view by position whether it is
  shown or not, and ignoring mouse events sends the touch on as a mouse event,
  never to the pane's touch surface. None of these lies over the pane.

All screens live in one content view instead of being swapped in with
`Window::setContentView`, so the root view that receives touches never changes.
`Tests/HiddenViewTouchTests.cpp` routes fingers from that root as eacp does on
the phone: a tap reaches the pane screen once the overview or dv closes, and a
drag scrolls the pane once the menu closes. `--tap-after-overview` and
`--drag-after-menu` do the same in the simulator.

- The app logs each finger that lands on the pane, the bar and the list, and
  each bar action, so `just ios-device-log` shows what a finger did.
- A finger on the bar below its row of controls, over the home indicator,
  counts as the control above it.
- A swipe right from the left edge of the pane goes back to the list, as the
  iOS back gesture does; it never scrolls.
- The phone never claims a grid narrower than 20 columns. A pane view built
  before it was laid out reported 11 columns, and one of Jamie's Mac panes was
  squeezed to 11 × 5 and later given back at that size.

### Links

- A tap on a link opens it on the phone (`eacp::Apps::openExternalURL`), even
  in a pane whose program asked for mouse reports, such as Claude inside tmux;
  a tap anywhere else reaches the program as a click. The press that opened a
  link keeps its release too, so the program never sees half a click (this
  holds on the Mac as well). Inside dv or lazygit a tap on a URL therefore
  opens the URL instead of clicking that cell; neither program does anything
  of its own with a click on a URL, so nothing is lost.
- On the phone every detected link is underlined all the time
  (`TerminalView::setUnderlinesEveryLink`), so a tap's effect is visible
  before it lands and is not told by colour alone. The Mac keeps underlining
  only the link under the pointer with Cmd held.
- Claude Code's /login URL inside tmux broke Cmd+click: tmux redraws each row
  on its own, so no row is marked as wrapped, and drops Claude's OSC 8 link.
  A row now runs on into an unindented next row when the row is one word from
  its first column to within a column of the edge, and the next row starts
  with URL text (`findColumnWhereBrokenUrlResumes`). Copying a selection uses
  the same rule, so a yanked URL is the one line a click opens; text with
  real line ends keeps them.

### Opening on protocol 4 daemons

Jamie's daemons (the Mac's CowTerm, cowtermd on tamby and macprorack) speak
protocol 4. The phone works with them as they are; this is what is missing
there:

- No last-input, so no daemon can say which pane he typed in last. The app
  opens on the pane it showed last time, if that pane still exists, and
  otherwise stays on the overview. It never opens a pane nobody chose, since
  opening one takes its size.
- No activity, so overview rows have no state line ("working, 4 min"), no
  waiting cards, and are not ordered by use: they are alphabetical within each
  host.
- No session names for the Mac's own panes. A pane with no session counts as in
  the session named after its folder, so `~/projects/CowTerm`'s panes form one
  row named CowTerm. Two Mac sessions in one folder show as one row, and a pane
  that changed folder moves to that folder's row.
- No size owner. The phone still takes the size of the pane it shows, and keeps
  the size the pane had in the first state its daemon sent. It sends that size
  back as a plain resize when it leaves the pane, goes to the background, or
  its link stalls, and takes the pane again on coming back. The Mac is never
  told: it shows no "on iPhone" note and typing on the Mac does not take the
  pane back. Any resize from the Mac (its window or layout changing) does take
  the pane back without the phone knowing, and the phone then shows it at the
  Mac's width until it opens the pane again. If the app is killed in the
  foreground (a reinstall from Xcode or devicectl, a crash), nothing is sent
  then; the next launch gives the size back (below). Before that was built, a
  reinstall left one of Jamie's eacp panes at 45 × 17 until the Mac laid it
  out again at 74 × 20. A daemon that answers `daemon-hello` with 5 or more keeps size
  owners itself and gets none of this.
- While the phone holds such a pane it keeps the pane and its earlier size in
  `held-pane.json`. If the app is killed in the foreground, the next launch
  sends that size back as soon as the pane's daemon says it is protocol 4.
- `scripts/check-ios-size-return.sh` (`just ios-test-size-return`) checks it on
  two throwaway panes on a host.

## What eacp lacks on iOS

Each gap was a branch of eacp off Eyal's develop. All of them are now on
jp/develop, and CowTerm pins jp/develop at 0b43002a (tag
jp/develop/2026-11-17.2), which no longer carries eacp's hidden-view change. The phone app builds from the pin alone; the local
`~/projects/eacp-ios-integration` checkout and `EACP_SOURCE` are gone.

| Gap | Branch | Commit |
| --- | --- | --- |
| Typing: no `UIKeyInput`, so no keyboard and no text; hardware keys reported nothing | `jp/ios-text-input` (`~/projects/eacp-ios-text-input`) | 176ab5a4 |
| The software keyboard's height and animation | `jp/ios-keyboard-frame` (`~/projects/eacp-ios-keyboard-frame`) | 2b287d8f |
| The status bar was always hidden | `jp/ios-status-bar` (`~/projects/eacp-ios-status-bar`) | c5ce3388 |
| No native text view, so no dictation or autocorrect for the composer; a hardware key's down event on it | `jp/ios-native-text-view` (`~/projects/eacp-ios-native-text-view`), stacked on `jp/ios-text-input` | eea9e5a7, f06a4c3d |

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
