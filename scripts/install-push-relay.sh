#!/usr/bin/env bash
# Installs the push relay on <host> (Linux, systemd) as a user service that
# survives a reboot: builds this checkout's headless ct there, puts the
# official 1Password CLI beside it (signature checked), copies the env file
# of 1Password references, the start script and the unit, and starts it once
# the service-account token is on the host (just push-relay-store-token).
# The daemons and ct already installed there are untouched.
#
#   scripts/install-push-relay.sh <host>
# EACP_SOURCE=<local eacp checkout> builds against that eacp's HEAD instead of
# the pinned one, for an eacp change not yet on jp/develop.
set -euo pipefail
cd "$(dirname "$0")/.."

host="${1:-jamiepond@macprorack}"
op_version="2.42.0"
op_signing_key="3FEF9748469ADBE15DA7CA80AC2D62742012EA22"
remote_build='$HOME/tmp/cowterm-push-relay-build'
relay_folder='$HOME/.local/libexec/cowterm-push-relay'
config_folder='$HOME/.config/cowterm-push-relay'
unit_folder='$HOME/.config/systemd/user'

run_there() {
    ssh -o BatchMode=yes "$host" "bash -lc '$1'"
}

echo "[push-relay] sending this checkout to $host"
git archive --format=tar HEAD | ssh -o BatchMode=yes "$host" \
    "rm -rf $remote_build/src && mkdir -p $remote_build/src && tar -x -C $remote_build/src"

eacp_option=""
if [[ -n "${EACP_SOURCE:-}" ]]; then
    echo "[push-relay] sending eacp from $EACP_SOURCE ($(git -C "$EACP_SOURCE" rev-parse --short HEAD))"
    git -C "$EACP_SOURCE" archive --format=tar HEAD | ssh -o BatchMode=yes "$host" \
        "rm -rf $remote_build/eacp && mkdir -p $remote_build/eacp && tar -x -C $remote_build/eacp"
    eacp_option="-DLOCKSTEP_eacp_SOURCE=$remote_build/eacp"
fi

echo "[push-relay] building ct there"
run_there "set -e
cd $remote_build/src
scripts/with-linux-deps.sh cmake -S . -B ../build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCOWTERM_HEADLESS=ON $eacp_option > ../configure.log
scripts/with-linux-deps.sh cmake --build ../build --target CowTermCli > ../build.log
mkdir -p $relay_folder
cp ../build/Remote/ct $relay_folder/ct.new && mv -f $relay_folder/ct.new $relay_folder/ct
$relay_folder/ct version"

echo "[push-relay] installing 1Password CLI $op_version there"
run_there "set -e
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
scp -q Deploy/cowterm-push-relay.service "$host:.config/systemd/user/cowterm-push-relay.service"
run_there "set -e
chmod 755 $relay_folder/start
systemctl --user daemon-reload
systemctl --user enable cowterm-push-relay.service >/dev/null
if [ -r $config_folder/op-service-account-token ]; then
    systemctl --user restart cowterm-push-relay.service
    sleep 3
    journalctl --user -u cowterm-push-relay -n 5 --no-pager -o cat
else
    echo \"enabled, not started: no 1Password token yet (run just push-relay-store-token on the Mac, then just push-relay-restart)\"
fi"
