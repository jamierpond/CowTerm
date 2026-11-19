#!/bin/sh
# Shows that coverage-report combines builds: builds the instrumented ct twice
# from HEAD, the second with printVersion changed, as instance
# coverage-builds (never the user's own counts). The first build runs
# `ct version`, the second only `ct help`. With only the second build kept,
# printVersion is listed as never run; with both kept, it is not.
#
#   scripts/check-line-coverage-across-builds.sh [cmake arguments...]
set -eu
cd "$(dirname "$0")/.."
export COWTERM_INSTANCE=coverage-builds
root="$PWD/build-coverage-builds"

build_ct() {
    name="$1"
    shift
    rm -rf "$root/$name-src"
    mkdir -p "$root/$name-src"
    git archive --format=tar HEAD | tar -x -C "$root/$name-src"
    if [ "$name" = second ]; then
        perl -0pi -e 's/int printVersion\(const Arguments&\)\n\{\n/int printVersion(const Arguments& arguments)\n{\n    if (arguments.size() > 1)\n        std::puts("second build");\n/' \
            "$root/$name-src/Remote/CtMain.cpp"
        grep -q '"second build"' "$root/$name-src/Remote/CtMain.cpp"
    fi
    cmake -S "$root/$name-src" -B "$root/$name-build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCOWTERM_HEADLESS=ON "$@" >/dev/null
    cmake --build "$root/$name-build" --target CowTermCli >/dev/null
}

is_print_version_never_run() {
    scripts/line-coverage.py report --instance "$COWTERM_INSTANCE" > "$root/report.txt"
    grep -E "^\[coverage\]|printVersion" "$root/report.txt"
    grep -q "printVersion" "$root/report.txt"
}

build_ct first "$@"
build_ct second "$@"
scripts/line-coverage.py reset --instance "$COWTERM_INSTANCE"
"$root/first-build/Remote/ct" version
"$root/second-build/Remote/ct" help > /dev/null

echo "[coverage-builds] only the second build kept:"
scripts/line-coverage.py keep --instance "$COWTERM_INSTANCE" --programs "$root/second-build/Remote"
is_print_version_never_run || { echo "[coverage-builds] failed: printVersion should be never run"; exit 1; }

echo "[coverage-builds] both builds kept:"
scripts/line-coverage.py keep --instance "$COWTERM_INSTANCE" --programs "$root/first-build/Remote"
if is_print_version_never_run; then
    echo "[coverage-builds] failed: printVersion ran in the first build but is listed as never run"
    exit 1
fi
scripts/line-coverage.py reset --instance "$COWTERM_INSTANCE"
echo "[coverage-builds] passed: a function run only under the first build is reported as run"
