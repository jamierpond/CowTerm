#!/usr/bin/env bash
# Runs a command with the App Store Connect key and team from 1Password
# (Deploy/asc.env), through the desktop app, which asks Jamie to approve.
# Drops the shell's OP_SERVICE_ACCOUNT_TOKEN: that is Tamber's account, not the personal one.
#
#   [COWTERM_OP_TOKEN=token] scripts/asc-run.sh <command...>
set -euo pipefail
cd "$(dirname "$0")/.."

if [[ -n "${COWTERM_OP_TOKEN:-}" ]]; then
    OP_SERVICE_ACCOUNT_TOKEN="$COWTERM_OP_TOKEN" exec op run --env-file Deploy/asc.env -- "$@"
fi

exec env -u OP_SERVICE_ACCOUNT_TOKEN op run --account my.1password.com \
    --env-file Deploy/asc.env -- "$@"
