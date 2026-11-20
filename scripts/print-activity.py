#!/usr/bin/env python3
"""Prints the activity report a running daemon gives (what the Mac's palette and
the phone sort sessions by): one line per pane with how long ago it was typed
into and how long ago a file last changed in its project. Asks the daemon only
"activity" through `ct bridge`, so nothing in the daemon changes.

usage: scripts/print-activity.py [host]   (no host: this Mac's daemon)
Exit 0 when the daemon answered.
"""
import importlib.util
import os
import select
import subprocess
import sys
import time

TIMEOUT_SECONDS = 30
POLL_SECONDS = 0.3
READ_BYTES = 65536
PANE_COLUMN = 0
LAST_INPUT_COLUMN = 1
STATE_COLUMN = 3
LAST_FILE_CHANGE_COLUMN = 9
MILLISECONDS_PER_SECOND = 1000

sys.dont_write_bytecode = True
probe_path = os.path.join(os.path.dirname(__file__), "probe-bridge.py")
probe_spec = importlib.util.spec_from_file_location("probe_bridge", probe_path)
probe_bridge = importlib.util.module_from_spec(probe_spec)
probe_spec.loader.exec_module(probe_bridge)


def describe_age(milliseconds, now_seconds):
    if milliseconds == 0:
        return "never"
    seconds = now_seconds - milliseconds / MILLISECONDS_PER_SECOND
    return f"{seconds:.0f} s ago"


def read_column(columns, index):
    if index < len(columns) and columns[index]:
        return int(columns[index])
    return 0


def print_report(payload):
    now_seconds = time.time()
    for line in payload.decode().splitlines():
        columns = line.split("\t")
        last_input = read_column(columns, LAST_INPUT_COLUMN)
        last_file_change = read_column(columns, LAST_FILE_CHANGE_COLUMN)
        print(f"{columns[PANE_COLUMN]:<24} {columns[STATE_COLUMN]:<9} "
              f"typed {describe_age(last_input, now_seconds):<14} "
              f"file changed {describe_age(last_file_change, now_seconds)}")


def make_bridge_command(arguments):
    if arguments:
        return ["ssh", "-o", "BatchMode=yes", arguments[0], probe_bridge.BRIDGE_COMMAND]
    return [os.path.expanduser("~/.local/bin/ct"), "bridge"]


def main():
    bridge = subprocess.Popen(make_bridge_command(sys.argv[1:]), stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    decoder = probe_bridge.Decoder()
    deadline = time.time() + TIMEOUT_SECONDS
    try:
        while time.time() < deadline:
            ready, _, _ = select.select([bridge.stdout], [], [], POLL_SECONDS)
            if not ready:
                continue
            for message in decoder.append(os.read(bridge.stdout.fileno(), READ_BYTES)):
                verb, _, payload = message.partition(b"\n")
                if verb.startswith(b"bridge-hello"):
                    bridge.stdin.write(probe_bridge.frame("activity"))
                    bridge.stdin.flush()
                if verb == b"activity":
                    print_report(payload)
                    return 0
        print("the daemon did not answer", file=sys.stderr)
        return 1
    finally:
        bridge.stdin.close()
        bridge.wait(timeout=TIMEOUT_SECONDS)


if __name__ == "__main__":
    sys.exit(main())
