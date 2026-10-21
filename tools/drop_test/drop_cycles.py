#!/usr/bin/env python3
"""Start the bench console over and over, to catch a chain drop and its error report.

Each cycle starts the console with the given command line, waits for "chain up", runs
`errors` once, leaves the chain idle for --idle seconds, then runs `errors` and `status`,
sends `quit` and starts again. A cycle where the log shows a failed access or a dropped
node runs --after seconds more (the nodes are found again), then saves `errors report` too.

Per cycle, in --out: cycleNN.log (the console's output, with the lines sent marked
"[driver T s] >> ..."). In the console's command line, {trace} becomes cycleNN_trace.txt,
written where the console runs (on a Pi, the remote folder). Lines appended to
<out>/cmds.txt while a cycle runs are sent to the console as well.

At the end it prints, per cycle, the drops and the board (chip ID) at each dropped position.

Usage (everything after "--" is the console's command line):
  drop_cycles.py --out runs -- a2b_bench.exe --board AB0310 --trace {trace} start
  drop_cycles.py --out runs -- ssh USER@HOST "cd ~/drop && exec ~/a2b_bench --board ref --trace {trace} start"
  drop_cycles.py --summary runs        the summary of an earlier run
"""

import argparse
import glob
import os
import re
import subprocess
import sys
import threading
import time

HIT = re.compile(rb"failed access|failed;|dropped$", re.I)
CHAIN_UP = b"chain up with"
FOUND = re.compile(r"^\s+sub-node (\d+): (AD\d+) version \S+, chip ID ([0-9A-F]+)")
DROPPED = re.compile(r"sub-node (\d+) dropped\s*$")
FAILED = re.compile(r"failed access: (.*)$")
START_TIMEOUT_S = 90  # no "chain up" by then: quit the cycle
QUIT_EOF_S = 5        # the Linux console exits on end of input, not on quit alone
QUIT_KILL_S = 15


def RunCycle(n, args):
    """Runs one cycle; returns True if the log showed a failed access or a drop."""
    log_path = os.path.join(args.out, "cycle%02d.log" % n)
    cmds_path = os.path.join(args.out, "cmds.txt")
    trace = "cycle%02d_trace.txt" % n
    cmd = [a.replace("{trace}", trace) for a in args.command]
    open(cmds_path, "w").close()
    t0 = time.time()
    proc = subprocess.Popen(cmd, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, bufsize=0)
    log = open(log_path, "wb", buffering=0)
    state = {"up": None, "hit": None}

    def Reader():
        line = bytearray()
        while True:
            b = proc.stdout.read(1)
            if not b:
                break
            log.write(b)
            line.extend(b)
            if b == b"\n":
                if state["up"] is None and CHAIN_UP in line:
                    state["up"] = time.time()
                if state["hit"] is None and HIT.search(bytes(line).rstrip()):
                    state["hit"] = time.time() - t0
                    print("cycle %d: at %.1f s: %s" % (n, state["hit"],
                          bytes(line).decode(errors="replace").strip()), flush=True)
                line.clear()

    threading.Thread(target=Reader, daemon=True).start()
    log.write(("driver: cycle %d, %s, command line: %s\n"
               % (n, time.strftime("%Y-%m-%d %H:%M:%S"), " ".join(cmd))).encode())

    def Send(text):
        log.write(("\n[driver %.1f s] >> %s\n" % (time.time() - t0, text)).encode())
        proc.stdin.write(text.encode() + b"\r\n")
        proc.stdin.flush()

    pos = 0
    sent_errors = False
    end = None
    quit_at = None
    while proc.poll() is None:
        now = time.time()
        with open(cmds_path, "rb") as f:
            f.seek(pos)
            data = f.read()
        nl = data.rfind(b"\n")
        if nl >= 0 and quit_at is None:
            pos += nl + 1
            for text in data[:nl + 1].splitlines():
                Send(text.decode())
        if state["up"] and not sent_errors and now - state["up"] > 5:
            Send("errors")
            sent_errors = True
        if end is None:
            if state["hit"] is not None:
                end = now + args.after
            elif state["up"] and now - state["up"] > args.idle:
                end = now
            elif not state["up"] and now - t0 > START_TIMEOUT_S:
                end = now
        if end is not None and quit_at is None and now >= end:
            if state["hit"] is not None:
                Send("errors report")
            Send("errors")
            Send("status")
            time.sleep(3)
            Send("quit")
            quit_at = time.time()
            sent_errors = True
        if quit_at is not None:
            if now - quit_at > QUIT_EOF_S and not proc.stdin.closed:
                proc.stdin.close()
            if now - quit_at > QUIT_KILL_S:
                log.write(b"\n[driver] still running after quit: stopped\n")
                proc.terminate()
        time.sleep(0.2)
    time.sleep(0.5)
    log.write(("\n[driver] exited %s after %.1f s\n" % (proc.returncode, time.time() - t0)).encode())
    if not state["up"]:
        print("cycle %d: the chain never came up (see %s)" % (n, log_path), flush=True)
    elif state["hit"] is None:
        print("cycle %d: no drop" % n, flush=True)
    return state["hit"] is not None


def Summary(out):
    """Prints, per cycle log, each drop with the chip at that position, then the totals."""
    per_chip = {}
    total = 0
    for path in sorted(glob.glob(os.path.join(out, "cycle*.log"))):
        chips = {}
        drops = []
        first_failure = None
        with open(path, encoding="utf-8", errors="replace") as f:
            for text in f:
                if "[driver" in text and ">> errors report" in text:
                    break  # the rest repeats the report
                m = FOUND.match(text)
                if m:
                    chips[int(m.group(1))] = "%s %s" % (m.group(2), m.group(3))
                m = FAILED.search(text)
                if m and first_failure is None:
                    first_failure = m.group(1)
                m = DROPPED.search(text)
                if m:
                    node = int(m.group(1))
                    chip = chips.get(node, "?")
                    drops.append("sub-node %d (%s)" % (node, chip))
                    per_chip[chip] = per_chip.get(chip, 0) + 1
        total += len(drops)
        name = os.path.basename(path)
        if drops:
            print("%s: %d drop(s): %s; first failed access: %s"
                  % (name, len(drops), ", ".join(drops), first_failure))
        else:
            print("%s: no drop" % name)
    print("total: %d drop(s)" % total)
    for chip, count in sorted(per_chip.items(), key=lambda kv: -kv[1]):
        print("  %3d  %s" % (count, chip))


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", default="drop_runs", help="folder for the logs (default drop_runs)")
    parser.add_argument("--cycles", type=int, default=10, help="number of starts (default 10)")
    parser.add_argument("--idle", type=float, default=120, help="idle seconds after chain up (default 120)")
    parser.add_argument("--after", type=float, default=60,
                        help="seconds to run on after a drop, for the re-find (default 60)")
    parser.add_argument("--stop-on-drop", action="store_true", help="stop after the first cycle with a drop")
    parser.add_argument("--summary", metavar="DIR", help="only print the summary of DIR's logs")
    parser.add_argument("command", nargs=argparse.REMAINDER, help="-- then the console's command line")
    args = parser.parse_args()
    if args.summary:
        Summary(args.summary)
        return 0
    if args.command and args.command[0] == "--":
        args.command = args.command[1:]
    if not args.command:
        parser.error("give the console's command line after --")
    os.makedirs(args.out, exist_ok=True)
    for n in range(1, args.cycles + 1):
        if RunCycle(n, args) and args.stop_on_drop:
            break
        time.sleep(3)  # let the host interface settle before the next start
    print()
    Summary(args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
