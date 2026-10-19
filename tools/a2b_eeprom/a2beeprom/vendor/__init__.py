"""Vendor sections: a vendor's own data in the Supplier Information block (0x400).

The Supplier Information block holds an 8-character vendor ID at 0x403, 32 reserved bytes,
then 212 bytes that are the vendor's own (0x42B-0x4FE; the block's CRC is at 0x4FF). The
vendor ID says how to read them. A vendor section is a declarative layout of those bytes:
a list of fields (text, whole numbers, 64-bit IDs) and at most one repeated record with
its count. The GUI's form, the checks, the project file and the bytes all come from the
layout, so a vendor adds a section by copying clockworks.py, changing the fields, and
listing the new layout in LAYOUTS below.

Numbers are stored low byte first, as ADI's own fields are. Text is ASCII (0x20-0x7E),
padded with NULs; a `terminated` field always keeps a NUL, so it holds one character less
than its size.

Copyright 2026 by ClockWorks Signal Processing LLC
Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
SPDX-License-Identifier: BSD-3-Clause
"""

import random


class TextField:
    """ASCII text in a fixed number of bytes, NUL-padded."""

    kind = "text"

    def __init__(self, name, label, size, terminated=False, help_text=""):
        self.name = name
        self.label = label
        self.size = size
        self.terminated = terminated
        self.help_text = help_text

    @property
    def max_chars(self):
        return self.size - 1 if self.terminated else self.size

    def default(self):
        return ""

    def encode(self, value):
        data = str(value).encode("ascii", "replace")[:self.max_chars]
        return data + bytes(self.size - len(data))

    def decode(self, data):
        end = data.find(b"\x00")
        text = data if end < 0 else data[:end]
        return text.decode("ascii", "replace")

    def check(self, value):
        """What is wrong with `value`, or an empty list."""
        text = str(value)
        problems = []
        if len(text) > self.max_chars:
            problems.append("longer than {} characters".format(self.max_chars))
        if any(ord(c) < 0x20 or ord(c) > 0x7E for c in text):
            problems.append("not printable ASCII")
        return problems

    def to_json(self, value):
        return str(value)

    def from_json(self, value):
        return str(value)


class IntField:
    """A whole number of `size` bytes, low byte first."""

    kind = "int"

    def __init__(self, name, label, size, signed=False, minimum=None, maximum=None,
                 help_text="", hex_display=False):
        self.name = name
        self.label = label
        self.size = size
        self.signed = signed
        bits = 8 * size
        self.minimum = minimum if minimum is not None else (-(1 << (bits - 1)) if signed else 0)
        self.maximum = maximum if maximum is not None else (
            (1 << (bits - 1)) - 1 if signed else (1 << bits) - 1)
        self.help_text = help_text
        self.hex_display = hex_display

    def default(self):
        return max(self.minimum, 0) if self.minimum <= 0 <= self.maximum else self.minimum

    def encode(self, value):
        return int(value).to_bytes(self.size, "little", signed=self.signed)

    def decode(self, data):
        return int.from_bytes(bytes(data[:self.size]), "little", signed=self.signed)

    def check(self, value):
        try:
            number = int(value)
        except (TypeError, ValueError):
            return ["not a whole number"]
        if number < self.minimum or number > self.maximum:
            return ["outside {} to {}".format(self.minimum, self.maximum)]
        return []

    def to_json(self, value):
        if self.hex_display:
            return "0x{:0{}X}".format(int(value), 2 * self.size)
        return int(value)

    def from_json(self, value):
        if isinstance(value, str):
            return int(value, 0)
        return int(value)


class UidField(IntField):
    """A 64-bit identifier, shown in hex; the GUI can make a random one."""

    kind = "uid"

    def __init__(self, name, label, help_text=""):
        IntField.__init__(self, name, label, 8, help_text=help_text, hex_display=True)

    @staticmethod
    def generate():
        return random.SystemRandom().getrandbits(64)


