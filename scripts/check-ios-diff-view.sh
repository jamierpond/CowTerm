#!/bin/sh
# Proves the pane screen's diff button on a real host, without touching anyone's
# panes or projects. On <user@host> it makes two throwaway folders in /tmp, a
# git repository with one changed file and a plain folder, and opens a
# throwaway pane in each with the probe. For each it launches the app on that
# pane with --open-diff-view --close-diff-view, screenshots what the button
# opened (dv, or the sentence saying why dv cannot run), and lists the host's
# panes after dv is closed: its pane must be gone. Then it opens dv in the
# repository again and kills the app with dv on screen, as a reinstall does:
# dv's pane is still listed. A plain launch then kills it, and the last list
# shows it gone. The folders and panes are removed at the end.
#
#   scripts/check-ios-diff-view.sh <user@host> <screenshot-prefix> ["iPhone 17 Pro"]
set -eu
cd "$(dirname "$0")/.."
destination="$1"
prefix="$2"
device_name="${3:-iPhone 17 Pro}"
run="cowterm-dv-test-$$"
repository="/tmp/$run-repository"
plain_folder="/tmp/$run-plain"
repository_pane="$run-repository"
plain_pane="$run-plain"
log="build-ios-sim/CowTermMobile.log"
bundle_id="audio.pond.cowterm.mobile"

# --open-diff-view presses the button 10 s after launch, --close-diff-view
# closes dv at 20 s (iOS/Main.cpp).
seconds_until_dv_has_drawn=16
seconds_until_dv_is_closed=8
seconds_for_the_relaunch_to_kill_it=12

cmake --build build --target CowTermEmbeddedSshProbe > /dev/null
probe="build/Tests/CowTermEmbeddedSshProbe"
device=$(scripts/find-ios-simulator.sh "$device_name")

# Booted before any SIMCTL_CHILD_ variable is set (see run-ios-against-host.sh).
if ! xcrun simctl list devices booted | grep -q "$device"; then
    xcrun simctl boot "$device"
fi

ssh "$destination" "mkdir -p $repository $plain_folder && cd $repository && git init -q \
    && printf 'one\n' > notes.txt && git add notes.txt \
    && git -c user.name=test -c user.email=test@example.com commit -q -m first \
    && printf 'two\n' >> notes.txt"

cleanUp() {
    xcrun simctl terminate "$device" "$bundle_id" 2> /dev/null || true
    "$probe" "$destination" --kill-pane "$repository_pane" | grep -v "^  " || true
    "$probe" "$destination" --kill-pane "$plain_pane" | grep -v "^  " || true
    ssh "$destination" "rm -rf $repository $plain_folder"
}
trap cleanUp EXIT

"$probe" "$destination" --leave-pane "$repository_pane" "$repository_pane" "$repository" | grep -v "^  "
"$probe" "$destination" --leave-pane "$plain_pane" "$plain_pane" "$plain_folder" | grep -v "^  "

launch_app() {
    pane="$1"
    shift
    SIMCTL_CHILD_COWTERM_DESTINATION="$destination" \
    SIMCTL_CHILD_COWTERM_PANE="$pane" \
    SIMCTL_CHILD_COWTERM_AGENT_SOCKET="${SSH_AUTH_SOCK:-}" \
    SIMCTL_CHILD_COWTERM_KEY_FILE="${PHONE_KEY_FILE:-}" \
    SIMCTL_CHILD_COWTERM_KNOWN_HOSTS="$HOME/.ssh/known_hosts" \
        scripts/run-ios-simulator.sh "$device_name" "$@" > /dev/null
}

list_diff_view_panes() {
    "$probe" "$destination" --list-panes | grep "phone-dv-" || echo "  no dv pane listed"
}

open_and_close_diff_view() {
    pane="$1"
    screenshot="$2"
    launch_app "$pane" --open-diff-view --close-diff-view
    sleep "$seconds_until_dv_has_drawn"
    scripts/screenshot-ios-simulator.sh "$screenshot" "$device_name" > /dev/null
    sleep "$seconds_until_dv_is_closed"
    grep "phone: link\|phone: pane \|phone: dv\|phone: no dv\|phone: killed\|dv:" "$log" || true
    echo "screenshot: $screenshot"
    echo "dv panes on $destination after dv was closed:"
    list_diff_view_panes
    xcrun simctl terminate "$device" "$bundle_id"
}

echo "== the repository"
open_and_close_diff_view "$repository_pane" "$prefix-repository.png"
echo "== the plain folder"
open_and_close_diff_view "$plain_pane" "$prefix-plain-folder.png"

echo "== the app killed with dv open"
launch_app "$repository_pane" --open-diff-view
sleep "$seconds_until_dv_has_drawn"
xcrun simctl terminate "$device" "$bundle_id"
grep "phone: dv\|phone: no dv" "$log" || true
echo "dv panes on $destination after the app was killed:"
list_diff_view_panes
launch_app "$repository_pane"
sleep "$seconds_for_the_relaunch_to_kill_it"
grep "phone: killed" "$log" || true
echo "dv panes on $destination after the next launch:"
list_diff_view_panes
