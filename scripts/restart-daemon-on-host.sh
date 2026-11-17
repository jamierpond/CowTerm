#!/bin/sh
# Restarts the CowTerm daemon on <user@host> so it runs the installed
# cowtermd. This ENDS every program running in that machine's panes (programs
# inside tmux survive, since tmux runs apart from the pane); the panes' screens
# come back with fresh shells when a client next opens them. Ask first.
#
# It asks the running daemon to save its panes and quit, waits for it to go,
# then dials with the installed ct, which starts the new daemon, and prints the
# protocol the new daemon answers.
#
#   scripts/restart-daemon-on-host.sh <user@host>
set -eu
destination="$1"
seconds_to_wait_for_the_old_daemon=30
bridge='ct="$HOME/.local/libexec/cowterm-remote/ct"; [ -x "$ct" ] || ct="$HOME/.local/bin/ct"; exec "$ct" bridge'

# One bridge run: sends the messages, prints the daemon's protocol.
talk() {
    python3 -I - "$destination" "$bridge" "$@" <<'PYTHON'
import os, select, subprocess, sys, time
destination, bridge_command, messages = sys.argv[1], sys.argv[2], sys.argv[3:]
bridge = subprocess.Popen(["ssh", "-o", "BatchMode=yes", destination, bridge_command],
                          stdin=subprocess.PIPE, stdout=subprocess.PIPE)
def send(message):
    data = message.encode()
    bridge.stdin.write(str(len(data)).encode() + b"\n" + data)
    bridge.stdin.flush()
buffer = b""
deadline = time.time() + 20
while b"bridge-hello" not in buffer and time.time() < deadline:
    if select.select([bridge.stdout], [], [], 0.2)[0]:
        buffer += os.read(bridge.stdout.fileno(), 1 << 20)
send("hello")
for message in messages:
    send(message)
deadline = time.time() + 5
while time.time() < deadline:
    if select.select([bridge.stdout], [], [], 0.2)[0]:
        chunk = os.read(bridge.stdout.fileno(), 1 << 20)
        if not chunk:
            break
        buffer += chunk
for line in buffer.split(b"\n"):
    if line.startswith(b"daemon-hello"):
        print(f"{destination}: daemon protocol {line.split()[1].decode()}")
bridge.stdin.close()
bridge.wait(5)
PYTHON
}

old_daemon=$(ssh -o BatchMode=yes "$destination" 'pgrep -x cowtermd | head -1' || true)
echo "$destination: old daemon pid ${old_daemon:-none}"
talk quit-server

waited=0
while ssh -o BatchMode=yes "$destination" "kill -0 $old_daemon 2> /dev/null"; do
    if [ "$waited" -ge "$seconds_to_wait_for_the_old_daemon" ]; then
        echo "$destination: the old daemon did not quit" >&2
        exit 1
    fi
    sleep 1
    waited=$((waited + 1))
done

talk
echo "$destination: new daemon pid $(ssh -o BatchMode=yes "$destination" 'pgrep -x cowtermd | head -1')"
