#!/bin/sh
# Prints this machine's name as the phone dials it: hostName in
# ~/.config/cowterm.json, the name the Mac colours its own sessions by. Exits 1
# with a message when the file does not set it, so pairing never gives the
# phone a different name from the one the Mac uses.
#
#   scripts/print-this-machine-name.sh
set -eu
config_file="$HOME/.config/cowterm.json"

name=$(python3 -I -c '
import json, sys
try:
    print(json.load(open(sys.argv[1])).get("hostName", ""))
except (OSError, ValueError):
    print("")
' "$config_file")

if [ -z "$name" ]; then
    echo "set \"hostName\" in $config_file to the name the phone dials this Mac by (an ssh alias here, e.g. big-cow)" >&2
    exit 1
fi

printf '%s\n' "$name"
