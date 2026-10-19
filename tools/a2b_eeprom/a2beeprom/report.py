"""A board's module-info image in words: what each field means, for the command line and the GUI.

Copyright 2026 by ClockWorks Signal Processing LLC
Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
SPDX-License-Identifier: BSD-3-Clause
"""

from . import layout as L
from . import registers as R
from .encode import split_units


def _bits(value, names):
    found = [names[b] for b in sorted(names) if value >> b & 1]
    others = value & ~sum(1 << b for b in names)
    if others:
        found.append("other bits 0x{:X}".format(others))
    return ", ".join(found) if found else "none"


def parse_channels(text, bits=32):
    """A bitmap from a list such as "0-1", "0,2,4-7" or "none"; ValueError if it isn't one."""
    text = text.strip().lower()
    if text in ("", "none"):
        return 0
    bitmap = 0
    for part in text.split(","):
        part = part.strip()
        if "-" in part:
            first, last = (int(x) for x in part.split("-", 1))
        else:
            first = last = int(part)
        if not 0 <= first <= last < bits:
            raise ValueError("'{}' isn't within 0-{}".format(part, bits - 1))
        for n in range(first, last + 1):
            bitmap |= 1 << n
    return bitmap


def channel_list(bitmap):
    """A bitmap as a list: "0-1", "0,2,4-7", or "none"."""
    return _channels(bitmap)


def _channels(bitmap):
    channels = [n for n in range(32) if bitmap >> n & 1]
    if not channels:
        return "none"
    runs = []
    start = prev = channels[0]
    for c in channels[1:] + [None]:
        if c is not None and c == prev + 1:
            prev = c
            continue
        runs.append(str(start) if start == prev else "{}-{}".format(start, prev))
        if c is not None:
            start = prev = c
    return ",".join(runs)


def _pins(value):
    count = (value >> 5) & 0x07
    sio = [n for n in range(5) if value >> n & 1]
    return "{} pin(s){}".format(count, ": " + ", ".join("SIO{}".format(n) for n in sio)
                                if sio else "")


