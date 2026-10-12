#!/usr/bin/env python3
"""Decode an A2B I2C trace into register accesses.

Input is any text containing "I2CT," lines, e.g. a whole console log saved
after "i2cTrace dump", or the file written by "i2cTrace dump <file>" (see
a2bcpp/tools/README.md for the line format).

Each transfer is decoded against the A2B addressing model:
  * main-node address M: register access to the main node (target "M");
  * bus address M+1: register access to the sub-node selected by the main
    node's NODEADR (target "S<n>"), a broadcast write (target "BC", NODEADR.BRCST),
    or a raw transfer to the peripheral at that sub-node's CHIP address
    (target "P<n>:<chip>", NODEADR.PERI);
  * any other address: a raw transfer to a host-side device (target "H:<addr>").
Register writes and reads with more than one data byte are split into one
event per register (auto-increment). AD243x MMRPAGE (0xE0) is tracked per node,
so paged registers decode as page*0x100 + offset (e.g. VMTR_VEN = 0x100).

Usage:
  trace_decode.py trace.log                 events, one per line
  trace_decode.py --writes-only trace.log   leave out reads
  trace_decode.py --main 0x68 trace.log     main-node address (default: inferred)
"""

import argparse
import re
import sys
from dataclasses import dataclass, field

REG_NODEADR = 0x01
REG_CHIP = 0x00
REG_MMRPAGE = 0xE0
NODEADR_BRCST = 0x80
NODEADR_PERI = 0x20
NODEADR_NODE = 0x0F

