#!/bin/sh
# Prints the log the iPhone app wrote on its last launch on the phone, copied
# out of the app's container.
#
#   scripts/read-ios-device-log.sh [pond]
set -eu
phone="${1:-pond}"
bundle_id="audio.pond.cowterm.mobile"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
xcrun devicectl device copy from --device "$phone" \
    --domain-type appDataContainer --domain-identifier "$bundle_id" \
    --source "Library/Application Support/pond/CowTerm/logs/CowTermMobile.log" \
    --destination "$work/CowTermMobile.log" > /dev/null
cat "$work/CowTermMobile.log"
