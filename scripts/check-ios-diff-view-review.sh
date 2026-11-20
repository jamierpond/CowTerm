#!/bin/sh
# Proves a whole dv review from the phone, in the simulator: dv opened from the
# pane screen's diff button, a comment typed straight into dv's comment box
# and saved with the key row's Ctrl then s, a second typed comment saved with
# dv's Save button, and the review submitted. On
# <user@host> it makes a throwaway git repository in /tmp with two changed
# files and a throwaway pane there, plays those steps with --diff-view-steps=
# (iOS/DiffViewLaunchSteps.h), screenshots the comment box with the typed text
# and the end, and prints the review round dv wrote. The pane, dv's pane and
# the repository are removed at the end.
#
#   scripts/check-ios-diff-view-review.sh <user@host> <screenshot-prefix> ["iPhone 17 Pro"]
set -eu
cd "$(dirname "$0")/.."
destination="$1"
prefix="$2"
device_name="${3:-iPhone 17 Pro}"
run="cowterm-dv-review-$$"
repository="/tmp/$run"
pane="$run"
log="build-ios-sim/CowTermMobile.log"
bundle_id="audio.pond.cowterm.mobile"
typed_comment="Typed on the phone straight into dv."
saved_comment="Saved with the Save button on the phone."

steps="keyboard;tap:beta.txt;tap:beta changed;tap:Comment;type:$typed_comment;key:ctrl;type:s"
steps="$steps;tap:1  one;tap:Comment;type:$saved_comment;tap:Save"
steps="$steps;tap:Submit;tap:Submit"

# Steps start 18 s after launch, 2.5 s apart (iOS/Main.cpp): the typed comment
# is in the box after the fifth step (28 s), the second one after the tenth
# (40.5 s), and the round is sent after the thirteenth (48 s).
seconds_until_the_typed_comment_is_in_the_box=29
seconds_until_the_saved_comment_is_in_the_box=13
seconds_until_the_round_is_sent=9

cmake --build build --target CowTermEmbeddedSshProbe > /dev/null
probe="build/Tests/CowTermEmbeddedSshProbe"
device=$(scripts/find-ios-simulator.sh "$device_name")

# Booted before any SIMCTL_CHILD_ variable is set (see run-ios-against-host.sh).
if ! xcrun simctl list devices booted | grep -q "$device"; then
    xcrun simctl boot "$device"
fi

ssh "$destination" "mkdir -p $repository && cd $repository && git init -q \
    && printf 'one\n' > alpha.txt && printf 'one\n' > beta.txt && git add . \
    && git -c user.name=test -c user.email=test@example.com commit -q -m first \
    && printf 'alpha changed\n' >> alpha.txt && printf 'beta changed\n' >> beta.txt"

cleanUp() {
    xcrun simctl terminate "$device" "$bundle_id" 2> /dev/null || true

    for diff_view_pane in $("$probe" "$destination" --list-panes | grep "phone-dv-.*$repository" | awk '{print $1}' || true); do
        "$probe" "$destination" --kill-pane "$diff_view_pane" | grep -v "^  " || true
    done

    "$probe" "$destination" --kill-pane "$pane" | grep -v "^  " || true
    ssh "$destination" "rm -rf $repository"
}
trap cleanUp EXIT

"$probe" "$destination" --leave-pane "$pane" "$pane" "$repository" | grep -v "^  "

SIMCTL_CHILD_COWTERM_DESTINATION="$destination" \
SIMCTL_CHILD_COWTERM_PANE="$pane" \
SIMCTL_CHILD_COWTERM_AGENT_SOCKET="${SSH_AUTH_SOCK:-}" \
SIMCTL_CHILD_COWTERM_KEY_FILE="${PHONE_KEY_FILE:-}" \
SIMCTL_CHILD_COWTERM_KNOWN_HOSTS="$HOME/.ssh/known_hosts" \
    scripts/run-ios-simulator.sh "$device_name" --open-diff-view "--diff-view-steps=$steps" > /dev/null

sleep "$seconds_until_the_typed_comment_is_in_the_box"
scripts/screenshot-ios-simulator.sh "$prefix-typed.png" "$device_name" > /dev/null
sleep "$seconds_until_the_saved_comment_is_in_the_box"
scripts/screenshot-ios-simulator.sh "$prefix-saved.png" "$device_name" > /dev/null
sleep "$seconds_until_the_round_is_sent"
scripts/screenshot-ios-simulator.sh "$prefix-submitted.png" "$device_name" > /dev/null

grep "phone: dv\|dv:\|dv step:\|key row:" "$log" || true
echo "screenshots: $prefix-typed.png $prefix-saved.png $prefix-submitted.png"
echo "== review rounds dv wrote in $repository/.diffview/reviews"
ssh "$destination" "ls $repository/.diffview/reviews 2> /dev/null && cat $repository/.diffview/latest.md" \
    || echo "  dv wrote no review round"
ssh "$destination" "grep -q '$typed_comment' $repository/.diffview/latest.md \
    && grep -q '$saved_comment' $repository/.diffview/latest.md" \
    && echo "PASS: both comments are in the round" \
    || { echo "FAIL: the round lacks a comment"; exit 1; }
