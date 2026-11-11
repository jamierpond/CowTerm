#!/bin/sh
# Runs `just test` and `just build` on a Windows box over ssh, against this
# checkout: the box checks out origin/BRANCH (default: this checkout's branch)
# in ~/projects/CowTerm-remote, then this checkout's uncommitted and untracked
# files are laid over it. Push the branch first.
#
#   scripts/test-on-windows.sh windows-vm
#   scripts/test-on-windows.sh jamie@tamby-windows jp/panels-refactor
#   NO_OVERLAY=1 scripts/test-on-windows.sh windows-vm main      (a baseline)
set -eu
cd "$(dirname "$0")/.."
host="$1"
branch="${2:-$(git rev-parse --abbrev-ref HEAD)}"

scp -q scripts/test-on-windows-box.ps1 "$host:test-on-windows-box.ps1"
ssh "$host" 'powershell -NoProfile -Command "Remove-Item -Recurse -Force -ErrorAction SilentlyContinue cowterm-overlay; New-Item -ItemType Directory -Force cowterm-overlay | Out-Null"'

overlay_files="$( (git diff --name-only --diff-filter=d HEAD; git ls-files --others --exclude-standard) | sort -u)"
if [ -n "$overlay_files" ] && [ -z "${NO_OVERLAY:-}" ]; then
    echo "$overlay_files" | COPYFILE_DISABLE=1 tar -cf - -T - | ssh "$host" 'tar -xmf - -C cowterm-overlay'
fi

ssh "$host" "powershell -NoProfile -ExecutionPolicy Bypass -File test-on-windows-box.ps1 -Branch $branch"
