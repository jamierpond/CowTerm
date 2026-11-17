#!/bin/sh
# Configures and builds CowTerm's libraries and the CowTermMobile app for the
# iOS Simulator in build-ios-sim, with the flags eacp's own iOS CI lane uses.
# Nothing is signed. Extra arguments go to `cmake --build`, e.g.
#
#   scripts/build-ios-simulator.sh --target CowTermMultiplexer
set -eu
cd "$(dirname "$0")/.."
build_dir="build-ios-sim"
configuration="RelWithDebInfo"

cmake -G Xcode -B "$build_dir" \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphonesimulator \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO

cmake --build "$build_dir" --config "$configuration" "$@" -- -sdk iphonesimulator -quiet
