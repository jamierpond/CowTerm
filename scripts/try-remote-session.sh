#!/bin/sh
# Opens a remote session in an isolated CowTerm (COWTERM_INSTANCE=remote-try,
# its own config and session store) without typing anything: seeds one
# session whose shell lives on <alias>, launches the app, then checks on the
# remote that the shell runs there and screenshots the window.
# usage: scripts/try-remote-session.sh <ssh alias> [app path]
set -e

alias_name="$1"
app="${2:-build-remote/Terminal/CowTerm.app}"
instance="remote-try"
shell_id="remotetry$(date +%s)"
store="$HOME/Library/Application Support/pond/CowTerm/sessions-$instance"
config="$HOME/.config/cowterm.$instance.json"

pkill -f "COWTERM_INSTANCE=$instance" 2>/dev/null || true
mkdir -p "$store"
cat > "$config" <<JSON
{ "searchDirs": ["~/projects", "$alias_name:~/projects"] }
JSON
cat > "$store/sessions.json" <<JSON
{ "activeIndex": 0, "sessions": [ { "name": "$alias_name/tmp", "projectDir": "$alias_name:/tmp", "cwd": "/tmp",
  "panes": [ { "cwd": "/tmp", "first": -1, "second": -1, "horizontal": false, "ratio": 0.5, "split": false, "shellId": "$shell_id" } ] } ] }
JSON

open -n --env COWTERM_INSTANCE="$instance" "$app"
sleep 8

if ssh -o BatchMode=yes "$alias_name" "pgrep -f cowtermd >/dev/null"; then
    echo "ok: cowtermd runs on $alias_name"
else
    echo "FAILED: no cowtermd on $alias_name"
fi

# The window is titled after its session; Jamie's own CowTerm windows aren't.
screencap --window "$alias_name/tmp" --out "/tmp/cowterm-remote-try.png" || true
