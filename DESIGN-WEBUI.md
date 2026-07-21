# CowTerm Web UI + remote control — design

Goal: browse to `http://127.0.0.1:$port/` and see your sessions, attach to any
pane, type into it, and watch it render correctly — from any browser, and
from any other CowTerm on the network ("remote pilot", see **CowTerm ↔
CowTerm** below). No auth yet, but nothing in the shape of the thing should
need reworking when auth arrives.

## Where the gateway lives

The web gateway is hosted **in the CowTerm app process** (`Terminal/Web/`),
not the daemon. Reasons:

- The session model (names, project dirs, pane trees, Claude detection,
  titles, cwds) lives in `SessionManager`/`SessionView` — the daemon only
  knows flat `shellId → PTY`. "See my sessions" is an app-level question.
- Every pane already has a fully parsed `TermScreen` in the app. That gives
  us pixel-faithful attach snapshots for free — no replay-buffer heuristics.
- The app is the daemon's single IPC client; a second client would need a
  daemon rewrite today.

**Later** (headless piloting with the GUI closed): move `SessionManager` down
into the daemon and make the daemon multi-client; the HTTP/WS surface below
moves with it unchanged — clients never see the difference. That's the main
reason the API is designed against *sessions and panes*, never against GUI
concepts (views, focus rectangles, overlays).

## Transport

Two listeners, both loopback-only:

| Port | What | Implementation |
|---|---|---|
| `webPort` (config, default **2697**) | REST + static UI | `eacp::HTTP::Server`, `EventLoop` dispatch |
| `webPort + 1` | WebSocket stream | `eacp::TCP::Listener` + our RFC 6455 server (`Terminal/Web/WebSocket.*`) |

- eacp's HTTP server is strictly request→response (no upgrade hook), so the
  WebSocket endpoint rides its own `TCP::Listener`. The handshake reuses
  eacp's public `HTTP::RequestParser`; frames are RFC 6455 server-side
  (unmask inbound, never mask outbound), with a vendored SHA-1 for
  `Sec-WebSocket-Accept`. If/when eacp grows an upgrade hook, both collapse
  onto one port with zero API change — clients discover the WS URL from
  `GET /api/v1/server` rather than assuming it.
- `EventLoop` dispatch means every REST handler runs on the app's main
  thread, so touching `SessionManager` is safe by construction. WS reader
  threads marshal onto the main thread (`Threads::callAsync` + alive-guard,
  same pattern as the rest of the app) before touching anything.

### Security posture now (pre-auth)

