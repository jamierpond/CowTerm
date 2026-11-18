#!/bin/sh
# Checks in the simulator that a closed screen or menu takes no touch: the
# overview opens and closes, then a tap lands on the pane; the menu opens and
# closes, then a drag lands on the pane. The app routes each finger from the
# window's root as a real one is (--tap-after-overview, --drag-after-menu) on
# its sample pane, so no host is needed. Screenshots go to
# build-ios-sim/screenshots/hidden-*.png; it fails if the pane did not log the
# finger. Build first with scripts/build-ios-simulator.sh. Shuts the simulator
# down when it is done, if it was not already running.
#
#   scripts/check-ios-hidden-views.sh ["iPhone 17e"]
set -eu
cd "$(dirname "$0")/.."
device_name="${1:-iPhone 17e}"
out="build-ios-sim/screenshots"
log="build-ios-sim/CowTermMobile.log"
seconds_until_open=5
seconds_from_open_to_after_the_finger=9
mkdir -p "$out"

check() {
    name="$1"
    expected_line="$2"
    shift 2
    scripts/run-ios-simulator.sh "$device_name" --sample --portrait "$@" > /dev/null
    sleep "$seconds_until_open"
    scripts/screenshot-ios-simulator.sh "$out/hidden-$name-open.png" "$device_name" > /dev/null 2>&1
    sleep "$seconds_from_open_to_after_the_finger"
    scripts/screenshot-ios-simulator.sh "$out/hidden-$name-after.png" "$device_name" > /dev/null 2>&1
    echo "$out/hidden-$name-open.png"
    echo "$out/hidden-$name-after.png"

    if ! sed -n '/hidden view check:/,$p' "$log" | grep -q "$expected_line"; then
        echo "$name: the pane never logged \"$expected_line\" after the finger" >&2
        exit 1
    fi

    echo "$name: the pane logged \"$expected_line\""
}

device=$(scripts/find-ios-simulator.sh "$device_name")

if ! xcrun simctl list devices booted | grep -q "$device"; then
    trap 'xcrun simctl shutdown "$device" 2> /dev/null || true' EXIT
fi

check tap-after-overview "pane screen: finger down" --tap-after-overview
# Streamed output gives the pane lines to scroll back through, so the pill shows.
check drag-after-menu "pane screen: drag scrolls from" --drag-after-menu --stream
