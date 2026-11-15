#!/bin/sh
# Runs on a remote machine (piped over ssh by install-remote.sh and
# `ct install-remote`): clones or updates CowTerm at ~/projects/CowTerm, then
# builds and installs its headless half there. A checkout there that is on
# another branch, or with uncommitted changes, is left alone; main gets a
# worktree at ~/projects/CowTerm-main.
set -e

repository="$HOME/projects/CowTerm"
main_worktree="$HOME/projects/CowTerm-main"

if [ ! -d "$repository/.git" ]; then
    echo "[cowterm] cloning into $repository"
    mkdir -p "$HOME/projects"
    git clone --quiet https://github.com/jamierpond/CowTerm.git "$repository"
fi

source_directory="$repository"
branch="$(git -C "$repository" rev-parse --abbrev-ref HEAD)"

if [ "$branch" != "main" ] || ! git -C "$repository" diff --quiet; then
    source_directory="$main_worktree"
    if [ ! -d "$main_worktree" ]; then
        echo "[cowterm] $repository is on $branch or has local changes; adding a main worktree at $main_worktree"
        git -C "$repository" fetch --quiet origin main
        git -c core.autocrlf=false -C "$repository" worktree add --quiet --detach "$main_worktree" origin/main
        git -C "$main_worktree" config core.autocrlf false
    fi
fi

echo "[cowterm] updating $source_directory"
git -C "$source_directory" fetch --quiet origin main
if [ "$source_directory" = "$main_worktree" ]; then
    git -C "$source_directory" checkout --quiet --detach FETCH_HEAD
else
    git -C "$source_directory" merge --ff-only --quiet FETCH_HEAD
fi
exec "$source_directory/scripts/install-headless.sh"
