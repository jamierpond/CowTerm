#!/usr/bin/env bash
# Asks for the 1Password service-account token that lets the push relay read
# its Apple key, without echoing it, and stores it on the relay host in a file
# only that user can read. The token never appears on a command line or in a log.
set -euo pipefail

host="${1:-jamiepond@macprorack}"
token_folder='.config/cowterm-push-relay'
token_file="$token_folder/op-service-account-token"

if [[ ! -t 0 ]]; then
    echo "Run this in a terminal: it reads the token from the keyboard." >&2
    exit 2
fi

printf 'Paste the 1Password service-account token for the CowTerm push relay, then press Return: '
IFS= read -rs token
echo

if [[ "$token" != ops_* ]]; then
    echo "That does not look like a service-account token (they start with ops_). Nothing was stored." >&2
    exit 1
fi

printf '%s' "$token" | ssh "$host" "umask 077 && mkdir -p ~/$token_folder && cat > ~/$token_file && chmod 600 ~/$token_file"
unset token

echo "Stored on $host in ~/$token_file (readable only by its user)."
