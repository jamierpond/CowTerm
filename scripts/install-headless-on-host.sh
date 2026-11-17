#!/bin/sh
# Installs this checkout's HEAD as the headless ct and cowtermd on <host>, with
# the host's own scripts/install-headless.sh (into ~/.local/libexec/cowterm-
# remote, renaming over the old binaries). A running daemon keeps running its
# old binary, so no shell ends; ct bridge leaves a daemon one protocol version
# older running. `ct update-remotes` installs origin/main instead; this
# installs a branch.
#
#   scripts/install-headless-on-host.sh <host> [folder there]
set -eu
cd "$(dirname "$0")/.."
host="$1"
folder="${2:-\$HOME/tmp/cowterm-headless-install}"

git archive --format=tar HEAD | ssh -o BatchMode=yes "$host" \
    "rm -rf $folder && mkdir -p $folder && tar -x -C $folder"
ssh -o BatchMode=yes "$host" bash -l -s "$folder" <<'REMOTE'
folder="$1"
export PATH="$HOME/.nix-profile/bin:$PATH"
cd "$(eval echo "$folder")"
deps=""
if [ "$(uname -s)" = Linux ]; then
    deps=scripts/with-linux-deps.sh
fi
$deps scripts/install-headless.sh 2>&1 | tail -3
"$HOME/.local/bin/ct" version
REMOTE
