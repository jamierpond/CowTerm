#!/bin/sh
# Checks the key row on the phone: opens a throwaway pane on <user@host> (never
# one of yours) running `cat -v`, which prints every byte it receives, opens it
# on the phone, asks Jamie to press each key-row key, then prints what the pane
# received and kills it. Expected, in order: ^[ (Esc), a tab, ^[[A ^[[B ^[[C
# ^[[D (the arrows), ^A (Ctrl then a). The phone must be unlocked.
#
#   scripts/check-ios-device-key-row.sh [jamiepond@macprorack] [pond]
set -eu
cd "$(dirname "$0")/.."
destination="${1:-jamiepond@macprorack}"
phone="${2:-pond}"
session="phone-keys-$$"
pane="$session-a"
bundle_id="audio.pond.cowterm.mobile"
seconds_for_the_phone_to_open_the_pane=10

cmake --build build --target CowTermEmbeddedSshProbe > /dev/null
probe="build/Tests/CowTermEmbeddedSshProbe"
"$probe" "$destination" --leave-pane "$pane" "$session" | grep "^left pane"
trap '"$probe" "$destination" --kill-pane "$pane" | grep "^killed"' EXIT

# Types `cat -v` into the pane through the host's own ct bridge.
python3 -I - "$destination" "$pane" <<'PYTHON'
import os, select, subprocess, sys, time
destination, pane = sys.argv[1], sys.argv[2]
bridge_command = ('ct="$HOME/.local/libexec/cowterm-remote/ct"; '
                  '[ -x "$ct" ] || ct="$HOME/.local/bin/ct"; exec "$ct" bridge')
bridge = subprocess.Popen(["ssh", "-o", "BatchMode=yes", destination, bridge_command],
                          stdin=subprocess.PIPE, stdout=subprocess.PIPE)
def send(message):
    data = message.encode()
    bridge.stdin.write(str(len(data)).encode() + b"\n" + data)
    bridge.stdin.flush()
buffer = b""
deadline = time.time() + 20
while b"bridge-hello" not in buffer and time.time() < deadline:
    if select.select([bridge.stdout], [], [], 0.5)[0]:
        buffer += os.read(bridge.stdout.fileno(), 1 << 20)
send(f"write {pane}\nclear; command cat -v\r")
time.sleep(1)
bridge.stdin.close()
bridge.wait(5)
PYTHON

xcrun devicectl device process launch --terminate-existing --device "$phone" "$bundle_id" \
    --open-session "$destination/$session" > /dev/null
sleep "$seconds_for_the_phone_to_open_the_pane"

ct notify-user --title "CowTerm · key row check" \
    --content "pond now shows a test pane running cat -v. Tap the keyboard button, then on the key row press: Esc, Tab, ↑, ↓, →, ←, then Ctrl and type a. Answer when done." \
    --options "done" > /dev/null

"$probe" "$destination" --adopt "$pane" | sed -n '/^screen of/,$p'
scripts/read-ios-device-log.sh "$phone" | grep "key row:" || true
