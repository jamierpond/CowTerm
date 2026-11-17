#!/usr/bin/env bash
# Archives CowTermMobile for the App Store and, with --upload, sends it to
# App Store Connect for TestFlight. The version is the project's (CMakeLists.txt);
# the build number is HEAD's commit count, so each upload's is higher than the
# last. Signing is Xcode's automatic signing through the App Store Connect key.
# `just ios-release` runs it through scripts/asc-run.sh, which fills the team
# and the key from 1Password.
#
#   COWTERM_IOS_TEAM=<team id>|none [COWTERM_ASC_KEY_ID= COWTERM_ASC_ISSUER_ID= COWTERM_ASC_KEY=|COWTERM_ASC_KEY_P8=] scripts/release-ios.sh [--upload]
#
# EACP_SOURCE builds against a local eacp checkout, as in scripts/build-ios-simulator.sh.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
cd "$root"

# Xcode's export calls rsync by name; a Homebrew or nix one rejects Apple's flags.
export PATH="/usr/bin:$PATH"

team="${COWTERM_IOS_TEAM:?set COWTERM_IOS_TEAM to the Apple Developer team id (or none)}"
out=Deploy/Apple-iOS/out
build=build-ios-appstore
archive="$out/CowTerm.xcarchive"
build_number="$(git rev-list --count HEAD)"

if [[ "${1:-}" == --upload && -n "$(git status --porcelain --untracked-files=no)" ]]; then
    echo "uncommitted changes: commit them first, so build $build_number is exactly HEAD" >&2
    exit 1
fi

if [[ -n "${EACP_SOURCE:-}" ]]; then
    eacp_source_flag="-DLOCKSTEP_eacp_SOURCE=$EACP_SOURCE"
    echo "building against $EACP_SOURCE, not the eacp lockstep.lock pins" >&2
else
    eacp_source_flag="-ULOCKSTEP_eacp_SOURCE"
fi

signing=(-DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM="$team" -DCMAKE_XCODE_ATTRIBUTE_CODE_SIGN_STYLE=Automatic)
xcode_signing=(-allowProvisioningUpdates)
if [[ -n "${COWTERM_ASC_KEY_ID:-}" ]]; then
    if [[ -z "${COWTERM_ASC_KEY:-}" ]]; then
        keydir="$(mktemp -d)"
        trap 'rm -rf "$keydir"' EXIT
        COWTERM_ASC_KEY="$keydir/AuthKey_$COWTERM_ASC_KEY_ID.p8"
        (umask 077 && printf '%s\n' "$COWTERM_ASC_KEY_P8" > "$COWTERM_ASC_KEY")
    fi
    xcode_signing+=(-authenticationKeyPath "$COWTERM_ASC_KEY"
        -authenticationKeyID "$COWTERM_ASC_KEY_ID"
        -authenticationKeyIssuerID "$COWTERM_ASC_ISSUER_ID")
fi
if [[ "$team" == none ]]; then
    signing=(-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO)
    xcode_signing=()
fi

cmake -G Xcode -B "$build" -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphoneos -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_XCODE_GENERATE_SCHEME=ON \
    -DCMAKE_XCODE_ATTRIBUTE_SKIP_INSTALL=YES \
    -DCOWTERM_IOS_BUILD_NUMBER="$build_number" \
    "$eacp_source_flag" \
    "${signing[@]}"

rm -rf "$archive"
xcodebuild -project "$build/CowTerm.xcodeproj" -scheme CowTermMobile -configuration Release \
    -destination generic/platform=iOS -archivePath "$archive" \
    "${xcode_signing[@]}" archive

app="$archive/Products/Applications/CowTermMobile.app"
[[ -d "$app" ]] || { echo "archive has no CowTermMobile.app" >&2; exit 1; }
echo "archived $(plutil -extract CFBundleShortVersionString raw "$app/Info.plist")" \
    "($(plutil -extract CFBundleVersion raw "$app/Info.plist"))" >&2

[[ "$team" == none ]] && exit 0

options="$(mktemp -d)/ExportOptions.plist"
sed -e "s/TEAM_ID/$team/" \
    -e "s/DESTINATION/$([[ "${1:-}" == --upload ]] && echo upload || echo export)/" \
    Deploy/Apple-iOS/ExportOptions.plist > "$options"

xcodebuild -exportArchive -archivePath "$archive" -exportPath "$out" \
    -exportOptionsPlist "$options" "${xcode_signing[@]}"
ls "$out"
