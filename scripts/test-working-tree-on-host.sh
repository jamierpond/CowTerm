#!/usr/bin/env bash
# Runs `just test` (and, with --ios, `just ios-build`) on another Mac in a
# throwaway folder there, for when this Mac is on a low battery. It tests the
# working tree, uncommitted changes included, or with --committed only HEAD,
# leaving out what other agents have not committed yet. --remove deletes the
# folder there.
#
#   scripts/test-working-tree-on-host.sh <user@host> <name> [--ios] [--committed]
#   scripts/test-working-tree-on-host.sh <user@host> <name> --remove
set -euo pipefail

host="$1"
name="$2"
shift 2
folder="tmp/cowterm-test-$name"
repo="$(git rev-parse --show-toplevel)"
steps="just test"
source_folder="$repo"

for option in "$@"; do
    case "$option" in
        --remove)
            ssh "$host" "rm -rf ~/$folder"
            exit 0
            ;;
        --ios)
            steps="just test && just ios-build"
            ;;
        --committed)
            source_folder="$(mktemp -d)"
            trap 'rm -rf "$source_folder"' EXIT
            git -C "$repo" archive HEAD | tar -x -C "$source_folder"
            ;;
    esac
done

ssh "$host" "mkdir -p ~/$folder"

# Only files whose content changed are copied, and they get the time of the
# copy, so a build there never keeps an object newer than its changed source.
rsync -rlp --checksum --delete --exclude '/build*' --exclude '/.git' "$source_folder/" "$host:$folder/"

ssh -o ServerAliveInterval=30 "$host" "export PATH=/etc/profiles/per-user/\$USER/bin:/opt/homebrew/bin:\$HOME/.local/bin:\$PATH; cd ~/$folder && $steps"
