# I2C trace tools

Tools to read and compare I2C traces: the C++ stack's own, and the golden traces captured from
ADI's stack, which the C++ stack was checked against ([testing.md](../docs/testing.md), "Golden
traces").

| File | What it does |
|---|---|
| `trace_decode.py` | Turns a trace into register accesses: bus, target (main node, sub-node *n*, broadcast, peripheral), register, value |
| `trace_diff.py` | Compares two traces of the same scenario: per-phase order, register image and waits |
| `trace_rules_default.txt` | Default allow-list for `trace_diff.py` (each rule has its reason) |
| `test_trace_tools.py` | Tests for the two tools, on synthetic traces: `python a2bcpp/tools/test_trace_tools.py` |
| `run_tests.sh`, `run_tests.ps1` | Build and run the CTest suite ([testing.md](../docs/testing.md)) |
| `build_pi.sh`, `build_windows.ps1`, `toolchain-pi.cmake` | Build the bench console for the Pi and for Windows ([setup_pi.md](../docs/setup_pi.md), [setup_windows.md](../docs/setup_windows.md)) |
| `ram_report.py`, `ram_probe.cpp` | RAM measurement for the RP2040 ([ram.md](../docs/ram.md)) |
| `gen_ss_plus_tables.py` | Generates the SigmaStudio+ loader's tables from ADI's header |
| `docs.ps1` | Doxygen on Windows |

The trace tools need Python 3.7 or later and nothing else.

## 1. The trace format

One line per I2C transfer, written as the transfer ends:

```
I2CT,<seq>,<t_us>,<bus>,<addr>,<W|R|WR>,<status>,<wLen>,<wHex>,<rLen>,<rHex>
```

`addr` is the 7-bit address in hex, `status` 1 is a failed transfer, and the time is in
microseconds from the start of the trace. A file may start with an `I2CT-BEGIN` header line.
`trace_decode.py` accepts a whole console log: it reads only the `I2CT` lines.

## 2. Capture

**The C++ stack** writes traces itself, on every host:
- the bench console: `i2cTrace on FILE` / `i2cTrace off` ([bench console](../docs/bench_console.md)),
  or `--trace FILE` on the command line;
- the minimal example on Linux: `a2b_minimal --trace FILE`;
- on the Pi from VS Code: **Pi bench: Run Trace on Pi** deploys and runs the bench console with
  `--trace traces/boot.txt` (every I2C transfer, from the start), and **Pi bench: Get traces from
  Pi** copies `~/traces` to `build/a2b-bench-pi/traces`.

The bench console has the error watch on from the start, which adds a BECCTL and an INTMSK0
write per node and a BECNT read before each clear ([bench console](../docs/bench_console.md),
`errors`). To compare a trace with a golden, or with an earlier trace, start the console with
`--commands="errors off"` so the traffic is the stack's own; the library's default is off, so
the other programs need nothing.

**The goldens** were captured from ADI's stack, with an I2C trace hook added to its three
platform layers (enabled with `A2B_TRACE_I2C`; the hook is not part of this snapshot, so a new
golden needs ADI's stack with such a hook). For each golden two runs of the same scenario were
recorded, and the pair must compare equal. Name the files `<golden>_<platform>_run<n>.txt`
(e.g. `G13_pi_run1.txt`); pairs that compare equal go in `a2bcpp/tests/golden/`, with a line in
its README.

## 3. Check

```
python a2bcpp/tools/trace_decode.py G13_pi_run1.txt
python a2bcpp/tools/trace_diff.py G13_pi_run1.txt G13_pi_run2.txt
```

The runs don't need to last equally long: only the span both traces cover is compared, and waits
may differ by one interrupt-poll interval (`--jitter-ms`). `trace_diff.py` prints `EQUAL` (exit
status 0) or each difference (exit status 1). A difference that is timing-dependent but
harmless gets a rule in `trace_rules_default.txt`, with its reason; see `trace_diff.py --help`
for the rule format and the timing options.

Hard minimums are rules too, checked in both traces (a break in the reference is only a note).
`delay` measures from a write to the next compared event; `mingap` from the last write matching
one pattern to the next write matching another, whatever comes between (on the same node when
both patterns name the same targets, e.g. `S*` and `S*`). The default rules hold the SUSTAIN
wait (INV and MSTR at least 25 ms after the main node's last soft reset) and the
power-configuration settle (a node's switch closes at least 100 ms after its power configuration
is overridden). A boot trace from before ADI's stack waited out SUSTAIN breaks the SUSTAIN rule;
the goldens keep it. The waits and their rules are listed in [timing.md](../docs/timing.md).

## 4. Reading a decoded trace

`python3 -I a2bcpp/tools/trace_decode.py TRACE` names each access's target:
- `M` is the main node, `S<n>` sub-node n, and `BC` a broadcast;
- `P<n>:<addr>` is sub-node n's peripheral at that I2C address: its EEPROM at 0x50, an
  AB0020's ADAU1761 at 0x39;
- `H:<addr>` is a device on the host's own bus, such as the main AB0020's codec.

What to look for:
- **A node read from its EEPROM:** the marker and the image's first 144 bytes, then its
  groups, at 100 kHz; then its register writes, ending with I2CCFG 0x01 for a 400 kHz image;
  then each peripheral unit, read from the EEPROM and written to the peripheral in one
  transfer.
- **A codec program's time:** from the node's first `P<n>:39` write to the start of the
  transfer after its last. **Per-byte rates:** from transfers of 1000 bytes or more, start to
  the next transfer's start (each transfer also has a fixed cost: about 8 ms behind a USBi).
- **Failures that are expected:** an AB0204 has no EEPROM, so its search at 0x50-0x54 fails
  five times, and the console then gives it the AB0204's default image.
- **Interrupts:** INTTYPE 0xFF (main node running) and 0x18 (discovery done) are normal, and
  so is 0x0C (open line) from the last node of a chain behind an AD242x
  ([hardware notes](../docs/hardware_notes.md#open-line-inttype-12-and-13)).
- **Routing:** the slot registers on each node, then CONTROL 0x81 (NEWSTRCT); on AD243x nodes,
  TXXBAR set to 0x1F first (the local mute), then the real crossbar.
- **A trace taken with the EEPROMs ignored** (`ignoreEEPROM 1`) shows the console's image and
  no EEPROM reads. `--trace FILE` and `i2cTrace on FILE` start FILE afresh: give each run its
  own name.
