#!/usr/bin/env python3
"""Tests for trace_decode.py and trace_diff.py on synthetic traces.

Run from anywhere:  python a2bcpp/tools/test_trace_tools.py
"""

import contextlib
import io
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import trace_decode as td  # noqa: E402
import trace_diff  # noqa: E402

MAIN = 0x68


class TraceBuilder:
    """Writes I2CT lines in the format of a2bcpp/tools/README.md."""

    def __init__(self):
        self.lines = ["A2B> i2cTrace dump\r\n", "I2CT-BEGIN,1,0,0,0,4194304\r\n"]
        self.t = 0
        self.seq = 0

    def _add(self, addr, op, w=b"", r=b"", r_len=None, failed=False, bus=0, dt=100):
        self.t += dt
        self.lines.append("I2CT,%d,%d,%d,%02X,%s,%d,%d,%s,%d,%s\r\n" % (
            self.seq, self.t, bus, addr, op, 1 if failed else 0, len(w), w.hex().upper(),
            len(r) if r_len is None else r_len, r.hex().upper()))
        self.seq += 1
        return self

    def wm(self, reg, *vals, **kw):          # main-node register write
        return self._add(MAIN, "W", bytes([reg, *vals]), **kw)

    def wb(self, reg, *vals, **kw):          # bus-address register write
        return self._add(MAIN + 1, "W", bytes([reg, *vals]), **kw)

    def rm(self, reg, *vals, **kw):          # main-node register read
        return self._add(MAIN, "WR", bytes([reg]), bytes(vals), **kw)

    def raw(self, addr, op, w=b"", r=b"", **kw):
        return self._add(addr, op, w, r, **kw)

    def text(self):
        return "".join(self.lines + ["I2CT-END\r\n"])


def boot(extra_becnt=False, probes=2, swctl=0x21, reset_wait_us=30000):
    """A small boot: reset, configure node 0, read its EEPROM, then blind probes."""
    b = TraceBuilder()
    b.wm(0x12, 0x84)                                  # CONTROL: SOFTRST
    b.wm(0x01, 0x00, dt=reset_wait_us)                # NODEADR node 0
    b.wb(0x09, swctl, 0x07)                           # S0 SWCTL, BCDNSLOTS
    b.rm(0x15, 0x00)                                  # INTSTAT poll
    if extra_becnt:
        b.wm(0x01, 0x80).wb(0x1F, 0x00).wm(0x01, 0x00)
    b.wb(0xE0, 0x01).wb(0x00, 0x03).wb(0xE0, 0x00)    # S0 VMTR_VEN via MMRPAGE 1
    b.wb(0x00, 0x50).wm(0x01, 0x20)                   # S0 CHIP 0x50, NODEADR PERI
    b.raw(MAIN + 1, "WR", b"\x00", bytes(range(8)))   # EEPROM read
    for _ in range(probes):
        b.wm(0x13, 0x7C).wm(0x09, 0x01, dt=3000)      # DISCVRY, then SWCTL 3 ms later
    return b.text()


class Files:
    def __init__(self):
        self.dir = tempfile.TemporaryDirectory()

    def write(self, name, text):
        path = os.path.join(self.dir.name, name)
        with open(path, "w", newline="") as f:
            f.write(text)
        return path


def run_diff(*args):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        rc = trace_diff.main(list(args))
    return rc, out.getvalue()


