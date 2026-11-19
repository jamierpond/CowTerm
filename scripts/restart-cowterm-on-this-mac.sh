#!/bin/sh
# Restarts CowTerm on this Mac so the app and its daemon run the installed
# build. This ENDS every program running directly in a CowTerm pane on this
# Mac (programs inside tmux survive); the sessions come back with their screens
# and fresh shells. Ask first.
#
# It runs detached from every pane (a process that starts a new session, so
# ending the panes cannot end it) and logs to ~/Library/Logs/cowterm-restart.log:
#   1. stops the running CowTerm app, whose session layout is already saved
#      (emberstore, atomic), before the daemon goes, so no pane is seen exiting;
#   2. asks the daemon to save its panes and quit, and waits for it to go;
#   3. opens the installed CowTerm app, which starts the installed daemon and
#      reopens the sessions.
#
#   scripts/restart-cowterm-on-this-mac.sh
#
# With --app-only it restarts just the app: the daemon and every program in
# every pane keep running, and the app comes back on the installed build.
#
#   scripts/restart-cowterm-on-this-mac.sh --app-only
set -eu
app_only=no

for argument in "$@"; do
    if [ "$argument" = "--app-only" ]; then
        app_only=yes
    fi
done

app="$HOME/.local/libexec/cowterm/CowTerm.app"
log="$HOME/Library/Logs/cowterm-restart.log"
seconds_to_wait=30

if [ "${COWTERM_RESTART_IS_DETACHED:-}" != "yes" ]; then
    python3 -I -c '
import os, sys
if os.fork():
    sys.exit(0)
os.setsid()
if os.fork():
    sys.exit(0)
log = os.open(sys.argv[2], os.O_WRONLY | os.O_CREAT | os.O_APPEND, 0o644)
os.dup2(log, 1)
os.dup2(log, 2)
os.dup2(os.open(os.devnull, os.O_RDONLY), 0)
os.environ["COWTERM_RESTART_IS_DETACHED"] = "yes"
os.execv("/bin/sh", ["/bin/sh", sys.argv[1]] + sys.argv[3:])
' "$0" "$log" "$@"
    echo "restarting CowTerm detached; log: $log"
    exit 0
fi

say_when() { echo "$(date '+%H:%M:%S') $*"; }

wait_for_exit() {
    waited=0
    while kill -0 "$1" 2> /dev/null; do
        if [ "$waited" -ge "$seconds_to_wait" ]; then
            say_when "$2 (pid $1) did not exit"
            exit 1
        fi
        sleep 1
        waited=$((waited + 1))
    done
}

app_pid=$(pgrep -x CowTerm | head -1 || true)
daemon_pid=$(pgrep -x CowTermDaemon | head -1 || true)
say_when "app pid ${app_pid:-none}, daemon pid ${daemon_pid:-none}"

if [ -n "$app_pid" ]; then
    kill -TERM "$app_pid"
    wait_for_exit "$app_pid" "the app"
    say_when "the app has quit"
fi

if [ -n "$daemon_pid" ] && [ "$app_only" = no ]; then
    python3 -I - "$app/Contents/MacOS/CowTerm" <<'PYTHON'
import os, select, subprocess, sys, time
bridge = subprocess.Popen([sys.argv[1], "bridge"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          executable=sys.argv[1], env=dict(os.environ))
buffer = b""
deadline = time.time() + 20
while b"bridge-hello" not in buffer and time.time() < deadline:
    if select.select([bridge.stdout], [], [], 0.2)[0]:
        buffer += os.read(bridge.stdout.fileno(), 1 << 20)
message = b"quit-server"
bridge.stdin.write(str(len(message)).encode() + b"\n" + message)
bridge.stdin.flush()
time.sleep(1)
bridge.stdin.close()
bridge.wait(5)
PYTHON
    wait_for_exit "$daemon_pid" "the daemon"
    say_when "the daemon saved its panes and quit"
fi

if [ "$app_only" = yes ]; then
    # Without -n, open sees the running daemon (same bundle) as the app and does nothing.
    "$(dirname "$0")/open-cowterm-cleanly.sh" "$app" -n
else
    "$(dirname "$0")/open-cowterm-cleanly.sh" "$app"
fi

# Names only, never values: a token must not reach the log.
find_inherited_session_variables() {
    ps eww -o command= -p "$1" | tr ' ' '\n' | grep -E '^(CLAUDE[A-Z_]*|CLAUDECODE|COWTERM_PANE)=' | cut -d= -f1 | tr '\n' ' '
}

waited=0
while ! pgrep -x CowTermDaemon > /dev/null; do
    if [ "$waited" -ge "$seconds_to_wait" ]; then
        say_when "FAILED: the daemon did not start"
        exit 1
    fi
    sleep 1
    waited=$((waited + 1))
done

waited=0
while ! pgrep -x CowTerm > /dev/null; do
    if [ "$waited" -ge "$seconds_to_wait" ]; then
        say_when "FAILED: the app did not start"
        exit 1
    fi
    sleep 1
    waited=$((waited + 1))
done

new_app_pid=$(pgrep -x CowTerm | head -1)
new_daemon_pid=$(pgrep -x CowTermDaemon | head -1)
inherited="$(find_inherited_session_variables "$new_app_pid")$(find_inherited_session_variables "$new_daemon_pid")"

if [ -n "$inherited" ]; then
    say_when "FAILED: the new app or daemon inherited $inherited; every pane's shell would start with them"
    exit 1
fi

say_when "opened $app: app pid $new_app_pid, daemon pid $new_daemon_pid, no CLAUDE* or COWTERM_PANE in either environment"
