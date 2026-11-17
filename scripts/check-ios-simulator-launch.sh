#!/bin/sh
# Launches the simulator build of CowTermMobile on the first available iPhone
# simulator, waits, and fails if the app is no longer running: a crash at
# start-up, which a build alone cannot show. Used by CI.
#
#   scripts/check-ios-simulator-launch.sh [seconds to wait]
set -eu
cd "$(dirname "$0")/.."
seconds_to_wait="${1:-15}"
app="build-ios-sim/iOS/RelWithDebInfo-iphonesimulator/CowTermMobile.app"
bundle_id="audio.pond.cowterm.mobile"

device=$(xcrun simctl list devices available --json | python3 -I -c '
import json, sys
devices = json.load(sys.stdin)["devices"]
for runtime in sorted(devices, reverse=True):
    if not runtime.startswith("com.apple.CoreSimulator.SimRuntime.iOS"):
        continue
    for device in devices[runtime]:
        if device["name"].startswith("iPhone"):
            print(device["udid"], device["name"], sep="\t")
            sys.exit(0)
sys.exit("no available iPhone simulator")
')
udid=$(printf '%s' "$device" | cut -f1)
echo "launching on $(printf '%s' "$device" | cut -f2) ($udid)"

xcrun simctl boot "$udid" 2> /dev/null || true
xcrun simctl bootstatus "$udid" > /dev/null
xcrun simctl install "$udid" "$app"
launched=$(xcrun simctl launch "$udid" "$bundle_id")
pid=${launched##*: }
sleep "$seconds_to_wait"

if xcrun simctl spawn "$udid" launchctl list | grep -q "UIKitApplication:$bundle_id"; then
    echo "CowTermMobile (pid $pid) is still running after $seconds_to_wait seconds"
    exit 0
fi

echo "CowTermMobile (pid $pid) is not running $seconds_to_wait seconds after launch" >&2
exit 1
