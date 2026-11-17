#!/bin/sh
# Checks that what is typed into a pane on this Mac shows on the phone as it
# happens. Opens a throwaway pane in this Mac's daemon (through a local
# `ct bridge`, never one of your panes), opens it on the phone, types an echo
# into it from the Mac every few seconds, and prints how long each took to
# reach the phone, from the phone's own log. Kills the pane at the end. The
# phone must be unlocked; the app is relaunched on the throwaway pane.
#
#   scripts/check-ios-device-live-output.sh [pond]
set -eu
cd "$(dirname "$0")/.."
phone="${1:-pond}"
session="phone-live-$$"
host="jamiepond@big-cow"
bundle_id="audio.pond.cowterm.mobile"
seconds_for_the_phone_to_open_the_pane=10
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

python3 -I - "$session" create <<'PYTHON'
import os, select, subprocess, sys, time
session, action = sys.argv[1], sys.argv[2]
bridge = subprocess.Popen([os.path.expanduser("~/.local/bin/ct"), "bridge"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
def send(message):
    data = message.encode()
    bridge.stdin.write(str(len(data)).encode() + b"\n" + data)
    bridge.stdin.flush()
buffer = b""
deadline = time.time() + 20
while time.time() < deadline:
    if not select.select([bridge.stdout], [], [], 0.5)[0]:
        continue
    buffer += os.read(bridge.stdout.fileno(), 1 << 20)
    if b"bridge-hello" in buffer:
        send(f"spawn {session} 80 24 {session}\n/tmp")
        time.sleep(1)
        send(f"release {session}")
        bridge.stdin.close()
        bridge.wait(5)
        print(f"opened throwaway pane {session} on this Mac")
        sys.exit(0)
sys.exit("ct bridge did not answer")
PYTHON

xcrun devicectl device process launch --terminate-existing --device "$phone" "$bundle_id" \
    --open-session "$host/$session" > /dev/null
sleep "$seconds_for_the_phone_to_open_the_pane"

python3 -I - "$session" "$work/sent" <<'PYTHON'
import os, select, subprocess, sys, time
session, sent_path = sys.argv[1], sys.argv[2]
writes, seconds_between_writes = 5, 3
bridge = subprocess.Popen([os.path.expanduser("~/.local/bin/ct"), "bridge"], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
def send(message):
    data = message.encode()
    bridge.stdin.write(str(len(data)).encode() + b"\n" + data)
    bridge.stdin.flush()
buffer = b""
while b"bridge-hello" not in buffer:
    select.select([bridge.stdout], [], [], 0.5)
    buffer += os.read(bridge.stdout.fileno(), 1 << 20)
with open(sent_path, "w") as sent:
    for number in range(writes):
        sent.write(f"{time.strftime('%H:%M:%S')}.{int(time.time() * 1000) % 1000:03d}\n")
        sent.flush()
        send(f"write {session}\necho typed-on-the-mac-{number}\r")
        time.sleep(seconds_between_writes)
send(f"kill {session}")
bridge.stdin.close()
bridge.wait(5)
PYTHON

scripts/read-ios-device-log.sh "$phone" | grep "phone: output from pane $session" | sed 's/ |.*//' > "$work/arrived"
echo "typed on the Mac   arrived on the phone"
paste "$work/sent" "$work/arrived" | while read -r typed arrived; do
    python3 -I -c '
import sys
def seconds(text):
    hours, minutes, rest = text.split(":")
    return int(hours) * 3600 + int(minutes) * 60 + float(rest)
typed, arrived = sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else ""
delay = "never"
shown = "-"
if arrived:
    delay = "%.0f ms" % ((seconds(arrived) - seconds(typed)) * 1000)
    shown = arrived
print(typed, "      ", shown, "  ", delay)' "$typed" "${arrived:-}"
done
echo "killed throwaway pane $session"
