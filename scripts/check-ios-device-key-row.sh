#!/bin/sh
# Checks the key row on the phone: opens a throwaway pane on <user@host> (never
# one of yours) running `cat -v`, which prints every byte it receives, opens it
# on the phone, asks Jamie to press each key-row key, then prints what the pane
# received and kills it. Expected, in order: ^[ (Esc), a tab, ^[[A ^[[B ^[[C
# ^[[D (the arrows), ^A (Ctrl then a), abc, then one ^? for every delete the
# held delete key sent. The phone must be unlocked.
#
#   scripts/check-ios-device-key-row.sh [jamiepond@macprorack] [pond]
set -eu
cd "$(dirname "$0")/.."
destination="${1:-jamiepond@macprorack}"
host_name="${destination#*@}"
phone="${2:-pond}"
session="phone-keys-$$"
pane="$session-a"
bundle_id="audio.pond.cowterm.mobile"
seconds_for_the_phone_to_open_the_pane=40

cmake --build build --target CowTermEmbeddedSshProbe > /dev/null
probe="build/Tests/CowTermEmbeddedSshProbe"
"$probe" "$destination" --leave-pane "$pane" "$session" /tmp | grep "^left pane"
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

if ! xcrun devicectl device process launch --terminate-existing --device "$phone" "$bundle_id" \
    --open-session="$host_name/$session" > /dev/null 2>&1; then
    ct notify-user --title "CowTerm · key row check" \
        --content "I could not start CowTerm on $phone (it may be locked). Unlock it and answer, and I'll try again; nothing was sent to any pane." \
        --options "unlocked" > /dev/null
    xcrun devicectl device process launch --terminate-existing --device "$phone" "$bundle_id" \
        --open-session="$host_name/$session" > /dev/null
fi

if ! shown_instead="$(scripts/wait-for-ios-device-pane.sh "$phone" "$pane" "$seconds_for_the_phone_to_open_the_pane")"; then
    echo "the phone did not open the test pane; its log: ${shown_instead:-nothing opened}"
    ct notify-user --title "CowTerm · key row check" \
        --content "Stop: the phone did not open my test pane, so don't press anything; it would go to whatever pane is showing. Its log says: ${shown_instead:-nothing opened}." \
        --options "ok" > /dev/null
    exit 1
fi

ct notify-user --title "CowTerm · key row check" \
    --content "The phone's log confirms it now shows my test pane (a shell running cat -v, which echoes each key as text such as ^[ for Esc). Tap the keyboard button, then on the key row press: Esc, Tab, ↑, ↓, →, ←, then Ctrl and type a, then type abc and hold the keyboard's delete key for three seconds. Answer when done." \
    --options "done" > /dev/null

"$probe" "$destination" --adopt "$pane" | sed -n '/^screen of/,$p'
scripts/read-ios-device-log.sh "$phone" | grep "key row:\|typing: delete\|phone: pane .* opened" || true
