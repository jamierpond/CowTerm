#!/bin/sh
# Shows a real pane on the phone: opens a throwaway pane on <user@host> running
# `seq 1 300` (with the probe tool, never one of your panes, because the phone
# takes the size of what it opens), launches CowTermMobile in a headless
# simulator on that pane through EmbeddedSshLink, waits for the link to come up,
# saves a screenshot, sends the app to the background and back (it stores the
# screen, then checks the link), relaunches it to check it loads the screen it
# stored, and kills the throwaway pane.
# The app signs in with $SSH_AUTH_SOCK's agent (the simulator reaches the Mac's
# unix sockets), or with PHONE_KEY_FILE, and checks ~/.ssh/known_hosts.
# Build the app first with scripts/build-ios-simulator.sh.
#
#   scripts/run-ios-against-host.sh <user@host> <screenshot.png> ["iPhone 17 Pro"]
# It defaults to iPhone 17 Pro, so it never replaces the app another run has
# open on the iPhone 17e the other simulator scripts default to.
set -eu
cd "$(dirname "$0")/.."
destination="$1"
screenshot="$2"
device_name="${3:-iPhone 17 Pro}"
pane="phone-test-$$"
log="build-ios-sim/CowTermMobile.log"
bundle_id="audio.pond.cowterm.mobile"
wait_seconds=60
stored_screen_interval_seconds=12

cmake --build build --target CowTermEmbeddedSshProbe > /dev/null
probe="build/Tests/CowTermEmbeddedSshProbe"
"$probe" "$destination" --leave-pane "$pane" | grep -v "^  "

device=$(scripts/find-ios-simulator.sh "$device_name")

# Booted here, before any SIMCTL_CHILD_ variable is set: a simulator booted
# with them keeps those values for every later launch, whatever the launch sets.
if ! xcrun simctl list devices booted | grep -q "$device"; then
    xcrun simctl boot "$device"
fi
trap 'xcrun simctl terminate "$device" "$bundle_id" 2> /dev/null || true; "$probe" "$destination" --kill-pane "$pane" | grep -v "^  "' EXIT

launch_app() {
    SIMCTL_CHILD_COWTERM_DESTINATION="$destination" \
    SIMCTL_CHILD_COWTERM_PANE="$pane" \
    SIMCTL_CHILD_COWTERM_AGENT_SOCKET="${SSH_AUTH_SOCK:-}" \
    SIMCTL_CHILD_COWTERM_KEY_FILE="${PHONE_KEY_FILE:-}" \
    SIMCTL_CHILD_COWTERM_KNOWN_HOSTS="$HOME/.ssh/known_hosts" \
        scripts/run-ios-simulator.sh "$device_name" > /dev/null
}

wait_for_log_line() {
    waited=0
    until grep -q "$1" "$log"; do
        if [ "$waited" -ge "$wait_seconds" ]; then
            echo "no '$1' in $log after $wait_seconds s:" >&2
            grep "phone:" "$log" >&2 || true
            exit 1
        fi
        sleep 1
        waited=$((waited + 1))
    done
}

launch_app
wait_for_log_line "phone: link live"
sleep "$stored_screen_interval_seconds"
mkdir -p "$(dirname "$screenshot")"
scripts/screenshot-ios-simulator.sh "$screenshot" "$device_name" > /dev/null
grep "phone:" "$log"
echo "screenshot: $screenshot"

echo "to the background and back:"
scripts/check-ios-lifecycle.sh "$device_name" > /dev/null
sleep "$stored_screen_interval_seconds"
grep "phone: storing\|phone: checking\|phone: link" "$log"

echo "relaunching to load the stored screen:"
xcrun simctl terminate "$device" "$bundle_id"
launch_app
wait_for_log_line "phone: stored screen of"
grep "phone: stored screen" "$log"
