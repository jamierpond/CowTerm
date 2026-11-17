#!/bin/sh
# Prints the push relay's address for a daemon: http://<relay host's full
# tailnet name>:8742. The full name, because on the relay host itself the bare
# name resolves to 127.0.1.1 (Ubuntu's own /etc/hosts line), where the relay,
# listening on the tailnet address only, does not answer.
#
#   scripts/find-push-relay-address.sh [macprorack]
set -eu
relay_host="${1:-macprorack}"
port=8742

tailscale status --json 2>/dev/null | python3 -c '
import json, sys
relay_host = sys.argv[1]
status = json.load(sys.stdin)
for peer in [status["Self"], *status.get("Peer", {}).values()]:
    if peer.get("HostName") == relay_host:
        print(peer["DNSName"].rstrip("."))
        break
else:
    sys.exit(f"{relay_host} is not on the tailnet")
' "$relay_host" | sed "s|.*|http://&:$port|"
