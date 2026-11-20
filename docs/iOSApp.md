# CowTerm for iPhone (exploration)

Status: idea, nothing built. [iOSDesign.md](iOSDesign.md) says how the app
behaves and overrules this page where the two disagree (pane size, gestures,
the key bar, Face ID, the order of the phases). This page looks at what an iOS
CowTerm would take and whether it's worth it. It is grounded in how the daemon and `ct bridge`
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

## Architecture: a normal eacp app, in C++

Decided: iPhone first, and the app is an eacp iOS app written in C++ like the
Mac app. No Swift, no SwiftUI. It lives in this repo as `iOS/` (target
`CowTermMobile`) and links the same libraries the Mac app does. eacp already
builds iOS apps: its CI has an iOS Simulator lane
(`cmake -G Xcode -DCMAKE_SYSTEM_NAME=iOS`), and `set_default_target_setting`
handles the iOS plist and signing.

```
iPhone: CowTermMobile (eacp, C++)                     each machine
┌───────────────────────────────────────────┐         ┌──────────────────────┐
│ iOS/  session list, pane pager, asks, keys│         │ cowtermd (owns PTYs) │
│ Lib/TerminalView  (Metal grid, GlyphAtlas)│         │        ▲             │
│ Lib/Emulator      (same parser, same state)│        │   ct bridge          │
│ Lib/Multiplexer   DaemonClient, RemoteHost│  ssh    │  (stdin/stdout)      │
│   └ EmbeddedSshLink : DaemonLink  ────────┼─tailnet▶│                      │
│       libssh2 + mbedTLS, Secure Enclave key│        └──────────────────────┘
└───────────────────────────────────────────┘
          ▲ APNs push (phase 3)
   ct push-relay on an always-on tailnet host
          ▲ daemons POST over the tailnet on OSC 9/777 and nu-request
```

### What is reused as-is

| Library | On the phone |
| --- | --- |
| `Lib/Emulator` | The same parser. It loads the daemon's `state` JSON directly, so a pane redraws correctly the moment it's adopted. |
| `Lib/TerminalView` | Already "the embeddable terminal… an injectable shell factory". It draws on eacp's `GPUView` (Metal on iOS too) with `GlyphAtlas` on eacp-text, whose CoreText rasterizer is shared Apple code. So the phone gets the Mac's renderer, themes, JetBrains Mono, links and selection. |
| `Lib/Multiplexer` | `DaemonClient` (spawn/adopt, routes, `info`), `RemoteHost` (reconnect with backoff, reattach, `scan`, `layouts`), bridge framing, the notify-user protocol and `HostColours`. The phone runs the same client code as the Mac. Only the link underneath is new. |

### What is new in CowTerm

**`EmbeddedSshLink : DaemonLink`.** iOS forbids starting processes (eacp's
`Process-iOS.cpp` is a stub), so the system `ssh` that `SshBridgeLink` runs
isn't available. The new link does ssh inside the app with **libssh2** on
**mbedTLS**. Both are C, BSD/Apache licensed and built with CMake, so
lockstep pins them like every other dependency. The link opens a session,
runs `remote::bridgeCommand` on an exec channel, and feeds the channel into
the existing `bridge::FrameDecoder`. Its reader thread marshals to the main
thread with `Threads::callAsync`, as `SshBridgeLink` does. The link reports
the same `onReady` / `onFailed` reasons, so pane banners read the same as on
the Mac.

The seam is one line. `RemoteHost::connect` (`RemoteHosts.cpp:74`) creates
`SshBridgeLink` directly; it gets a link factory instead. The desktop factory
still returns `SshBridgeLink`, and the iOS one returns `EmbeddedSshLink`.

**Secure Enclave key.** The phone's key is a P-256 key created in the Secure
Enclave with `SecKeyCreateRandomKey`: it can't be exported and is gated by
Face ID. libssh2's `libssh2_userauth_publickey` takes a *sign callback*, so
the private key never leaves the enclave. The callback calls
`SecKeyCreateSignature` and re-encodes the DER `(r, s)` as the SSH
`ecdsa-sha2-nistp256` signature blob. This goes in one small `.mm` file.

