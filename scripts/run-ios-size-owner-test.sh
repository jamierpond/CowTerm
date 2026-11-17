#!/bin/sh
# Proves the phone's size ownership end to end against a test daemon, never
# Jamie's: on <user@host>, the ct and cowtermd that
# scripts/build-headless-instance-on-host.sh built in <folder there> run as
# COWTERM_INSTANCE=phone-test. The probe opens a pane there at 150 columns and
# holds its size, as the Mac would. The phone opens the pane and must take it
# ("size-owner ... owner=iPhone" at its own width); when the phone quits the
# pane must go back to 150 columns. Then the pane is killed and the test daemon
# quits. Saves a screenshot of the phone holding the pane.
# Build the app first with scripts/build-ios-simulator.sh (EACP_SOURCE as it says).
#
#   scripts/run-ios-size-owner-test.sh <user@host> <folder there | installed> <screenshot.png> ["iPhone 17 Pro"]
set -eu
cd "$(dirname "$0")/.."
destination="$1"
folder="$2"
screenshot="$3"
device_name="${4:-iPhone 17 Pro}"
pane="phone-size-test-$$"
watch_log="build-ios-sim/size-owner-watch.log"
app_log="build-ios-sim/CowTermMobile.log"
bundle_id="audio.pond.cowterm.mobile"
wait_seconds=60
desk_size="150x40"
seconds_for_the_phone_to_draw=3

# "installed" runs against the host's own daemon, with a throwaway pane only,
# and leaves that daemon running.
if [ "$folder" = installed ]; then
    COWTERM_BRIDGE_COMMAND=""
else
    COWTERM_BRIDGE_COMMAND="COWTERM_INSTANCE=phone-test exec $folder/bin/ct bridge"
fi
export COWTERM_BRIDGE_COMMAND

cmake --build build --target CowTermEmbeddedSshProbe > /dev/null
probe="build/Tests/CowTermEmbeddedSshProbe"

device=$(scripts/find-ios-simulator.sh "$device_name")

# Booted before any SIMCTL_CHILD_ variable is set (see run-ios-against-host.sh).
if ! xcrun simctl list devices booted | grep -q "$device"; then
    xcrun simctl boot "$device"
fi

mkdir -p build-ios-sim
"$probe" "$destination" --watch-pane "$pane" 2>&1 | while IFS= read -r line; do echo "$(date +%H:%M:%S) $line"; done > "$watch_log" &
watcher=$!

clean_up() {
    xcrun simctl terminate "$device" "$bundle_id" 2> /dev/null || true
    kill "$watcher" 2> /dev/null || true
    "$probe" "$destination" --kill-pane "$pane" | grep -v "^  "

    if [ "$folder" != installed ]; then
        "$probe" "$destination" --quit-server | grep -v "^  "
    fi
}
trap clean_up EXIT

wait_for_line() {
    waited=0
    until grep -q "$2" "$1"; do
        if [ "$waited" -ge "$wait_seconds" ]; then
            echo "no '$2' in $1 after $wait_seconds s:" >&2
            cat "$1" >&2
            exit 1
        fi
        sleep 1
        waited=$((waited + 1))
    done
}

wait_for_line "$watch_log" "watching pane"

SIMCTL_CHILD_COWTERM_DESTINATION="$destination" \
SIMCTL_CHILD_COWTERM_PANE="$pane" \
SIMCTL_CHILD_COWTERM_BRIDGE_COMMAND="$COWTERM_BRIDGE_COMMAND" \
SIMCTL_CHILD_COWTERM_AGENT_SOCKET="${SSH_AUTH_SOCK:-}" \
SIMCTL_CHILD_COWTERM_KEY_FILE="${PHONE_KEY_FILE:-}" \
SIMCTL_CHILD_COWTERM_KNOWN_HOSTS="$HOME/.ssh/known_hosts" \
    scripts/run-ios-simulator.sh "$device_name" > /dev/null

wait_for_line "$watch_log" "owner=iPhone"
sleep "$seconds_for_the_phone_to_draw"
mkdir -p "$(dirname "$screenshot")"
scripts/screenshot-ios-simulator.sh "$screenshot" "$device_name" > /dev/null
echo "screenshot: $screenshot"

# Counted first: terminate can return after the daemon has already answered.
lines_before_quitting=$(wc -l < "$watch_log")
xcrun simctl terminate "$device" "$bundle_id"
waited=0
until tail -n "+$((lines_before_quitting + 1))" "$watch_log" | grep -q "size-owner $desk_size owner=probe"; do
    if [ "$waited" -ge "$wait_seconds" ]; then
        echo "the pane did not go back to $desk_size after the phone quit" >&2
        cat "$watch_log" >&2
        exit 1
    fi
    sleep 1
    waited=$((waited + 1))
done

echo "what the probe, as the desk client, saw:"
cat "$watch_log"
echo "what the phone logged:"
grep "phone:" "$app_log"
