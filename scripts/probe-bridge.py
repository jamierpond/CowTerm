#!/usr/bin/env python3
"""Checks `ct bridge` end to end: starts it (over ssh, or a local ct), waits
for its hello, opens a shell in the daemon, runs a command and reads the
output back, then kills the shell (or, with --keep, leaves it running).

usage: scripts/probe-bridge.py <host | --local path/to/ct> [--keep]
Exit 0 when the command's output came back through the bridge.
"""
import os
import select
import subprocess
import sys
import time

TIMEOUT_SECONDS = 60
# The same as bridgeCommand in Remote/RemoteLocation.h.
BRIDGE_COMMAND = ('ct="$HOME/.local/libexec/cowterm-remote/ct"; '
                  '[ -x "$ct" ] || [ -x "$ct.exe" ] || ct="$HOME/.local/bin/ct"; '
                  'exec "$ct" bridge')
POLL_SECONDS = 0.5
READ_BYTES = 65536
MARKER = "cowterm-bridge-probe"
EXPECTED = f"{MARKER}-42".encode()


def frame(message: str) -> bytes:
    data = message.encode()
    return str(len(data)).encode() + b"\n" + data


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


class Probe:
    def __init__(self, command, keep):
        self.bridge = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.keep = keep
        self.pane = f"probe{os.getpid()}"
        self.sent_command = False
        self.output = b""

    def send(self, message: str):
        self.bridge.stdin.write(frame(message))
        self.bridge.stdin.flush()

    def handle(self, message: bytes):
        verb, _, payload = message.partition(b"\n")
        if verb.startswith(b"bridge-error"):
            raise RuntimeError("bridge error: " + message.decode(errors="replace"))
        if verb.startswith(b"bridge-hello"):
            print("hello:", verb.decode())
            self.send(f"spawn {self.pane} 80 24 probe\n/tmp")
        if verb.startswith(b"output"):
            self.output += payload
            if not self.sent_command:
                self.send(f"write {self.pane}\necho {MARKER}-$((40+2))\n")
                self.sent_command = True

    def finish(self):
        self.send(("release " if self.keep else "kill ") + self.pane)
        self.bridge.stdin.close()
        self.bridge.wait(timeout=5)
        outcome = "left running" if self.keep else "killed"
        print(f"ok: shell output came back through the bridge (pane {self.pane}, {outcome})")

    def run(self):
        decoder = Decoder()
        deadline = time.time() + TIMEOUT_SECONDS
        while time.time() < deadline:
            ready, _, _ = select.select([self.bridge.stdout], [], [], POLL_SECONDS)
            if not ready:
                continue
            data = os.read(self.bridge.stdout.fileno(), READ_BYTES)
            if not data:
                raise RuntimeError("bridge closed its output")
            for message in decoder.append(data):
                self.handle(message)
            if EXPECTED in self.output:
                return self.finish()
        raise RuntimeError(f"timed out; output so far: {self.output[-400:]!r}")


def main():
    arguments = [a for a in sys.argv[1:] if a != "--keep"]
    if arguments[0] == "--local":
        command = [arguments[1], "bridge"]
    else:
        command = ["ssh", "-o", "BatchMode=yes", arguments[0], BRIDGE_COMMAND]
    try:
        Probe(command, "--keep" in sys.argv).run()
    except RuntimeError as error:
        print(error)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