# Register offsets from a2bstack/a2bstack/inc/a2b/regdefs.h (paged AD243x registers as page*0x100 + offset)
REG_NAMES = {
    0x00: "CHIP", 0x01: "NODEADR", 0x02: "VENDOR", 0x03: "PRODUCT", 0x04: "VERSION",
    0x05: "CAPABILITY", 0x09: "SWCTL", 0x0A: "BCDNSLOTS", 0x0B: "LDNSLOTS", 0x0C: "LUPSLOTS",
    0x0D: "DNSLOTS", 0x0E: "UPSLOTS", 0x0F: "RESPCYCS", 0x10: "SLOTFMT", 0x11: "DATCTL",
    0x12: "CONTROL", 0x13: "DISCVRY", 0x14: "SWSTAT", 0x15: "INTSTAT", 0x16: "INTSRC",
    0x17: "INTTYPE", 0x18: "INTPND0", 0x19: "INTPND1", 0x1A: "INTPND2", 0x1B: "INTMSK0",
    0x1C: "INTMSK1", 0x1D: "INTMSK2", 0x1E: "BECCTL", 0x1F: "BECNT", 0x20: "TESTMODE",
    0x21: "ERRCNT0", 0x22: "ERRCNT1", 0x23: "ERRCNT2", 0x24: "ERRCNT3", 0x25: "SEED0",
    0x26: "SEED1", 0x27: "SEED2", 0x28: "SEED3", 0x29: "NODE", 0x2A: "TRANSTAT", 0x2B: "DISCSTAT",
    0x2C: "NSCURCNT", 0x2D: "VREGCTL", 0x2E: "TXACTL", 0x2F: "RXACTL", 0x30: "TXBCTL",
    0x31: "RXBCTL", 0x32: "RESPCCC", 0x33: "DCTLCC", 0x34: "DNSCC", 0x35: "LDNSCC", 0x36: "UPSCC",
    0x37: "LUPSCC", 0x38: "BCDNSCC", 0x39: "SFMTCC", 0x3A: "PTSTMODE", 0x3B: "STRAPVAL",
    0x3E: "LINTTYPE", 0x3F: "I2CCFG", 0x40: "PLLCTL", 0x41: "I2SGCFG", 0x42: "I2SCFG",
    0x43: "I2SRATE", 0x44: "I2STXOFFSET", 0x45: "I2SRXOFFSET", 0x46: "SYNCOFFSET", 0x47: "PDMCTL",
    0x48: "ERRMGMT", 0x49: "CLKCFG", 0x4A: "GPIODAT", 0x4B: "GPIODATSET", 0x4C: "GPIODATCLR",
    0x4D: "GPIOOEN", 0x4E: "GPIOIEN", 0x4F: "GPIOIN", 0x50: "PINTEN", 0x51: "PINTINV",
    0x52: "PINCFG", 0x53: "I2STEST", 0x54: "RAISE", 0x55: "GENERR", 0x56: "I2SRRATE",
    0x57: "I2SRRCTL", 0x58: "I2SRRSOFFS", 0x59: "CLK1CFG", 0x5A: "CLK2CFG", 0x5B: "BMMCFG",
    0x5C: "SUSCFG", 0x5D: "PDMCTL2", 0x5E: "BSDSTAT", 0x60: "UPMASK0", 0x61: "UPMASK1",
    0x62: "UPMASK2", 0x63: "UPMASK3", 0x64: "UPOFFSET", 0x65: "DNMASK0", 0x66: "DNMASK1",
    0x67: "DNMASK2", 0x68: "DNMASK3", 0x69: "DNOFFSET", 0x6A: "CHIPID0", 0x6B: "CHIPID1",
    0x6C: "CHIPID2", 0x6D: "CHIPID3", 0x6E: "CHIPID4", 0x6F: "CHIPID5", 0x7B: "USER_SWCTL",
    0x7C: "DTCFG", 0x7D: "DTSLOTS", 0x7E: "DTDNOFFS", 0x7F: "DTUPOFFS", 0x80: "GPIODEN",
    0x81: "GPIOD0MSK", 0x82: "GPIOD1MSK", 0x83: "GPIOD2MSK", 0x84: "GPIOD3MSK", 0x85: "GPIOD4MSK",
    0x86: "GPIOD5MSK", 0x87: "GPIOD6MSK", 0x88: "GPIOD7MSK", 0x89: "GPIODDAT", 0x8A: "GPIODINV",
    0x8C: "DTCFGCC", 0x8D: "DTSLTSCC", 0x8E: "DTDNOFCC", 0x8F: "DTUPOFCC", 0x90: "MBOX0CTL",
    0x91: "MBOX0STAT", 0x92: "MBOX0B0", 0x93: "MBOX0B1", 0x94: "MBOX0B2", 0x95: "MBOX0B3",
    0x96: "MBOX1CTL", 0x97: "MBOX1STAT", 0x98: "MBOX1B0", 0x99: "MBOX1B1", 0x9A: "MBOX1B2",
    0x9B: "MBOX1B3", 0xA0: "SWCTL2", 0xA1: "SWCTL3", 0xA2: "SWCTL4", 0xA3: "SWCTL5",
    0xA4: "SWCTL6", 0xA5: "SWSTAT2", 0xA6: "SWSTAT3", 0xAF: "SPIDTLCMD", 0xB0: "SPICFG",
    0xB1: "SPISTAT", 0xB2: "SPICKDIV", 0xB3: "SPIFDSIZE", 0xB4: "SPIFDTARG", 0xB5: "SPIPINCFG",
    0xB6: "SPIINT", 0xB7: "SPIMSK", 0xB8: "RXMASK0", 0xB9: "RXMASK1", 0xBA: "RXMASK2",
    0xBB: "RXMASK3", 0xBC: "RXMASK4", 0xBD: "RXMASK5", 0xBE: "RXMASK6", 0xBF: "RXMASK7",
    0xC0: "TXXBAR0", 0xC1: "TXXBAR1", 0xC2: "TXXBAR2", 0xC3: "TXXBAR3", 0xC4: "TXXBAR4",
    0xC5: "TXXBAR5", 0xC6: "TXXBAR6", 0xC7: "TXXBAR7", 0xC8: "TXXBAR8", 0xC9: "TXXBAR9",
    0xCA: "TXXBAR10", 0xCB: "TXXBAR11", 0xCC: "TXXBAR12", 0xCD: "TXXBAR13", 0xCE: "TXXBAR14",
    0xCF: "TXXBAR15", 0xD0: "TXXBAR16", 0xD1: "TXXBAR17", 0xD2: "TXXBAR18", 0xD3: "TXXBAR19",
    0xD4: "TXXBAR20", 0xD5: "TXXBAR21", 0xD6: "TXXBAR22", 0xD7: "TXXBAR23", 0xD8: "TXXBAR24",
    0xD9: "TXXBAR25", 0xDA: "TXXBAR26", 0xDB: "TXXBAR27", 0xDC: "TXXBAR28", 0xDD: "TXXBAR29",
    0xDE: "TXXBAR30", 0xDF: "TXXBAR31", 0xE0: "MMRPAGE", 0xF0: "EFUSEADDR", 0xF1: "EFUSERDAT",
    0xF2: "EFUSEWDAT", 0xF3: "TM1KEY", 0xF4: "TM3KEY", 0x100: "VMTR_VEN", 0x101: "VMTR_INTEN",
    0x102: "VMTR_MXSTAT", 0x103: "VMTR_MNSTAT", 0x108: "VMTR_CHP1L", 0x109: "VMTR_CHP1H",
    0x10A: "VMTR_CHP0L", 0x10B: "VMTR_CHP0H", 0x10F: "VMTR_ENG", 0x120: "VMTR_VLTG0",
    0x121: "VMTR_VMAX0", 0x122: "VMTR_VMIN0", 0x123: "VMTR_VLTG1", 0x124: "VMTR_VMAX1",
    0x125: "VMTR_VMIN1", 0x126: "VMTR_VLTG2", 0x127: "VMTR_VMAX2", 0x128: "VMTR_VMIN2",
    0x129: "VMTR_VLTG3", 0x12A: "VMTR_VMAX3", 0x12B: "VMTR_VMIN3", 0x12C: "VMTR_VLTG4",
    0x12D: "VMTR_VMAX4", 0x12E: "VMTR_VMIN4", 0x12F: "VMTR_VLTG5", 0x130: "VMTR_VMAX5",
    0x131: "VMTR_VMIN5", 0x132: "VMTR_VLTG6", 0x133: "VMTR_VMAX6", 0x134: "VMTR_VMIN6",
    0x135: "VMTR_VLTG7", 0x136: "VMTR_VMAX7", 0x137: "VMTR_VMIN7", 0x140: "PWMCFG",
    0x141: "PWMFREQ", 0x142: "PWMBLINK1", 0x143: "PWMBLINK2", 0x148: "PWM1VALL", 0x149: "PWM1VALH",
    0x14A: "PWM2VALL", 0x14B: "PWM2VALH", 0x14C: "PWM3VALL", 0x14D: "PWM3VALH", 0x14E: "PWMOEVALL",
    0x14F: "PWMOEVALH",
}
REG_BY_NAME = {name: reg for reg, name in REG_NAMES.items()}

