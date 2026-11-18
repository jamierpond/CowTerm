#!/usr/bin/env bash
# Prints one line per thing the push relay on <host> needs, "ready" or "not
# ready" and why, so anyone can see what is left before the phone can ring.
# It reads only: it starts, stops and installs nothing, and prints no secret.
# Exits 0 when every line is ready.
#
#   scripts/check-push-relay-ready.sh [user@host]
set -euo pipefail
cd "$(dirname "$0")/.."

host="${1:-jamiepond@macprorack}"
relay_port=8742

hash_of() {
    shasum -a 256 "$1" | cut -d' ' -f1
}

ssh -o BatchMode=yes "$host" bash -s -- \
    "$(hash_of scripts/start-push-relay.sh)" \
    "$(hash_of Deploy/cowterm-push-relay.service)" \
    "$(hash_of Deploy/cowterm-push-relay.path)" \
    "$(hash_of Deploy/push-relay.env)" \
    "$relay_port" <<'CHECKS'
set -u
start_hash="$1"
service_hash="$2"
path_hash="$3"
env_hash="$4"
relay_port="$5"
relay_folder="$HOME/.local/libexec/cowterm-push-relay"
config_folder="$HOME/.config/cowterm-push-relay"
token_file="$config_folder/op-service-account-token"
unit_folder="$HOME/.config/systemd/user"
ct="$HOME/.local/libexec/cowterm-remote/ct"
devices_file="$HOME/.local/share/pond/CowTerm/push-relay-devices.json"
all_ready=1

report() {
    if [ "$1" = 0 ]; then
        echo "ready      $2"
    else
        echo "not ready  $2: $3"
        all_ready=0
    fi
}

hash_matches() {
    [ -f "$1" ] && [ "$(sha256sum "$1" | cut -d' ' -f1)" = "$2" ]
}

loginctl show-user "$USER" -p Linger 2>/dev/null | grep -q "Linger=yes"
report $? "user services start at boot without a login" "run: loginctl enable-linger $USER"

"$ct" help 2>/dev/null | grep -q push-relay
report $? "the installed ct has push-relay" "run just push-relay-install"

"$relay_folder/op" --version > /dev/null 2>&1
report $? "the 1Password command line tool is installed" "run just push-relay-install"

hash_matches "$relay_folder/start" "$start_hash"
report $? "the relay's start script is this checkout's" "run just push-relay-install"

hash_matches "$unit_folder/cowterm-push-relay.service" "$service_hash" \
    && hash_matches "$unit_folder/cowterm-push-relay.path" "$path_hash"
report $? "the relay's service and path units are this checkout's" "run just push-relay-install"

systemctl --user is-enabled cowterm-push-relay.path > /dev/null 2>&1 \
    && systemctl --user is-active cowterm-push-relay.path > /dev/null 2>&1
report $? "the relay starts by itself once its token is stored" "run just push-relay-install"

hash_matches "$config_folder/apns.env" "$env_hash"
report $? "the relay knows where Apple's key is in 1Password" "run just push-relay-install"

[ "$(stat -c %a "$config_folder" 2>/dev/null)" = 700 ]
report $? "the relay's config folder is readable only by $USER" "run: chmod 700 $config_folder"

apple_answer="$(curl -sS -o /dev/null -w '%{http_code}' --max-time 10 --http2 \
    https://api.sandbox.push.apple.com/3/device/0 2>/dev/null || true)"
[ -n "$apple_answer" ] && [ "$apple_answer" != 000 ]
report $? "Apple's push service answers on port 443" "no answer from api.sandbox.push.apple.com"

grep -q '"pushRelay": *"http' "$HOME/.config/cowterm.json" 2>/dev/null \
    && grep -q '"pushHostName": *"[^"]' "$HOME/.config/cowterm.json" 2>/dev/null
report $? "this machine's daemon knows the relay and its own name" "run just push-relay-set-host"

daemon_pid="$(pgrep -x cowtermd | head -n 1)"
[ -n "$daemon_pid" ] && ! readlink "/proc/$daemon_pid/exe" | grep -q deleted
report $? "the running daemon is the installed build" "restart it with just restart-daemon-on-host (ends programs in its panes)"

[ "$(grep -c 'ct pane-state --claude-hook' "$HOME/.claude/settings.json" 2>/dev/null)" -ge 3 ]
report $? "Claude Code's hooks report to the daemon" "run just claude-hooks-add"

[ -f "$token_file" ] && [ "$(stat -c %a "$token_file")" = 600 ]
report $? "the 1Password service-account token is stored" "Jamie runs just push-relay-store-token"

[ -f "$token_file" ] && OP_SERVICE_ACCOUNT_TOKEN="$(cat "$token_file")" "$relay_folder/op" run \
    --env-file="$config_folder/apns.env" -- sh -c 'test -n "$COWTERM_PUSH_PRIVATE_KEY"' > /dev/null 2>&1
report $? "the token reads Apple's push key from 1Password" "needs the token and the APNs Key item in the CowTerm Push vault"

relay_address="$(tailscale ip -4 2>/dev/null | head -n 1)"
curl -sS --max-time 5 "http://$relay_address:$relay_port/health" 2>/dev/null | grep -q ok
report $? "the relay answers on $relay_address:$relay_port" "it starts once the token reads the key"

[ -f "$devices_file" ] && grep -q '"token"' "$devices_file"
report $? "a phone has registered with the relay" "needs the push-capable phone build"

[ "$all_ready" = 1 ]
CHECKS
