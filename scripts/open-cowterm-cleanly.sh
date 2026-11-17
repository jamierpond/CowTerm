#!/bin/sh
# Opens a CowTerm app bundle with the environment the Dock would give it, not
# the caller's. `open` hands the app the caller's environment, and the app
# passes it on to its daemon and every shell in every pane: opened from a
# Claude Code session, every pane's shell carried that session's CLAUDE*
# markers, id and messaging token, and every Claude started there ran without
# a saved transcript. Further arguments go to `open` (e.g. -n, --env NAME=value).
#
#   scripts/open-cowterm-cleanly.sh <CowTerm.app> [open arguments]
set -eu
app="$1"
shift
exec env -i \
    HOME="$HOME" USER="${USER:-$(id -un)}" LOGNAME="${LOGNAME:-$(id -un)}" \
    SHELL="${SHELL:-/bin/zsh}" TMPDIR="${TMPDIR:-/tmp/}" LANG="${LANG:-en_US.UTF-8}" \
    PATH=/usr/bin:/bin:/usr/sbin:/sbin \
    /usr/bin/open "$@" "$app"
