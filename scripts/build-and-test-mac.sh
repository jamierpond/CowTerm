#!/usr/bin/env bash
# Builds CowTerm and its tests in build/ (Release, Ninja) and runs every test.
# Nothing is installed or launched.
set -euo pipefail

repository_root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$repository_root"

cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure "$@"
