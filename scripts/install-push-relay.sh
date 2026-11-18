#!/usr/bin/env bash
# Installs the push relay on <host> (Linux, systemd) as a user service that
# survives a reboot: installs this checkout's headless ct and cowtermd there
# (scripts/install-headless-on-host.sh; the running daemon keeps running),
# puts the official 1Password CLI in the relay's folder (signature checked),
# and copies the env file of 1Password references, the start script and the
# two units. The path unit starts the relay once the service-account token is
# stored (just push-relay-store-token); until then the relay does not run.
#
#   scripts/install-push-relay.sh <host>
set -euo pipefail
cd "$(dirname "$0")/.."

host="${1:-jamiepond@macprorack}"
op_version="2.42.0"
op_signing_key="3FEF9748469ADBE15DA7CA80AC2D62742012EA22"
relay_folder='$HOME/.local/libexec/cowterm-push-relay'
config_folder='$HOME/.config/cowterm-push-relay'
unit_folder='$HOME/.config/systemd/user'

run_there() {
    ssh -o BatchMode=yes "$host" "bash -lc '$1'"
}

echo "[push-relay] installing this checkout's ct and cowtermd on $host"
scripts/install-headless-on-host.sh "$host"

echo "[push-relay] installing 1Password CLI $op_version there"
run_there "set -e
mkdir -p $relay_folder
if [ \"\$($relay_folder/op --version 2>/dev/null)\" != \"$op_version\" ]; then
    download=\$(mktemp -d)
    cd \$download
    curl -sSfLo op.zip https://cache.agilebits.com/dist/1P/op2/pkg/v$op_version/op_linux_amd64_v$op_version.zip
    unzip -q op.zip op op.sig
    export GNUPGHOME=\$download/gnupg
    mkdir -m 700 \$GNUPGHOME
    curl -sSfL https://downloads.1password.com/linux/keys/1password.asc | gpg -q --import
    gpg --status-fd 1 --verify op.sig op 2>/dev/null | grep -q \"VALIDSIG $op_signing_key\"
    install -m 755 op $relay_folder/op
    rm -rf \$download
fi
$relay_folder/op --version"

echo "[push-relay] installing the service"
ssh -o BatchMode=yes "$host" "mkdir -p $config_folder $unit_folder && chmod 700 $config_folder"
scp -q Deploy/push-relay.env "$host:.config/cowterm-push-relay/apns.env"
scp -q scripts/start-push-relay.sh "$host:.local/libexec/cowterm-push-relay/start"
scp -q Deploy/cowterm-push-relay.service Deploy/cowterm-push-relay.path "$host:.config/systemd/user/"
run_there "set -e
chmod 755 $relay_folder/start
rm -f $relay_folder/ct
systemctl --user daemon-reload
systemctl --user enable cowterm-push-relay.service cowterm-push-relay.path >/dev/null 2>&1
systemctl --user start cowterm-push-relay.path
systemctl --user reset-failed cowterm-push-relay.service 2>/dev/null || true
systemctl --user restart cowterm-push-relay.service"
scripts/check-push-relay-ready.sh "$host"
