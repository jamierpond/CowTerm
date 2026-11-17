#!/bin/sh
# Checks that the phone gives a protocol 4 daemon's panes their size back, on
# two throwaway panes in one session, never on yours. Both open at the probe's
# 80 x 24. The simulator opens the first (the phone takes its own size), moves
# to the second as the dots would (the first must be 80 x 24 again), goes to
# the background (the second must be 80 x 24 again) and comes back (the phone's
# size again). The probe reads each size from the daemon's state, then kills
# both panes.
#
#   scripts/check-ios-size-return.sh <user@host> ["iPhone 17 Pro"]
set -eu
cd "$(dirname "$0")/.."
destination="$1"
device_name="${2:-iPhone 17 Pro}"
session="phone-size-return-$$"
first_pane="$session-a"
second_pane="$session-b"
log="build-ios-sim/CowTermMobile.log"
bundle_id="audio.pond.cowterm.mobile"
seconds_until_the_first_pane_shows=6
seconds_until_the_app_moved_to_the_second_pane=10
seconds_to_settle=5

cmake --build build --target CowTermEmbeddedSshProbe > /dev/null
probe="build/Tests/CowTermEmbeddedSshProbe"
"$probe" "$destination" --leave-pane "$first_pane" "$session" | grep -v "^  "
"$probe" "$destination" --leave-pane "$second_pane" "$session" | grep -v "^  "

device=$(scripts/find-ios-simulator.sh "$device_name")

# Booted before any SIMCTL_CHILD_ variable is set (see run-ios-against-host.sh).
if ! xcrun simctl list devices booted | grep -q "$device"; then
    xcrun simctl boot "$device"
fi
trap 'xcrun simctl terminate "$device" "$bundle_id" 2> /dev/null || true; for pane in "$first_pane" "$second_pane"; do "$probe" "$destination" --kill-pane "$pane" | grep -v "^  "; done' EXIT

print_size() {
    echo "$1: $("$probe" "$destination" --adopt "$2" | grep "^pane $2 is")"
}

print_size "before the phone" "$first_pane"
SIMCTL_CHILD_COWTERM_DESTINATION="$destination" \
SIMCTL_CHILD_COWTERM_PANE="$first_pane" \
SIMCTL_CHILD_COWTERM_AGENT_SOCKET="${SSH_AUTH_SOCK:-}" \
SIMCTL_CHILD_COWTERM_KNOWN_HOSTS="$HOME/.ssh/known_hosts" \
    scripts/run-ios-simulator.sh "$device_name" --portrait --switch-to-second-pane > /dev/null
sleep "$seconds_until_the_first_pane_shows"
print_size "phone showing it" "$first_pane"
sleep "$seconds_until_the_app_moved_to_the_second_pane"
print_size "phone moved to the second pane" "$first_pane"
print_size "phone showing the second pane" "$second_pane"

xcrun simctl launch "$device" com.apple.Preferences > /dev/null
sleep "$seconds_to_settle"
print_size "phone in the background" "$second_pane"

xcrun simctl launch "$device" "$bundle_id" > /dev/null
sleep "$seconds_to_settle"
print_size "phone back in the foreground" "$second_pane"
grep "phone: pane\|phone: gave\|phone: .*link\|lifecycle:" "$log" || true