**`Lib/Shell` without a PTY on iOS.** `Pty-Posix.cpp` uses `forkpty`, which
the iOS sandbox refuses. An iOS build compiles a `Pty-iOS.cpp` stub, the way
eacp stubs `Process`. The `Shell` interface is all the phone needs, because
every pane is a `DaemonShell`.

**`iOS/`.** The app itself: the session list, a pane pager, the ask sheet,
the key bar and pairing. All of it is eacp views drawn on the GPU, like the
Mac's palette. `PaletteRanking` (fuzzy match + MRU) moves from
`Terminal/` into a library both apps link.

### What is new in eacp (PRs on `jp/develop`, as usual)

Today's iOS backend draws and takes touches, but it can't type, and it can't
tell the app it's being backgrounded. A terminal needs:

1. **Text input on `View-iOS.mm`.** Make the view a `UIKeyInput`
   (`insertText:` and `deleteBackward` become eacp `KeyEvent`s with the
   typed characters), with the trait switches a terminal wants:
   autocorrect, autocapitalisation and smart quotes off. Add
   `pressesBegan`/`pressesEnded` with `UIKey`, so a hardware keyboard
   produces real key codes and modifiers. Today `Keyboard-iOS.mm` reports
   nothing pressed.
2. **Software keyboard frame.** An event carrying the keyboard's height as it
   animates in and out, so the pane shrinks above it and the key bar can sit
   on top of it.
3. **App lifecycle.** "Entering background" and "back in foreground" events.
   CowTerm drops its links on the first and reconnects and adopts on the
   second, instead of waiting for TCP to notice.
4. **Notifications.** Register for remote notifications and get the device
   token. Post, receive and act on notifications with action buttons and a
   text reply, through `UNUserNotificationCenter`. `App-iOS.mm` already says
   "nothing to do here until eacp has notifications". The Mac's
   `Notifier-macOS.mm` could move onto the same API.

All four are UIKit plumbing behind interfaces eacp already has the shape of,
which makes them good eacp PRs rather than CowTerm hacks.

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
  happens to be on, it still works, because libssh2 tries `none` auth first.
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

The phone scans it with the camera (eacp's `Camera` module has an iOS
backend; the QR decode is Vision or CoreImage in an `.mm`), creates its Secure Enclave
key, and sends back the public key. `ct` then appends that key to
`authorized_keys` on every host, in parallel as `ct update-remotes` does,
tagged `cowterm-device:<name>`. `ct revoke-device <name>` removes the key
again. One scan and every machine works.

### Touch on the terminal

`TerminalView` handles the mouse and keys today. On the phone it also needs:

