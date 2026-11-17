#!/bin/sh
# Configures and builds CowTerm's libraries and the CowTermMobile app for the
# iOS Simulator in build-ios-sim, with the flags eacp's own iOS CI lane uses.
# Nothing is signed. Extra arguments go to `cmake --build`, e.g.
#
#   scripts/build-ios-simulator.sh --target CowTermMultiplexer
#
# EACP_SOURCE builds against a local eacp checkout instead of the pinned one:
#
#   EACP_SOURCE=~/projects/eacp-ios-integration scripts/build-ios-simulator.sh
set -eu
cd "$(dirname "$0")/.."
build_dir="build-ios-sim"
configuration="RelWithDebInfo"

if [ -n "${EACP_SOURCE:-}" ]; then
    eacp_source_flag="-DLOCKSTEP_eacp_SOURCE=$EACP_SOURCE"
else
    eacp_source_flag="-ULOCKSTEP_eacp_SOURCE"
fi

cmake -G Xcode -B "$build_dir" \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphonesimulator \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO \
    "$eacp_source_flag"

cmake --build "$build_dir" --config "$configuration" "$@" -- -sdk iphonesimulator -quiet
