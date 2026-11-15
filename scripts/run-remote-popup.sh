#!/bin/sh
# Opens Ctrl+A i's lazygit popup for a pane on <host>, in <directory> there,
# through the same Popup::show the app calls, with no window and no key press,
# and prints what the command drew. Isolated instance: your CowTerm is untouched.
# usage: scripts/run-remote-popup.sh <host> <directory there>
set -e
build_directory="${BUILD_DIRECTORY:-build-remote}"
trace="$(mktemp)"
COWTERM_INSTANCE="run-remote-popup" COWTERM_POPUP_DEBUG="$trace" \
    "$build_directory/Tests/CowTermRunRemotePopup" "$@"
sed -n '/screen @1.5s/,/^----/p' "$trace"
rm -f "$trace"
