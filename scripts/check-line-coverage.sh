#!/bin/sh
# Shows that line-coverage counts survive a daemon that never exits cleanly, on
# a throwaway instance (COWTERM_INSTANCE=coverage-check), never the user's own
# daemon: builds the instrumented ct and daemon, opens a shell through
# `ct bridge`, waits past the daemon's one-minute write, kills the daemon with
# SIGKILL, then prints the report from what it wrote.
#
#   scripts/check-line-coverage.sh <build directory> [cmake arguments...]
set -eu
cd "$(dirname "$0")/.."
build_directory="$1"
shift
headless=OFF
[ "$(uname -s)" = Linux ] && headless=ON
seconds_past_first_write=75
export COWTERM_INSTANCE=coverage-check

cmake -S . -B "$build_directory" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCOWTERM_HEADLESS=$headless "$@" >/dev/null
cmake --build "$build_directory" --target CowTermDaemon CowTermCli >/dev/null
mkdir -p "$build_directory/coverage-check"
programs="$(cd "$build_directory/coverage-check" && pwd)"
cp "$build_directory/Daemon/CowTermDaemon" "$programs/cowtermd"
cp "$build_directory/Remote/ct" "$programs/ct"

scripts/line-coverage.py reset --instance "$COWTERM_INSTANCE"
scripts/probe-bridge.py --local "$programs/ct" --keep
echo "[coverage-check] waiting ${seconds_past_first_write}s for the daemon's timed write"
sleep "$seconds_past_first_write"
pkill -KILL -f "$programs/cowtermd"
echo "[coverage-check] daemon killed with SIGKILL at $(date '+%H:%M:%S')"
scripts/line-coverage.py keep --instance "$COWTERM_INSTANCE" --programs "$programs"
scripts/line-coverage.py report --instance "$COWTERM_INSTANCE"
echo "[coverage-check] done"
