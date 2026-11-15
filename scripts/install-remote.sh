#!/bin/sh
# Installs or updates CowTerm's headless half (ct + cowtermd) on a remote host.
# usage: scripts/install-remote.sh <host>    (anything ssh accepts)
set -e

if [ $# -ne 1 ]; then
    echo "usage: $0 <host>" >&2
    exit 2
fi

exec ssh -o BatchMode=yes "$1" bash -l -s < "$(dirname "$0")/remote-bootstrap.sh"
