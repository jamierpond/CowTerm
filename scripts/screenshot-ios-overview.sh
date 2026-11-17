#!/bin/sh
# Screenshots the overview (the app launched with --overview and its sample
# sessions) on the smallest and largest iPhones, portrait and landscape, into
# OUTPUT_DIR. Build first with scripts/build-ios-simulator.sh. The two
# simulators are this script's own, created on first use, so it never takes
# over a simulator another agent is driving. A simulator keeps the last
# orientation it was turned to, so the portrait shot asks for portrait.
#
#   scripts/screenshot-ios-overview.sh <output-dir>
set -eu
cd "$(dirname "$0")/.."
output_dir="$1"
runtime=$(xcrun simctl list runtimes | grep -o 'com.apple.CoreSimulator.SimRuntime.iOS-[0-9-]*' | sort -V | tail -1)
settle_seconds=4
mkdir -p "$output_dir"

shoot() {
    simulator_name="$1"
    device_type="$2"
    file_stem="$3"

    if ! xcrun simctl list devices | grep -q "$simulator_name ("; then
        xcrun simctl create "$simulator_name" "$device_type" "$runtime" > /dev/null
    fi

    scripts/run-ios-simulator.sh "$simulator_name" --overview --portrait
    sleep "$settle_seconds"
    scripts/screenshot-ios-simulator.sh "$output_dir/$file_stem-portrait.png" "$simulator_name"

    scripts/run-ios-simulator.sh "$simulator_name" --overview --landscape
    sleep "$settle_seconds"
    scripts/screenshot-ios-simulator.sh "$output_dir/$file_stem-landscape.png" "$simulator_name"

    xcrun simctl shutdown "$(scripts/find-ios-simulator.sh "$simulator_name")"
}

shoot "CowTerm Overview Small" "iPhone 17e" overview-small
shoot "CowTerm Overview Large" "iPhone 18 Pro Max" overview-large
