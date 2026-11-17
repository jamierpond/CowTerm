#!/bin/sh
# Takes the phone's key off this Mac, tamby and macprorack: removes the lines
# marked "cowterm-device:<phone>" from ~/.ssh/authorized_keys on each, and
# nothing else. The files on the phone stay; the hosts no longer accept them.
#
#   scripts/unpair-ios-device.sh [pond]
set -eu
phone="${1:-pond}"
key_comment="cowterm-device:$phone"
this_mac="jamiepond@big-cow"
hosts="$this_mac jamiepond@tamby jamiepond@macprorack"
ssh_timeout_seconds=10

# Rewrites the file in place (same file, same permissions) without the phone's lines.
remove_lines='
    file="$HOME/.ssh/authorized_keys"
    if [ ! -f "$file" ] || ! grep -q " $1\$" "$file"; then
        echo "not there"
        exit 0
    fi
    kept=$(mktemp)
    grep -v " $1\$" "$file" > "$kept" || true
    cat "$kept" > "$file"
    rm -f "$kept"
    echo "removed"'

for host in $hosts; do
    if [ "$host" = "$this_mac" ]; then
        result=$(printf '%s\n' "$remove_lines" | sh -s "$key_comment")
    else
        result=$(printf '%s\n' "$remove_lines" | ssh -o BatchMode=yes -o ConnectTimeout="$ssh_timeout_seconds" \
            "$host" "sh -s '$key_comment'")
    fi
    echo "$host authorized_keys: $result"
done