class DecodeTest(unittest.TestCase):
    def setUp(self):
        self.files = Files()

    def events(self, text):
        return td.load(self.files.write("t.log", text)).events

    def test_addressing(self):
        ev = self.events(boot())
        got = [(e.target, e.op, e.reg_name(), e.value) for e in ev if e.reg is not None]
        self.assertIn(("M", "W", "CONTROL", 0x84), got)
        self.assertIn(("S0", "W", "SWCTL", 0x21), got)
        self.assertIn(("S0", "W", "BCDNSLOTS", 0x07), got)   # auto-increment
        self.assertIn(("M", "R", "INTSTAT", 0x00), got)
        self.assertIn(("S0", "W", "VMTR_VEN", 0x03), got)    # paged: 0x100
        self.assertIn(("S0", "W", "CHIP", 0x50), got)
        raw = [e for e in ev if e.reg is None]
        self.assertEqual(raw[0].target, "P0:50")
        self.assertEqual(raw[0].r_data, bytes(range(8)))

    def test_broadcast_and_host_device(self):
        b = TraceBuilder().wm(0x01, 0x80).wb(0x1F, 0x00).raw(0x38, "W", b"\x40\x00\x01")
        ev = self.events(b.text())
        self.assertEqual((ev[1].target, ev[1].reg_name()), ("BC", "BECNT"))
        self.assertEqual(ev[2].target, "H:38")

    def test_main_address_inferred(self):
        global MAIN
        MAIN = 0x6A
        try:
            ev = self.events(boot())
        finally:
            MAIN = 0x68
        self.assertEqual(ev[0].target, "M")
        self.assertIn("S0", {e.target for e in ev})

    def test_truncated_and_failed(self):
        b = TraceBuilder().raw(0x38, "W", bytes(64), r_len=0, failed=True)
        b.lines[-1] = b.lines[-1].replace(",64,", ",100,", 1)   # 100 bytes sent, 64 kept
        ev = self.events(b.text())
        self.assertTrue(ev[0].truncated and ev[0].failed)