- **one-finger drag** scrolls the scrollback,
- **long-press then drag** selects, then copies through eacp's iOS `Clipboard`,
- **pinch** sets the font size (the Mac's `Cmd +/-`),
- **tap** brings up the keyboard,
- **two-finger swipe** moves between the session's panes.

eacp already delivers `touchBegan/Moved/Ended`, so this is CowTerm code.

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

- **`ct push-relay`** runs on one always-on host (macprorack, Jamie's choice
  on 17 November) and listens only on
  its tailnet address. It holds the `.p8` key and is the only thing that talks
  to Apple. It's about 100 lines: a small HTTP endpoint plus an
  ES256-signed APNs request.
- Every `cowtermd` POSTs `{device, title, body, host, pane, ask options}` to
  `http://<relay>.<tailnet>.ts.net` when a pane rings (OSC 9/777) or asks
  (`nu-request`). It only does this when no interactive client has had
  input in the last few minutes, so the phone stays quiet at the desk. The relay's
  address goes in `cowterm.json` (`"pushRelay": "http://macprorack:8742"`).
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

- A **key bar** drawn by CowTerm on top of the software keyboard, using
  eacp's keyboard-frame event above the keyboard: `Esc`, `Tab`, a sticky `Ctrl`,
  `⌃A` (the leader), arrows, and `| ~ / -`. Dragging on the bar moves the
  cursor, like dragging on the space bar.
- **Quick replies** when the screen shows a Claude permission prompt
  (`1. Yes  2. Yes, don't ask again  3. No`): buttons instead of keys.
- **Hardware keyboards** go through the Mac's own `KeyEncoding.cpp` /
  `KittyKeyEncoding.cpp` once eacp sends real key events.

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

Live Activities and widgets are SwiftUI-only app extensions. If they're
wanted later, each one is a small Swift extension target next to the C++
app. That's the one place Swift can't be avoided, and none of it is needed
for phases 1–3.

## Plan

**Phase 0: spike (a weekend, on a Mac).** No UI yet.
- Configure CowTerm for the iOS Simulator the way eacp's CI lane does. Get
  `CowTermEmulator`, `CowTermTerminal` and `CowTermMultiplexer` to build,
  with the `Pty-iOS` stub. Find out whether emberstore, PondApp and SessionLog
  build for iOS too, or have to stay out of the phone's link.
- Add libssh2 + mbedTLS to lockstep. Write `EmbeddedSshLink`, and test it on
  macOS first as a `ct`-style CLI that ports `scripts/probe-bridge.py`:
  connect to tamby over the tailnet, adopt a pane, print it. Check that
  libssh2's mbedTLS backend negotiates key exchange and host keys with
  the hosts' stock OpenSSH. If it doesn't, use its OpenSSL backend instead.
- The smallest possible `CowTermMobile`: one `TerminalView` filling the
  screen, attached to one tamby pane, with a file-based key for now.

**Phase 1: Glance.** Session list across hosts with Claude badges and host
colours, the pane pager with touch scrolling, adopt on foreground, `ct
pair-device` + the Secure Enclave key. Read-only at the daemon's size, with
pinch to zoom. eacp PR: lifecycle events.

**Phase 2: Steer.** eacp PRs: text input and keyboard frame. Then the key
bar and quick replies, and take-over resize (daemon
protocol v5).

**Phase 3: Ring.** eacp PR: notifications. Then `ct push-relay` on a
tailnet host, notify-user fan-out with first-answer-wins, and answers from
the lock screen.

**Later:** iPad with real splits, and Live Activities as a Swift extension.

### Changes, all phases

| Change | Where | Phase |
| --- | --- | --- |
| libssh2 + mbedTLS in lockstep | `lockstep.lock`, `CMakeLists.txt` | 0 |
| `EmbeddedSshLink`, Secure Enclave signing | `Lib/Multiplexer`, `iOS/` | 0 |
| Link factory in `RemoteHost::connect` | `Lib/Multiplexer/RemoteHosts.cpp` | 0 |
| `Pty-iOS.cpp` stub | `Lib/Shell` | 0 |
| `CowTermMobile` app target | `iOS/`, `CMakeLists.txt` | 0 |
| Touch scrolling, selection and pinch in `TerminalView` | `Lib/TerminalView` | 1 |
| `PaletteRanking` moved into a shared library | `Terminal/` → `Lib/` | 1 |
| eacp: background / foreground events | eacp `jp/develop` | 1 |
| `ct pair-device`, `ct revoke-device` | `Remote/CtMain.cpp`, `RemoteInstall` | 1 |
| Make sure `ct bridge` works against the Mac's bundled daemon | `Bridge.cpp`, `DaemonDialer` | 1 |
| Window title in `info` (Claude's conversation title), if the daemon's model keeps OSC 0/2 | `Daemon/Main.cpp` | 1 |
| eacp: `UIKeyInput`, hardware keys, keyboard frame | eacp `jp/develop` | 2 |
| Size ownership (`claim-size`, `size-owner`), protocol v5 | `Daemon/Main.cpp`, `DaemonClient` | 2 |
| "On iPhone" banner on the Mac | `Terminal/SessionView` | 2 |
| eacp: notifications (token, actions, text reply) | eacp `jp/develop` | 3 |
| Push on ring/ask when idle, POST to the relay | `Daemon/Main.cpp` | 3 |
| `ct push-relay` (APNs, listens on the tailnet only) | `Remote/CtMain.cpp` | 3 |
| notify-user goes through the daemon, fans out, first answer wins | `NotifyUserServer`, `Bridge.cpp`, `ct` | 3 |

## Open questions for Jamie

Decided: the phone and every host are on the tailnet; iPhone first; a C++
eacp app in this repo (`iOS/`).

1. **Which host runs `ct push-relay`?** Answered: macprorack, which is always on.
2. **The phone's name in the app:** `CowTermMobile` as the target, but what
   goes on the home screen?
3. It needs an Apple Developer account for TestFlight and APNs; eacp's
   `EACP_APPLE_DEVELOPMENT_TEAM` takes the team id.
