#!/bin/sh
# Pairs the iPhone app with this Mac, tamby, macprorack and tamby-windows, so it reaches them
# when opened from the home screen.
#
# Pairing again with a new key removes the phone's older keys from each host,
# so no old key stays authorised.
#
# The phone gets its own ECDSA P-256 key with no passphrase (the app's mbedTLS
# cannot sign with ed25519), written by openssl as SEC1 PEM on the named curve:
# mbedTLS reads neither OpenSSH's own format nor the explicit curve parameters
# macOS's ssh-keygen -m PEM writes. Its public key goes into ~/.ssh/authorized_keys
# (on tamby-windows, Windows' administrators_authorized_keys) on each host, once, marked "cowterm-device:<phone>"; that line is the only
# change made there. Each host keeps its name on the phone (tamby), and the
# phone dials the address this Mac's ssh config gives that name (ssh -G; tamby
# is jamies-mac-mini.tail8acf23.ts.net since 2026-11-18), so a renamed machine
# needs only a new pairing. The phone gets the host keys this Mac's
# ~/.ssh/known_hosts already trusts for each address it dials. An address known_hosts lacks is scanned
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
# The hosts Jamie approved for the phone: this Mac, tamby and macprorack on
# 2026-11-17, tamby-windows on 2026-11-18. Each is this Mac's ssh alias.
hosts="$this_mac jamiepond@tamby jamiepond@macprorack jamie@tamby-windows"
# Windows' ssh server reads an administrator's keys only from this file, which
# must stay owned by and readable only to Administrators and SYSTEM.
windows_administrator_hosts="jamie@tamby-windows"
windows_administrators_keys_file="/c/ProgramData/ssh/administrators_authorized_keys"
ssh_timeout_seconds=10
default_ssh_port=22

# What `ssh -G` resolves for host: its "hostname", "user" or "port" line.
read_ssh_setting() {
    ssh -G "$1" 2> /dev/null | awk -v setting="$2" '$1 == setting { print $2; exit }'
}

# The address the phone dials for host: the hostname this Mac's ssh config gives it.
find_address_to_dial() {
    read_ssh_setting "$1" hostname
}

# How the phone's hosts file names host: "user@name" when it dials the name
# itself, else "user@name=address[:port]".
describe_host_for_the_phone() {
    name="${1#*@}"
    user=$(read_ssh_setting "$1" user)
    address=$(find_address_to_dial "$1")
    port=$(read_ssh_setting "$1" port)
    port_suffix=""

    if [ "$port" != "$default_ssh_port" ]; then
        port_suffix=":$port"
    fi

    if [ "$address" = "$name" ]; then
        echo "$user@$name$port_suffix"
    else
        echo "$user@$name=$address$port_suffix"
    fi
}

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

# Removes any other key marked for this phone (an earlier pairing), then appends
# the line once; a file whose last line has no newline gets one first. Prints
# what it did, then how many lines carry the phone's mark afterwards.
append_once='
    file="${4:-$HOME/.ssh/authorized_keys}"
    stale=0
    if [ -f "$file" ]; then
        stale=$(grep " $3\$" "$file" | grep -vcF "$1" || true)
    fi
    if [ "$stale" -gt 0 ]; then
        kept=$(mktemp)
        awk -v mark=" $3" -v blob="$1" "index(\$0, blob) || substr(\$0, length(\$0) - length(mark) + 1) != mark" "$file" > "$kept"
        cat "$kept" > "$file"
        rm -f "$kept"
        printf "removed %s older key(s) for this phone, " "$stale"
    fi
    if [ -f "$file" ] && grep -qF "$1" "$file"; then
        echo "already there; $(grep -c " $3\$" "$file") line(s) for this phone"
        exit 0
    fi
    mkdir -p "$HOME/.ssh"
    if [ -s "$file" ] && [ -n "$(tail -c 1 "$file")" ]; then
        printf "\n" >> "$file"
    fi
    printf "%s\n" "$2" >> "$file"
    if [ -z "${4:-}" ]; then
        chmod 600 "$file"
    fi
    echo "added; $(grep -c " $3\$" "$file") line(s) for this phone"'

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
        result=$(printf '%s\n' "$append_once" | sh -s "$key_blob" "$authorized_line" "$key_comment")
    else
        result=$(printf '%s\n' "$append_once" | ssh -o BatchMode=yes -o ConnectTimeout="$ssh_timeout_seconds" \
            "$host" "sh -s '$key_blob' '$authorized_line' '$key_comment' '$keys_file'")
    fi
    echo "$host authorized_keys: $result"
