#!/bin/sh
# Runs scripts/check-line-coverage.sh from this checkout's HEAD on <host>,
# detached there so a dropped ssh link does not stop it, and prints its log once
# it says done or failed. Then copies the host's counts and programs here and
# prints the report.
#
#   scripts/check-line-coverage-on-host.sh <host> [cmake arguments...]
set -eu
cd "$(dirname "$0")/.."
host="$1"
shift
folder='$HOME/tmp/cowterm-coverage-check'
poll_seconds=20

git archive --format=tar HEAD | ssh -o BatchMode=yes "$host" \
    "rm -rf $folder && mkdir -p $folder/src && tar -x -C $folder/src"
ssh -o BatchMode=yes "$host" "cd $folder/src && PATH=\$HOME/.nix-profile/bin:\$PATH nohup sh -c \
    'scripts/with-linux-deps.sh scripts/check-line-coverage.sh ../build $* || echo \"[coverage-check] failed\"' \
    > $folder/check.log 2>&1 < /dev/null &"

until ssh -o BatchMode=yes -o ConnectTimeout=10 "$host" \
        "grep -q '^\[coverage-check\] \(done\|failed\)' $folder/check.log" 2>/dev/null; do
    sleep "$poll_seconds"
done
ssh -o BatchMode=yes "$host" "cat $folder/check.log"
ssh -o BatchMode=yes "$host" "grep -q '^\[coverage-check\] done' $folder/check.log"

scripts/line-coverage.py collect "$host" --instance coverage-check --binaries "$folder/build/coverage-check"
scripts/line-coverage.py report --instance coverage-check
