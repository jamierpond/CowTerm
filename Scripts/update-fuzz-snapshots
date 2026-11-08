#!/bin/sh
# Rewrites Fuzz/Snapshots.txt from the current parser. Run it after adding a
# recording to Fuzz/Corpus, or after a change that is meant to alter a screen.
set -eu
cd "$(dirname "$0")/.."
build_dir="${1:-build}"
cmake --build "$build_dir" --target CowTermWriteFuzzSnapshots
"$build_dir/Fuzz/CowTermWriteFuzzSnapshots" Fuzz/Corpus Fuzz/Snapshots.txt
