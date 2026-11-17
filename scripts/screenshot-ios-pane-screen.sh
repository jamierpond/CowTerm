#!/bin/sh
# Screenshots every pane screen state on the smallest and the largest current
# iPhone, in portrait and landscape, into build-ios-sim/screenshots/pane-*.png.
# Build first with scripts/build-ios-simulator.sh.
#
#   scripts/screenshot-ios-pane-screen.sh
set -eu
cd "$(dirname "$0")/.."
out="build-ios-sim/screenshots"
seconds_for_the_screen_to_settle=5
seconds_for_scrolling_back_with_output_arriving=8
mkdir -p "$out"

take() {
    device_name="$1"
    name="$2"
    settle="$3"
    shift 3
    scripts/run-ios-simulator.sh "$device_name" "$@" > /dev/null
    sleep "$settle"
    slug=$(echo "$device_name" | tr ' A-Z' '-a-z')
    scripts/screenshot-ios-simulator.sh "$out/pane-$slug-$name.png" "$device_name" > /dev/null 2>&1
    echo "$out/pane-$slug-$name.png"
}

for device_name in "iPhone 17e" "iPhone 18 Pro Max"; do
    for orientation in portrait landscape; do
        rotate=""
        [ "$orientation" = landscape ] && rotate="--landscape"
        take "$device_name" "live-$orientation" "$seconds_for_the_screen_to_settle" $rotate
        take "$device_name" "menu-$orientation" "$seconds_for_the_screen_to_settle" $rotate --menu
        take "$device_name" "stalled-one-pane-$orientation" "$seconds_for_the_screen_to_settle" \
            $rotate --link=stalled --panes=1
        take "$device_name" "scrolled-back-$orientation" \
            "$seconds_for_scrolling_back_with_output_arriving" $rotate --stream --scroll-back
        take "$device_name" "keyboard-$orientation" "$seconds_for_the_screen_to_settle" \
            $rotate --raise-keyboard
    done
done
