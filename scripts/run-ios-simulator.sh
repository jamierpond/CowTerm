#!/bin/sh
# Boots an iOS simulator headless (Simulator.app stays closed), installs the
# CowTermMobile build from scripts/build-ios-simulator.sh and launches it.
# The simulator defaults to the smallest current iPhone. Further arguments go
# to the app: --landscape turns it to landscape, --raise-keyboard focuses the
# terminal with the software keyboard up. The app's output goes to
# build-ios-sim/CowTermMobile.log.
#
#   scripts/run-ios-simulator.sh ["iPhone 18 Pro Max"] [--landscape]
set -eu
cd "$(dirname "$0")/.."
device_name="${1:-iPhone 17e}"
[ $# -gt 0 ] && shift
log="$PWD/build-ios-sim/CowTermMobile.log"
app="build-ios-sim/iOS/RelWithDebInfo-iphonesimulator/CowTermMobile.app"
bundle_id="audio.pond.cowterm.mobile"

device=$(scripts/find-ios-simulator.sh "$device_name")

if ! xcrun simctl list devices booted | grep -q "$device"; then
    xcrun simctl boot "$device"
fi

xcrun simctl bootstatus "$device" > /dev/null
xcrun simctl install "$device" "$app"
: > "$log"
xcrun simctl launch --terminate-running-process --stdout="$log" --stderr="$log" \
    "$device" "$bundle_id" "$@"
echo "$device_name: $device"
