#!/bin/sh
# Builds the kitty key encoder's libFuzzer target the way run-parser-fuzzer.sh
# builds the parser's, and fuzzes it for SECONDS (default 120) from an empty
# corpus. Crashes go to build-fuzz/key-crashes.
#
#   scripts/run-key-encoder-fuzzer.sh [SECONDS] [extra libFuzzer flags...]
set -eu
cd "$(dirname "$0")/.."
seconds="${1:-120}"
[ $# -gt 0 ] && shift

llvm_bin=""
if command -v brew >/dev/null 2>&1 && [ -d "$(brew --prefix llvm)/bin" ]; then
    llvm_bin="$(brew --prefix llvm)/bin/"
fi

cmake -S . -B build-fuzz -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER="${llvm_bin}clang" -DCMAKE_CXX_COMPILER="${llvm_bin}clang++" \
    -DCOWTERM_ENABLE_FUZZING=ON -DCOWTERM_ENABLE_TESTS=OFF
cmake --build build-fuzz --target CowTermKeyEncoderFuzzer

mkdir -p build-fuzz/key-corpus build-fuzz/key-crashes
build-fuzz/Fuzz/CowTermKeyEncoderFuzzer build-fuzz/key-corpus \
    -max_total_time="$seconds" -max_len=64 -timeout=10 \
    -artifact_prefix=build-fuzz/key-crashes/ "$@"
