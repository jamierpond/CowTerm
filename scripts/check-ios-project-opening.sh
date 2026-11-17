#!/bin/sh
# Proves the phone opens a project that has no session, without touching any
# real project. On <user@host> it makes a throwaway folder in ~/projects, so the
# host's scan lists it, and launches the app on the overview searching for it.
# It screenshots the list showing the project's row, then --open-project opens
# it through the same path as a tap: one login shell in that folder, named as
# the Mac names that project's session. It screenshots the pane, lists the
# host's panes in that folder, then kills the pane, removes the folder and
# lists them again: the pane must be gone.
#
#   scripts/check-ios-project-opening.sh <user@host> <screenshot-prefix> ["iPhone 17 Pro"]
set -eu
cd "$(dirname "$0")/.."
destination="$1"
prefix="$2"
device_name="${3:-iPhone 17 Pro}"
project="cowterm-phone-project-test-$$"
host_label="${destination#*@}"
log="build-ios-sim/CowTermMobile.log"
bundle_id="audio.pond.cowterm.mobile"

# --open-project opens the project 10 s after launch (iOS/Main.cpp), so the
# list is screenshotted before that and the pane well after.
seconds_until_the_list_shows_the_project=8
seconds_until_the_pane_has_drawn=12

cmake --build build --target CowTermEmbeddedSshProbe > /dev/null
probe="build/Tests/CowTermEmbeddedSshProbe"
device=$(scripts/find-ios-simulator.sh "$device_name")

# Booted before any SIMCTL_CHILD_ variable is set (see run-ios-against-host.sh).
if ! xcrun simctl list devices booted | grep -q "$device"; then
    xcrun simctl boot "$device"
fi

list_project_panes() {
    "$probe" "$destination" --list-panes | grep "$project" || echo "  no pane in $project"
}

find_project_pane_id() {
    grep -o "starts a login shell in pane [0-9a-f]*" "$log" | awk '{print $NF}' | tail -1
}

cleanUp() {
    xcrun simctl terminate "$device" "$bundle_id" 2> /dev/null || true
    pane=$(find_project_pane_id || true)

    if [ -n "$pane" ]; then
        "$probe" "$destination" --kill-pane "$pane" | grep -v "^  " || true
    fi

    ssh "$destination" "rm -rf ~/projects/$project"
    echo "panes in $project on $destination after cleanup:"
    list_project_panes
}
trap cleanUp EXIT

ssh "$destination" "mkdir -p ~/projects/$project"
echo "panes in $project on $destination before:"
list_project_panes

SIMCTL_CHILD_COWTERM_DESTINATION="$destination" \
SIMCTL_CHILD_COWTERM_AGENT_SOCKET="${SSH_AUTH_SOCK:-}" \
SIMCTL_CHILD_COWTERM_KEY_FILE="${PHONE_KEY_FILE:-}" \
SIMCTL_CHILD_COWTERM_KNOWN_HOSTS="$HOME/.ssh/known_hosts" \
    scripts/run-ios-simulator.sh "$device_name" --overview "--search=$project" \
    "--open-project=$destination/$project" > /dev/null

sleep "$seconds_until_the_list_shows_the_project"
scripts/screenshot-ios-simulator.sh "$prefix-list.png" "$device_name" > /dev/null
echo "screenshot: $prefix-list.png"
sleep "$seconds_until_the_pane_has_drawn"
scripts/screenshot-ios-simulator.sh "$prefix-pane.png" "$device_name" > /dev/null
echo "screenshot: $prefix-pane.png"
grep "phone: project\|overview: open\|phone: pane \|phone: link" "$log" || true
echo "panes in $project on $destination with the project open:"
list_project_panes
