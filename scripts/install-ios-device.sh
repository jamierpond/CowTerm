#!/bin/sh
# Builds CowTermMobile for a real iPhone in build-ios-device, signed by Xcode's
# automatic signing, installs it on the phone and launches it. The phone must
# be plugged in, unlocked and trusted. Arguments after the phone's name go to
# the app.
#
#   scripts/install-ios-device.sh [pond] [app arguments]
#
# The signing team is COWTERM_IOS_TEAM, else the first team Xcode has signed
# into, else Jamie's Personal Team. EACP_SOURCE builds against a local eacp
# checkout, as in scripts/build-ios-simulator.sh.
set -eu
cd "$(dirname "$0")/.."
phone="${1:-pond}"
[ $# -gt 0 ] && shift
build_dir="build-ios-device"
configuration="RelWithDebInfo"
bundle_id="audio.pond.cowterm.mobile"
jamies_personal_team="DN5WMFL5W5"

find_team_xcode_is_signed_into() {
    defaults export com.apple.dt.Xcode - 2>/dev/null | python3 -I -c '
import plistlib, sys
try:
    teams = plistlib.loads(sys.stdin.buffer.read()).get("IDEProvisioningTeams", {})
except Exception:
    teams = {}
for account in teams.values():
    for team in account:
        if team.get("teamID"):
            print(team["teamID"]); raise SystemExit
'
}

team="${COWTERM_IOS_TEAM:-$(find_team_xcode_is_signed_into)}"
team="${team:-$jamies_personal_team}"

if [ -n "${EACP_SOURCE:-}" ]; then
    eacp_source_flag="-DLOCKSTEP_eacp_SOURCE=$EACP_SOURCE"
else
    eacp_source_flag="-ULOCKSTEP_eacp_SOURCE"
fi

cmake -G Xcode -B "$build_dir" \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphoneos \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_STYLE=Automatic \
    -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM="$team" \
    "$eacp_source_flag"

cmake --build "$build_dir" --config "$configuration" --target CowTermMobile -- \
    -allowProvisioningUpdates -allowProvisioningDeviceRegistration -quiet

app="$build_dir/iOS/$configuration-iphoneos/CowTermMobile.app"
xcrun devicectl device install app --device "$phone" "$app"
xcrun devicectl device process launch --terminate-existing --device "$phone" "$bundle_id" "$@"
echo "$phone: signed by team $team"
