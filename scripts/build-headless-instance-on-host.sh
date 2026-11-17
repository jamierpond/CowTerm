#!/bin/sh
# Builds the headless ct and cowtermd from this checkout's HEAD on <host>, in
# <folder> there (src, build and bin under it), without installing anything:
# ~/.local and the daemon already running there are untouched. ct finds the
# daemon beside itself, so both land in <folder>/bin. Run them as a separate
# instance, e.g. COWTERM_INSTANCE=phone-test <folder>/bin/ct bridge.
#
#   scripts/build-headless-instance-on-host.sh <host> <folder there>
set -eu
cd "$(dirname "$0")/.."
host="$1"
folder="$2"

git archive --format=tar HEAD | ssh -o BatchMode=yes "$host" \
    "rm -rf $folder/src && mkdir -p $folder/src && tar -x -C $folder/src"

ssh -o BatchMode=yes "$host" "bash -lc 'set -e
cd $folder/src
scripts/with-linux-deps.sh cmake -S . -B ../build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCOWTERM_HEADLESS=ON > ../configure.log
scripts/with-linux-deps.sh cmake --build ../build --target CowTermDaemon CowTermCli > ../build.log
mkdir -p ../bin
cp ../build/Daemon/CowTermDaemon ../bin/cowtermd
cp ../build/Remote/ct ../bin/ct
../bin/ct version'"
