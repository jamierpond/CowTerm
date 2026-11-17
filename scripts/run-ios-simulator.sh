#!/bin/sh
# Boots an iOS simulator headless (Simulator.app stays closed), installs the
# CowTermMobile build from scripts/build-ios-simulator.sh and launches it.
# The simulator defaults to the smallest current iPhone.
#
#   scripts/run-ios-simulator.sh ["iPhone 18 Pro Max"]
set -eu
cd "$(dirname "$0")/.."
device_name="${1:-iPhone 17e}"
app="build-ios-sim/iOS/RelWithDebInfo-iphonesimulator/CowTermMobile.app"
bundle_id="audio.pond.cowterm.mobile"

device=$(scripts/find-ios-simulator.sh "$device_name")

if ! xcrun simctl list devices booted | grep -q "$device"; then
    xcrun simctl boot "$device"
fi

xcrun simctl bootstatus "$device" > /dev/null
xcrun simctl install "$device" "$app"
xcrun simctl terminate "$device" "$bundle_id" 2> /dev/null || true
xcrun simctl launch "$device" "$bundle_id"
echo "$device_name: $device"