- Both listeners bind loopback by default; `webBind: "any"` in the config
  opts into serving the local network (what remote piloting needs — an
  explicit, deliberate act while there's no auth).
- On a loopback bind, every HTTP request and WS handshake validates that
  `Host` (and `Origin` when a browser sends one) is loopback — this closes
  DNS-rebinding, which is the one attack that works against an
  unauthenticated localhost server. On a network bind the check is moot
  (peers dial by machine name) and drops away.
- `webPort = 0` disables the gateway entirely.

### Auth later (designed for, not built)

Token bearer, not cookies: `Authorization: Bearer <token>` on REST,
`?token=` on the WS URL (browsers can't set WS headers). One check in the
HTTP router + one in the WS handshake — the two single funnels every request
already flows through. Remote access = flip `bindTo` to `any` (or better:
keep loopback and tunnel via ssh/tailscale) + require the token. Nothing
else changes.

## REST API (`/api/v1`)

JSON in/out via Miro reflection (`toJSONString`/`fromJSONString` on
`MIRO_REFLECT` structs — same machinery as the config file, tolerant of
missing/unknown keys, so the API is forward/backward compatible by default).
Routing is a small in-house matcher over `Server::listen(port, handler)`
(eacp's route map is exact-match only; we need `/panes/{id}/...`).

```
GET  /                          the web app (embedded via res_embed)
GET  /api/v1/server             { name, version, wsUrl } — capability discovery
GET  /api/v1/sessions           { ev: "sessions", sessions: [ { key, name,
                                    projectDir, active, claude, panes: [
                                    { id, title, cwd, cols, rows, active } ] } ] }
                                (the same payload the WS pushes, so clients
                                 need one parser)
POST /api/v1/sessions           { dir }  → open (or focus) a project session
POST /api/v1/sessions/activate  { key }  → make a session active in the GUI
GET  /api/v1/panes/{id}/screen  ANSI repaint of the pane, text/plain
POST /api/v1/panes/{id}/input   raw bytes body → written to the shell verbatim
POST /api/v1/panes/{id}/text    { text } → same, JSON-friendly (send-keys)
```

Pane ids are the daemon `shellId`s — stable across GUI restarts, URL-safe
hex. Session keys are project paths, so they travel in bodies, never in path
segments. The `input` (raw) / `text` (JSON) pair exists because remote
piloting from scripts wants `curl -d 'ls\n'` ergonomics while structured
clients want JSON; both funnel into the same write.

## WebSocket protocol

One socket multiplexes everything (N panes + events); a client opens exactly
one. Text frames are JSON control messages; binary frames are terminal
output (`[u8 idLen][pane id][raw PTY bytes]`) — raw bytes never pay for
base64 or JSON escaping.

Client → server:
```
{ "op": "attach",   "pane": id }     subscribe; server replies with snapshot
{ "op": "detach",   "pane": id }
{ "op": "input",    "pane": id, "data": "…" }   keystrokes (xterm.js onData)
{ "op": "sessions" }                 request a sessions push
{ "op": "activate", "key": k }       pilot the GUI's active session
{ "op": "open",     "dir": d }       open a project
```

Server → client:
```
text   { "ev": "attached", "pane", "cols", "rows" }   then the binary snapshot
text   { "ev": "sessions", "sessions": […] }          pushed on every change
text   { "ev": "error", "message" }
binary pane output, id-prefixed as above
```

## Rendering

**xterm.js in the browser does the terminal emulation.** The server never
ships cell grids over the wire; it ships the same byte stream the PTY
produces, which is what xterm.js is built to eat. This is the ttyd/VS Code
architecture and it scales: server cost per subscriber is a memcpy.

Attach correctness comes from `ScreenSerializer`: it walks the pane's
already-parsed `TermScreen` and emits one ANSI string that reproduces the
current state on a fresh terminal — recent scrollback (primary screen),
the grid (24-bit SGR runs, wide chars, trailing-run trimming), cursor
position/visibility, and the input-relevant modes (app cursor keys,
bracketed paste, SGR mouse reporting, alt screen). After the snapshot the
client just plays the live stream.

Sizing: **the GUI owns the grid size** in v1. The web client sets xterm.js
to the pane's cols×rows and mirrors; a web-side resize is not sent. (A
follow-up can add size negotiation — smallest-client-wins like tmux — via a
`resize` op that's already trivial to slot into the WS protocol.)

Known v1 limits, all deliberate:
- A pane sitting in the alt screen snapshots only the alt screen; its
  primary scrollback appears blank in the web until new output arrives.
- Panes running in-process (daemon unreachable, ephemeral popup commands)
  stream fine — the tap is at `TerminalView`, above the Shell abstraction —
  but popup panes aren't listed since they belong to no session.
- xterm.js + fit addon load from CDN; offline shows a plain message.

## CowTerm ↔ CowTerm remote control

A CowTerm pilots another CowTerm through **exactly the surface the browser
uses** — same REST discovery, same websocket, same ops, same binary frames.
There is no second protocol, so the two piloting surfaces cannot diverge:
anything the web UI learns to do, a peer CowTerm can do, by construction.
The message shapes live once, in `Terminal/Web/Wire.h`, shared verbatim by
server (`WebGateway`) and native client (`GatewayClient`).

The pieces, top down:

- **Config**: the served side sets `webBind: "any"`; the piloting side lists
  it in `remotes: ["studio.local:2697"]`.
- **`GatewayClient`** (`Terminal/Web/GatewayClient.*`): the native end of a
  remote gateway. Discovers the websocket via `GET /api/v1/server` exactly
  like the browser does (the configured address is the HTTP port, never the
  WS port). Dials in the background (never blocks the UI), holds the
  remote's session roster live, redials on loss and re-attaches its panes —
  every attach opens with a server-side snapshot, so reconnects self-heal.
  One websocket multiplexes all of that machine's panes, as designed.
- **`RemoteShell`**: the keystone. It implements the existing `Shell`
  interface — the same abstraction that already hides "in-process PTY" vs
  "daemon-held PTY" — so a remote pane *is just a pane* to `TerminalView`:
  parsing, rendering, mouse reporting, copy mode all work unchanged.
  `terminate()`/`detach()` both only detach: closing a window here must
  never kill work running over there.
- **Grid ownership**: each GUI owns its own panes' sizes. `Shell` grew
  `fixedSize()`; a `RemoteShell` reports the remote pane's dimensions (from
  the `attached` event) and the local view mirrors them instead of imposing
  its bounds — the tmux "other client is smaller" model, minus reflow.
- **Remote HUD** (`Ctrl+A r`): every configured remote with its live
  sessions/panes. Enter attaches the pane full-window in the popup;
  `a` activates that session on the remote's GUI; offline remotes show as
  retrying. The HUD is a view over `GatewayClient`s, never a connection
  manager — links live for the app's lifetime.
- **The client half of RFC 6455** lives in the same `WsConnection` as the
  server half (outbound masking + `wsConnectClient` handshake), so the two
  ends are tested against each other in `Test/WebTests.cpp`.

"Two cowterms remote-controlling each other" is just both machines setting
`webBind: "any"` and listing each other in `remotes` — the roles are fully
symmetric, and each side can also be piloted from a browser at any time.

## Wiring into the app (kept deliberately thin)

- `TerminalView` gains an `onOutput` raw-bytes tap (fired in `flushOutput`,
  main thread), a public `writeToShell()`, and a `screenModel()` accessor.
- `SessionView::panes()` exposes the leaf list (the private `collectLeaves`
  already existed).
- `AppShell` owns a `WebGateway` and forwards two existing hooks:
  `onPaneWired → web.wirePane(pane)` and `onSessionsChanged →
  web.sessionsChanged()`.
- `AppConfig.webPort` (default 2697 — "COWS"; 0 = off).

Everything else is new code under `Terminal/Web/`:
`WebGateway` (HTTP + routing + WS session brokering, main-thread object),
`WebSocket` (listener/connection), `ScreenSerializer`, `Sha1.h`, and the
single-file client in `Terminal/Resources/webui.html`.

## Scaling the API later — the intended growth path

- **Auth**: bearer token at the two funnels (above).
- **Multi-client**: already true — N sockets, per-client subscription sets.
- **Headless**: move gateway + session model into the daemon (above).
- **Write ops**: split/close/new-pane land as `POST /api/v1/panes/{id}/split`
  etc. once pane mutation is exposed outside `SessionView`.
- **Events**: the `ev` push channel is where richer telemetry (Claude
  waiting-on-you, PR dashboard state) goes — the web HUD mirrors of
  Ctrl+A `a` / Ctrl+A `p` are just new `ev` payloads + client panels.
- **Versioning**: breaking changes bump `/api/v1` → `/api/v2`; Miro's
  tolerant JSON means additive changes need nothing.
