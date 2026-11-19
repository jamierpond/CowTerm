#!/bin/sh
# Takes the phone's key off this Mac, tamby, macprorack and tamby-windows:
# removes the lines marked "cowterm-device:<phone>" from ~/.ssh/authorized_keys
# on each (Windows' administrators_authorized_keys on tamby-windows), and
# nothing else. The files on the phone stay; the hosts no longer accept them.
#
#   scripts/unpair-ios-device.sh [pond]
set -eu
phone="${1:-pond}"
key_comment="cowterm-device:$phone"
this_mac="jamiepond@$("$(dirname "$0")"/print-this-machine-name.sh)"
hosts="$this_mac jamiepond@tamby jamiepond@macprorack jamie@tamby-windows"
windows_administrator_hosts="jamie@tamby-windows"
windows_administrators_keys_file="/c/ProgramData/ssh/administrators_authorized_keys"
ssh_timeout_seconds=10

# Rewrites the file in place (same file, same permissions) without the phone's lines.
remove_lines='
    file="${2:-$HOME/.ssh/authorized_keys}"
    if [ ! -f "$file" ] || ! grep -q " $1\$" "$file"; then
        echo "not there"
        exit 0
    fi
    kept=$(mktemp)
    grep -v " $1\$" "$file" > "$kept" || true
    cat "$kept" > "$file"
    rm -f "$kept"
    echo "removed"'

# The file that host's ssh server reads the phone's key from; empty means
# ~/.ssh/authorized_keys.
find_authorized_keys_file() {
    for windows_host in $windows_administrator_hosts; do
        if [ "$1" = "$windows_host" ]; then
            echo "$windows_administrators_keys_file"
            return
        fi
    done
}

for host in $hosts; do
    keys_file=$(find_authorized_keys_file "$host")

    if [ "$host" = "$this_mac" ]; then
        result=$(printf '%s\n' "$remove_lines" | sh -s "$key_comment")
    else
        result=$(printf '%s\n' "$remove_lines" | ssh -o BatchMode=yes -o ConnectTimeout="$ssh_timeout_seconds" \
            "$host" "sh -s '$key_comment' '$keys_file'")
    fi
    echo "$host authorized_keys: $result"
done
