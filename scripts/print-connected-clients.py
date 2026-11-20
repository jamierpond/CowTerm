#!/usr/bin/env python3
"""Prints the clients a running daemon has connected (the Mac app, each
`ct bridge`, the phone) with the build stamp each sent in hello, which the Mac
app compares with its own to tell when the phone is out of date. Asks the
daemon only "clients" through `ct bridge`; the bridge asking shows up unnamed.
A daemon from before build stamps does not answer.

usage: scripts/print-connected-clients.py [host]   (no host: this Mac's daemon)
Exit 0 when the daemon answered.
"""
import importlib.util
import os
import sys
import urllib.parse

sys.dont_write_bytecode = True
activity_path = os.path.join(os.path.dirname(__file__), "print-activity.py")
activity_spec = importlib.util.spec_from_file_location("print_activity", activity_path)
print_activity = importlib.util.module_from_spec(activity_spec)
activity_spec.loader.exec_module(print_activity)


def describe_client(line):
    name, _, stamp = line.partition("\t")
    shown_name = urllib.parse.unquote(name) or "(unnamed)"
    shown_stamp = stamp or "no build stamp"
    return f"{shown_name:<16} {shown_stamp}"


def main():
    payload = print_activity.ask_daemon("clients", sys.argv[1:])
    if payload is None:
        print("the daemon did not answer (one from before build stamps never does)", file=sys.stderr)
        return 1
    for line in payload.decode().splitlines():
        print(describe_client(line))
    return 0


if __name__ == "__main__":
    sys.exit(main())
