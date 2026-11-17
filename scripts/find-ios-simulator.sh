#!/bin/sh
# Prints the id of the available iOS simulator called NAME on the newest iOS
# runtime that has one, e.g. `scripts/find-ios-simulator.sh "iPhone 17e"`.
set -eu
name="$1"

xcrun simctl list devices available --json | python3 -I -c '
import json, sys
name = sys.argv[1]
devices = json.load(sys.stdin)["devices"]
for runtime in sorted(devices, reverse=True):
    for device in devices[runtime]:
        if device["name"] == name and runtime.startswith("com.apple.CoreSimulator.SimRuntime.iOS"):
            print(device["udid"])
            sys.exit(0)
sys.exit("no available simulator called " + name)
' "$name"
