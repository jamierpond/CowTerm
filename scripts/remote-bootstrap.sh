#!/bin/sh
# Runs on a remote machine (piped over ssh by install-remote.sh and
# `ct install-remote`): clones or updates CowTerm at ~/projects/CowTerm, then
# builds and installs its headless half there.
set -e

repository="$HOME/projects/CowTerm"

if [ -d "$repository/.git" ]; then
    branch="$(git -C "$repository" rev-parse --abbrev-ref HEAD)"
    if [ "$branch" != "main" ]; then
        echo "[cowterm] $repository is on branch $branch, not main; switch it to main (or move it aside) and run this again" >&2
        exit 1
    fi
    echo "[cowterm] updating $repository"
    git -C "$repository" pull --ff-only --quiet
else
    echo "[cowterm] cloning into $repository"
    mkdir -p "$HOME/projects"
    git clone --quiet https://github.com/jamierpond/CowTerm.git "$repository"
fi

exec "$repository/scripts/install-headless.sh"
