#!/usr/bin/env python3
"""Walks one throwaway pane through unread and read on a test daemon, never
Jamie's: on <user@host>, the ct and cowtermd that
scripts/build-headless-instance-on-host.sh built in <folder there> run as
COWTERM_INSTANCE=unread-check. The script opens a pane there through `ct
bridge` and types Claude Code's own hook command into it, fed the JSON Claude
Code gives its hooks, so the pane reports itself exactly as a Claude in it
would. After each step it reads the daemon's activity line for the pane and
prints its unread columns: a finished turn is unread, viewing reads it, a
question while not viewed is unread, a prompt submitted reads it, a shell bell
is unread. Then it kills the pane and stops the test daemon.

usage: scripts/check-unread-on-host.py <user@host> <folder there>
Exit 0 when every step showed what it should.
"""
import importlib.util
import json
import os
import select
import subprocess
import sys
import time

INSTANCE = "unread-check"
SESSION = "unread-check"
STEP_SECONDS = 15
POLL_SECONDS = 0.3
READ_BYTES = 65536
ACTIVITY_UNREAD_EVENT_COLUMN = 6
ACTIVITY_UNREAD_TEXT_COLUMN = 8

probe_path = os.path.join(os.path.dirname(__file__), "probe-bridge.py")
probe_spec = importlib.util.spec_from_file_location("probe_bridge", probe_path)
probe_bridge = importlib.util.module_from_spec(probe_spec)
probe_spec.loader.exec_module(probe_bridge)


class UnreadCheck:
    def __init__(self, host, folder):
        self.folder = folder
        bridge = f"COWTERM_INSTANCE={INSTANCE} exec {folder}/bin/ct bridge"
        self.bridge = subprocess.Popen(["ssh", "-o", "BatchMode=yes", host, bridge],
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        self.decoder = probe_bridge.Decoder()
        self.pane = f"unread-check-{os.getpid()}"
        self.received = []

    def send(self, message):
        self.bridge.stdin.write(probe_bridge.frame(message))
        self.bridge.stdin.flush()

    def wait_for(self, verb):
        deadline = time.time() + STEP_SECONDS
        while time.time() < deadline:
            payload = self.take_received(verb)
            if payload is not None:
                return payload
            self.read_available()
        raise RuntimeError(f"no {verb} within {STEP_SECONDS} s")

    def take_received(self, verb):
        for index, (received_verb, payload) in enumerate(self.received):
            if received_verb.startswith(verb):
                del self.received[: index + 1]
                return payload
        return None

    def read_available(self):
        ready, _, _ = select.select([self.bridge.stdout], [], [], POLL_SECONDS)
        if not ready:
            return
        for message in self.decoder.append(os.read(self.bridge.stdout.fileno(), READ_BYTES)):
            verb_line, _, payload = message.partition(b"\n")
            self.received.append((verb_line.decode(), payload.decode(errors="replace")))

    def run_hook(self, hook_input):
        hook = json.dumps(hook_input).replace("'", "'\\''")
        self.send(f"write {self.pane}\nprintf '%s' '{hook}' | {self.folder}/bin/ct pane-state --claude-hook\r")

    def read_unread(self):
        deadline = time.time() + STEP_SECONDS
        while True:
            self.send("activity")
            for line in self.wait_for("activity").splitlines():
                columns = line.split("\t")
                if columns[0] == self.pane and len(columns) > ACTIVITY_UNREAD_TEXT_COLUMN:
                    return columns[ACTIVITY_UNREAD_EVENT_COLUMN], columns[ACTIVITY_UNREAD_TEXT_COLUMN]
            if time.time() > deadline:
                return None, None

    def expect(self, step, event, text=None):
        deadline = time.time() + STEP_SECONDS
        while True:
            unread_event, unread_text = self.read_unread()
            matches = unread_event == event and (text is None or unread_text == text)
            if matches or time.time() > deadline:
                break
            time.sleep(POLL_SECONDS)
        shown = f"unread {unread_event}: {unread_text!r}" if unread_event else "read"
        print(f"{'ok' if matches else 'FAILED'}: {step}: {shown}")
        return matches

    def run(self):
        self.wait_for("bridge-hello")
        self.send(f"spawn {self.pane} 80 24 {SESSION}\n/tmp")
        self.wait_for("output")
        results = []

        self.run_hook({"hook_event_name": "Stop", "last_assistant_message": "Checked.\nAll 420 tests pass."})
        results.append(self.expect("a finished turn nobody watched", "finished", "All 420 tests pass."))

        self.send(f"viewing {self.pane}")
        results.append(self.expect("a client views the pane", ""))

        self.send(f"not-viewing {self.pane}")
        self.run_hook({"hook_event_name": "Notification", "notification_type": "permission_prompt",
                       "message": "Claude needs your permission to use Bash"})
        results.append(self.expect("a question after the client looked away", "waiting",
                                   "Claude needs your permission to use Bash"))

        self.run_hook({"hook_event_name": "UserPromptSubmit", "prompt": "yes"})
        results.append(self.expect("a prompt submitted", ""))

        self.send(f"write {self.pane}\nprintf '\\a'\r")
        results.append(self.expect("a shell bell", "bell"))

        self.send(f"kill {self.pane}")
        self.send("quit-server")
        self.bridge.stdin.close()
        self.bridge.wait(timeout=5)
        return all(results)


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    host, folder = sys.argv[1], sys.argv[2]
    sys.exit(0 if UnreadCheck(host, folder).run() else 1)


if __name__ == "__main__":
    main()
