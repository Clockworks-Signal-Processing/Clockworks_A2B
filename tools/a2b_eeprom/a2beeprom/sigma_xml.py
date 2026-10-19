"""Reads a SigmaStudio XML program (the <ROM> file SigmaStudio exports for a codec, or a
sequence written by hand in the same form) into peripheral units: writes and waits.

The actions, in order (as a2bcpp/examples/bench/sigma_xml.cpp reads them):
- instr="writeXbytes": `addr` (the sub-address, decimal or 0x..), `len` (sub-address and
  data bytes), `addr_width` (the sub-address's bytes; else `len` less the data, else the
  ROM's Address_byte_length), `i2caddr` (7-bit, decimal or 0x..); the content is the data in
  hex. The sub-address goes first, high byte first.
- instr="delay": the content is the wait in ms, in hex, high byte first (SigmaStudio's
  "00 64" is 100 ms).
- instr="read": dropped and counted: a module's peripheral script has no reads.

A write's I2C address is its own `i2caddr` (7-bit, as in a sequence written by hand), else
the ROM element's IC_Address, which SigmaStudio gives in 8-bit form (114 = 0x72 is 7-bit
0x39, the AB0020's ADAU1761), else the peripheral's address. Hex content may be bytes
separated by spaces ("00 64") or run together ("00FF").

Copyright 2026 by ClockWorks Signal Processing LLC
Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
SPDX-License-Identifier: BSD-3-Clause
"""

import xml.parsers.expat

from . import layout as L
from .model import Unit


class XmlError(Exception):
    """The file can't be read as a program."""


class XmlProgram:
    """A program read from a file."""

    def __init__(self):
        self.units = []
        self.reads_dropped = 0
        self.rom_address = None  # The ROM's IC_Address as 7-bit, if it has one.
        self.addresses = set()  # The I2C addresses written.
        self.notes = []  # Things to know about it.

    @property
    def writes(self):
        return sum(1 for u in self.units if u.kind == "write")

    @property
    def delays(self):
        return sum(1 for u in self.units if u.kind == "delay")

    @property
    def write_bytes(self):
        return sum(len(u.data) for u in self.units if u.kind == "write")

    @property
    def largest_write(self):
        return max([len(u.data) for u in self.units if u.kind == "write"] or [0])

    def summary(self):
        text = "{} write(s), {} bytes (largest {}), {} wait(s)".format(
            self.writes, self.write_bytes, self.largest_write, self.delays)
        if self.reads_dropped:
            text += ", {} read(s) dropped".format(self.reads_dropped)
        return text


def _number(text, what, line):
    try:
        return int(text, 0) if text.strip().lower().startswith("0x") else int(text, 10)
    except ValueError:
        raise XmlError("line {}: {} '{}' isn't a number".format(line, what, text))


def _hex_bytes(text, line):
    words = text.split()
    try:
        if all(len(w) <= 2 for w in words):
            return bytes(int(w, 16) for w in words)
        joined = "".join(words)
        if len(joined) % 2:
            raise ValueError
        return bytes.fromhex(joined)
    except ValueError:
        raise XmlError("line {}: content '{}' isn't hex bytes".format(line, text.strip()[:40]))


