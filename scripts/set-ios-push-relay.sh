#!/bin/sh
# Tells the iPhone app where the push relay is and what name to register under,
# by copying two one-line files into its pairing folder (as just ios-pair copies
# the hosts). The app reads them at launch and registers for pushes.
#
#   scripts/set-ios-push-relay.sh [pond] [http://macprorack:8742]
set -eu
phone="${1:-pond}"
relay="${2:-http://macprorack:8742}"
bundle_id="audio.pond.cowterm.mobile"
pairing_folder_on_phone="Library/Application Support/pond/CowTerm/pairing"

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
printf '%s\n' "$relay" > "$work/push-relay"
printf '%s\n' "$phone" > "$work/device-name"

xcrun devicectl device copy to --device "$phone" \
    --domain-type appDataContainer --domain-identifier "$bundle_id" \
    --source "$work/push-relay" --source "$work/device-name" \
    --destination "$pairing_folder_on_phone" > /dev/null
echo "told $phone: relay $relay, registering as $phone"