class DiffTest(unittest.TestCase):
    def setUp(self):
        self.files = Files()
        self.ref = self.files.write("ref.log", boot())

    def test_equal_despite_becnt_and_probe_count(self):
        new = self.files.write("new.log", boot(extra_becnt=True, probes=5))
        rc, out = run_diff(self.ref, new)
        self.assertEqual(rc, 0, out)
        self.assertIn("EQUAL", out)

    def test_strict_repeats(self):
        new = self.files.write("new.log", boot(probes=5))
        rc, out = run_diff("--strict-repeats", self.ref, new)
        self.assertEqual(rc, 1, out)

    def test_value_change(self):
        new = self.files.write("new.log", boot(swctl=0x23))
        rc, out = run_diff(self.ref, new)
        self.assertEqual(rc, 1)
        self.assertIn("reference 0x21, new 0x23", out)

    def test_shorter_wait(self):
        ref = self.files.write("ref100.log", boot(reset_wait_us=100000))
        new = self.files.write("new.log", boot(reset_wait_us=40000))
        rc, out = run_diff(ref, new)
        self.assertEqual(rc, 1)
        self.assertIn("reference 100.1 ms", out)

    def test_polling_jitter_allowed(self):
        new = self.files.write("new.log", boot(reset_wait_us=10000))   # 30 ms -> 10 ms
        rc, out = run_diff(self.ref, new)
        self.assertEqual(rc, 0, out)

    def test_trace_that_stops_early(self):
        full = self.files.write("full.log", boot(probes=4))
        lines = boot().splitlines(keepends=True)
        cut = lines[:7] + lines[-1:]           # stops in the SOFTRST phase, after S0 VMTR_VEN
        short = self.files.write("short.log", "".join(cut))
        for a, b in ((full, short), (short, full)):
            rc, out = run_diff(a, b)
            self.assertEqual(rc, 0, out)
            self.assertIn("compared that far", out)
            self.assertIn("not compared", out)
        wrong = self.files.write("wrong.log", "".join(cut).replace(",69,W,0,3,092107,", ",69,W,0,3,092307,"))
        self.assertEqual(run_diff(full, wrong)[0], 1)     # a real difference is still found

    def test_stops_during_repeated_probes(self):
        full = self.files.write("full.log", boot(probes=4))
        cut = boot(probes=2).splitlines(keepends=True)
        del cut[-2]                                          # the last probe stops part way
        short = self.files.write("short.log", "".join(cut))
        self.assertEqual(run_diff(full, short)[0], 0)
        self.assertEqual(run_diff(short, full)[0], 0)

    def test_delay_rule(self):
        rules = self.files.write("r.txt", "delay M CONTROL=0x84 50   # reset needs 50 ms\n")
        slow = self.files.write("slow.log", boot(reset_wait_us=60000))
        rc, out = run_diff("--rules", rules, self.ref, slow)
        self.assertEqual(rc, 0, out)                         # the reference only gets a note
        self.assertIn("reference breaks a delay rule", out)
        rc, out = run_diff("--rules", rules, slow, self.ref)
        self.assertEqual(rc, 1)                              # the new trace must keep the rule

    def test_mingap_rule(self):
        # Two soft resets 12.6 ms apart, then INV and MSTR: timed from the second one.
        def start(wait_us):
            b = TraceBuilder().wm(0x12, 0x04).wm(0x12, 0x04, dt=12600)
            b.wm(0x41, 0x80, dt=wait_us).wm(0x12, 0x80).rm(0x16, 0x80, dt=25000)
            return b.text()
        good = self.files.write("good.log", start(250000))
        early = self.files.write("early.log", start(100))       # INV and MSTR inside SUSTAIN
        rc, out = run_diff(good, good)
        self.assertEqual(rc, 0, out)
        rc, out = run_diff(good, early)
        self.assertEqual(rc, 1)
        self.assertIn("trace_rules_default.txt", out)
        self.assertEqual(out.count("rule trace_rules_default.txt"), 2)  # INV and MSTR
        rc, out = run_diff(early, good)
        self.assertEqual(rc, 0, out)                          # the reference only gets a note
        self.assertIn("reference breaks a delay rule", out)
        # A later CONTROL write with MSTR (e.g. NEWSTRCT) long after the reset is fine, and
        # so is a reset with MSTR in the same write.
        b = TraceBuilder().wm(0x12, 0x84).wm(0x12, 0x91, dt=30000)
        self.assertEqual(run_diff(*[self.files.write("m.log", b.text())] * 2)[0], 0)
        # Mask and value patterns in a rules file.
        rules = self.files.write("r.txt", "mingap M CONTROL=0x04 M SWCTL&0x01 5\n")
        b = TraceBuilder().wm(0x12, 0x04).wm(0x09, 0x10).wm(0x09, 0x11)
        slow = TraceBuilder().wm(0x12, 0x04).wm(0x09, 0x10).wm(0x09, 0x11, dt=6000)
        fast, ok = self.files.write("f.log", b.text()), self.files.write("s.log", slow.text())
        self.assertEqual(run_diff("--rules", rules, ok, fast)[0], 1)
        self.assertEqual(run_diff("--rules", rules, ok, ok)[0], 0)

    def test_mingap_per_node(self):
        # The power-configuration settle: 100 ms from a node's override (SWCTL 0x12) to its
        # switch closing (ENSW), timed per node when both patterns are the same target.
        def run(settle_us):
            b = TraceBuilder().wm(0x12, 0x84)
            b.wm(0x01, 0x00).wb(0x09, 0x12)                   # sub-node 0: override
            b.wm(0x01, 0x01).wb(0x09, 0x11, dt=1000)          # sub-node 1: its own switch
            b.wm(0x01, 0x00).wb(0x09, 0x13, dt=settle_us)     # sub-node 0: switch on
            return b.text()
        good = self.files.write("good.log", run(100000))
        early = self.files.write("early.log", run(50000))
        self.assertEqual(run_diff(good, good)[0], 0)             # sub-node 1 isn't timed
        rc, out = run_diff(good, early)
        self.assertEqual(rc, 1)
        self.assertIn("rule trace_rules_default.txt", out)

    def test_unordered_and_ignore_rules(self):
        b = TraceBuilder().wm(0x12, 0x84).wm(0x1B, 0x01).wm(0x1C, 0x02)
        c = TraceBuilder().wm(0x12, 0x84).wm(0x1C, 0x02).wm(0x1B, 0x01)
        ref, new = self.files.write("a.log", b.text()), self.files.write("b.log", c.text())
        self.assertEqual(run_diff(ref, new)[0], 1)
        rules = self.files.write("r.txt", "unordered M INTMSK0\n")
        self.assertEqual(run_diff("--rules", rules, ref, new)[0], 0)
        rules = self.files.write("r2.txt", "ignore M INTMSK0 W\n")
        self.assertEqual(run_diff("--rules", rules, ref, new)[0], 0)

    def test_reads_compared_on_request(self):
        b = TraceBuilder().wm(0x12, 0x84).rm(0x15, 0x00)
        c = TraceBuilder().wm(0x12, 0x84).rm(0x16, 0x00)
        ref, new = self.files.write("a.log", b.text()), self.files.write("b.log", c.text())
        self.assertEqual(run_diff(ref, new)[0], 0)
        self.assertEqual(run_diff("--reads", ref, new)[0], 1)


if __name__ == "__main__":
    unittest.main()
