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
# It also checks the Mac would show that session as the project's own, not as
# a second one: the layout the phone left with the daemon must be filed under
# the directory the Mac's palette lists the project under (<host>:<folder>,
# the host named as in the Mac's searchDirs) and carry the name the Mac's
# getSessionNameForProject gives it, and the pane must be in that session.
#
#   scripts/check-ios-project-opening.sh <user@host> <screenshot-prefix> ["iPhone 17 Pro"]
set -eu
cd "$(dirname "$0")/.."
destination="$1"
prefix="$2"
device_name="${3:-iPhone 17 Pro}"
project="cowterm-phone-project-test-$$"
host_name="${destination#*@}"
mac_config="$HOME/.config/cowterm.json"
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
    "--open-project=$host_name/$project" > /dev/null

sleep "$seconds_until_the_list_shows_the_project"
scripts/screenshot-ios-simulator.sh "$prefix-list.png" "$device_name" > /dev/null
echo "screenshot: $prefix-list.png"
sleep "$seconds_until_the_pane_has_drawn"
scripts/screenshot-ios-simulator.sh "$prefix-pane.png" "$device_name" > /dev/null
echo "screenshot: $prefix-pane.png"
grep "phone: project\|overview: open\|phone: pane \|phone: link" "$log" || true
echo "panes in $project on $destination with the project open:"
list_project_panes

# The Mac's palette lists the project as <searchDirs host>:<folder the scan found>.
if ! grep -q "\"$host_name:" "$mac_config"; then
    echo "FAIL: the Mac's searchDirs ($mac_config) name no host $host_name" >&2
    exit 1
fi

folder=$(ssh "$destination" "cd ~/projects/$project && pwd")
mac_project_dir="$host_name:$folder"
pane=$(find_project_pane_id)
layout=$("$probe" "$destination" --list-layouts | grep " projectDir=$mac_project_dir " || true)
echo "the Mac lists the project as $mac_project_dir; the daemon's layout for it:"
echo "${layout:-  none}"

layout_name=$(echo "$layout" | sed -n 's/.* name=\([^ ]*\) projectDir=.*/\1/p')
mac_name=$(echo "$layout" | sed -n 's/.* mac-name=\([^ ]*\)$/\1/p')
pane_session=$("$probe" "$destination" --list-panes | grep "^  $pane " | awk '{print $NF}')
echo "layout name: $layout_name; the Mac's name for $mac_project_dir: $mac_name; pane $pane's session: $pane_session"

if [ -z "$layout" ] || [ "$layout_name" != "$mac_name" ] || [ "$pane_session" != "$mac_name" ] \
    || ! echo "$layout" | grep -q " panes= $pane "; then
    echo "FAIL: the Mac would not see the phone's session as $mac_project_dir's" >&2
    exit 1
fi

echo "PASS: one session $mac_name, pane $pane, filed under $mac_project_dir as the Mac's palette lists it"
