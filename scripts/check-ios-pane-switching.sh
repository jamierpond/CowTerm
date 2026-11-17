#!/bin/sh
# Proves the phone moves between a session's panes on a real host, without
# touching anyone's panes: opens a throwaway session of two panes on the
# phone-test daemon instance on <user@host> (built there by cowterm-ios-ssh in
# ~/tmp/cowterm-ios-phone-test), opens the first on the phone, has the app move
# to the second as the dots or a swipe would, screenshots both, and kills both
# panes.
#
#   scripts/check-ios-pane-switching.sh <user@host> <screenshot-prefix> ["iPhone Air"]
set -eu
cd "$(dirname "$0")/.."
destination="$1"
prefix="$2"
device_name="${3:-iPhone Air}"
session="phone-two-panes-$$"
first_pane="$session-a"
second_pane="$session-b"
log="build-ios-sim/CowTermMobile.log"
seconds_for_the_first_pane=8
seconds_for_the_switch=8

COWTERM_BRIDGE_COMMAND='COWTERM_INSTANCE=phone-test exec $HOME/tmp/cowterm-ios-phone-test/bin/ct bridge'
export COWTERM_BRIDGE_COMMAND

cmake --build build --target CowTermEmbeddedSshProbe > /dev/null
probe="build/Tests/CowTermEmbeddedSshProbe"

device=$(scripts/find-ios-simulator.sh "$device_name")

# Booted before any SIMCTL_CHILD_ variable is set (see run-ios-against-host.sh).
if ! xcrun simctl list devices booted | grep -q "$device"; then
    xcrun simctl boot "$device"
fi

"$probe" "$destination" --leave-pane "$first_pane" "$session" | grep -v "^  "
"$probe" "$destination" --leave-pane "$second_pane" "$session" | grep -v "^  "
trap '"$probe" "$destination" --kill-pane "$first_pane" | grep -v "^  "; "$probe" "$destination" --kill-pane "$second_pane" | grep -v "^  "' EXIT

SIMCTL_CHILD_COWTERM_DESTINATION="$destination" \
SIMCTL_CHILD_COWTERM_PANE="$first_pane" \
SIMCTL_CHILD_COWTERM_BRIDGE_COMMAND="$COWTERM_BRIDGE_COMMAND" \
SIMCTL_CHILD_COWTERM_AGENT_SOCKET="${SSH_AUTH_SOCK:-}" \
SIMCTL_CHILD_COWTERM_KEY_FILE="${PHONE_KEY_FILE:-}" \
SIMCTL_CHILD_COWTERM_KNOWN_HOSTS="$HOME/.ssh/known_hosts" \
    scripts/run-ios-simulator.sh "$device_name" --switch-to-second-pane > /dev/null

sleep "$seconds_for_the_first_pane"
scripts/screenshot-ios-simulator.sh "$prefix-first.png" "$device_name" > /dev/null
sleep "$seconds_for_the_switch"
scripts/screenshot-ios-simulator.sh "$prefix-second.png" "$device_name" > /dev/null
grep "phone:" "$log"
echo "screenshots: $prefix-first.png $prefix-second.png"
