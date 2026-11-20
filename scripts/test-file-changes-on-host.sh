#!/usr/bin/env bash
# Runs `just test-file-changes` on another machine (Linux, macOS, or Windows
# through git-bash) against this working tree, uncommitted files included, in
# ~/tmp/cowterm-file-changes there. Only files whose content differs are
# copied over, with the time of the copy, so a second run rebuilds just those.
# --remove deletes the folder there.
#
#   scripts/test-file-changes-on-host.sh <user@host> [--remove]
set -euo pipefail

host="$1"
folder="tmp/cowterm-file-changes"
cd "$(git rev-parse --show-toplevel)"

if [ "${2:-}" = "--remove" ]; then
    ssh "$host" "rm -rf ~/$folder ~/$folder-incoming"
    exit 0
fi

ssh "$host" "rm -rf ~/$folder-incoming && mkdir -p ~/$folder ~/$folder-incoming"
git ls-files --cached --others --exclude-standard -z \
    | COPYFILE_DISABLE=1 tar --null -T - -cf - \
    | ssh "$host" "tar -xf - -C ~/$folder-incoming"
ssh "$host" "bash ~/$folder-incoming/scripts/copy-changed-files.sh ~/$folder-incoming ~/$folder && rm -rf ~/$folder-incoming"

ssh -o ServerAliveInterval=30 "$host" \
    "export PATH=/etc/profiles/per-user/\$USER/bin:\$HOME/.nix-profile/bin:\$HOME/.local/bin:\$PATH; cd ~/$folder && just test-file-changes"
