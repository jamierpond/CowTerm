#!/bin/sh
# Renders Ctrl+A F off screen to a PNG with your real config, in an isolated
# instance (its own copy of the config and an empty session store, removed
# afterwards). Remote hosts are connected first, so their rows are real.
# usage: scripts/render-picker.sh <out.png> [query] [rows to move the selection down]
set -e

instance="render-picker"
config_copy="$HOME/.config/cowterm.$instance.json"
build_directory="${BUILD_DIRECTORY:-build-remote}"

cp "$HOME/.config/cowterm.json" "$config_copy"
COWTERM_INSTANCE="$instance" "$build_directory/Tests/CowTermRenderPicker" "$@" || status=$?
rm -f "$config_copy"
rm -rf "$HOME/Library/Application Support/pond/CowTerm/sessions-$instance" \
       "$HOME/.local/share/pond/CowTerm/sessions-$instance"
exit "${status:-0}"
