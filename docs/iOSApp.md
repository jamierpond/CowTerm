# CowTerm for iPhone (exploration)

Status: idea, nothing built. This page looks at what an iOS CowTerm would take
and whether it's worth it. It is grounded in how the daemon and `ct bridge`
work today.

## The problem

Jamie's shells live in `cowtermd` on several machines (the Macs, tamby,
macprorack, the Windows box). Many of them run Claude Code. When Jamie walks
away from the desk, those sessions keep going, but there's no way to see them:
which Claude finished, which one is blocked on `ct notify-user`, what the test
run printed. A generic iOS ssh app (Blink, Termius) opens a new login shell. It
can't see the panes the daemon already holds.

## What the phone is for

Writing code with your thumbs is not the goal. The phone is for **steering**:

1. **Glance**: one list of every session on every machine, with Claude
   sessions marked (`✳`) and the ones waiting on you at the top.
2. **Answer**: when an agent asks `ct notify-user --options "merge" "wait"`,
   the phone buzzes and you tap **merge**, from the lock screen if you like.
3. **Nudge**: open the pane, read the scrollback, dictate a sentence to
   Claude, press Enter, or run a short command (`just test`, `git push`).
4. **Pick up**: back at the Mac, everything is exactly where you left it,
   because both clients were looking at the same daemon pane.

## Why this is cheaper than it sounds

Most of the work is already done. An iOS app is another client of a protocol
that exists today.

| Already built | What it gives the phone |
| --- | --- |
| `ssh <host> ct bridge` relays the daemon's messages over stdin/stdout as `<length>\n<bytes>` frames (`Lib/Multiplexer/BridgeFraming.h`) | One transport for every machine. The phone needs an in-app ssh client that runs this one command. No new server or port. |
| `scripts/probe-bridge.py` drives a remote daemon over that bridge in about 100 lines of Python | Proof that a client that isn't CowTerm can speak the protocol. |
| `spawn <id> <cols> <rows> <session>` on a live id *adopts* the pane and replies `state <id>` with the full serialized screen | Resilience like mosh, without mosh. iOS kills the socket within seconds of backgrounding, and on foreground one reconnect + spawn redraws the whole pane. |
| `info` returns `id \t foregroundProcess \t cwd \t session` for every pane | The session list, with Claude detection (`process contains "claude"`, as `SessionView::isClaudeAnywhere` does). |
| `layouts` returns every session's saved split layout | Which panes form a session, so the phone can show a session as a stack of panes. |
| `nu-request` / `nu-reply` / `nu-cancel` already cross the bridge (`Bridge.cpp`) | Asks from agents can reach the phone. |
| `Lib/Emulator` is "bytes in, screen state out: no process, no GPU, no daemon" and depends only on `eacp-core` | The phone can run the *same* parser and load the same `state` JSON. Its scrollback, wide chars, reflow and Kitty modes behave like the Mac's. |

## Architecture

```
iPhone                                         each machine
┌───────────────────────────────┐              ┌──────────────────────┐
│ SwiftUI: sessions, panes, asks│              │ cowtermd (owns PTYs) │
│ Metal/CoreText grid renderer  │              │        ▲             │
│ Lib/Emulator (C++ via interop)│   ssh        │   ct bridge          │
│ Bridge framing + proto verbs  │──(Tailscale)─▶│  (stdin/stdout)      │
│ SwiftNIO SSH, Secure Enclave  │   exec       └──────────────────────┘
└───────────────────────────────┘
          ▲ APNs push (phase 3)
          │
   ct push-relay on an always-on tailnet host
          ▲ daemons POST over the tailnet on OSC 9/777 and nu-request
```

### Transport: SwiftNIO SSH, a key in the Secure Enclave

