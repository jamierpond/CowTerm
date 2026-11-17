#!/bin/sh
# Sends a running CowTermMobile to the background by opening Settings, brings
# it back, and prints the lifecycle lines it logged. Run
# scripts/run-ios-simulator.sh first.
#
#   scripts/check-ios-lifecycle.sh ["iPhone 18 Pro Max"]
set -eu
cd "$(dirname "$0")/.."
device_name="${1:-iPhone 17e}"
log="build-ios-sim/CowTermMobile.log"
seconds_for_the_switch_to_settle=3

device=$(scripts/find-ios-simulator.sh "$device_name")
xcrun simctl launch "$device" com.apple.Preferences > /dev/null
sleep "$seconds_for_the_switch_to_settle"
xcrun simctl launch "$device" audio.pond.cowterm.mobile > /dev/null
sleep "$seconds_for_the_switch_to_settle"
grep "lifecycle:" "$log"
