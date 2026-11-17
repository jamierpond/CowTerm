#!/bin/sh
# Rings Jamie's phone from a throwaway pane, never one of his: on <user@host>,
# the ct and cowtermd that scripts/build-headless-instance-on-host.sh built in
# <folder there> run as COWTERM_INSTANCE=push-check, with a config of their own
# that sends notices to the relay. The probe leaves one pane there in session
# push-check, and Claude Code's own hook command, fed the JSON Claude Code
# gives it, reports the pane working and then finished. The daemon posts to the
# relay, the relay to Apple, and the script prints the relay's log of it. Last,
# it kills the pane, stops the test daemon and removes its config.
#
#   scripts/check-push-on-phone.sh [user@host] [folder there] [relay]
set -eu
cd "$(dirname "$0")/.."
destination="${1:-jamiepond@macprorack}"
folder="${2:-\$HOME/tmp/cowterm-push-check}"
relay="${3:-$(scripts/find-push-relay-address.sh)}"
host_name="${destination#*@}"
instance="push-check"
pane="push-check-$$"
seconds_for_the_relay=8
finished_line="Push check at $(date +%H:%M): a throwaway pane on $host_name finished."

COWTERM_BRIDGE_COMMAND="COWTERM_INSTANCE=$instance exec $folder/bin/ct bridge"
export COWTERM_BRIDGE_COMMAND

cmake --build build --target CowTermEmbeddedSshProbe > /dev/null
probe="build/Tests/CowTermEmbeddedSshProbe"

clean_up() {
    "$probe" "$destination" --kill-pane "$pane" | grep -v "^  " || true
    "$probe" "$destination" --quit-server | grep -v "^  " || true
    ssh -o BatchMode=yes "$destination" "rm -f ~/.config/cowterm.$instance.json"
}
trap clean_up EXIT

ssh -o BatchMode=yes "$destination" \
    "printf '%s' '{\"pushRelay\":\"$relay\",\"pushHostName\":\"$host_name\"}' > ~/.config/cowterm.$instance.json"

"$probe" "$destination" --leave-pane "$pane" "$instance" | grep "left pane"

report_hook() {
    ssh -o BatchMode=yes "$destination" \
        "COWTERM_INSTANCE=$instance $folder/bin/ct pane-state --claude-hook --pane $pane" <<HOOK
$1
HOOK
}

report_hook '{"hook_event_name":"UserPromptSubmit","prompt":"check the push"}'
report_hook "{\"hook_event_name\":\"Stop\",\"last_assistant_message\":\"Checked.\\n$finished_line\"}"
echo "reported: finished, \"$finished_line\""

sleep "$seconds_for_the_relay"
ssh -o BatchMode=yes "$destination" \
    "${RELAY_LOG_COMMAND:-journalctl --user -u cowterm-push-relay --since '-1 min' --no-pager -o short-iso}"
