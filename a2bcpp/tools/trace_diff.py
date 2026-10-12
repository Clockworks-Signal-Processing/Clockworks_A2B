#!/usr/bin/env python3
"""Compare two A2B I2C traces of the same scenario.

Both traces are decoded with trace_decode.py, then compared bus by bus:

Phases     Each bus's events are split into phases. A phase starts at every
           main-node DISCVRY write and every main-node CONTROL write with
           SOFTRST set. Runs of identical phases (e.g. repeated blind probes
           while nothing is plugged in) collapse into one phase with a repeat
           count, so two runs of different length still line up.
Order      Within a phase the writes must match one for one: target, register,
           value and success. Reads are left out unless --reads is given; then
           register reads must match by target and register (not value).
Image      The register image (last value written per bus/target/register) must
           match at the end of every phase.
Ends       Traces are dumped by hand, so one usually stops first. Only the span
           both cover is compared: a last phase that stops part way is compared
           as far as it goes, and phases after the shorter trace ends are noted.
Delays     Where the reference waits between two writes in a phase (a gap of at
           least --min-gap-ms and at most --max-gap-ms), the new trace must
           wait at least --delay-ratio times as long, allowing --jitter-ms for
           waits that end on a poll. Explicit "delay" and "mingap" rules add
           absolute minimums, checked in both traces.

Rules (the default file trace_rules_default.txt is read first, then --rules):
  ignore <target> <reg> [W|R|*]      leave these events out entirely
  unordered <target> <reg>           check by register image only, not position
  delay <target> <reg>[=<value>] <ms>   a matching write must be followed by at
                                     least <ms> before the next compared event
  mingap <target> <reg>[=<value>|&<mask>] <target> <reg>[=<value>|&<mask>] <ms>
                                     a write matching the second pattern must
                                     come at least <ms> after the last earlier
                                     write matching the first, other events in
                                     between or not (e.g. MSTR after a soft reset);
                                     when both targets are the same pattern (e.g.
                                     S* and S*), on the same node
<target> is a pattern: M, S3, S*, BC, P*, H:50, * ... <reg> is a register name
from trace_decode.py, a number, or *. =<value> matches that value; &<mask>
matches a value with any of the mask's bits set. Text after # is a comment.

Exit status: 0 the traces compare equal, 1 they differ, 2 bad input.
"""

import argparse
import fnmatch
import os
import sys
from dataclasses import dataclass, field

import trace_decode as td

REG_CONTROL = 0x12
REG_DISCVRY = 0x13
CONTROL_SOFTRST = 0x04
DEFAULT_RULES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "trace_rules_default.txt")


@dataclass
class Rule:
    kind: str                # "ignore", "unordered", "delay", "mingap"
    target: str
    reg: object              # int, or None for any register
    op: str = "*"
    value: int = None        # delay and mingap rules: only writes of this value
    ms: float = 0.0
    source: str = ""
    mask: int = None         # mingap rules: only writes with one of these bits set
    then: object = None      # mingap rules: the Rule matching the later write

    def matches(self, e):
        if not fnmatch.fnmatchcase(e.target, self.target):
            return False
        if self.reg is not None and e.reg != self.reg:
            return False
        if self.op != "*" and e.op != self.op:
            return False
        if self.value is not None and e.value != self.value:
            return False
        if self.mask is not None and (e.value is None or (e.value & self.mask) == 0):
            return False
        return True


def _parse_reg(text):
    if text == "*":
        return None
    if text.upper() in td.REG_BY_NAME:
        return td.REG_BY_NAME[text.upper()]
    return int(text, 0)


def _parse_write(kind, target, reg_text, where):
    """A write pattern: <reg>, <reg>=<value> or <reg>&<mask>."""
    if "&" in reg_text:
        reg_text, _, mask_text = reg_text.partition("&")
        return Rule(kind, target, _parse_reg(reg_text), op="W", mask=int(mask_text, 0), source=where)
    reg_text, _, value_text = reg_text.partition("=")
    return Rule(kind, target, _parse_reg(reg_text), op="W",
                value=int(value_text, 0) if value_text else None, source=where)


