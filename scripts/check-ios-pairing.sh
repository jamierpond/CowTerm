#!/bin/sh
# Walks the iPhone app's pairing screen in a Release simulator build and saves
# a screenshot of each state, portrait and landscape, on the 375 x 812 iPhone
# 12 mini and the largest iPhone. The computer it pairs with is a throwaway
# sshd on 127.0.0.1 run from a temporary folder, with its own host key and an
# authorized_keys holding only the simulator's key; nothing in ~/.ssh changes.
# Every state is reached through the app's launch flags, with no taps.
#
#   scripts/check-ios-pairing.sh [screenshot folder]
#
# Build first with scripts/build-ios-simulator.sh (it configures build-ios-sim).
set -eu
cd "$(dirname "$0")/.."
folder="${1:-build-ios-sim/pairing-screenshots}"
configuration="Release"
app="build-ios-sim/iOS/$configuration-iphonesimulator/CowTermMobile.app"
bundle_id="audio.pond.cowterm.mobile"
runtime="com.apple.CoreSimulator.SimRuntime.iOS-27-2"
port=22622
closed_port=1
seconds_to_settle=6
seconds_to_connect=10
pairing_folder="Library/Application Support/pond/CowTerm/pairing"
user=$(id -un)

mkdir -p "$folder"
folder=$(cd "$folder" && pwd)
work=$(mktemp -d)
trap 'kill "$(cat "$work/sshd.pid" 2> /dev/null)" 2> /dev/null || true; rm -rf "$work"' EXIT

cmake --build build-ios-sim --config "$configuration" --target CowTermMobile -- -sdk iphonesimulator -quiet

# The build is unsigned, and an unsigned app gets no keychain, where the phone
# keeps its key: sign a copy ad hoc (no identity, no keychain on this Mac) with
# the application identifier a team-signed build would have.
cp -R "$app" "$work/"
app="$work/CowTermMobile.app"
cat > "$work/entitlements.plist" << ENTITLEMENTS
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>application-identifier</key>
    <string>SIMULATOR.$bundle_id</string>
    <key>keychain-access-groups</key>
    <array><string>SIMULATOR.$bundle_id</string></array>
</dict>
</plist>
ENTITLEMENTS
codesign --force --sign - --entitlements "$work/entitlements.plist" "$app"

find_or_create_simulator() {
    name="$1"
    device_type="$2"
    udid=$(xcrun simctl list devices available --json | python3 -I -c '
import json, sys
for devices in json.load(sys.stdin)["devices"].values():
    for device in devices:
        if device["name"] == sys.argv[1]:
            print(device["udid"]); sys.exit(0)
' "$name")
    if [ -z "$udid" ]; then
        udid=$(xcrun simctl create "$name" "$device_type" "$runtime")
    fi
    xcrun simctl boot "$udid" 2> /dev/null || true
    xcrun simctl bootstatus "$udid" > /dev/null
    echo "$udid"
}

launch() {
    xcrun simctl terminate "$udid" "$bundle_id" 2> /dev/null || true
    xcrun simctl launch "$udid" "$bundle_id" "$@" > /dev/null
}

shoot() {
    sleep "$1"
    xcrun simctl io "$udid" screenshot "$folder/$2.png" > /dev/null 2>&1
    echo "$folder/$2.png"
}

# The QR code on the key screen must read back as the key line itself.
check_qr_code() {
    read_back=$(swift scripts/read-qr-code.swift "$1")
    if [ "$read_back" = "$key_line" ]; then
        echo "$1: the QR code reads back as the key line"
    else
        echo "$1: the QR code reads back as '$read_back', not the key line" >&2
    fi
}

reset_pairing() {
    rm -rf "$(xcrun simctl get_app_container "$udid" "$bundle_id" data)/$pairing_folder"
}

start_sshd() {
    ssh-keygen -q -t ecdsa -b 256 -N '' -f "$work/host_key"
    cat > "$work/sshd_config" << CONFIG
Port $port
ListenAddress 127.0.0.1
HostKey $work/host_key
AuthorizedKeysFile $work/authorized_keys
PidFile $work/sshd.pid
UsePAM no
StrictModes no
PasswordAuthentication no
KbdInteractiveAuthentication no
CONFIG
    /usr/sbin/sshd -f "$work/sshd_config" -E "$work/sshd.log"
}

read_phone_key_line() {
    data=$(xcrun simctl get_app_container "$udid" "$bundle_id" data)
    log=$(find "$data" -name CowTermMobile.log | head -1)
    sed -n 's/.*pairing: this phone.s key: //p' "$log" | tail -1
}

walk_pairing() {
    label="$1"
    orientation="$2"
    host="$user@127.0.0.1:$port"

    reset_pairing
    launch $orientation
    shoot "$seconds_to_settle" "$label-1-key"
    check_qr_code "$folder/$label-1-key.png"

    launch $orientation --pair-with="$user@127.0.0.1:$closed_port"
    shoot "$seconds_to_connect" "$label-2-could-not-connect"

    launch $orientation --pair-with="$host"
    shoot "$seconds_to_connect" "$label-3-confirm-fingerprint"

    ssh-keygen -q -t ecdsa -b 256 -N '' -f "$work/other_key"
    known_hosts="$(xcrun simctl get_app_container "$udid" "$bundle_id" data)/$pairing_folder/known_hosts"
    mkdir -p "$(dirname "$known_hosts")"
    echo "[127.0.0.1]:$port $(cut -d' ' -f1,2 "$work/other_key.pub")" > "$known_hosts"
    launch $orientation --pair-with="$host"
    shoot "$seconds_to_connect" "$label-4-host-key-changed"
    rm -f "$work/other_key" "$work/other_key.pub"

    reset_pairing
    launch $orientation --pair-with="$host" --pair-trust
    shoot "$seconds_to_connect" "$label-5-paired"
}

small=$(find_or_create_simulator "CowTerm TestFlight 12 mini" com.apple.CoreSimulator.SimDeviceType.iPhone-12-mini)
large=$(find_or_create_simulator "CowTerm TestFlight 18 Pro Max" com.apple.CoreSimulator.SimDeviceType.iPhone-18-Pro-Max)

for udid in $small $large; do
    xcrun simctl install "$udid" "$app"
done

udid=$small
launch
sleep "$seconds_to_settle"
key_line=$(read_phone_key_line)
[ -n "$key_line" ] || { echo "the app logged no key line" >&2; exit 1; }
echo "$key_line" > "$work/authorized_keys"
udid=$large
launch
sleep "$seconds_to_settle"
large_key_line=$(read_phone_key_line)
echo "$large_key_line" >> "$work/authorized_keys"
start_sshd

udid=$small
walk_pairing iphone-12-mini-portrait --portrait
walk_pairing iphone-12-mini-landscape --landscape
udid=$large
key_line=$large_key_line
walk_pairing iphone-18-pro-max-portrait --portrait
walk_pairing iphone-18-pro-max-landscape --landscape
