#!/usr/bin/env python3
# Records what a program writes to its terminal, for the TermParser fuzz corpus.
# Runs the command in a pseudo-terminal of the given size, writes the scripted
# keys into the pty's input after the given delays, and saves every byte the
# program wrote, prefixed with the four-byte header Fuzz/ParserFuzzInput.h reads.
#
#   scripts/record-fuzz-corpus.py OUT COLS ROWS 'command' [delaySeconds keys]...
#   scripts/record-fuzz-corpus.py Fuzz/Corpus/vim.bin 80 24 'vim -u NONE' 1 ':q\r'
import os, pty, select, signal, struct, sys, termios, fcntl, time

def readAvailable(fd, until):
    out = b""
    while time.time() < until:
        ready, _, _ = select.select([fd], [], [], 0.05)
        if fd in ready:
            try:
                chunk = os.read(fd, 65536)
            except OSError:
                return out, True
            if not chunk:
                return out, True
            out += chunk
    return out, False

def main():
    outPath, cols, rows, command = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
    steps = list(zip(sys.argv[5::2], sys.argv[6::2]))
    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.execvp("/bin/sh", ["/bin/sh", "-c", command])
    fcntl.ioctl(fd, termios.TIOCSWINSZ, struct.pack("HHHH", rows, cols, 0, 0))
    os.kill(pid, signal.SIGWINCH)
    recorded = b""
    for delay, keys in steps:
        chunk, ended = readAvailable(fd, time.time() + float(delay))
        recorded += chunk
        if ended:
            break
        os.write(fd, keys.encode().decode("unicode_escape").encode("latin-1"))
    chunk, _ = readAvailable(fd, time.time() + 1.5)
    recorded += chunk
    try:
        os.kill(pid, signal.SIGKILL)
    except ProcessLookupError:
        pass
    header = bytes([cols & 0xFF, rows & 0xFF, cols & 0xFF, rows & 0xFF])
    with open(outPath, "wb") as f:
        f.write(header + recorded)
    print(f"{outPath}: {len(recorded)} bytes")

main()