def load_rules(path):
    rules = []
    with open(path, "r", encoding="utf-8") as f:
        for n, raw in enumerate(f, 1):
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            words = line.split()
            where = "%s:%d" % (os.path.basename(path), n)
            try:
                kind = words[0]
                if kind == "ignore" and len(words) in (3, 4):
                    op = words[3].upper() if len(words) == 4 else "*"
                    if op not in ("W", "R", "*"):
                        raise ValueError("op must be W, R or *")
                    rules.append(Rule(kind, words[1], _parse_reg(words[2]), op=op, source=where))
                elif kind == "unordered" and len(words) == 3:
                    rules.append(Rule(kind, words[1], _parse_reg(words[2]), source=where))
                elif kind == "delay" and len(words) == 4:
                    reg_text, _, value_text = words[2].partition("=")
                    rules.append(Rule(kind, words[1], _parse_reg(reg_text), op="W",
                                      value=int(value_text, 0) if value_text else None,
                                      ms=float(words[3]), source=where))
                elif kind == "mingap" and len(words) == 6:
                    rule = _parse_write(kind, words[1], words[2], where)
                    rule.then = _parse_write(kind, words[3], words[4], where)
                    rule.ms = float(words[5])
                    rules.append(rule)
                else:
                    raise ValueError("unknown rule or wrong number of fields")
            except ValueError as err:
                raise SystemExit("%s: %s: %s" % (where, raw.strip(), err))
    return rules


def is_read(e):
    return e.op in ("R", "WR")


def compare_key(e):
    """What must match between runs; register reads match by target/register only."""
    if is_read(e) and e.reg is not None:
        return (e.bus, e.target, "R", e.reg)
    return e.key()


@dataclass
class Phase:
    label: str
    events: list = field(default_factory=list)      # compared in order
    image: dict = field(default_factory=dict)       # register image at the end of the phase
    count: int = 1

    def signature(self):
        return (self.label, tuple(compare_key(e) for e in self.events))


def phase_label(e):
    if e.target == "M" and e.op == "W" and e.reg == REG_DISCVRY:
        return "DISCVRY=0x%02X" % e.value
    if e.target == "M" and e.op == "W" and e.reg == REG_CONTROL and (e.value & CONTROL_SOFTRST):
        return "SOFTRST"
    return None


def split_phases(events, rules, with_reads):
    """Filter one bus's events and split them into collapsed phases."""
    ignore = [r for r in rules if r.kind == "ignore"]
    unordered = [r for r in rules if r.kind == "unordered"]
    phases = [Phase("start")]
    image = {}
    for e in events:
        if any(r.matches(e) for r in ignore):
            continue
        if is_read(e) and not with_reads:
            continue
        label = phase_label(e)
        if label is not None:
            phases[-1].image = dict(image)
            phases.append(Phase(label))
        if e.op == "W" and e.reg is not None and not e.failed:
            image[(e.target, e.reg)] = e.value
        if not any(r.matches(e) for r in unordered):
            phases[-1].events.append(e)
    phases[-1].image = dict(image)

    collapsed = []
    for p in phases:
        if collapsed and collapsed[-1].signature() == p.signature() and collapsed[-1].image == p.image:
            collapsed[-1].count += 1
        else:
            collapsed.append(p)
    return collapsed


def _fmt_events(events, start, stop):
    return ["      %s" % e for e in events[max(0, start):stop]]


