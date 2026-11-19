#!/bin/sh
# Renders the palette, the switcher and each session's panes under the window
# chrome strip at minimum, tall narrow, default and ultrawide window sizes,
# with sessions on Jamie's hosts and a local one on this Mac, named big-cow as
# in Jamie's cowterm.json, then writes protanopia,
# deuteranopia and greyscale views of each. Runs in an
# isolated instance with an empty session store, removed afterwards; the
# shells are scripted, so no daemon or ssh is touched.
# usage: scripts/render-host-colours.sh [output directory]
set -e

instance="render-host-colours"
build_directory="${BUILD_DIRECTORY:-build}"
output_directory="${1:-$build_directory/renders/host-colours}"

rm -rf "$output_directory"
COWTERM_INSTANCE="$instance" "$build_directory/Tests/CowTermRenderHostColours" "$output_directory" || status=$?
rm -rf "$HOME/Library/Application Support/pond/CowTerm/sessions-$instance" \
       "$HOME/.local/share/pond/CowTerm/sessions-$instance"

for picture in "$output_directory"/*.png; do
    scripts/simulate-colour-vision.py "$picture"
done

exit "${status:-0}"