LINE_RE = re.compile(
    r"I2CT,(\d+),(\d+),(\d+),([0-9A-Fa-f]{2}),(WR|W|R),([01]),(\d+),([0-9A-Fa-f]*),(\d+),([0-9A-Fa-f]*)")
BEGIN_RE = re.compile(r"I2CT-BEGIN,(\d+),(\d+),(\d+),(\d+),(\d+)")


@dataclass
class Transfer:
    """One I2C transfer as recorded by the trace hook."""
    seq: int
    t_us: int
    bus: int
    addr: int
    op: str                  # "W", "R" or "WR"
    failed: bool
    w_len: int
    w_data: bytes
    r_len: int
    r_data: bytes

    @property
    def truncated(self):
        return len(self.w_data) < self.w_len or len(self.r_data) < self.r_len


@dataclass
class Event:
    """One decoded access: a register write/read, or a raw peripheral transfer."""
    seq: int
    t_us: int
    bus: int
    target: str              # "M", "S<n>", "BC", "P<n>:<chip>", "H:<addr>"
    op: str                  # "W" or "R" for registers; "W", "R", "WR" for raw
    reg: int = None          # None for raw transfers
    value: int = None        # register value (None for raw transfers)
    w_data: bytes = b""      # raw transfers only
    r_data: bytes = b""
    failed: bool = False
    truncated: bool = False

    def reg_name(self):
        if self.reg is None:
            return ""
        return REG_NAMES.get(self.reg, "0x%02X" % self.reg)

    def key(self):
        """What two runs must agree on for this event (timing excluded)."""
        if self.reg is None:
            return (self.bus, self.target, self.op, None, self.w_data.hex(),
                    self.r_data.hex() if self.op != "W" else "", self.failed)
        return (self.bus, self.target, self.op, self.reg, self.value, self.failed)

    def __str__(self):
        head = "%6d %10.3f  %d %-8s %-2s" % (self.seq, self.t_us / 1000.0, self.bus, self.target, self.op)
        if self.reg is None:
            body = "w=%s r=%s" % (self.w_data.hex().upper() or "-", self.r_data.hex().upper() or "-")
        else:
            body = "%-12s 0x%02X" % (self.reg_name(), self.value)
        tail = (" FAILED" if self.failed else "") + (" TRUNCATED" if self.truncated else "")
        return head + " " + body + tail


