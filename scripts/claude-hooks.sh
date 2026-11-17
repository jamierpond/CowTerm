#!/usr/bin/env bash
# Adds or removes the Claude Code hooks that tell a CowTerm pane's daemon what
# Claude is doing (ct pane-state --claude-hook), in ~/.claude/settings.json on
# this machine or, given user@host, on that one. Only entries running that exact
# command are touched; every other hook stays as it is. Running add twice adds nothing.
set -euo pipefail

action="${1:-}"
host="${2:-}"

if [[ "$action" != add && "$action" != remove ]]; then
    echo "usage: scripts/claude-hooks.sh add|remove [user@host]" >&2
    exit 2
fi

read -r -d '' edit_settings <<'PYTHON' || true
import json, os, sys

action = sys.argv[1]
command = "ct pane-state --claude-hook 2>/dev/null || true"
timeout_seconds = 5
events = {
    "UserPromptSubmit": None,
    "Notification": "permission_prompt|elicitation_dialog",
    "Stop": None,
}

path = os.path.expanduser("~/.claude/settings.json")
settings = json.load(open(path)) if os.path.exists(path) else {}
hooks = settings.setdefault("hooks", {})

def is_ours(group):
    return any(hook.get("command") == command for hook in group.get("hooks", []))

for event, matcher in events.items():
    groups = [group for group in hooks.get(event, []) if not is_ours(group)]

    if action == "add":
        group = {"hooks": [{"type": "command", "command": command, "timeout": timeout_seconds}]}
        if matcher:
            group = {"matcher": matcher, **group}
        groups.append(group)

    if groups:
        hooks[event] = groups
    else:
        hooks.pop(event, None)

real_path = os.path.realpath(path)
temporary = real_path + ".cowterm-hooks"
with open(temporary, "w") as file:
    json.dump(settings, file, indent=2)
    file.write("\n")
os.replace(temporary, real_path)
print(f"{action}: ct pane-state --claude-hook on UserPromptSubmit, Notification and Stop in {real_path}")
PYTHON

if [[ -z "$host" ]]; then
    python3 -c "$edit_settings" "$action"
else
    ssh "$host" python3 - "$action" <<<"$edit_settings"
fi
