#!/bin/sh
# Checks that the phone draws the screen it stored, dimmed with its age, before
# the link is up. Runs scripts/run-ios-against-host.sh (which shows a throwaway
# pane on <user@host> and stores its screen), then launches the app on that
# pane through a port nothing listens on, so the link never comes up, and
# screenshots the stored screen.
#
#   scripts/check-ios-stored-screen.sh <user@host> <stored-screen.png> ["iPhone Air"]
set -eu
cd "$(dirname "$0")/.."
destination="$1"
screenshot="$2"
device_name="${3:-iPhone Air}"
log="build-ios-sim/CowTermMobile.log"
port_nothing_listens_on=1
seconds_to_draw_the_stored_screen=4

scripts/run-ios-against-host.sh "$destination" "${screenshot%.png}-live.png" "$device_name"

pane=$(grep "phone: stored screen of" "$log" | tail -1 | sed 's/.* pane \([^ ]*\) from.*/\1/')
SIMCTL_CHILD_COWTERM_DESTINATION="$destination:$port_nothing_listens_on" \
SIMCTL_CHILD_COWTERM_PANE="$pane" \
SIMCTL_CHILD_COWTERM_KNOWN_HOSTS="$HOME/.ssh/known_hosts" \
    scripts/run-ios-simulator.sh "$device_name" > /dev/null
sleep "$seconds_to_draw_the_stored_screen"
scripts/screenshot-ios-simulator.sh "$screenshot" "$device_name" > /dev/null
grep "phone:" "$log"
echo "screenshot: $screenshot"