@dataclass
class Trace:
    transfers: list
    dropped: int = 0
    header_count: int = None
    events: list = field(default_factory=list)


def parse_trace(lines):
    """Read the I2CT lines of a log (other text is ignored)."""
    transfers = []
    dropped = 0
    header_count = None
    for line in lines:
        m = BEGIN_RE.search(line)
        if m:
            header_count = int(m.group(2))
            dropped += int(m.group(4))
            continue
        m = LINE_RE.search(line)
        if not m:
            continue
        g = m.groups()
        transfers.append(Transfer(
            seq=int(g[0]), t_us=int(g[1]), bus=int(g[2]), addr=int(g[3], 16), op=g[4],
            failed=g[5] == "1", w_len=int(g[6]), w_data=bytes.fromhex(g[7]),
            r_len=int(g[8]), r_data=bytes.fromhex(g[9])))
    return Trace(transfers=transfers, dropped=dropped, header_count=header_count)


def infer_main_addrs(transfers):
    """Main-node address per bus: an address A written at NODEADR, with A+1 also used."""
    result = {}
    for bus in sorted({t.bus for t in transfers}):
        addrs = {t.addr for t in transfers if t.bus == bus}
        nodeadr_writes = {}
        for t in transfers:
            if (t.bus == bus and t.op == "W" and len(t.w_data) >= 2 and t.w_data[0] == REG_NODEADR
                    and (t.addr + 1) in addrs):
                nodeadr_writes[t.addr] = nodeadr_writes.get(t.addr, 0) + 1
        if nodeadr_writes:
            result[bus] = max(nodeadr_writes, key=nodeadr_writes.get)
    return result


class _BusState:
    def __init__(self):
        self.nodeadr = 0
        self.page = {}       # target -> MMRPAGE value
        self.chip = {}       # sub-node number -> CHIP value


def _register_events(t, target, state, base_reg, values, op):
    """Split a register access into one event per register, tracking paging."""
    events = []
    for i, value in enumerate(values):
        reg = base_reg + i
        page = state.page.get(target, 0)
        full = reg if (reg == REG_MMRPAGE or page == 0) else (page << 8) | reg
        events.append(Event(seq=t.seq, t_us=t.t_us, bus=t.bus, target=target, op=op, reg=full,
                            value=value, failed=t.failed, truncated=t.truncated))
        if op == "W" and not t.failed:
            if reg == REG_MMRPAGE:
                state.page[target] = value
            if target == "M" and reg == REG_NODEADR:
                state.nodeadr = value
            if target.startswith("S") and reg == REG_CHIP:
                state.chip[int(target[1:])] = value
            if target == "BC" and reg == REG_MMRPAGE:
                for key in list(state.page):
                    if key.startswith("S"):
                        state.page[key] = value
    return events


