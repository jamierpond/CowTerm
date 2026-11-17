#!/bin/sh
# Pairs the iPhone app with this Mac, tamby and macprorack, so it reaches them
# when opened from the home screen.
#
# The phone gets its own ECDSA P-256 key with no passphrase (the app's mbedTLS
# cannot sign with ed25519), written by openssl as SEC1 PEM on the named curve:
# mbedTLS reads neither OpenSSH's own format nor the explicit curve parameters
# macOS's ssh-keygen -m PEM writes. Its public key goes into ~/.ssh/authorized_keys on
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

# Rewrites an EC key with explicit curve parameters (from macOS's ssh-keygen -m
# PEM) as the same key on the named curve, which mbedTLS reads.
name_the_curve() {
    python3 -I - "$1" <<'PYTHON'
import base64, sys
path = sys.argv[1]
pem = open(path).read()
der = base64.b64decode("".join(line for line in pem.splitlines() if not line.startswith("-----")))

def read(data, at):
    tag, length = data[at], data[at + 1]
    at += 2
    if length & 0x80:
        count = length & 0x7F
        length = int.from_bytes(data[at:at + count], "big")
        at += count
    return tag, data[at:at + length], at + length

def encode(tag, value):
    if len(value) < 0x80:
        return bytes([tag, len(value)]) + value
    length = len(value).to_bytes((len(value).bit_length() + 7) // 8, "big")
    return bytes([tag, 0x80 | len(length)]) + length + value

_, body, _ = read(der, 0)
fields, at = {}, 0
while at < len(body):
    tag, value, at = read(body, at)
    fields[tag] = value
prime256v1 = bytes.fromhex("06082a8648ce3d030107")
_, public_point, _ = read(fields[0xA1], 0)
named = encode(0x30, encode(0x02, b"\x01") + encode(0x04, fields[0x04]) + encode(0xA0, prime256v1)
               + encode(0xA1, encode(0x03, public_point)))
text = base64.b64encode(named).decode()
lines = [text[i:i + 64] for i in range(0, len(text), 64)]
open(path, "w").write("-----BEGIN EC PRIVATE KEY-----\n" + "\n".join(lines) + "\n-----END EC PRIVATE KEY-----\n")
PYTHON
}

if copy_from_phone phone-key "$work/from-phone/phone-key" && [ -s "$work/from-phone/phone-key" ]; then
    echo "$phone already has a key; reusing it"
    cp "$work/from-phone/phone-key" "$key"
    chmod 600 "$key"

    if ! openssl asn1parse -in "$key" | grep -q prime256v1; then
        name_the_curve "$key"
        echo "rewrote $phone's key on the named curve, which mbedTLS reads; the key is the same"
    fi
else
    echo "making a new ECDSA P-256 key for $phone"
    (umask 077 && openssl ecparam -name prime256v1 -genkey -noout -out "$key")
fi

key_type_and_blob=$(ssh-keygen -y -f "$key" | cut -d' ' -f1,2)
authorized_line="$key_type_and_blob $key_comment"
# libssh2 on mbedTLS cannot derive an ECDSA key's public half, so the phone gets it too.
echo "$authorized_line" > "$key.pub"
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

# The app's own ssh client (libssh2 on mbedTLS) reads keys OpenSSH accepts and
# it does not, so the probe signs in with the phone's files too: it opens a
# throwaway pane on each host, echoes in it and kills it.
cmake --build build --target CowTermEmbeddedSshProbe > /dev/null
mkdir "$work/home" "$work/home/.ssh"
cp "$key" "$work/home/.ssh/id_ecdsa"
cp "$key.pub" "$work/home/.ssh/id_ecdsa.pub"
cp "$work/pairing/known_hosts" "$work/home/.ssh/known_hosts"

for host in $hosts; do
    if env -u SSH_AUTH_SOCK HOME="$work/home" build/Tests/CowTermEmbeddedSshProbe "$host" | grep -q "^killed throwaway pane"; then
        echo "$host: the app's ssh client signs in and reaches the daemon"
    else
        echo "$host: the app's ssh client could not reach the daemon; run scripts/probe-embedded-ssh.sh $host" >&2
    fi
done

xcrun devicectl device copy to --device "$phone" \
    --domain-type appDataContainer --domain-identifier "$bundle_id" \
    --source "$work/pairing/hosts" --source "$work/pairing/known_hosts" --source "$key" --source "$key.pub" \
    --destination "$pairing_folder_on_phone" > /dev/null
echo "copied hosts, known_hosts, phone-key and phone-key.pub to $phone; the private key is deleted from this Mac"
