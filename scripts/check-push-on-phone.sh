#!/bin/sh
# Rings Jamie's phone from a throwaway pane, never one of his: on <user@host>,
# the installed ct and cowtermd run as a separate daemon (COWTERM_INSTANCE=
# unread-check) whose config sends its notices to the relay. In one throwaway
# pane, Claude Code's own hook command reports a finished turn and then a
# question (scripts/check-unread-on-host.py), each a new unread event the
# daemon posts to the relay. The script says first whether the relay answers
# at all, and last prints the relay's log of the last minute, with Apple's
# answer for each push. The test daemon and its config are removed after.
#
#   scripts/check-push-on-phone.sh [user@host]
set -eu
cd "$(dirname "$0")/.."
destination="${1:-jamiepond@macprorack}"
relay="$(scripts/find-push-relay-address.sh)"
host_name="${destination#*@}"
instance="unread-check"
installed_ct='$HOME/.local/libexec/cowterm-remote/ct'
health_wait_seconds=5
seconds_for_the_relay=8

clean_up() {
    ssh -o BatchMode=yes "$destination" "rm -f ~/.config/cowterm.$instance.json"
}
trap clean_up EXIT

if curl -sS --max-time "$health_wait_seconds" "$relay/health" 2> /dev/null | grep -q ok; then
    echo "the relay at $relay answers"
else
    echo "the relay at $relay does not answer: it is not running yet (no 1Password token), so the daemon's notices stop at its door"
fi

ssh -o BatchMode=yes "$destination" \
    "printf '%s' '{\"pushRelay\":\"$relay\",\"pushHostName\":\"$host_name\"}' > ~/.config/cowterm.$instance.json"

scripts/check-unread-on-host.py "$destination" "$installed_ct"

sleep "$seconds_for_the_relay"
ssh -o BatchMode=yes "$destination" \
    "journalctl --user -u cowterm-push-relay --since '-1 min' --no-pager -o short-iso | grep 'notice from\|push to\|gave up\|no phone' || echo 'the relay logged no notice in the last minute'"