done

known_hosts="$work/pairing/known_hosts"
: > "$known_hosts"

# libssh2 on mbedTLS, the phone's ssh client, checks RSA and ECDSA host keys
# but not ed25519.
key_types_the_phone_can_check="ssh-rsa ecdsa-sha2-nistp256 ecdsa-sha2-nistp384 ecdsa-sha2-nistp521"

is_a_key_type_the_phone_can_check() {
    for checkable_type in $key_types_the_phone_can_check; do
        if [ "$1" = "$checkable_type" ]; then
            return 0
        fi
    done

    return 1
}

# Whether any known_hosts line given holds a key type the phone can check.
has_a_key_the_phone_can_check() {
    for key_type in $(printf '%s\n' "$1" | awk '{ print $2 }'); do
        if is_a_key_type_the_phone_can_check "$key_type"; then
            return 0
        fi
    done

    return 1
}

# Lines of the phone's known_hosts for name, hashed names included.
find_phone_known_host_lines() {
    ssh-keygen -F "$1" -f "$known_hosts" 2> /dev/null | grep -v '^#' || true
}

# Lines of this Mac's known_hosts for name, without ssh-keygen's comments.
find_known_host_lines() {
    ssh-keygen -F "$1" -f "$HOME/.ssh/known_hosts" 2> /dev/null | grep -v '^#' || true
}

for host in $hosts; do
    name=$(find_address_to_dial "$host")
    lines=$(find_known_host_lines "$name")

    if [ -n "$lines" ]; then
        echo "$lines" >> "$known_hosts"
        echo "$name: host keys from known_hosts"

        if has_a_key_the_phone_can_check "$lines"; then
            continue
        fi

        echo "$name: none of them is a type the phone's ssh client can check; scanning it"
    fi

    ssh-keyscan -T "$ssh_timeout_seconds" "$name" 2> /dev/null | grep -v '^#' > "$work/scanned" || true

    while read -r scanned_name scanned_type scanned_blob; do
        if is_a_key_type_the_phone_can_check "$scanned_type" && grep -qF "$scanned_blob" "$HOME/.ssh/known_hosts"; then
            echo "$scanned_name $scanned_type $scanned_blob" >> "$known_hosts"
            echo "$name: added its $scanned_type key, which known_hosts holds for this machine under another name"
        fi
    done < "$work/scanned"

    if ! has_a_key_the_phone_can_check "$(find_phone_known_host_lines "$name")"; then
        echo "$name: no host key of a type the phone can check that this Mac already trusts; the phone will refuse it" >&2
    fi
done

for host in $hosts; do
    describe_host_for_the_phone "$host"
done > "$work/pairing/hosts"
sed 's/^/the phone dials: /' "$work/pairing/hosts"

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

while read -r host; do
    if env -u SSH_AUTH_SOCK HOME="$work/home" build/Tests/CowTermEmbeddedSshProbe "$host" | grep -q "^killed throwaway pane"; then
        echo "$host: the app's ssh client signs in and reaches the daemon"
    else
        echo "$host: the app's ssh client could not reach the daemon; run scripts/probe-embedded-ssh.sh $host" >&2
    fi
done < "$work/pairing/hosts"

xcrun devicectl device copy to --device "$phone" \
    --domain-type appDataContainer --domain-identifier "$bundle_id" \
    --source "$work/pairing/hosts" --source "$work/pairing/known_hosts" --source "$key" --source "$key.pub" \
    --destination "$pairing_folder_on_phone" > /dev/null
echo "copied hosts, known_hosts, phone-key and phone-key.pub to $phone; the private key is deleted from this Mac"