def describe(module, decoded=None, units_shown=20):
    """Lines of text describing the module (and what the decoder found, if given)."""
    family = module.family()
    lines = []
    add = lines.append
    if decoded is not None:
        add("Blocks:")
        for b in decoded.blocks:
            crc = {True: "CRC ok", False: "CRC BAD", None: ""}[b.crc_ok]
            add("  0x{:04X}  {:5d} bytes  {:<34} {} {}".format(b.start, b.size, b.name, crc,
                                                                b.note).rstrip())
        add("  Image: {} bytes (0x0000-0x{:04X})".format(decoded.image_end,
                                                         max(decoded.image_end - 1, 0)))
        for level, where, message in decoded.issues:
            add("  {}: {}: {}".format(level, where, message))
    add("Header:")
    add("  Board vendor 0x{:04X}, product 0x{:04X}, version 0x{:04X}, SW version {}".format(
        module.vendor_id, module.product_id, module.version_id,
        ".".join(str(v) for v in module.sw_version)))
    add("  Target chip: {}".format(module.target_chip))
    add("Board information (ADI: module information):")
    add("  Device name '{}', type {}".format(module.device_name,
                                            _bits(module.device_type, L.DEVICE_TYPE_BITS)))
    add("  Power: CFG{}, {} powered, most {} mW, least {} mW, most {} mA".format(
        module.power_config, "locally" if module.local_power else "bus", module.max_power_mw,
        module.min_power_mw, module.max_current_ma))
    add("  B port: {} ({}); its cable {}".format(
        L.CABLE_TYPES.get(module.cable_type, "reserved {}".format(module.cable_type)),
        L.CABLE_TYPE_HINTS.get(module.cable_type, "the stack doesn't probe past it"),
        "{} m".format(module.cable_length) if module.cable_length else "not given"))
    add("  Minimum slot sizes: downstream {}, upstream {}".format(
        L.SLOT_SIZES.get(module.min_down_slot, "?"), L.SLOT_SIZES.get(module.min_up_slot, "?")))
    add("  Port ({} family): {}".format(family, R.port_summary(module.regs, family)))
    add("  Register settings ({}):".format(module.target_chip))
    for reg in R.REGISTERS:
        value = module.regs[reg.key]
        if family not in reg.families:
            if value != reg.power_on:
                add("    0x{:02X} {:<9} 0x{:02X} (not on this chip)".format(reg.offset, reg.name,
                                                                         value))
            continue
        mark = "" if value != reg.power_on else " (power-on value: not written)"
        add("    0x{:02X} {:<9} 0x{:02X}{}".format(reg.offset, reg.name, value, mark))
        if value != reg.power_on:
            for line in reg.describe(value, family):
                add("           " + line)
    if module.spi_tunnel or module.branches or module.local_micro:
        add("  SPI tunnel {} (down {}, up {}), branches {}, local micro {} (mode {}, "
            "peripherals {})".format(module.spi_tunnel, module.spi_tunnel_down,
                                     module.spi_tunnel_up, module.branches, module.local_micro,
                                     module.local_micro_mode, module.local_micro_peripherals))
    for direction, groups in (("TX", module.tx_groups), ("RX", module.rx_groups)):
        add("  {} groups: {}".format(direction, len(groups)))
        for g in groups:
            add("    {} '{}': channels {}".format(g.id, g.name, _channels(g.channels)))
    add("Supplier information: vendor '{}'".format(module.supplier_vendor_id))
    vendor_layout = module.vendor_layout()
    if vendor_layout is not None:
        values = module.vendor_values
        for item in vendor_layout.items:
            value = values.get(item.name)
            if isinstance(value, list):
                add("  {}s: {}".format(item.label, len(value)))
                for n, record in enumerate(value):
                    parts = ["{} {}".format(f.label, f.to_json(record.get(f.name, f.default())))
                             for f in item.fields]
                    add("    {}: {}".format(n + 1, ", ".join(parts)))
            else:
                add("  {}: {}".format(item.label, item.to_json(value)))
    elif module.vendor_data:
        add("  Data: {}".format(module.vendor_data.hex(" ").upper()))
    add("Peripherals: {}".format(len(module.peripherals)))
    for n, p in enumerate(module.peripherals):
        interface = {L.INTERFACE_I2C: "I2C", L.INTERFACE_SPI: "SPI",
                     L.INTERFACE_NONE: "not programmable"}.get(p.interface, str(p.interface))
        add("  {} '{}': {} at 0x{:02X}, TX groups {}, RX groups {}, RX {}, TX {}, widths "
            "address {} data {} command {}".format(
                n + 1, p.name, interface, p.address, _channels(p.tx_groups),
                _channels(p.rx_groups), _pins(p.rx_interface), _pins(p.tx_interface),
                p.addr_width, p.data_width, p.cmd_width))
        if p.xml:
            add("    Program: {}{}".format(p.xml, " ({})".format(p.xml_report.summary())
                                          if p.xml_report else ""))
        units = split_units(p)
        writes = [u for u in units if u.kind == "write"]
        add("    Units: {} ({} write(s), {} bytes, {} wait(s))".format(
            len(units), len(writes), sum(len(u.data) for u in writes), len(units) - len(writes)))
        for u in units[:units_shown]:
            if u.kind == "delay":
                add("      wait {} ms".format(u.delay_ms))
            else:
                head = u.data[:8].hex(" ").upper()
                add("      write 0x{:02X}: {} byte(s) {}{}".format(
                    u.address, len(u.data), head, " ..." if len(u.data) > 8 else ""))
        if len(units) > units_shown:
            add("      ... {} more".format(len(units) - units_shown))
    if module.reserved:
        add("Reserved bytes not 0: {}".format(", ".join(
            "0x{:03X}=0x{:02X}".format(k, v) for k, v in sorted(module.reserved.items()))))
    return lines
