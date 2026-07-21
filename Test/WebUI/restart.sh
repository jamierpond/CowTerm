#!/bin/zsh
# Brings the isolated test instance up from scratch and waits until it serves.
#
# Kills the daemon as well as the GUI, which is easy to forget: the daemon
# outlives the app on purpose, so a restart that leaves it running restores
# stale panes whose shells have already exited — input then goes nowhere and
# every test fails as though the app had regressed.
#
# Only ever touches COWTERM_INSTANCE=test processes, never the real CowTerm.
#
#   restart.sh            keep the test instance's saved sessions
#   restart.sh --wipe     start from an empty session store

set -u

here=${0:a:h}
root=${here:h:h}
port=3697
app="$root/build/Terminal/CowTerm.app/Contents/MacOS/CowTerm"

cd "$root"

kill $(cat "$here/test.pid" 2>/dev/null) 2>/dev/null

ps -Ao pid,command | grep -E 'CowTerm(Daemon)?$' | grep -v grep | awk '{print $1}' |
    while read pid; do
        ps -Eo pid,command -p "$pid" 2>/dev/null | grep -q 'COWTERM_INSTANCE=test' &&
            kill -9 "$pid" 2>/dev/null
    done

for pid in $(lsof -nP -iTCP:$port -iTCP:$((port + 1)) -sTCP:LISTEN -t 2>/dev/null | sort -u); do
    kill -9 "$pid" 2>/dev/null
done

[ "${1:-}" = "--wipe" ] && rm -rf "$HOME/Library/Application Support/Tamber/cowterm-test"

sleep 2

COWTERM_INSTANCE=test "$app" > "$here/test-app.log" 2>&1 &
echo $! > "$here/test.pid"

# Serving is not enough: wait for a session, or the drivers find no pane.
for i in $(seq 1 30); do
    sleep 1
    if curl -s --max-time 2 "http://127.0.0.1:$port/api/v1/sessions" 2>/dev/null |
        grep -q '"panes"'; then
        echo "test instance up on :$port after ${i}s"
        exit 0
    fi
done

echo "test instance failed to come up" >&2
tail -5 "$here/test-app.log" >&2
exit 1