class Repeat:
    """Up to `max_count` records of `fields`, after a `count_size`-byte count. Every slot
    takes its space; unused ones are zero."""

    kind = "repeat"

    def __init__(self, name, label, max_count, fields, count_size=1, unique=None):
        self.name = name
        self.label = label
        self.max_count = max_count
        self.fields = fields
        self.count_size = count_size
        self.unique = unique  # A field no two records may share.

    @property
    def record_size(self):
        return sum(f.size for f in self.fields)

    @property
    def size(self):
        return self.count_size + self.max_count * self.record_size

    def default_record(self):
        return {f.name: f.default() for f in self.fields}


class VendorLayout:
    """A vendor's section: its vendor ID and its fields, in order."""

    def __init__(self, vendor_id, title, description, items):
        if len(vendor_id) != 8:
            raise ValueError("a vendor ID is 8 characters")
        self.vendor_id = vendor_id
        self.title = title
        self.description = description
        self.items = items

    @property
    def size(self):
        return sum(item.size for item in self.items)

    def defaults(self):
        values = {}
        for item in self.items:
            values[item.name] = [] if isinstance(item, Repeat) else item.default()
        return values

    def encode(self, values):
        """The section's bytes."""
        out = bytearray()
        for item in self.items:
            if isinstance(item, Repeat):
                records = values.get(item.name, [])[:item.max_count]
                out += len(records).to_bytes(item.count_size, "little")
                for i in range(item.max_count):
                    record = records[i] if i < len(records) else None
                    for f in item.fields:
                        out += f.encode(record.get(f.name, f.default())) if record else bytes(f.size)
            else:
                out += item.encode(values.get(item.name, item.default()))
        return bytes(out)

    def decode(self, data):
        """The values in `data` (at least size bytes), and what didn't make sense."""
        values = {}
        problems = []
        at = 0
        for item in self.items:
            if isinstance(item, Repeat):
                count = int.from_bytes(bytes(data[at:at + item.count_size]), "little")
                at += item.count_size
                if count > item.max_count:
                    problems.append("{}: count {} is over {}".format(item.label, count,
                                                                    item.max_count))
                    count = item.max_count
                records = []
                for i in range(item.max_count):
                    record = {}
                    for f in item.fields:
                        record[f.name] = f.decode(data[at:at + f.size])
                        at += f.size
                    if i < count:
                        records.append(record)
                values[item.name] = records
            else:
                values[item.name] = item.decode(data[at:at + item.size])
                at += item.size
        return values, problems

    def check(self, values):
        """(where, message) for each problem with `values`."""
        problems = []
        for item in self.items:
            if isinstance(item, Repeat):
                records = values.get(item.name, [])
                if len(records) > item.max_count:
                    problems.append((item.label, "more than {} records".format(item.max_count)))
                seen = set()
                for i, record in enumerate(records):
                    for f in item.fields:
                        for message in f.check(record.get(f.name, f.default())):
                            problems.append(("{} {} {}".format(item.label, i + 1, f.label),
                                             message))
                    if item.unique:
                        key = record.get(item.unique)
                        if key in seen:
                            problems.append(("{} {}".format(item.label, i + 1),
                                             "the same {} as an earlier one".format(item.unique)))
                        seen.add(key)
            else:
                for message in item.check(values.get(item.name, item.default())):
                    problems.append((item.label, message))
        return problems

    def to_json(self, values):
        out = {}
        for item in self.items:
            if isinstance(item, Repeat):
                out[item.name] = [{f.name: f.to_json(r.get(f.name, f.default()))
                                   for f in item.fields} for r in values.get(item.name, [])]
            else:
                out[item.name] = item.to_json(values.get(item.name, item.default()))
        return out

    def from_json(self, data):
        values = self.defaults()
        for item in self.items:
            if item.name not in data:
                continue
            if isinstance(item, Repeat):
                values[item.name] = [{f.name: f.from_json(r.get(f.name, f.default()))
                                      for f in item.fields} for r in data[item.name]]
            else:
                values[item.name] = item.from_json(data[item.name])
        return values


def _layouts():
    from . import clockworks
    return {layout.vendor_id: layout for layout in (clockworks.LAYOUT,)}


LAYOUTS = _layouts()


def find(vendor_id):
    """The layout for an 8-character vendor ID, or None."""
    return LAYOUTS.get(vendor_id)
