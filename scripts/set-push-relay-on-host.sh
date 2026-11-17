#!/usr/bin/env bash
# Tells a machine's CowTerm daemon where to send a Claude's finishing or asking
# and what the phone calls the machine: sets pushRelay and pushHostName in
# ~/.config/cowterm.json there (or here, for "local"), keeping every other key.
# The daemon reads them when it starts, so they take effect at its next start.
#
#   scripts/set-push-relay-on-host.sh <user@host | local> <name the phone dials> [relay address]
set -euo pipefail
cd "$(dirname "$0")/.."

host="${1:?usage: scripts/set-push-relay-on-host.sh <user@host | local> <host name> [relay address]}"
host_name="${2:?the name the phone dials this machine by, e.g. big-cow}"
relay="${3:-$(scripts/find-push-relay-address.sh)}"

read -r -d '' merge_settings <<'PYTHON' || true
import json, os, sys

path = os.path.expanduser("~/.config/cowterm.json")
config = json.load(open(path)) if os.path.exists(path) else {}
config["pushRelay"] = sys.argv[1]
config["pushHostName"] = sys.argv[2]
temporary = path + ".push-relay"
with open(temporary, "w") as file:
    json.dump(config, file, indent=4)
    file.write("\n")
os.replace(temporary, path)
print(f"{path}: pushRelay {sys.argv[1]}, pushHostName {sys.argv[2]}")
PYTHON

if [[ "$host" == local ]]; then
    python3 -c "$merge_settings" "$relay" "$host_name"
else
    ssh "$host" python3 - "$relay" "$host_name" <<<"$merge_settings"
fi
