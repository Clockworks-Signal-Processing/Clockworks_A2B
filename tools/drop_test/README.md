# Drop test

A procedure and a script to catch the chain dropping a node, and what the nodes saw, on any
bench. It runs the bench console's start-up again and again with the error watch on, and keeps
each run's log and I2C trace. It is the bench check "Catching a drop" in
[testing.md](../../a2bcpp/docs/testing.md), run as a series of short starts.

To hand it to a Claude session on another bench: "follow tools/drop_test/README.md on this
bench's chain".

## What it needs

- The bench console with the error watch (`errors`, `errors report`, the drop report): branch
  `update-4` or later. The console's first line names its build time.
- Python 3.7 or later on the PC that runs the script. Nothing else.

Build the console:
- Windows (USBi): `a2bcpp/tools/build_windows.ps1 Release`; the program is
  `build/a2b-bench-windows/Release/examples/bench/a2b_bench.exe`.
- Pi: `a2bcpp/tools/build_pi.sh Release` in the Debian WSL distro
  ([setup_pi.md](../../a2bcpp/docs/setup_pi.md)), then copy
  `build/a2b-bench-pi/Release/a2b_bench` to the Pi under its own name (e.g.
  `~/a2b_bench_drop`), `chmod +x` it, and make a folder for the traces (`mkdir ~/drop`).

Before a run, check that no other console is using the bench (`tasklist | findstr a2b_bench`
on Windows, `pgrep -a a2b_bench` on the Pi). Don't stop someone else's.

## The console's command line

Start from the bench's usual start-up, add `--trace {trace}`, and change nothing in
discovery's settings, so the runs compare. `{trace}` becomes `cycleNN_trace.txt`.

- **Every node's EEPROM holds its module info:** `--board B --trace {trace} start`.
- **A node with a blank EEPROM** (the log says "sub-node N rejected: no module info"): add
  `"module N AB0312"` (or its board) before `start`; the other nodes still use their EEPROMs.
- **EEPROMs not used:** `--ignore-eeprom`, and `"module N BOARD"` for every node.
- **A node with its own supply** (its A side draws no bias current): `"link N custom"`, and
  `"link N custom inverted"` over an RJ45 cable.

The benches run so far:

| Bench | Console's command line |
|---|---|
| AB0310 over the USBi (Windows) or the reference board (Pi), mixed chain: AB0020, AB0204, AD2437 with its own supply, three bus-powered AD2437 | `--board AB0310` (or `ref`) `--ignore-eeprom --trace {trace} "link 0 custom inverted" "link 2 custom" "module 0 AB0020" "module 2 AB0312" "module 3 AB0312" "module 4 AB0312" "module 5 AB0312" start` |
| Reference board (Pi), five bus-powered AD2437 with AB0312 images, the fifth blank | `--board ref --trace {trace} "module 4 AB0312" start` |

