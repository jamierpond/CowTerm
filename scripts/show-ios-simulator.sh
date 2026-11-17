#!/bin/sh
# Opens the window that shows iOS simulators, so a person can see and touch the
# app that scripts/run-ios-simulator.sh launched headless. Xcode 27 shows
# simulators in DeviceHub; earlier Xcodes have Simulator.app.
#
#   scripts/show-ios-simulator.sh ["iPhone 18 Pro Max"]
set -eu
cd "$(dirname "$0")/.."
device_name="${1:-iPhone 17e}"
device=$(scripts/find-ios-simulator.sh "$device_name")
xcode_applications="$(dirname "$(xcode-select -p)")/Applications"
device_hub="$xcode_applications/DeviceHub.app"
simulator="$(xcode-select -p)/Applications/Simulator.app"

if [ -d "$device_hub" ]; then
    open "$device_hub"
elif [ -d "$simulator" ]; then
    open "$simulator" --args -CurrentDeviceUDID "$device"
else
    echo "this Xcode has neither DeviceHub.app nor Simulator.app" >&2
    exit 1
fi
echo "$device_name: $device"
