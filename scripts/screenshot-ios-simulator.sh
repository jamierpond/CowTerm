#!/bin/sh
# Saves a PNG of a booted iOS simulator's screen. Further arguments go to
# `simctl io screenshot`: a phone with two screens (iPhone Duo) needs one,
# e.g. --display=1.
#
#   scripts/screenshot-ios-simulator.sh <file.png> ["iPhone 18 Pro Max"] [--display=1]
set -eu
cd "$(dirname "$0")/.."
file="$1"
device_name="${2:-iPhone 17e}"
shift
[ $# -gt 0 ] && shift

device=$(scripts/find-ios-simulator.sh "$device_name")
xcrun simctl io "$device" screenshot "$@" "$file"