def compare_bus(bus, ref_phases, new_phases, args):
    """Return (problems, notes) for one bus.

    The traces were dumped by hand, so one usually stops before the other. They
    are compared over the span both cover: a trace's last phase may stop part
    way (only the events it has are compared), and phases after the end of the
    shorter trace are reported as a note.
    """
    problems, notes = [], []
    common = min(len(ref_phases), len(new_phases))
    for i in range(common):
        rp, np_ = ref_phases[i], new_phases[i]
        name = "bus %d phase %d (%s)" % (bus, i, rp.label)
        ref_ends, new_ends = i == len(ref_phases) - 1, i == len(new_phases) - 1
        if rp.label != np_.label:
            problems.append("%s: new trace has phase %s here" % (name, np_.label))
            break
        # Where a trace ends, how many repeats it reached depends on when it was dumped
        if rp.count != np_.count and (args.strict_repeats or not (ref_ends or new_ends)):
            msg = "%s: runs %d time(s) in a row in reference, %d in new" % (name, rp.count, np_.count)
            (problems if args.strict_repeats else notes).append(msg)
        rk = [compare_key(e) for e in rp.events]
        nk = [compare_key(e) for e in np_.events]
        # A trace that ends in this phase may have stopped part way through it
        open_end = False
        if (ref_ends and len(rk) < len(nk)) or (new_ends and len(nk) < len(rk)) or \
                (ref_ends and new_ends and len(rk) != len(nk)):
            m = min(len(rk), len(nk))
            if rk[:m] == nk[:m]:
                open_end = True
                which = "reference" if len(rk) < len(nk) else "new trace"
                notes.append("%s: %s stops after %d of %d events; compared that far" % (
                    name, which, m, max(len(rk), len(nk))))
                rk, nk = rk[:m], nk[:m]
        if rk != nk:
            k = next((j for j in range(min(len(rk), len(nk))) if rk[j] != nk[j]), min(len(rk), len(nk)))
            lines = ["%s: order differs at event %d (reference %d events, new %d)" %
                     (name, k, len(rk), len(nk)), "    reference:"]
            lines += _fmt_events(rp.events, k - 2, k + 3)
            lines.append("    new:")
            lines += _fmt_events(np_.events, k - 2, k + 3)
            problems.append("\n".join(lines))
        if rp.image != np_.image and not open_end:
            diffs = []
            for key in sorted(set(rp.image) | set(np_.image), key=lambda x: (x[0], x[1])):
                a, b = rp.image.get(key), np_.image.get(key)
                if a != b:
                    diffs.append("      %-8s %-12s reference %s, new %s" % (
                        key[0], td.REG_NAMES.get(key[1], "0x%02X" % key[1]),
                        "-" if a is None else "0x%02X" % a, "-" if b is None else "0x%02X" % b))
            problems.append("%s: register image differs at end of phase\n%s" % (name, "\n".join(diffs[:20])))
        if rk == nk:
            problems += check_gaps(name, rp.events[:len(rk)], np_.events[:len(nk)], args)
        if problems and not args.all:
            break
    if len(ref_phases) != len(new_phases) and (args.all or not problems):
        extra = ref_phases[len(new_phases):] or new_phases[len(ref_phases):]
        which = "reference" if len(ref_phases) > len(new_phases) else "new trace"
        notes.append("bus %d: the other trace ends first; %s has %d more phase(s), from %s, not compared" %
                     (bus, which, len(extra), extra[0].label))
    return problems, notes


def check_gaps(name, ref_events, new_events, args):
    """A wait in the reference must not shrink below --delay-ratio in the new trace.

    Waits that end when the stack polls something vary by up to one poll
    interval between runs, so a wait only counts as shortened when it is also
    more than --jitter-ms shorter. Exact minimums belong in "delay" rules.
    """
    problems = []
    for j in range(1, len(ref_events)):
        ref_gap = (ref_events[j].t_us - ref_events[j - 1].t_us) / 1000.0
        if ref_gap < args.min_gap_ms or ref_gap > args.max_gap_ms:
            continue
        new_gap = (new_events[j].t_us - new_events[j - 1].t_us) / 1000.0
        if new_gap < ref_gap * args.delay_ratio and new_gap < ref_gap - args.jitter_ms:
            problems.append("%s: wait before event %d is %.1f ms, reference %.1f ms\n%s" % (
                name, j, new_gap, ref_gap, "\n".join(_fmt_events(new_events, j - 1, j + 1))))
    return problems


def check_delay_rules(label, events, rules, with_reads):
    """Explicit minimum delays from a matching write to the next compared event."""
    delay_rules = [r for r in rules if r.kind == "delay"]
    ignore = [r for r in rules if r.kind == "ignore"]
    kept = [e for e in events if not any(r.matches(e) for r in ignore) and (with_reads or not is_read(e))]
    problems = []
    for j in range(len(kept) - 1):
        for r in delay_rules:
            if r.matches(kept[j]) and kept[j + 1].t_us - kept[j].t_us < r.ms * 1000.0:
                gap = (kept[j + 1].t_us - kept[j].t_us) / 1000.0
                problems.append("%s: %.1f ms after write, rule %s wants %g ms\n%s" % (
                    label, gap, r.source, r.ms, "\n".join(_fmt_events(kept, j, j + 2))))
    return problems


