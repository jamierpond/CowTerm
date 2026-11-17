#!/usr/bin/env bash
# What the relay host's service runs: waits for the tailnet address, reads the
# 1Password service-account token from its file, and starts ct push-relay with
# Apple's key filled in by op run. The key exists only in the relay's memory.
set -euo pipefail

relay_folder="$HOME/.local/libexec/cowterm-push-relay"
config_folder="$HOME/.config/cowterm-push-relay"
token_file="$config_folder/op-service-account-token"
port="${COWTERM_PUSH_RELAY_PORT:-8742}"
tailnet_wait_seconds=120

if [[ ! -r "$token_file" ]]; then
    echo "push-relay: no 1Password token in $token_file; run just push-relay-store-token on the Mac" >&2
    exit 1
fi

address=""
for ((waited = 0; waited < tailnet_wait_seconds; ++waited)); do
    address="$(tailscale ip -4 2>/dev/null | head -n 1 || true)"
    [[ -n "$address" ]] && break
    sleep 1
done

if [[ -z "$address" ]]; then
    echo "push-relay: no tailnet address after ${tailnet_wait_seconds}s; is tailscaled up?" >&2
    exit 1
fi

OP_SERVICE_ACCOUNT_TOKEN="$(<"$token_file")"
export OP_SERVICE_ACCOUNT_TOKEN

exec "$relay_folder/op" run --env-file="$config_folder/apns.env" -- \
    env -u OP_SERVICE_ACCOUNT_TOKEN "$relay_folder/ct" push-relay --listen "$address" --port "$port"
