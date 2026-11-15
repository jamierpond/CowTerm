#!/bin/sh
# Opens a remote session in an isolated CowTerm (COWTERM_INSTANCE=remote-try,
# its own config and session store) without typing anything: seeds one
# session whose shell lives on <alias>, launches the app, then checks on the
# remote that the shell runs there and screenshots the window.
# With --notify-user, also runs ct notify-user in that pane's environment on
# the remote and checks that its prompt window opens here; the ct is then
# stopped, which closes the prompt again.
# REMOTE_DIRECTORY picks the pane's directory there (default /tmp).
# usage: scripts/try-remote-session.sh <ssh alias> [--notify-user]
set -e

alias_name="$1"
app="build-remote/Terminal/CowTerm.app"
check_notify_user="${2:-}"
instance="remote-try"
remote_directory="${REMOTE_DIRECTORY:-/tmp}"
shell_id="remotetry$(date +%s)"
store="$HOME/Library/Application Support/pond/CowTerm/sessions-$instance"
config="$HOME/.config/cowterm.$instance.json"

pkill -f "COWTERM_INSTANCE=$instance" 2>/dev/null || true
mkdir -p "$store"
cat > "$config" <<JSON
{ "searchDirs": ["~/projects", "$alias_name:~/projects"] }
JSON
cat > "$store/sessions.json" <<JSON
{ "activeIndex": 0, "sessions": [ { "name": "$alias_name/try", "projectDir": "$alias_name:$remote_directory", "cwd": "$remote_directory",
  "panes": [ { "cwd": "$remote_directory", "first": -1, "second": -1, "horizontal": false, "ratio": 0.5, "split": false, "shellId": "$shell_id" } ] } ] }
JSON

open -n --env COWTERM_INSTANCE="$instance" "$app"
sleep 8

echo "look at the screenshot: the pane should show $alias_name's prompt in $remote_directory"

# The window is titled after its session; Jamie's own CowTerm windows aren't.
screencap --window "$alias_name/try" --out "/tmp/cowterm-remote-try.png" || true

if [ "$check_notify_user" = "--notify-user" ]; then
    ssh -o BatchMode=yes "$alias_name" "COWTERM_PANE=$shell_id timeout 6 \$HOME/.local/bin/ct notify-user --title try-remote-session --content 'remote notify-user check' --options ok" &
    sleep 3
    if screencap --list | grep -q "CowTerm · try-remote-session"; then
        echo "ok: notify-user from $alias_name opened a prompt here"
    else
        echo "FAILED: no notify-user prompt from $alias_name"
    fi
    wait
fi
