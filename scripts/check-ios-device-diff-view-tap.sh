#!/bin/sh
# Checks that a tap on the phone reaches dv as a click. On <user@host> it makes
# a throwaway git repository in /tmp with two changed files, alpha.txt and
# beta.txt, opens a throwaway pane there, launches the app on pond showing that
# pane, and presses the diff button. Jamie taps beta.txt in dv's file list; dv
# then shows beta.txt's diff. It prints dv's screen before and after the tap and
# the phone's log lines about the tap, then removes the pane, dv's pane and the
# repository. The phone must be unlocked.
#
#   scripts/check-ios-device-diff-view-tap.sh [jamiepond@macprorack] [pond]
set -eu
cd "$(dirname "$0")/.."
destination="${1:-jamiepond@macprorack}"
host_name="${destination#*@}"
phone="${2:-pond}"
run="cowterm-dv-tap-$$"
repository="/tmp/$run"
pane="$run"
bundle_id="audio.pond.cowterm.mobile"
# --open-diff-view presses the diff button 10 s after launch (iOS/Main.cpp).
seconds_until_dv_has_drawn=16

cmake --build build --target CowTermEmbeddedSshProbe > /dev/null
probe="build/Tests/CowTermEmbeddedSshProbe"

ssh "$destination" "mkdir -p $repository && cd $repository && git init -q \
    && printf 'one\n' > alpha.txt && printf 'one\n' > beta.txt && git add . \
    && git -c user.name=test -c user.email=test@example.com commit -q -m first \
    && printf 'alpha changed\n' >> alpha.txt && printf 'beta changed\n' >> beta.txt"

find_diff_view_panes() {
    "$probe" "$destination" --list-panes | grep "phone-dv-.*$repository" | awk '{print $1}'
}

print_diff_view_screen() {
    diff_view_pane="$(find_diff_view_panes | tail -1)"

    if [ -z "$diff_view_pane" ]; then
        echo "  no dv pane in $repository"
        return
    fi

    "$probe" "$destination" --adopt "$diff_view_pane" | sed -n '/^screen of/,$p'
}

cleanUp() {
    for diff_view_pane in $(find_diff_view_panes || true); do
        "$probe" "$destination" --kill-pane "$diff_view_pane" | grep -v "^  " || true
    done

    "$probe" "$destination" --kill-pane "$pane" | grep -v "^  " || true
    ssh "$destination" "rm -rf $repository"
}
trap cleanUp EXIT

"$probe" "$destination" --leave-pane "$pane" "$pane" "$repository" | grep -v "^  "

xcrun devicectl device process launch --terminate-existing --device "$phone" "$bundle_id" \
    --open-session="$host_name/$pane" --open-diff-view > /dev/null
sleep "$seconds_until_dv_has_drawn"

echo "== dv before the tap"
print_diff_view_screen

ct notify-user --title "CowTerm · dv tap check" \
    --content "pond shows dv on a test repository with two files, alpha.txt and beta.txt. Tap beta.txt once in dv's file list, then answer." \
    --options "done" > /dev/null

echo "== dv after the tap"
print_diff_view_screen
scripts/read-ios-device-log.sh "$phone" | grep "phone: dv\|touch\|tap" || true