iOS apps can't run the system `ssh`, so the app embeds a client:
[SwiftNIO SSH](https://github.com/apple/swift-nio-ssh), probably through the
Citadel wrapper. It opens one exec channel per host running the same
`bridgeCommand` that `RemoteLocation.h` builds, and speaks the frames.

The key is a **P-256 key in the Secure Enclave**: it can't be exported, it's
unlocked by Face ID, and it's one key per device. NIOSSH speaks
`ecdsa-sha2-nistp256`, the only curve the Secure Enclave supports. This keeps
the Mac's own policy (key auth only, never a password prompt).

### Reaching the machines: the tailnet

Decided: the phone is on Jamie's tailnet, and so is every machine. That
settles reachability and changes a few other things:

- **Addresses:** every host is reachable by its MagicDNS name (or its
  100.x address) from anywhere: home, 5G, a café. No port forwarding, no
  ProxyJump, nothing exposed to the internet. The app never deals with
  `~/.ssh/config`; it stores `user@magicdns-name` per host.
- **sshd only on the tailnet:** the hosts can bind sshd to the tailscale
  interface, or firewall port 22 to `100.64.0.0/10`. On the Macs, Remote
  Login listens everywhere, so pf or the macOS firewall has to narrow it.
- **Normal sshd, not Tailscale SSH:** Tailscale SSH would remove key
  management entirely, with auth by tailnet identity and ACLs. But it can't
  serve on macOS (the App Store Tailscale has no SSH server) or on Windows,
  and the Macs and tamby-windows are hosts. So the app uses the system sshd
  everywhere with its Secure Enclave key. One code path. Where Tailscale SSH
  happens to be on, it still works, because the app offers `none` auth first.
- **Tailscale is off:** iOS reports the VPN state. If a 100.x host
  doesn't answer and the tunnel isn't up, the session list shows one banner,
  "Tailscale is off", instead of every host failing on its own timeout.
- **Woken in the background:** Tailscale's tunnel is a Network Extension.
  It stays up while CowTerm is suspended, so a lock-screen answer can dial
  ssh straight away. Nothing has to reconnect first.

### Pairing: `ct pair-device`

This is the setup step, and it's where the app should feel good to use.
Running `ct pair-device` on the Mac prints a QR code holding:

- every host in `cowterm.json`, mapped to its tailnet name: `ssh -G <alias>`
  gives the user and the real hostname, and `tailscale status --json` matches
  that to a peer's MagicDNS name. A host that isn't on the tailnet is
  listed as a warning on the Mac and left out,
- each host's **host key fingerprint**, so the phone pins it. WireGuard
  already proves which node it's talking to, but pinning is cheap and also
  catches a reinstalled box,
- a one-time pairing token.

The phone scans it, creates its Secure Enclave key, and sends back the public
key. `ct` then appends that key to `authorized_keys` on every host, in
parallel as `ct update-remotes` does, tagged `cowterm-device:<name>`.
`ct revoke-device <name>` removes the key again. One scan and every machine
works.

### Emulator: reuse `Lib/Emulator`, don't adopt SwiftTerm

The daemon's adopt reply is `state <id>` followed by `serializeTerminalState`, which is
Miro JSON of CowTerm's own `TerminalState`. A third-party Swift emulator such
as SwiftTerm can't load that, so it would show a blank pane until new output
arrives. Building `CowTermEmulator` as an XCFramework and calling it through
Swift's C++ interop:

- loads `state` directly, so the redraw on reconnect is right the first time,
- means one parser, one set of fuzz corpora, one set of bugs.

**Risk #1:** `eacp-core` (for `Primitives.h` and Miro reflection) must build
for `arm64-apple-ios`. If eacp doesn't target iOS yet, the emulator only
needs a small part of it. Phase 0 checks this first.

### Rendering

For phase 1, draw the grid with CoreText into a `CALayer`. Phone panes are
small and change rarely. If scrolling feels slow, move to a Metal glyph atlas
like the Mac's `GlyphAtlas`, by reusing `Lib/TerminalView` if eacp's graphics
run on iOS, or with a small Swift renderer otherwise.

## The hard problems

### 1. Resizing: the phone and the Mac both want to set the pane size

Every client watching a pane shares one PTY, and `resize` is last-writer-wins
(`Daemon::resize`). If the phone sends 46×30, the Mac's 200×50 Claude pane
reflows to phone width, and an open TUI redraws for 46 columns.

Proposal, in two steps:

- **View mode (default):** the phone never resizes. It renders the pane at the
  daemon's size and fits it to the screen width. You can pinch-zoom and pan,
  and in landscape most panes are readable. This is enough to glance and answer.
- **Take over:** when you start typing, the phone claims the size. The daemon
  gets a size *owner*, the client that last sent input or `claim-size`. It
  resizes the PTY to the owner's size and tells the other watchers
  `size-owner <id> <client>`, so the Mac shows a thin "on iPhone" banner. The
  first key pressed on the Mac takes the size back. This is tmux's
  `window-size latest`. It's a small daemon change and bumps
  `daemonProtocolVersion` to 5.

### 2. Backgrounding: push notifications need a server

iOS suspends the app shortly after it leaves the screen, which closes every ssh
channel. Tailscale doesn't change that: the tunnel stays up, but the app
isn't running to listen on it. Rings and asks then need **APNs**, and APNs
needs something holding the `.p8` key.

The tailnet lets that be one of Jamie's own machines, with no public
server:

- **`ct push-relay`** runs on one always-on host (tamby) and listens only on
  its tailnet address. It holds the `.p8` key and is the only thing that talks
  to Apple. It's about 100 lines: a small HTTP endpoint plus an
  ES256-signed APNs request.
- Every `cowtermd` POSTs `{device, title, body, host, pane, ask options}` to
  `http://<relay>.<tailnet>.ts.net` when a pane rings (OSC 9/777) or asks
  (`nu-request`). It only does this when no interactive client has had
  input in the last few minutes, so the phone stays quiet at the desk. The relay's
  address goes in `cowterm.json` (`"pushRelay": "tamby"`).
- No auth beyond the tailnet itself: Tailscale ACLs decide who may reach the
  relay's port.
- The phone registers its APNs token once, with the relay, over the tailnet.
  Daemons only need to know the device *name*, not the token.
- If the relay host is down, pushes stop and nothing else breaks. Opening
  the app still shows everything.

**Answering from the lock screen:** a `nu-request`'s options become
`UNNotificationAction` buttons, plus a text field when `--allow-type-response`
is set. Tapping one wakes the app in the background for about 30 seconds,
long enough to dial ssh and send `nu-reply`. Use
`.authenticationRequired` actions, so a locked phone has to be unlocked first.

### 3. Asks have more than one listener

Today an ask has exactly one destination. Local `ct notify-user` dials the
Mac app's IPC name, and a remote pane's ask travels over the bridge to the Mac
that owns it. With a phone, both should show it and **the first answer wins**:
the others get `nu-cancel` and close their window. The cleanest design routes
every ask through the daemon of the machine it was asked on. It fans the ask
out to every attached client and every registered device, then cancels the
rest after the first reply. On the Mac, `ct notify-user` would dial the daemon
instead of the app. The app becomes one more listener.

### 4. Sessions on the Mac itself

The phone reaches the Mac like any other host: Remote Login (sshd) on, then
`ct bridge` against the bundled daemon. Phase 0 must check that `ct bridge`
finds the daemon inside `CowTerm.app`.

### 5. Typing on glass

- An **accessory bar** above the keyboard: `Esc`, `Tab`, a sticky `Ctrl`,
  `⌃A` (the leader), arrows, and `| ~ / -`. Dragging on the bar moves the
  cursor, like dragging on the space bar.
- A **composer for Claude panes**: a native text field with dictation and
  autocorrect that sends as a bracketed paste plus Enter. Speaking a paragraph
  to Claude beats typing it in a terminal grid.
- **Quick replies** when the screen shows a Claude permission prompt
  (`1. Yes  2. Yes, don't ask again  3. No`): buttons instead of keys.
- iPad with a hardware keyboard: the Mac keymap and Kitty encoding
  (`KeyEncoding.cpp`), and real splits from `layouts`.

## Things that would make it a joy

- **Live Activity / Dynamic Island** for a Claude session you're watching:
  "tamby/Grazer · Claude working… 4m" → "waiting for you", updated by push.
- **Waiting first**: the session list sorts by *needs you* (open asks, rang
  since last viewed, Claude idle at a prompt), then MRU, which the Mac already
  tracks in `MruStore`.
- **Host colours** from `HostColours.cpp`, so `tamby` is the same pink on
  the phone as in the Mac picker.
- **Handoff:** the Mac advertises the active pane as an `NSUserActivity`. When
  you pick up the phone, it opens the same pane.
- **Shortcuts / widgets**: "Ask CowTerm: what's Claude doing on tamby?" shows
  the last 20 lines of the pane.

## Plan

**Phase 0: spike (a weekend).** No UI yet.
- A Swift command-line tool on macOS, using Citadel, that ports
  `probe-bridge.py`: ssh to tamby, `ct bridge`, `info`, adopt a pane, print the
  `state`.
- Build `CowTermEmulator` + `eacp-core` for the iOS simulator. Load a
  captured `state` blob and dump the grid as text.
- If both work, the remaining risk is product work, not feasibility.

**Phase 1: Glance.** iOS app over the tailnet, `ct pair-device` with MagicDNS names, one session list
across hosts with Claude badges, read-only panes at daemon size with pinch-zoom,
and an instant redraw on foreground through adopt + `state`.

**Phase 2: Steer.** Typing, accessory bar, Claude composer and quick replies,
take-over resize (daemon protocol v5).

**Phase 3: Ring.** `ct push-relay` on a tailnet host, device registration, notify-user fan-out with
first-answer-wins, lock-screen answers, Live Activities.

**Phase 4: iPad.** Hardware keyboard, real splits, maybe the Metal renderer.

### CowTerm-side changes, all phases

| Change | Where | Phase |
| --- | --- | --- |
| `ct pair-device`, `ct revoke-device` | `Remote/CtMain.cpp`, `RemoteInstall` | 1 |
| Make sure `ct bridge` works against the Mac's bundled daemon | `Bridge.cpp`, `DaemonDialer` | 1 |
| Window title in `info` (Claude's conversation title), if the daemon's model keeps OSC 0/2 | `Daemon/Main.cpp` | 1 |
| Size ownership (`claim-size`, `size-owner`), protocol v5 | `Daemon/Main.cpp`, `DaemonClient` | 2 |
| "On iPhone" banner on the Mac | `Terminal/SessionView` | 2 |
| Push on ring/ask when idle, POST to the relay | `Daemon/Main.cpp` | 3 |
| `ct push-relay` (APNs, listens on the tailnet only) | `Remote/CtMain.cpp` | 3 |
| notify-user goes through the daemon, fans out, first answer wins | `NotifyUserServer`, `Bridge.cpp`, `ct` | 3 |

## Open questions for Jamie

Decided: the phone and every host are on the tailnet.

1. **Which host runs `ct push-relay`?** It needs to be always on and on the
   tailnet; tamby is assumed above.
2. **iPhone first or iPad first?** iPad with a keyboard is nearly the Mac
   app. iPhone is the steering use case this page is built around.
3. **Same repo?** An `iOS/` directory here would share `Lib/Emulator`
   directly. A separate repo would pin CowTerm through lockstep like the other
   dependencies.
4. It needs an Apple Developer account for TestFlight and APNs.
