#!/bin/sh
# Shows the unread mark on the phone against a test daemon, never Jamie's: on
# <user@host>, the ct and cowtermd that scripts/build-headless-instance-on-host.sh
# built in <folder there> run as COWTERM_INSTANCE=unread-test. The probe leaves
# two throwaway panes there, each its own session, and `ct pane-state finished`
# reports a Claude finishing in the second, which makes it unread. The app opens
# on its overview, which must list the unread session first, and the script
# saves <prefix>-list.png. Then it relaunches the app on that session, as a tap
# does, saves <prefix>-pane.png, and checks the daemon now has the pane read,
# because the phone sent `viewing`. Last, it kills both panes and stops the
# test daemon. Build the app first with scripts/build-ios-simulator.sh.
#
#   scripts/check-ios-unread.sh <user@host> <folder there> <prefix> ["iPhone 17 Pro"]
set -eu
cd "$(dirname "$0")/.."
destination="$1"
host_name="${destination#*@}"
folder="$2"
prefix="$3"
device_name="${4:-iPhone 17 Pro}"
idle_pane="unread-idle-$$"
unread_pane="unread-finished-$$"
claude_line="All tests pass."
app_log="build-ios-sim/CowTermMobile.log"
bundle_id="audio.pond.cowterm.mobile"
wait_seconds=60
seconds_for_the_overview_to_draw=4
seconds_for_the_pane_to_draw=4

COWTERM_BRIDGE_COMMAND="COWTERM_INSTANCE=unread-test exec $folder/bin/ct bridge"
export COWTERM_BRIDGE_COMMAND

cmake --build build --target CowTermEmbeddedSshProbe > /dev/null
probe="build/Tests/CowTermEmbeddedSshProbe"

device=$(scripts/find-ios-simulator.sh "$device_name")

# Booted before any SIMCTL_CHILD_ variable is set (see run-ios-against-host.sh);
# bootstatus -b boots it only if it is not up, and waits until it is.
xcrun simctl bootstatus "$device" -b > /dev/null

clean_up() {
    xcrun simctl terminate "$device" "$bundle_id" 2> /dev/null || true
    "$probe" "$destination" --kill-pane "$idle_pane" | grep -v "^  "
    "$probe" "$destination" --kill-pane "$unread_pane" | grep -v "^  "
    "$probe" "$destination" --quit-server | grep -v "^  "
}
trap clean_up EXIT

print_unread_mark() {
    "$probe" "$destination" --list-unread | grep "^unread $unread_pane "
}

"$probe" "$destination" --leave-pane "$idle_pane" | grep "left pane"
"$probe" "$destination" --leave-pane "$unread_pane" | grep "left pane"
ssh -o BatchMode=yes "$destination" \
    "COWTERM_INSTANCE=unread-test $folder/bin/ct pane-state finished --pane $unread_pane --message '$claude_line'"
print_unread_mark | grep "finished: $claude_line"

launch_app() {
    SIMCTL_CHILD_COWTERM_DESTINATION="$destination" \
    SIMCTL_CHILD_COWTERM_BRIDGE_COMMAND="$COWTERM_BRIDGE_COMMAND" \
    SIMCTL_CHILD_COWTERM_AGENT_SOCKET="${SSH_AUTH_SOCK:-}" \
    SIMCTL_CHILD_COWTERM_KEY_FILE="${PHONE_KEY_FILE:-}" \
    SIMCTL_CHILD_COWTERM_KNOWN_HOSTS="$HOME/.ssh/known_hosts" \
        scripts/run-ios-simulator.sh "$device_name" --overview "$@" > /dev/null
}

wait_for_app_log_line() {
    waited=0
    until grep -q "$1" "$app_log"; do
        if [ "$waited" -ge "$wait_seconds" ]; then
            echo "no '$1' in the app's log after $wait_seconds s:" >&2
            grep "overview:\|phone:" "$app_log" >&2 || true
            exit 1
        fi
        sleep 1
        waited=$((waited + 1))
    done
}

mkdir -p "$(dirname "$prefix")"
launch_app
wait_for_app_log_line "overview: [1-9][0-9]* sessions from"
sleep "$seconds_for_the_overview_to_draw"
scripts/screenshot-ios-simulator.sh "$prefix-list.png" "$device_name" > /dev/null
echo "screenshot: $prefix-list.png"

xcrun simctl terminate "$device" "$bundle_id"
launch_app "--open-session=$host_name/$unread_pane"
wait_for_app_log_line "phone: link live"
sleep "$seconds_for_the_pane_to_draw"
scripts/screenshot-ios-simulator.sh "$prefix-pane.png" "$device_name" > /dev/null
echo "screenshot: $prefix-pane.png"
print_unread_mark | grep " read$"
echo "opening the pane on the phone read it"