def decode(trace, main_addrs=None, default_main=0x68):
    """Turn transfers into events. main_addrs maps bus -> main-node address."""
    mains = infer_main_addrs(trace.transfers)
    if main_addrs:
        mains.update(main_addrs)
    states = {}
    events = []
    for t in trace.transfers:
        main = mains.get(t.bus, default_main)
        state = states.setdefault(t.bus, _BusState())
        target = None
        if t.addr == main:
            target = "M"
        elif t.addr == main + 1:
            node = state.nodeadr & NODEADR_NODE
            if state.nodeadr & NODEADR_PERI:
                chip = state.chip.get(node)
                target = "P%d:%s" % (node, "%02X" % chip if chip is not None else "??")
            elif state.nodeadr & NODEADR_BRCST:
                target = "BC"
            else:
                target = "S%d" % node
        if target is None or target.startswith("P"):
            if target is None:
                target = "H:%02X" % t.addr
            events.append(Event(seq=t.seq, t_us=t.t_us, bus=t.bus, target=target, op=t.op,
                                w_data=t.w_data, r_data=t.r_data, failed=t.failed,
                                truncated=t.truncated))
            continue
        # Register access: first written byte is the register offset
        if t.op == "W" and t.w_data:
            events += _register_events(t, target, state, t.w_data[0], t.w_data[1:], "W")
        elif t.op == "WR" and len(t.w_data) == 1:
            events += _register_events(t, target, state, t.w_data[0], t.r_data, "R")
        else:
            # Not a register access pattern (e.g. a bare read): keep it raw
            events.append(Event(seq=t.seq, t_us=t.t_us, bus=t.bus, target=target, op=t.op,
                                w_data=t.w_data, r_data=t.r_data, failed=t.failed,
                                truncated=t.truncated))
    trace.events = events
    return events


def parse_main_arg(text):
    """"0x68" (all buses) or "0:0x68,1:0x6A" -> ({bus: addr}, default)."""
    if text is None:
        return None, 0x68
    if ":" not in text:
        return None, int(text, 0)
    mains = {}
    for part in text.split(","):
        bus, addr = part.split(":")
        mains[int(bus)] = int(addr, 0)
    return mains, 0x68


def load(path, main_arg=None):
    """Read and decode a trace file; returns the Trace with its events."""
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        trace = parse_trace(f)
    mains, default_main = parse_main_arg(main_arg)
    decode(trace, mains, default_main)
    return trace


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("trace", help="console log or trace file containing I2CT lines")
    ap.add_argument("--main", help="main-node address: 0x68, or per bus 0:0x68,1:0x6A (default: inferred)")
    ap.add_argument("--writes-only", action="store_true", help="leave out reads")
    args = ap.parse_args(argv)

    trace = load(args.trace, args.main)
    if not trace.transfers:
        print("no I2CT lines in %s" % args.trace, file=sys.stderr)
        return 1
    if trace.header_count is not None and trace.header_count != len(trace.transfers):
        print("warning: header says %d transfers, found %d" % (trace.header_count, len(trace.transfers)),
              file=sys.stderr)
    if trace.dropped:
        print("warning: %d transfers dropped (trace buffer full)" % trace.dropped, file=sys.stderr)
    print("%6s %10s  %s %-8s %-2s %s" % ("seq", "t_ms", "b", "target", "op", "register/data"))
    for e in trace.events:
        if args.writes_only and e.op == "R":
            continue
        print(e)
    return 0


if __name__ == "__main__":
    sys.exit(main())