def check_min_gaps(label, events, rules):
    """"mingap" rules: a later write must keep its distance from the last earlier one.

    Every successful write counts, whatever the ignore rules say: these are hard
    minimums between writes to the hardware, such as the SUSTAIN wait between a
    main node's soft reset and CONTROL.MSTR. The gap is measured per bus, or per node
    when both patterns name the same target pattern (a wait on each sub-node).
    """
    problems = []
    for r in (r for r in rules if r.kind == "mingap"):
        last = {}                                           # bus (or bus, node) -> the last matching write
        per_node = (r.target == r.then.target)
        for e in events:
            if e.op != "W" or e.failed:
                continue
            key = (e.bus, e.target) if per_node else e.bus
            before = last.get(key)
            if before is not None and r.then.matches(e) and e.t_us - before.t_us < r.ms * 1000.0:
                problems.append("%s: %.1f ms from the write before, rule %s wants %g ms\n%s" % (
                    label, (e.t_us - before.t_us) / 1000.0, r.source, r.ms,
                    "\n".join(_fmt_events([before, e], 0, 2))))
            if r.matches(e):
                last[key] = e
    return problems


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("reference", help="reference trace (console log or trace file)")
    ap.add_argument("new", help="trace to check against it")
    ap.add_argument("--rules", action="append", default=[], help="extra rules file (may repeat)")
    ap.add_argument("--no-default-rules", action="store_true", help="don't read trace_rules_default.txt")
    ap.add_argument("--reads", action="store_true", help="also compare register reads (target and register)")
    ap.add_argument("--main", help="main-node address: 0x68, or per bus 0:0x68,1:0x6A (default: inferred)")
    ap.add_argument("--min-gap-ms", type=float, default=5.0, help="smallest reference wait checked (default 5)")
    ap.add_argument("--max-gap-ms", type=float, default=2000.0,
                    help="largest reference wait checked; longer waits are the operator or a periodic "
                         "timer (default 2000)")
    ap.add_argument("--delay-ratio", type=float, default=0.8,
                    help="the new wait must be at least this times the reference wait (default 0.8)")
    ap.add_argument("--jitter-ms", type=float, default=25.0,
                    help="a wait may also be this much shorter, for polling jitter; ADI's stack "
                         "polls interrupts every 20 ms (default 25)")
    ap.add_argument("--strict-repeats", action="store_true", help="repeat counts of collapsed phases must match")
    ap.add_argument("--all", action="store_true", help="report every difference, not just the first per bus")
    args = ap.parse_args(argv)

    rules = []
    if not args.no_default_rules and os.path.exists(DEFAULT_RULES):
        rules += load_rules(DEFAULT_RULES)
    for path in args.rules:
        rules += load_rules(path)

    traces = []
    for path in (args.reference, args.new):
        trace = td.load(path, args.main)
        if not trace.transfers:
            print("no I2CT lines in %s" % path, file=sys.stderr)
            return 2
        if trace.dropped:
            print("warning: %s: %d transfers dropped (trace buffer full)" % (path, trace.dropped))
        traces.append(trace)
    ref, new = traces

    problems, notes = [], []
    for label, trace in (("reference", ref), ("new", new)):
        found = check_delay_rules(label, trace.events, rules, args.reads)
        found += check_min_gaps(label, trace.events, rules)
        if label == "reference":
            notes += ["reference breaks a delay rule: " + p for p in found]
        else:
            problems += found

    buses = sorted({e.bus for e in ref.events} | {e.bus for e in new.events})
    for bus in buses:
        rp = split_phases([e for e in ref.events if e.bus == bus], rules, args.reads)
        np_ = split_phases([e for e in new.events if e.bus == bus], rules, args.reads)
        print("bus %d: reference %d phases (%d before collapsing), new %d phases (%d)" % (
            bus, len(rp), sum(p.count for p in rp), len(np_), sum(p.count for p in np_)))
        p, n = compare_bus(bus, rp, np_, args)
        problems += p
        notes += n

    for n in notes:
        print("note: " + n)
    for p in problems:
        print("DIFF: " + p)
    print("EQUAL" if not problems else "DIFFERENT (%d problem%s)" % (len(problems), "" if len(problems) == 1 else "s"))
    return 0 if not problems else 1


if __name__ == "__main__":
    sys.exit(main())
