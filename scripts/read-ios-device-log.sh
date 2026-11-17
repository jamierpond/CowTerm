#!/bin/sh
# Prints the log the iPhone app wrote on its last launch on the phone, copied
# out of the app's container; with "previous", the launch before that.
#
#   scripts/read-ios-device-log.sh [pond] [previous]
set -eu
phone="${1:-pond}"
log_name="CowTermMobile.log"
[ "${2:-}" = previous ] && log_name="CowTermMobile.previous.log"
bundle_id="audio.pond.cowterm.mobile"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
xcrun devicectl device copy from --device "$phone" \
    --domain-type appDataContainer --domain-identifier "$bundle_id" \
    --source "Library/Application Support/pond/CowTerm/logs/$log_name" \
    --destination "$work/$log_name" > /dev/null
cat "$work/$log_name"
