#!/usr/bin/env python3
"""Resizes a pane running Claude Code 150 -> 49 -> 150 columns through the
daemon protocol and reports what its scrollback holds afterwards: whether
Claude's frames were printed again into history at each width.

It starts its own daemon under a separate instance name ($COWTERM_INSTANCE),
so the user's daemon, panes and saved state are never touched, opens one pane
there, and quits that daemon at the end.

usage: scripts/resize-scrollback-test.py [--build build] [--program claude|claude-inline|vim]
                                         [--out report-dir] [--directory dir] [--control]
Exit 0 when the test ran to the end (the finding is in the report either way).
"""
import argparse
import json
import os
import select
import subprocess
import sys
import tempfile
import time

INSTANCE = "resize-scrollback-test"
PANE = f"resizetest{os.getpid()}"
DESKTOP_COLUMNS = 150
PHONE_COLUMNS = 49
ROWS = 40
MARKER_COUNT = 60
MARKER_PREFIX = "RESIZE-MARKER-"
MARKER_TO_COUNT = f"{MARKER_PREFIX}{MARKER_COUNT // 2:03d}"
CLAUDE_PROMPT = (f"Reply with exactly the lines {MARKER_PREFIX}001 through "
                 f"{MARKER_PREFIX}{MARKER_COUNT:03d}, one per line, nothing else.")
CLAUDE_COMMAND = f"claude --tools '' --model haiku \"{CLAUDE_PROMPT}\"\r"
# The same, drawn inline (into scrollback) instead of on the alternate screen.
CLAUDE_INLINE_COMMAND = (f"claude --settings '{{\"tui\": \"default\"}}' --tools '' --model haiku "
                         f"\"{CLAUDE_PROMPT}\"\r")
VIM_COMMAND = f"seq -f '{MARKER_PREFIX}%03g' 1 {MARKER_COUNT}; vim -u NONE /etc/hosts\r"
COMMANDS = {"claude": CLAUDE_COMMAND, "claude-inline": CLAUDE_INLINE_COMMAND, "vim": VIM_COMMAND}
DAEMON_START_SECONDS = 2.0
POLL_SECONDS = 0.25
READ_BYTES = 65536
QUIET_SECONDS_BEFORE_SETTLED = 6.0
PROGRAM_START_TIMEOUT_SECONDS = 240
STATE_TIMEOUT_SECONDS = 30
TRUST_PROMPT_TEXT = b"Yes, I trust this folder"


def frame(message: bytes) -> bytes:
    return str(len(message)).encode() + b"\n" + message


class Decoder:
    def __init__(self):
        self.buffer = b""

    def append(self, data: bytes):
        self.buffer += data
        messages = []
        while b"\n" in self.buffer:
            line, rest = self.buffer.split(b"\n", 1)
            length = int(line)
            if len(rest) < length:
                break
            messages.append(rest[:length])
            self.buffer = rest[length:]
        return messages


class Bridge:
    """One `ct bridge` conversation with the test instance's daemon."""

    def __init__(self, ct, environment):
        self.process = subprocess.Popen([ct, "bridge"], stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, env=environment)
        self.decoder = Decoder()
        self.pending = []

    def send(self, message: str, payload: bytes = b""):
        body = message.encode()
        if payload:
            body += b"\n" + payload
        self.process.stdin.write(frame(body))
        self.process.stdin.flush()

    def receive(self, timeout):
        """Messages that arrived within timeout seconds, as (verb line, payload)."""
        ready, _, _ = select.select([self.process.stdout], [], [], timeout)
        if not ready:
            return []
        data = os.read(self.process.stdout.fileno(), READ_BYTES)
        if not data:
            raise RuntimeError("the bridge closed its output")
        received = []
        for message in self.decoder.append(data):
            verb, _, payload = message.partition(b"\n")
            received.append((verb.decode(errors="replace"), payload))
        return received

    def wait_for(self, prefix, timeout):
        deadline = time.time() + timeout
        while time.time() < deadline:
            for verb, payload in self.receive(POLL_SECONDS):
                if verb.startswith(prefix):
                    return verb, payload
        raise RuntimeError(f"no '{prefix}' within {timeout} s")

    def close(self):
        self.process.stdin.close()
        self.process.wait(timeout=5)


def line_text(line):
    return "".join(run["text"] for run in line["runs"]).rstrip()


def describe_screen(state_json: bytes):
    screen = json.loads(state_json)["screen"]
    scrollback = [line_text(line) for line in screen["scrollback"]]
    visible_grid = "alternate" if screen["modes"]["altScreen"] else "primary"
    visible = [line_text(line) for line in screen[visible_grid]]
    return {
        "columns": screen["columns"],
        "rows": screen["rows"],
        "alternateScreen": screen["modes"]["altScreen"],
        "scrollbackLines": len(scrollback),
        "markerInScrollback": sum(MARKER_TO_COUNT in line for line in scrollback),
        "markerOnScreen": sum(MARKER_TO_COUNT in line for line in visible),
        "promptInScrollback": sum(MARKER_PREFIX + "001 through" in line for line in scrollback),
        "text": scrollback + ["----- screen -----"] + visible,
    }


