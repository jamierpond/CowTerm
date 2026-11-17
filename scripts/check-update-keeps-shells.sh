#!/bin/sh
# Shows what updating CowTerm does to running shells, on a separate daemon
# instance (COWTERM_INSTANCE=update-test) on <user@host>, never on yours. It
# starts the installed (old) ct's daemon there with a throwaway pane running
# `sleep 86401`, then:
#   1. dials it with the new ct from <new folder>/bin (scripts/build-headless-
#      instance-on-host.sh builds it) and checks the same daemon and the same
#      sleep process are still there;
#   2. dials it with the old ct again;
#   3. restarts the daemon as an update would (quit-server, then the new ct
#      starts the new daemon) and checks whether the sleep process survived.
# It quits the test daemon at the end.
#
#   scripts/check-update-keeps-shells.sh <user@host> <new folder there>
set -eu
cd "$(dirname "$0")/.."
destination="$1"
new_folder="$2"
pane="update-test-sleeper"
marker_command="sleep 86401"
old_ct='ct="$HOME/.local/libexec/cowterm-remote/ct"; [ -x "$ct" ] || ct="$HOME/.local/bin/ct"; COWTERM_INSTANCE=update-test exec "$ct" bridge'
new_ct="COWTERM_INSTANCE=update-test exec $new_folder/bin/ct bridge"

# Sends messages through one bridge run and prints the daemon's hello and the
# panes it lists.
talk() {
    bridge_command="$1"
    shift
    python3 -I - "$destination" "$bridge_command" "$@" <<'PYTHON'
import os, select, subprocess, sys, time
destination, bridge_command, messages = sys.argv[1], sys.argv[2], sys.argv[3:]
bridge = subprocess.Popen(["ssh", "-o", "BatchMode=yes", destination, bridge_command],
                          stdin=subprocess.PIPE, stdout=subprocess.PIPE)
def send(message):
    data = message.encode()
    bridge.stdin.write(str(len(data)).encode() + b"\n" + data)
    bridge.stdin.flush()
buffer = b""
def read_messages(seconds):
    global buffer
    deadline = time.time() + seconds
    found = []
    while time.time() < deadline:
        if not select.select([bridge.stdout], [], [], 0.2)[0]:
            continue
        chunk = os.read(bridge.stdout.fileno(), 1 << 20)
        if not chunk:
            break
        buffer += chunk
        while b"\n" in buffer:
            line, rest = buffer.split(b"\n", 1)
            length = int(line)
            if len(rest) < length:
                break
            found.append(rest[:length])
            buffer = rest[length:]
    return found
for message in read_messages(5):
    if message.startswith(b"bridge-hello"):
        print("  bridge:", message.split(b"\n")[0].decode())
send("hello 5 update-test")
for message in messages:
    send(message.replace("\\n", "\n"))
send("info")
for message in read_messages(4):
    verb, _, payload = message.partition(b"\n")
    if verb.startswith(b"daemon-hello"):
        print("  daemon says protocol", verb.split()[1].decode())
    if verb.startswith(b"info"):
        print("  panes:", [line.split(b"\t")[0].decode() for line in payload.split(b"\n") if line])
bridge.stdin.close()
bridge.wait(5)
PYTHON
}

describe_sleeper() {
    ssh -o BatchMode=yes "$destination" \
        "pid=\$(pgrep -fx '$marker_command' | head -1); if [ -n \"\$pid\" ]; then echo \"  $marker_command: pid \$pid, daemon pid \$(ps -o ppid= -p \$pid | tr -d ' ')\"; else echo '  $marker_command: not running'; fi"
}

echo "old daemon with a throwaway pane:"
talk "$old_ct" "spawn $pane 80 24 update-test\\n/tmp\\n$marker_command" "release $pane"
describe_sleeper

echo "1. the new ct dials the old daemon:"
talk "$new_ct"
describe_sleeper

echo "2. the old ct dials it again:"
talk "$old_ct"
describe_sleeper

echo "3. the daemon restarts as an update would:"
talk "$old_ct" "quit-server"
sleep 2
talk "$new_ct"
describe_sleeper

talk "$new_ct" "kill $pane" "quit-server" > /dev/null
echo "test daemon quit"