Do one start by hand first (or the script's first cycle) and check that every node is found:
the log lists each `sub-node N: ... chip ID ...`, then "chain up with N sub-node(s)", and
`errors` says "Error watch: on".

## Running it

Ten starts of two minutes each, from the repository root:

```
python tools/drop_test/drop_cycles.py --out drop_runs -- build/a2b-bench-windows/Release/examples/bench/a2b_bench.exe --board AB0310 --trace {trace} start
```

For a Pi, the command line runs over ssh (key login), in the trace folder:

```
python tools/drop_test/drop_cycles.py --out drop_runs -- ssh USER@PI "cd ~/drop && exec ~/a2b_bench_drop --board ref --trace {trace} start"
```

Quote each console argument with spaces in single quotes inside the ssh string
(`'link 2 custom'`). The traces stay on the Pi: `scp 'USER@PI:drop/cycle*_trace.txt' drop_runs/`.

Options: `--cycles` (10), `--idle` seconds after "chain up" (120), `--after` seconds run on
after a drop so the nodes are found again (60), `--stop-on-drop`. Each cycle is about 2.2
minutes, ten about 22. Lines added to `drop_runs/cmds.txt` during a cycle are typed into the
console.

Each cycle: `errors` 5 s after "chain up", idle, then `errors`, `status`, `quit`. A cycle
with a failed access or a drop also gets `errors report`. The script prints a line per cycle
and, at the end, a summary: each cycle's drops with the chip ID at the dropped position, and
the drops per chip. `drop_cycles.py --summary drop_runs` prints it again.

## Reading a drop

A drop in the log:

```
error report at 1736433 ms: the probe for sub-node 5 failed (sub-node 4, 0x09 write, 1 try, 0 ms)
  main node: BECNT 0 (sum 0), INTPND0 0x00, INTPND2 0x08, SWSTAT 0x00
  sub-node 2: BECNT 0 (sum 0), INTPND0 0x80 SRF-CRC, SWSTAT 0x01 FIN, NODE 0xA2 LAST
  sub-node 3: no answer
warning: a2b: failed access: sub-node 3, 0x02 read, 3 tries, 2 ms
  interrupt from sub-node 1: type 5 (SRF missed)
error report at 1736443 ms: sub-node 3 failed the chain check (...); no answer after that either
warning: a2b: sub-node 3 dropped
```

Note for each drop:
- **When:** at start-up (a probe failed right after a node was found), at a probe of the next
  position, or idle (no probe; in the trace, only INTSRC polls before it).
- **The failed access** and its time: about 500 ms a try on the USBi, under 2 ms on the Pi.
- **The dropped node and its board:** position and chip ID (the summary gives both).
- **The node before it:** NODE (0xA_ = LAST: it lost the node after it), INTPND0 (SRF-missed,
  PWRERR, BECOVF), SWSTAT (FIN; FAULT and its code), BECNT sums. A sum is an upper bound, as
  a header-count error mostly counts a CRC error too: compare the nodes by it, and read 0
  against anything else, rather than taking the figure for the number of errors.
- **The late read:** "answered late" or "no answer after that either".
- Which node raised an interrupt, if one did.

`INTPND0 0x80 SRF-CRC` on every node but the last is there after any start, good or not.

In the trace (`a2bcpp/tools/trace_decode.py` decodes it), a failed transfer has status 1
(the 7th field). The time to the next successful read of the dropped node's VENDOR (0x02)
says how long it was gone.

Then move boards or cables and run again, to see whether the drop follows a board, a cable or
a position.

## Results (2026-10-20)

**The cause was the cable lengths**: the short links (0.1 m modules on one board, short patch
cables) were left at the 4 m default, so every node's response cycles were wrong. With each
link's real length (`cable N 0.1`) each site's Pi bench ran ten starts with no drop and no
failed transfer; a routing plan, which rewrites every node's RESPCYCS, also stopped them. The
USBi's mixed chain has not been run with the real lengths yet. What a wrong length looks like,
and the response cycles it gives, are in [hardware
notes](../../a2bcpp/docs/hardware_notes.md#routing-and-audio); the procedure and the result are
the bench check in [testing.md](../../a2bcpp/docs/testing.md#catching-a-drop).

Use the runs below as the signature to compare a new bench against, not as an open question.
Error watch on, BECCTL 0x7F, ten two-minute starts per arrangement unless noted, every link at
the 4 m default.

| Bench and chain | Drops | Dropped node |
|---|---|---|
| USBi, mixed chain, 20 min idle, then three 5-min starts | 1, at start-up | sub-node 3 (11A1EE042316) |
| Pi, mixed chain | 8 of 10 starts | sub-node 3 (11A1EE042316) |
| Pi, mixed chain, sub-nodes 3 and 4 swapped | 5 of 10 (13 drops) | sub-node 3 (9080C924220A) |
| Pi, as above, `link 4 rj45 single` | 8 of 10 (18 drops) | sub-node 3 (9080C924220A) |
| Pi, five bus-powered AD2437 | 4 of 10, all at start-up | sub-node 1 (9080C924220A) |

In every arrangement:
- The dropped node stays at one position whichever board is moved there: in these chains the
  node behind the first two-step link, right after the AD2437 with chip ID 18ACEE042316
  wherever that one sits (own supply at sub-node 2, or bus-powered at sub-node 0).
- At start-up it drops just after the node two positions further on is powered and found
  (the next write fails).
- The node before it has NODE.LAST set, and shows no switch fault, no PWRERR, no BECOVF, and
  BECNT 0 on every node.
- It doesn't answer the late read, and is found again at the next probe, about 0.5 s later.
- No host access comes just before the idle drops.
- Leaving the dropped position's IO7 alone (`link 4 rj45 single`) changes nothing.

That signature — one position, no fault, no bit errors, back within a second — is a wrong
cable length, not a bad board or cable. On a new bench, set the real lengths first and run
again before looking any further.

On another bench, record the chip IDs of every node and their order, so drops can be compared
by board.