def capture_state(ct, environment):
    """Adopts the pane from a second client, as the phone would, and returns
    the daemon's state of it. Adopting nudges the pane's height by one row and
    back, exactly as every real adopt does."""
    bridge = Bridge(ct, environment)
    bridge.wait_for("bridge-hello", STATE_TIMEOUT_SECONDS)
    bridge.send(f"spawn {PANE} {DESKTOP_COLUMNS} {ROWS} resize-test")
    _, payload = bridge.wait_for("state", STATE_TIMEOUT_SECONDS)
    bridge.send(f"release {PANE}")
    bridge.close()
    return describe_screen(payload)


def read_pane_output(bridge):
    output = b""
    for verb, payload in bridge.receive(POLL_SECONDS):
        if verb.startswith("output"):
            output += payload
        if verb.startswith("exit"):
            raise RuntimeError("the pane's program exited")
    return output


def wait_until_quiet(bridge, timeout):
    deadline = time.time() + timeout
    last_output = time.time()
    seen = b""
    while time.time() < deadline:
        output = read_pane_output(bridge)
        if output:
            last_output = time.time()
            seen += output
        elif time.time() - last_output > QUIET_SECONDS_BEFORE_SETTLED:
            return seen
    raise RuntimeError("the pane never went quiet")


def wait_until_program_has_drawn(bridge, timeout):
    _, first_output = bridge.wait_for("output", timeout)
    return first_output + wait_until_quiet(bridge, timeout)


def write_report(out_dir, program, snapshots):
    os.makedirs(out_dir, exist_ok=True)
    summary = []
    for name, snapshot in snapshots:
        with open(os.path.join(out_dir, f"{name}.txt"), "w") as file:
            file.write("\n".join(snapshot["text"]) + "\n")
        counts = {key: value for key, value in snapshot.items() if key != "text"}
        summary.append({"snapshot": name, **counts})
    with open(os.path.join(out_dir, "summary.json"), "w") as file:
        json.dump({"program": program, "snapshots": summary}, file, indent=1)
    for row in summary:
        print(json.dumps(row))
    print(f"report: {out_dir}")


def run(arguments):
    environment = dict(os.environ, COWTERM_INSTANCE=INSTANCE)
    ct = os.path.join(arguments.build, "Remote", "ct")
    daemon = subprocess.Popen([os.path.join(arguments.build, "Daemon", "CowTermDaemon")],
                              env=environment)
    time.sleep(DAEMON_START_SECONDS)
    working_directory = arguments.directory or tempfile.mkdtemp(prefix="cowterm-resize-test-")
    bridge = Bridge(ct, environment)
    try:
        bridge.wait_for("bridge-hello", STATE_TIMEOUT_SECONDS)
        bridge.send(f"spawn {PANE} {DESKTOP_COLUMNS} {ROWS} resize-test",
                    working_directory.encode())
        bridge.wait_for("output", STATE_TIMEOUT_SECONDS)
        wait_until_quiet(bridge, STATE_TIMEOUT_SECONDS)
        bridge.send(f"write {PANE}", COMMANDS[arguments.program].encode())
        started = wait_until_program_has_drawn(bridge, PROGRAM_START_TIMEOUT_SECONDS)
        if TRUST_PROMPT_TEXT in started:
            raise RuntimeError("Claude asks to trust the folder; pass --directory with a trusted one")

        snapshots = [("1-desktop-before", capture_state(ct, environment))]
        wait_until_quiet(bridge, STATE_TIMEOUT_SECONDS)
        if not arguments.control:
            bridge.send(f"resize {PANE} {PHONE_COLUMNS} {ROWS}")
        wait_until_quiet(bridge, STATE_TIMEOUT_SECONDS)
        snapshots.append(("2-phone", capture_state(ct, environment)))
        wait_until_quiet(bridge, STATE_TIMEOUT_SECONDS)
        if not arguments.control:
            bridge.send(f"resize {PANE} {DESKTOP_COLUMNS} {ROWS}")
        wait_until_quiet(bridge, STATE_TIMEOUT_SECONDS)
        snapshots.append(("3-desktop-after", capture_state(ct, environment)))
        write_report(arguments.out, arguments.program, snapshots)
    finally:
        bridge.send(f"kill {PANE}")
        bridge.send("quit-server")
        bridge.close()
        daemon.wait(timeout=10)
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", default="build")
    parser.add_argument("--program", default="claude", choices=sorted(COMMANDS))
    parser.add_argument("--out", default="build/resize-scrollback-test")
    # Takes the same snapshots without resizing, to tell the resizes' effect
    # from the one-row nudge each adopt makes.
    parser.add_argument("--control", action="store_true")
    # Claude asks whether to trust a new folder; pass one it already trusts.
    parser.add_argument("--directory")
    try:
        return run(parser.parse_args())
    except RuntimeError as error:
        print(error)
        return 1


if __name__ == "__main__":
    sys.exit(main())