def parse(text, default_address=0):
    """Reads a program from the file's text. `default_address`: the 7-bit address of
    writes the file gives none for (0: none)."""
    program = XmlProgram()
    state = {"rom_width": None, "action": None, "line": 0, "text": []}
    parser = xml.parsers.expat.ParserCreate()

    def start(name, attrs):
        if name == "ROM":
            if "IC_Address" in attrs:
                eight_bit = _number(attrs["IC_Address"], "IC_Address", parser.CurrentLineNumber)
                program.rom_address = eight_bit >> 1
                if eight_bit & 1 or eight_bit > 0xFF:
                    program.notes.append("IC_Address {} isn't an 8-bit write address".format(
                        eight_bit))
            if "Address_byte_length" in attrs:
                state["rom_width"] = _number(attrs["Address_byte_length"], "Address_byte_length",
                                             parser.CurrentLineNumber)
        elif name == "action":
            state["action"] = dict(attrs)
            state["line"] = parser.CurrentLineNumber
            state["text"] = []

    def chars(data):
        if state["action"] is not None:
            state["text"].append(data)

    def end(name):
        if name != "action" or state["action"] is None:
            return
        attrs = state["action"]
        state["action"] = None
        _action(program, attrs, "".join(state["text"]), state["line"], state["rom_width"],
                default_address)

    parser.StartElementHandler = start
    parser.CharacterDataHandler = chars
    parser.EndElementHandler = end
    try:
        parser.Parse(text, True)
    except xml.parsers.expat.ExpatError as error:
        raise XmlError("not well-formed XML: {}".format(error))
    if not program.units:
        raise XmlError("no writes or waits (no <action> elements)")
    return program


def _action(program, attrs, content, line, rom_width, default_address):
    instr = attrs.get("instr")
    if instr is None:
        raise XmlError("line {}: an <action> with no instr".format(line))
    if instr == "read":
        program.reads_dropped += 1
        return
    data = _hex_bytes(content, line)
    if instr == "delay":
        ms = int.from_bytes(data, "big") if data else -1
        if not data or ms > L.MAX_DELAY_MS:
            raise XmlError("line {}: a delay that isn't 0-{} ms".format(line, L.MAX_DELAY_MS))
        program.units.append(Unit.delay(ms, note="line {}".format(line)))
        return
    if instr != "writeXbytes":
        raise XmlError("line {}: instr \"{}\" (writeXbytes, delay and read are read)".format(
            line, instr))
    if not data:
        raise XmlError("line {}: a write with no data".format(line))
    length = _number(attrs["len"], "len", line) if "len" in attrs else None
    if length is not None and length < len(data):
        raise XmlError("line {}: len {} is shorter than its {} data bytes".format(
            line, length, len(data)))
    if "addr_width" in attrs:
        width = _number(attrs["addr_width"], "addr_width", line)
        if length is not None and length != width + len(data):
            raise XmlError("line {}: len is not addr_width plus the data bytes".format(line))
    elif length is not None:
        width = length - len(data)
    elif rom_width is not None:
        width = rom_width
    else:
        raise XmlError("line {}: no addr_width, len or Address_byte_length, so no sub-address "
                       "size".format(line))
    if width > 4:
        raise XmlError("line {}: a {}-byte sub-address (most: 4)".format(line, width))
    if "addr" not in attrs:
        raise XmlError("line {}: a write with no addr".format(line))
    sub_address = _number(attrs["addr"], "addr", line)
    if sub_address >= 1 << (8 * width) and width < 4 or sub_address >= 1 << 32:
        raise XmlError("line {}: addr {} doesn't fit {} byte(s)".format(line, sub_address, width))
    if "i2caddr" in attrs:
        address = _number(attrs["i2caddr"], "i2caddr", line)
    elif program.rom_address is not None:
        address = program.rom_address
    else:
        address = default_address
    if not 0 < address <= 0x7F:
        raise XmlError("line {}: no 7-bit I2C address (give the peripheral's address)".format(
            line))
    payload = sub_address.to_bytes(width, "big") + data
    if len(payload) > L.MAX_WRITE:
        raise XmlError("line {}: a write of {} bytes (a unit holds {})".format(
            line, len(payload), L.MAX_WRITE))
    program.addresses.add(address)
    note = "line {}".format(line)
    if attrs.get("ParamName"):
        note += ": " + attrs["ParamName"]
    program.units.append(Unit.write(address, payload, note=note, sub_address_width=width))


def read_file(path, default_address=0):
    """Reads a program from a file."""
    with open(path, "rb") as f:
        raw = f.read()
    try:
        return parse(raw, default_address)
    except XmlError as error:
        raise XmlError("{}: {}".format(path, error))
