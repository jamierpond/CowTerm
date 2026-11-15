# Remote sessions

## The problem

Jamie works across several machines, but the project picker (Ctrl+A F) and
the session switcher only know the Mac CowTerm runs on. Opening a project on
another box meant an ssh pane whose shell died with the laptop lid.

## The fix

CowTerm opens projects on any machine you can ssh to. The shells live in a
`cowtermd` on that machine, so closing the laptop, quitting CowTerm or losing
the network never kills them: the next CowTerm to connect picks them up where
they were, like tmux, without tmux.

## Behaviour

### Config

`searchDirs` in `~/.config/cowterm.json` takes remote entries beside local
ones:

```json
{
    "searchDirs": ["~/projects", "tamby:~/projects", "macprorack:~/projects"],
    "hosts": { "tamby": { "color": "#eb6f92" } }
}
```

Use bare ssh aliases (`tamby:~/projects`): the user, real host name, keys
and ProxyJump live in `~/.ssh/config`, not in `cowterm.json` (which may sit
in a public dotfiles repo). The host part goes to the system `ssh` exactly as
written. Key auth only: CowTerm runs ssh with
`BatchMode=yes`, never asks for a passphrase, and shows an auth failure as an
error.

Every host has a colour, worked out from its name so it is the same on every
machine; `hosts.<name>.color` overrides it. The colour marks the host label in
the picker and switcher, and tints the pane border thinly.

### Picker and switcher

- One list across all machines. A remote row reads `Grazer · tamby`, the host
  dim and in its colour.
- Local rows appear at once. Each host's rows arrive when its scan returns;
  the last good scan is cached and shown straight away on the next open.
- An unreachable host is one grey row: `tamby — unreachable (<reason>)`.
- A remote session is named `<host>/<project>`. Most-recently-used ordering
  works across hosts.
- The switcher lists sessions from every reachable daemon, with the host
  label, including sessions another Mac started.

### Transport

`ssh <host> ct bridge` connects to the remote `cowtermd` (starting it if
needed) and relays the same Messenger messages the local daemon speaks over
stdin and stdout. Sessions are created and live on the remote daemon; the
Mac only draws them. One engine everywhere.

The bridge opens with a handshake carrying a protocol version. A missing
`ct`/`cowtermd` or a version mismatch shows an error in the pane naming the
command that fixes it (`ct install-remote <host>`).

### Staying alive

- Quitting CowTerm, sleep and network loss leave remote shells running.
- Reconnecting re-attaches and redraws, as after a local daemon restart.
- While the link is down, affected panes show a "reconnecting…" banner and
  CowTerm retries with backoff. Typing is blocked, not queued.
- Another Mac with the same config sees and attaches to the same sessions.

### Inside a remote pane

`$COWTERM_PANE` is set. `ct notify-user` and `notify` travel back over the
bridge to the CowTerm window being looked at.

### Installing and updating remotes

A remote needs only `ct` and `cowtermd`, built headless (no GUI).

- `scripts/install-remote.sh <host>`, or `ct install-remote <host>`: clones or
  pulls CowTerm at `~/projects/CowTerm` on the host, builds only the headless
  targets, and installs `ct` and `cowtermd` to `~/.local/bin`.
- `ct update-remotes` does that for every host in the config, in parallel,
  and reports each.
- Updating keeps running remote sessions. If the daemon has to restart, panes
  come back through the same restore path a local daemon restart uses.

macOS and Linux remotes are supported.
