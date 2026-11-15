#!/usr/bin/env python3
"""Checks `ct bridge` end to end: starts it (over ssh, or a local ct), waits
for its hello, opens a shell in the daemon, runs a command and reads the
output back, then kills the shell (or, with --keep, leaves it running).

usage: scripts/probe-bridge.py <host | --local path/to/ct> [--keep]
                               [--command <program> --directory <dir>]
Exit 0 when the command's output came back through the bridge.

With --command, opens a command pane instead (as Ctrl+A i does) in --directory
and passes when that program draws something there, or when the daemon says
it isn't installed (printed either way).
"""
import os
import select
import subprocess
import sys
import time

TIMEOUT_SECONDS = 60
# The same as bridgeCommand in Lib/Multiplexer/RemoteLocation.h.
BRIDGE_COMMAND = ('ct="$HOME/.local/libexec/cowterm-remote/ct"; '
                  '[ -x "$ct" ] || [ -x "$ct.exe" ] || ct="$HOME/.local/bin/ct"; '
                  'exec "$ct" bridge')
POLL_SECONDS = 0.5
READ_BYTES = 65536
MARKER = "cowterm-bridge-probe"
COMMAND_OUTPUT_BYTES_TO_PASS = 2000
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


class CommandProbe:
    """Opens a command pane (the program, in a directory) through the bridge."""

    def __init__(self, command, program, directory):
        self.bridge = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.program = program
        self.directory = directory
        self.pane = f"probecommand{os.getpid()}"
        self.output = b""

    def send(self, message: str):
        self.bridge.stdin.write(frame(message))
        self.bridge.stdin.flush()

    def run(self):
        decoder = Decoder()
        deadline = time.time() + TIMEOUT_SECONDS
        while time.time() < deadline:
            ready, _, _ = select.select([self.bridge.stdout], [], [], POLL_SECONDS)
            if not ready:
                continue
            for message in decoder.append(os.read(self.bridge.stdout.fileno(), READ_BYTES)):
                verb, _, payload = message.partition(b"\n")
                if verb.startswith(b"bridge-hello"):
                    self.send(f"spawn {self.pane} 120 40 probe\n{self.directory}\n{self.program}")
                if verb.startswith(b"missing-command"):
                    print(f"missing: {payload.decode()} is not installed there")
                    return self.finish()
                if verb.startswith(b"output"):
                    self.output += payload
                if verb.startswith(b"exit"):
                    raise RuntimeError("the command exited: " + repr(self.output[-300:]))
            if len(self.output) > COMMAND_OUTPUT_BYTES_TO_PASS:
                print(f"ok: {self.program} is drawing in {self.directory}")
                print("last screen text:", repr(self.output[-200:]))
                return self.finish()
        raise RuntimeError(f"timed out; output so far: {self.output[-400:]!r}")

    def finish(self):
        self.send(f"kill {self.pane}")
        self.bridge.stdin.close()
        self.bridge.wait(timeout=5)


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
                # 4'2' prints as 42 in sh and PowerShell alike, while the echoed
                # command line itself never contains the expected text.
                self.send(f"write {self.pane}\necho {MARKER}-4'2'\r")
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


def read_option(arguments, name):
    if name not in arguments:
        return None
    index = arguments.index(name)
    value = arguments[index + 1]
    del arguments[index:index + 2]
    return value


def main():
    arguments = [a for a in sys.argv[1:] if a != "--keep"]
    program = read_option(arguments, "--command")
    directory = read_option(arguments, "--directory")
    if arguments[0] == "--local":
        command = [arguments[1], "bridge"]
    else:
        command = ["ssh", "-o", "BatchMode=yes", arguments[0], BRIDGE_COMMAND]
    try:
        if program:
            CommandProbe(command, program, directory or "/tmp").run()
        else:
            Probe(command, "--keep" in sys.argv).run()
    except RuntimeError as error:
        print(error)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
