#!/bin/sh
# Copies this working tree (tracked and untracked, not ignored) to a Linux box
# over ssh and runs `just test` and `just build` there, so uncommitted platform code gets run on
# Linux too. HOST defaults to ubuntu-vm; the copy lives in ~/projects/CowTerm-remote.
#
#   scripts/test-on-linux.sh [HOST]
set -eu
cd "$(dirname "$0")/.."
host="${1:-ubuntu-vm}"
remote_dir="projects/CowTerm-remote"

ssh "$host" "mkdir -p $remote_dir"
git ls-files --cached --others --exclude-standard -z \
    | rsync -rlpc --from0 --files-from=- --delete-missing-args ./ "$host:$remote_dir/"
ssh "$host" "cd $remote_dir && just build_type=Debug test && just build_type=Debug build"
