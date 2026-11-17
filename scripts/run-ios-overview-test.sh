#!/bin/sh
# Shows the phone's overview with real rows from a test daemon, never Jamie's:
# on <user@host>, the ct and cowtermd that scripts/build-headless-instance-on-host.sh
# built in <folder there> run as COWTERM_INSTANCE=phone-test. The probe leaves
# two throwaway panes there, each its own session, and `ct pane-state` marks the
# first one waiting. CowTermMobile opens on its overview, which must list both
# sessions with the waiting one first; the script saves a screenshot, then kills
# both panes and stops the test daemon.
# Build the app first with scripts/build-ios-simulator.sh (EACP_SOURCE as it says).
#
#   scripts/run-ios-overview-test.sh <user@host> <folder there> <screenshot.png> ["iPhone 17 Pro"]
set -eu
cd "$(dirname "$0")/.."
destination="$1"
folder="$2"
screenshot="$3"
device_name="${4:-iPhone 17 Pro}"
waiting_pane="overview-waiting-$$"
idle_pane="overview-idle-$$"
waiting_question="Run the full test suite now?"
app_log="build-ios-sim/CowTermMobile.log"
bundle_id="audio.pond.cowterm.mobile"
wait_seconds=60
seconds_for_the_overview_to_draw=2

COWTERM_BRIDGE_COMMAND="COWTERM_INSTANCE=phone-test exec $folder/bin/ct bridge"
export COWTERM_BRIDGE_COMMAND

cmake --build build --target CowTermEmbeddedSshProbe > /dev/null
probe="build/Tests/CowTermEmbeddedSshProbe"

device=$(scripts/find-ios-simulator.sh "$device_name")

# Booted before any SIMCTL_CHILD_ variable is set (see run-ios-against-host.sh).
if ! xcrun simctl list devices booted | grep -q "$device"; then
    xcrun simctl boot "$device"
fi

clean_up() {
    xcrun simctl terminate "$device" "$bundle_id" 2> /dev/null || true
    "$probe" "$destination" --kill-pane "$waiting_pane" | grep -v "^  "
    "$probe" "$destination" --kill-pane "$idle_pane" | grep -v "^  "
    "$probe" "$destination" --quit-server | grep -v "^  "
}
trap clean_up EXIT

"$probe" "$destination" --leave-pane "$idle_pane" | grep "left pane"
"$probe" "$destination" --leave-pane "$waiting_pane" | grep "left pane"
ssh -o BatchMode=yes "$destination" \
    "COWTERM_INSTANCE=phone-test $folder/bin/ct pane-state waiting --pane $waiting_pane --message '$waiting_question'"

SIMCTL_CHILD_COWTERM_DESTINATION="$destination" \
SIMCTL_CHILD_COWTERM_BRIDGE_COMMAND="$COWTERM_BRIDGE_COMMAND" \
SIMCTL_CHILD_COWTERM_AGENT_SOCKET="${SSH_AUTH_SOCK:-}" \
SIMCTL_CHILD_COWTERM_KEY_FILE="${PHONE_KEY_FILE:-}" \
SIMCTL_CHILD_COWTERM_KNOWN_HOSTS="$HOME/.ssh/known_hosts" \
    scripts/run-ios-simulator.sh "$device_name" --overview > /dev/null

waited=0
until grep -q "overview: 2 sessions" "$app_log"; do
    if [ "$waited" -ge "$wait_seconds" ]; then
        echo "the overview never listed both sessions:" >&2
        grep "overview:\|phone:" "$app_log" >&2 || true
        exit 1
    fi
    sleep 1
    waited=$((waited + 1))
done

sleep "$seconds_for_the_overview_to_draw"
mkdir -p "$(dirname "$screenshot")"
scripts/screenshot-ios-simulator.sh "$screenshot" "$device_name" > /dev/null
echo "screenshot: $screenshot"
grep "overview:" "$app_log" | head -5
