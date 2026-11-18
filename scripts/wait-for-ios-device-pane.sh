#!/bin/sh
# Waits until the iPhone app's log on <phone> says the last pane it opened on
# its pane screen is <pane id>, and still says so a few seconds later, reading
# the log of the app's latest launch.
# Exits 0 once it has, 1 if it has not after <seconds>. Prints the pane the
# phone opened instead, if any, so a caller can tell the person what is on
# screen.
#
#   scripts/wait-for-ios-device-pane.sh <phone> <pane id> [seconds]
set -eu
cd "$(dirname "$0")/.."
phone="$1"
pane="$2"
seconds_to_wait="${3:-40}"
seconds_between_reads=3
# The launch may open another pane just after the one asked for.
seconds_the_pane_must_stay_on_screen=8
deadline=$(( $(date +%s) + seconds_to_wait ))

while [ "$(date +%s)" -lt "$deadline" ]; do
    log="$(scripts/read-ios-device-log.sh "$phone" 2> /dev/null || true)"

    last_opened="$(printf '%s\n' "$log" | grep "phone: pane .* opened" | tail -1 || true)"

    if printf '%s\n' "$last_opened" | grep -q "phone: pane $pane on "; then
        if [ -n "${confirmed_once:-}" ]; then
            exit 0
        fi

        confirmed_once=yes
        sleep "$seconds_the_pane_must_stay_on_screen"
        continue
    fi

    confirmed_once=

    sleep "$seconds_between_reads"
done

printf '%s\n' "$log" | grep "phone: pane .* opened\|overview: open" | tail -1 || true
exit 1
