#!/bin/sh
# Reaches <user@host>'s session daemon through EmbeddedSshLink, the ssh client
# inside the process that the iPhone will use, instead of the system ssh. Lists
# the host's panes, opens a throwaway pane, runs an echo in it, prints its
# screen from the daemon's state message and kills it. With --adopt [pane id]
# it prints an existing pane's screen instead and releases it untouched.
# Authenticates with $SSH_AUTH_SOCK's agent, else ~/.ssh/id_ecdsa or id_rsa,
# and checks the host key against ~/.ssh/known_hosts.
# PROBE_KEY=<key file> signs with that key through a private ssh-agent that
# lives only for this run, for an ed25519 key: libssh2 on mbedTLS cannot read
# ed25519 key files, but an agent signs for it. Your own agent is untouched.
# usage: [PROBE_KEY=<key file>] scripts/probe-embedded-ssh.sh <user@host[:port]> [--adopt [pane id]]
set -e
build_directory="${BUILD_DIRECTORY:-build}"
cmake --build "$build_directory" --target CowTermEmbeddedSshProbe > /dev/null
probe="$build_directory/Tests/CowTermEmbeddedSshProbe"

if [ -z "$PROBE_KEY" ]; then
    exec "$probe" "$@"
fi

agent_directory="$(mktemp -d)"
trap 'SSH_AGENT_PID="$agent_pid" ssh-agent -k > /dev/null; rm -rf "$agent_directory"' EXIT
eval "$(ssh-agent -a "$agent_directory/agent.sock" -s)" > /dev/null
agent_pid="$SSH_AGENT_PID"
ssh-add -q "$PROBE_KEY" < /dev/null
"$probe" "$@"
