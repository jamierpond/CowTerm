#!/bin/sh
# Pairs the iPhone app with this Mac, tamby and macprorack, so it reaches them
# when opened from the home screen.
#
# The phone gets its own ECDSA P-256 key with no passphrase (the app's mbedTLS
# cannot sign with ed25519). Its public key goes into ~/.ssh/authorized_keys on
# each host, once, marked "cowterm-device:<phone>"; that line is the only
# change made there. The phone gets the host keys this Mac's ~/.ssh/known_hosts
# already trusts for each name it dials. A name known_hosts lacks is scanned
# with ssh-keyscan and accepted only when the key equals one known_hosts holds
# for that machine under another name. The private key, the host list and the
# known hosts are copied into the app's container on the phone, and the private
# key is deleted from this Mac.
#
# Run it again and nothing is added: the phone's key is read back from the
# phone and reused. scripts/unpair-ios-device.sh takes the key off the hosts.
#
#   scripts/pair-ios-device.sh [pond]
set -eu
cd "$(dirname "$0")/.."
phone="${1:-pond}"
key_comment="cowterm-device:$phone"
bundle_id="audio.pond.cowterm.mobile"
pairing_folder_on_phone="Library/Application Support/pond/CowTerm/pairing"
this_mac="jamiepond@big-cow"
# The hosts Jamie approved for the phone on 2026-11-17, by their tailnet names.
hosts="$this_mac jamiepond@tamby jamiepond@macprorack"
ssh_timeout_seconds=10

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir "$work/pairing" "$work/from-phone"
key="$work/pairing/phone-key"

copy_from_phone() {
    xcrun devicectl device copy from --device "$phone" \
        --domain-type appDataContainer --domain-identifier "$bundle_id" \
        --source "$pairing_folder_on_phone/$1" --destination "$2" > /dev/null 2>&1
}

if copy_from_phone phone-key "$work/from-phone/phone-key" && [ -s "$work/from-phone/phone-key" ]; then
    echo "$phone already has a key; reusing it"
    cp "$work/from-phone/phone-key" "$key"
    chmod 600 "$key"
else
    echo "making a new ECDSA P-256 key for $phone"
    ssh-keygen -q -t ecdsa -b 256 -m PEM -N "" -C "$key_comment" -f "$key"
fi

key_type_and_blob=$(ssh-keygen -y -f "$key" | cut -d' ' -f1,2)
authorized_line="$key_type_and_blob $key_comment"
key_blob=$(echo "$key_type_and_blob" | cut -d' ' -f2)

# Appends the line once; a file whose last line has no newline gets one first.
append_once='
    file="$HOME/.ssh/authorized_keys"
    if [ -f "$file" ] && grep -qF "$1" "$file"; then
        echo "already there"
        exit 0
    fi
    mkdir -p "$HOME/.ssh"
    if [ -s "$file" ] && [ -n "$(tail -c 1 "$file")" ]; then
        printf "\n" >> "$file"
    fi
    printf "%s\n" "$2" >> "$file"
    chmod 600 "$file"
    echo "added"'

for host in $hosts; do
    if [ "$host" = "$this_mac" ]; then
        result=$(printf '%s\n' "$append_once" | sh -s "$key_blob" "$authorized_line")
    else
        result=$(printf '%s\n' "$append_once" | ssh -o BatchMode=yes -o ConnectTimeout="$ssh_timeout_seconds" \
            "$host" "sh -s '$key_blob' '$authorized_line'")
    fi
    echo "$host authorized_keys: $result"
done

known_hosts="$work/pairing/known_hosts"
: > "$known_hosts"

# Lines of this Mac's known_hosts for name, without ssh-keygen's comments.
find_known_host_lines() {
    ssh-keygen -F "$1" -f "$HOME/.ssh/known_hosts" 2> /dev/null | grep -v '^#' || true
}

for host in $hosts; do
    name="${host#*@}"
    lines=$(find_known_host_lines "$name")

    if [ -n "$lines" ]; then
        echo "$lines" >> "$known_hosts"
        echo "$name: host keys from known_hosts"
        continue
    fi

    ssh-keyscan -T "$ssh_timeout_seconds" "$name" 2> /dev/null | grep -v '^#' > "$work/scanned" || true

    while read -r scanned_name scanned_type scanned_blob; do
        if grep -qF "$scanned_blob" "$HOME/.ssh/known_hosts"; then
            echo "$scanned_name $scanned_type $scanned_blob" >> "$known_hosts"
            echo "$name: added its $scanned_type key, which known_hosts holds for this machine under another name"
        fi
    done < "$work/scanned"

    if ! grep -q "^$name " "$known_hosts"; then
        echo "$name: no host key this Mac already trusts; the phone will refuse it" >&2
    fi
done

for host in $hosts; do
    echo "$host"
done > "$work/pairing/hosts"

for host in $hosts; do
    if ssh -o BatchMode=yes -o IdentitiesOnly=yes -o IdentityAgent=none -o ConnectTimeout="$ssh_timeout_seconds" \
        -i "$key" "$host" true 2> /dev/null; then
        echo "$host accepts the phone's key"
    else
        echo "$host refuses the phone's key" >&2
    fi
done

xcrun devicectl device copy to --device "$phone" \
    --domain-type appDataContainer --domain-identifier "$bundle_id" \
    --source "$work/pairing/hosts" --source "$work/pairing/known_hosts" --source "$key" \
    --destination "$pairing_folder_on_phone" > /dev/null
echo "copied hosts, known_hosts and phone-key to $phone; the private key is deleted from this Mac"
