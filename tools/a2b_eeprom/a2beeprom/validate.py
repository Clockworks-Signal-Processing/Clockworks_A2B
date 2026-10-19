"""Checks a Module before it is written: against the format, against what the stack does
with an image, against the target chip, and against the EEPROM.

Each finding is an Issue: an error (the image can't be made, or the stack would reject the
node or fail its set-up), a warning (it works, but probably not as meant), or a note (worth
knowing). Export is refused while there are errors.

Copyright 2026 by ClockWorks Signal Processing LLC
Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
SPDX-License-Identifier: BSD-3-Clause
"""

from . import layout as L
from . import registers as R
from .encode import EncodeError, encode, group_table, split_units

ERROR = "error"
WARNING = "warning"
NOTE = "note"

# A peripheral write through an ADI USBi, as measured on the bench (a2bcpp/docs/timing.md):
# the time per byte at the sub-node's I2C rate, 100 kHz or 400 kHz (I2CCFG.DATARATE). The
# USBi checks a transfer's status for about 0.3 s (a2bcpp/docs/porting.md), so a write is
# warned about once it takes longer than LONG_WRITE_S: about 1150 bytes at 100 kHz, 1540 at
# 400 kHz.
USBI_US_PER_BYTE = {False: 217, True: 162}
LONG_WRITE_S = 0.25


class Issue:
    def __init__(self, level, where, message):
        self.level = level
        self.where = where
        self.message = message

    def __repr__(self):
        return "{}: {}: {}".format(self.level, self.where, self.message)

    def __iter__(self):
        return iter((self.level, self.where, self.message))


def _ascii_problem(text, max_chars):
    if len(text) > max_chars:
        return "longer than {} characters".format(max_chars)
    if any(ord(c) < 0x20 or ord(c) > 0x7E for c in text):
        return "not printable ASCII"
    return None


def check(module):
    """Every problem with `module`, errors first. Peripheral units must be read already."""
    issues = []

    def add(level, where, message):
        issues.append(Issue(level, where, message))

    chip = module.chip()
    family = module.family()
    if chip is None and module.target_chip not in (R.EXTERNAL, R.NOT_STATED):
        add(ERROR, "target chip", "'{}' isn't one the stack supports ({}), External or "
            "Not stated".format(module.target_chip, ", ".join(c.name for c in R.CHIPS)))
    elif module.target_chip == R.NOT_STATED:
        add(NOTE, "target chip", "not stated (as ADI's images): any chip takes the image; its "
            "settings are checked as the {}'s".format(family))

    # Header.
    for name, value in (("vendor ID", module.vendor_id), ("product ID", module.product_id),
                        ("version ID", module.version_id)):
        if not 0 <= value <= 0xFFFF:
            add(ERROR, "header", "{} 0x{:X} isn't 16 bits".format(name, value))
    if len(module.sw_version) != 4 or any(not 0 <= v <= 255 for v in module.sw_version):
        add(ERROR, "header", "SW version is four numbers 0-255")
    if module.vendor_id == 0x00AD:
        add(NOTE, "header", "module vendor ID 0x00AD is ADI's")
    if module.vendor_id == L.CLOCKWORKS_MODULE_VENDOR and not _board_number(module.product_id):
        add(NOTE, "header", "product ID 0x{:04X} isn't a board number (AB0020: 0x0020)".format(
            module.product_id))

    # Module information.
    problem = _ascii_problem(module.device_name, L.DEVICE_NAME_LEN - 1)
    if problem:
        add(ERROR, "device name", problem)
    if not 0 <= module.power_config <= 15:
        add(ERROR, "power", "power configuration {} isn't 0-15".format(module.power_config))
    elif module.power_config not in (0, 4):
        add(WARNING, "power", "power configuration {} (the format names CFG0 and CFG4)".format(
            module.power_config))
    if module.external():
        add(NOTE, "power", "with a plug-in module, the stack uses the chip's own power "
            "configuration, not the image's: CFG4 on an AD2437, CFG0 on an AD2433 or AD242x")
    elif chip is not None and module.power_config != chip.power_config:
        add(WARNING, "power", "CFG{} for an {}, which has CFG{} only: the stack uses CFG{}".format(
            module.power_config, chip.name, chip.power_config, chip.power_config))
    for name, value in (("most power", module.max_power_mw), ("least power", module.min_power_mw),
                        ("most current", module.max_current_ma)):
        if not 0 <= value <= 0xFFFF:
            add(ERROR, "power", "{} {} isn't 0-65535".format(name, value))
    if module.min_power_mw > module.max_power_mw:
        add(WARNING, "power", "least power is more than most power")
    _check_b_port(module, chip, family, add)
    for name, value in (("downstream", module.min_down_slot), ("upstream", module.min_up_slot)):
        if value not in L.SLOT_SIZES:
            add(ERROR, "slots", "minimum {} slot size {} isn't 0-7".format(name, value))
    if (module.min_down_slot, module.min_up_slot) != (7, 7):
        add(NOTE, "slots", "the minimum slot sizes are stored, not used by this stack (the bus's "
            "slot format is the main node's)")

    _check_registers(module, chip, family, add)
    _check_groups(module, add)
    _check_peripherals(module, chip, add)
    _check_peripheral_pins(module, add)
    _check_vendor(module, add)

    # The whole image.
    try:
        image = encode(module).image
    except EncodeError as error:
        add(ERROR, "image", str(error))
        image = None
    if image is not None:
        if len(image) > L.MAX_IMAGE:
            add(ERROR, "image", "{} bytes: past the 16-bit EEPROM address range".format(len(image)))
        elif len(image) > module.eeprom_size:
            add(ERROR, "image", "{} bytes: more than the EEPROM's {}".format(len(image),
                                                                            module.eeprom_size))
    order = {ERROR: 0, WARNING: 1, NOTE: 2}
    issues.sort(key=lambda i: order[i.level])
    return issues


def _board_number(product_id):
    """True if the product ID reads as a board number: four decimal digits (0x0020)."""
    return all((product_id >> shift) & 0x0F <= 9 for shift in (0, 4, 8, 12))


def _check_b_port(module, chip, family, add):
    """The B port's type and cable, as the stack takes them (a2bcpp/src/link_profile.cpp)."""
    kind = module.cable_type
    name = L.CABLE_TYPES.get(kind)
    if not 0 <= kind <= 0xFF or name is None:
        add(ERROR, "B port", "B port type {} is reserved: the stack would not probe past this node "
            "(0-5: {})".format(kind, ", ".join(L.CABLE_TYPES.values())))
    elif kind in L.CUSTOM_CABLES:
        add(WARNING, "B port", "{}: the stack can't run this B port yet (\"Unsupported B port "
            "type\"): the chain ends at this node unless the app sets the link".format(name))
    elif kind in L.TWO_STEP_CABLES:
        if chip is not None and chip.name != "AD2437":
            add(NOTE, "B port", "{} on an {}: UTP (two-step needs the AD2437)".format(
                name, chip.name))
        else:
            add(NOTE, "B port", "{}: two-step on an AD2437, which the stack doesn't run yet (the "
                "chain ends at this node unless the app sets the link); UTP on other "
                "chips".format(name))
    length = module.cable_length
    if not 0 <= length <= 0xFFFF:
        add(ERROR, "B port", "cable length {} isn't 0-65535".format(length))
    elif length > L.MAX_CABLE_M:
        add(WARNING, "B port", "cable length {} m: longer than the data sheets' longest link, "
            "{} m (AD2437); the stack takes it".format(length, L.MAX_CABLE_M))
    elif length > L.MAX_CABLE_M_AD242X and (module.external() or family == R.AD242X):
        add(WARNING, "B port", "cable length {} m: an AD242x link is at most {} m".format(
            length, L.MAX_CABLE_M_AD242X))


def _external_checks(module, add):
    """A plug-in module: one set of settings for either family (registers.py,
    I2SCFG_SHARED): what reads differently is an error, what only the AD243x has a note."""
    regs = module.regs
    i2scfg = regs["i2scfg"]
    if not R.i2scfg_shared(i2scfg):
        add(ERROR, "I2SCFG", "0x{:02X} reads differently: {} {}; {} {}. With a plug-in module, "
            "use one data pin each way (DTX0, DRX0) or none".format(
                i2scfg, R.AD242X, R.port_summary(regs, R.AD242X), R.AD243X,
                R.port_summary(regs, R.AD243X)))
    for key, bits in R.CONFLICT_BITS.items():
        reg = R.find_register(key)
        if regs[key] & bits:
            names = [f.name for f in reg.fields if f.mask == bits]  # AD242x's, then AD243x's.
            add(ERROR, reg.name, "bit 0x{:02X} is {} on the AD242x and {} on the AD243x: keep it "
                "0 with a plug-in module".format(bits, names[0], names[1]))
    for reg in R.REGISTERS:
        value = regs.get(reg.key, reg.power_on)
        only = R.ad243x_only_fields(reg)
        if not only or not 0 <= value <= 0xFF:
            continue
        mask = 0
        for field in only:
            mask |= field.mask
        if reg.families == (R.AD243X,):
            if value != reg.power_on:
                add(NOTE, reg.name, "0x{:02X}: AD243x only; the stack doesn't write it on an "
                    "AD242x module".format(value))
        elif reg.key in R.AD242X_UNUSED_BITS and value & mask:
            add(NOTE, reg.name, "{}: AD243x only; the stack writes these bits clear on an AD242x "
                "module".format(", ".join(f.name for f in only if f.get(value))))


def _check_i2ccfg(value, family, add):
    """The stack takes DATARATE alone from the image's I2CCFG (a2bcpp/src/module_info.cpp)."""
    if value & 0x01:
        add(NOTE, "I2CCFG", "DATARATE: the node's I2C port moves to 400 kHz once the image's "
            "start is read (at 100 kHz); the EEPROM, every peripheral and the pull-ups must "
            "take 400 kHz")
    reg = next(r for r in R.REGISTERS if r.key == "i2ccfg")
    ignored = [f.name for f in reg.fields_for(family)
               if f.name != "DATARATE" and f.get(value)]
    if ignored:
        add(WARNING, "I2CCFG", "{} set, but the stack takes only DATARATE from the image (it "
            "sets FRAMERATE for a 44.1 kHz bus itself)".format(", ".join(ignored)))


def _check_registers(module, chip, family, add):
    if module.external():
        # A plug-in module: the settings must suit either family.
        found = []

        def once(level, where, message):
            if (level, where, message) not in found:
                found.append((level, where, message))
                add(level, where, message)

        for each in R.BOTH:
            _check_registers_as(module, None, each, once, quiet_missing=True)
        _external_checks(module, add)
        return
    _check_registers_as(module, chip, family, add)


def _check_registers_as(module, chip, family, add, quiet_missing=False):
    regs = module.regs
    for reg in R.REGISTERS:
        value = regs.get(reg.key, reg.power_on)
        if not 0 <= value <= 0xFF:
            add(ERROR, reg.name, "0x{:X} isn't a byte".format(value))
            continue
        if family not in reg.families:
            if quiet_missing:
                continue
            if value != reg.power_on:
                add(WARNING, reg.name, "set, but the {} has no {} (the stack doesn't write it)"
                    .format(chip.name if chip else family, reg.name))
            continue
        undefined = value & ~reg.defined_mask(family) & 0xFF
        if quiet_missing and family == R.AD242X:
            undefined &= ~R.AD242X_UNUSED_BITS.get(reg.key, 0)  # The stack clears them there.
        if undefined:
            add(WARNING, reg.name, "reserved bits 0x{:02X} set".format(undefined))
        for field in reg.fields_for(family):
            if field.values is not None and field.get(value) not in field.values:
                add(ERROR, reg.name, "{} = {} is reserved".format(field.name, field.get(value)))
    _check_i2ccfg(regs["i2ccfg"], family, add)
    rate = regs["i2srate"]
    if rate & 0xC0 or (rate & 0x07) not in (0, 5, 6):
        add(ERROR, "I2SRATE", "the stack refuses a reduced rate (SHARE, REDUCE, or a rate other "
            "than 1x, 2x, 4x): the node would be rejected")
    mode = regs["i2sgcfg"] & 0x07
    if mode in (3, 5, 6):
        add(ERROR, "I2SGCFG", "TDM{} has no sub-node support".format({3: 12, 5: 20, 6: 24}[mode]))
    i2scfg = regs["i2scfg"]
    if family == R.AD242X:
        pins = bin(i2scfg & 0x33).count("1")
        if mode == 7 and (bin(i2scfg & 0x03).count("1") > 1 or bin(i2scfg & 0x30).count("1") > 1):
            add(ERROR, "I2SGCFG", "TDM32 works on one data pin each way only (AD242x)")
    else:
        pins = (i2scfg & 0x07) + ((i2scfg >> 4) & 0x07)
        if (i2scfg & 0x07) in (5, 6) or ((i2scfg >> 4) & 0x07) in (5, 6):
            add(ERROR, "I2SCFG", "a pin count of 5 or 6 is reserved")
    if chip is not None and not chip.has_i2s and pins:
        add(WARNING, "I2SCFG", "I2S/TDM pins set, but the {} has no I2S/TDM port".format(
            chip.name))
    pdm = regs["pdmctl"]
    if (pdm >> 5) & 0x03 == 3:
        add(ERROR, "PDMCTL", "PDMRATE 3 is reserved")
    if (regs["pdmctl2"] & 0x03) == 3:
        add(ERROR, "PDMCTL2", "PDMDEST 3 is reserved")
    if family == R.AD243X and regs["clk2cfg"] & 0x80 and (regs["spicfg"] & 0x03) != 2:
        add(WARNING, "CLK2CFG", "CLKOUT2 on, but SPI isn't off (SPICFG.SPIMODE 2): ADR2/CLKOUT2 "
            "doesn't work with SPI on and the default GPIO select (AD2437 TRM)")
    both = regs["gpiooen"] & regs["gpioien"]
    if both:
        add(WARNING, "GPIO", "pins 0x{:02X} are both outputs and inputs".format(both))
    stray = regs["pinten"] & ~regs["gpioien"] & 0xFF
    if stray:
        add(WARNING, "PINTEN", "interrupts on pins 0x{:02X}, which aren't inputs".format(stray))


def _check_groups(module, add):
    for direction, groups in (("TX", module.tx_groups), ("RX", module.rx_groups)):
        if len(groups) > L.MAX_GROUPS:
            add(ERROR, direction + " groups", "{} groups (most: 15)".format(len(groups)))
        elif len(groups) > L.STACK_MAX_GROUPS:
            add(WARNING, direction + " groups", "{} groups: the stack keeps the first {}".format(
                len(groups), L.STACK_MAX_GROUPS))
        seen = set()
        for g in groups:
            where = "{} group {}".format(direction, g.id)
            if not 0 <= g.id <= 0xFF:
                add(ERROR, where, "ID isn't 0-255")
            if g.id in seen:
                add(ERROR, where, "two {} groups with ID {}".format(direction, g.id))
            seen.add(g.id)
            problem = _ascii_problem(g.name, L.GROUP_NAME_LEN - 1)
            if problem:
                add(ERROR, where, "name " + problem)
            if not 0 <= g.channels <= 0xFFFFFFFF:
                add(ERROR, where, "channels aren't 32 bits")
            elif g.channels == 0:
                add(WARNING, where, "no channels")
    end = L.GROUP_TABLE + len(group_table(module)) + 1
    if end > L.DEVICE_FUNCTION:
        add(ERROR, "groups", "the groups take 0x100-0x{:X}, past 0x2FF: at most 13 in all".format(
            end - 1))


def _check_peripherals(module, chip, add):
    if len(module.peripherals) > L.STACK_MAX_PERIPHERALS:
        add(ERROR, "peripherals", "{} peripheral blocks (the stack runs at most {})".format(
            len(module.peripherals), L.STACK_MAX_PERIPHERALS))
    tx_ids = {g.id for g in module.tx_groups}
    rx_ids = {g.id for g in module.rx_groups}
    for n, p in enumerate(module.peripherals):
        where = "peripheral {} '{}'".format(n + 1, p.name)
        problem = _ascii_problem(p.name, L.PERIPHERAL_NAME_LEN - 1)
        if problem:
            add(ERROR, where, "name " + problem)
        if p.interface == L.INTERFACE_SPI:
            add(WARNING, where, "SPI: the stack skips SPI peripherals' units (not supported yet)")
            if chip is not None and not chip.has_spi:
                add(ERROR, where, "SPI, but the {} has no SPI".format(chip.name))
        elif p.interface not in (L.INTERFACE_I2C, L.INTERFACE_NONE):
            add(ERROR, where, "interface {} (0 I2C, 1 SPI, 0xFF none)".format(p.interface))
        if p.interface == L.INTERFACE_I2C and not 0 <= p.address <= 0x7F:
            add(ERROR, where, "I2C address 0x{:X} isn't 7-bit".format(p.address))
        for bit in range(16):
            if p.tx_groups >> bit & 1 and bit not in tx_ids:
                add(WARNING, where, "fed by TX group {}, which the module hasn't got".format(bit))
            if p.rx_groups >> bit & 1 and bit not in rx_ids:
                add(WARNING, where, "feeds RX group {}, which the module hasn't got".format(bit))
        if p.interface == L.INTERFACE_NONE and p.units:
            add(WARNING, where, "not programmable, but it has units")
        if p.xml_report is not None and p.xml_report.reads_dropped:
            add(NOTE, where, "{} read(s) in its XML dropped: a peripheral script only writes and "
                "waits".format(p.xml_report.reads_dropped))
        if p.xml_report is not None and p.interface == L.INTERFACE_I2C:
            others = p.xml_report.addresses - {p.address}
            if others:
                add(NOTE, where, "its XML writes to {} as well as its own address 0x{:02X}".format(
                    ", ".join("0x{:02X}".format(a) for a in sorted(others)), p.address))
        try:
            units = split_units(p)
        except EncodeError as error:
            add(ERROR, where, str(error))
            continue
        longest = max([len(u.data) for u in units if u.kind == "write"] or [0])
        fast = bool(module.regs["i2ccfg"] & 0x01)
        seconds = longest * USBI_US_PER_BYTE[fast] / 1e6
        if seconds > LONG_WRITE_S:
            add(WARNING, where, "a write of {} bytes: about {:.2f} s through an ADI USBi at {} kHz, "
                "which reads a transfer's status for about 0.3 s (split it)".format(
                    longest, seconds, 400 if fast else 100))
        for i, u in enumerate(units):
            unit_where = "{} unit {}".format(where, i + 1)
            if u.kind == "delay" and not 0 <= u.delay_ms <= L.MAX_DELAY_MS:
                add(ERROR, unit_where, "a wait of {} ms (most: 4095)".format(u.delay_ms))
            if u.kind == "write":
                if not u.data or len(u.data) > L.MAX_WRITE:
                    add(ERROR, unit_where, "a write of {} bytes (1 to 4094)".format(len(u.data)))
                if not 0 < u.address <= 0x7F:
                    add(ERROR, unit_where, "I2C address 0x{:02X}".format(u.address))


def _pin_names(streams, value):
    """The streams of the SIO pins set in an interface field's bits 4:0."""
    return [streams.get(sio, "SIO{}".format(sio)) for sio in range(5) if value >> sio & 1]


def _check_peripheral_pins(module, add):
    """Each peripheral's A2B pins (its RX and TX interface fields) against the transceiver's
    settings and the channel groups it names. The stack doesn't use the fields; they describe
    the board, and ADI's images fill them in."""
    regs = module.regs
    family = module.family()
    streams = R.sio_streams(regs)
    enabled = dict(zip(("rx", "tx"), R.enabled_streams(regs, family, module.external())))
    per_pin = R.TDM_CHANNELS.get(regs["i2sgcfg"] & 0x07)
    used = {"rx": set(), "tx": set()}
    groups = {"rx": {g.id: g for g in module.rx_groups}, "tx": {g.id: g for g in module.tx_groups}}
    for n, p in enumerate(module.peripherals):
        where = "peripheral {} '{}'".format(n + 1, p.name)
        for direction, field, ids, data in (("rx", p.rx_interface, p.rx_groups, "DRX"),
                                            ("tx", p.tx_interface, p.tx_groups, "DTX")):
            label = {"rx": "RX", "tx": "TX"}[direction]
            sio = field & 0x1F
            count = (field >> 5) & 0x07
            if count != bin(sio).count("1"):
                add(ERROR, where, "its {} pin count is {}, but it names {} pin(s)".format(
                    label, count, bin(sio).count("1")))
            if ids and not sio:
                add(ERROR, where, "{} {} group(s) {}, but it is on no A2B {} pin ({}n)".format(
                    "feeds" if direction == "rx" else "fed by", label,
                    ",".join(str(b) for b in range(16) if ids >> b & 1), label, data))
            names = _pin_names(streams[direction], sio)
            used[direction].update(names)
            off = [name for name in names if name not in enabled[direction]]
            if off:
                add(ERROR, where, "on {}, which the transceiver's settings don't {} ({})".format(
                    ", ".join(off), "turn on" if not module.external() else
                    "turn on for both families", ", ".join(sorted(enabled[direction])) or
                    "no {} pin on".format(label)))
            # Each channel of its groups is on pin channel / channels-per-pin.
            if per_pin is None or not sio or (direction == "rx" and regs["pdmctl"] & 0x05):
                continue
            for bit in range(16):
                group = groups[direction].get(bit) if ids >> bit & 1 else None
                if group is None:
                    continue
                for channel in range(32):
                    pin = "{}{}".format(data, channel // per_pin)
                    if group.channels >> channel & 1 and pin not in names:
                        add(WARNING, where, "{} group {}'s channel {} is on {}, which it isn't "
                            "on".format(label, bit, channel, pin))
                        break
    if not module.peripherals:
        return
    for direction in ("rx", "tx"):
        for name in sorted(enabled[direction] - used[direction]):
            add(NOTE, "peripherals", "{} is on, but no peripheral block is on it (fine for a "
                "header, or a part with no program)".format(name))


def _check_vendor(module, add):
    problem = _ascii_problem(module.supplier_vendor_id, L.SUPPLIER_VENDOR_ID_LEN)
    if problem:
        add(ERROR, "supplier vendor ID", problem)
    vendor_layout = module.vendor_layout()
    if vendor_layout is None:
        if len(module.vendor_data) > L.SUPPLIER_DATA_LEN:
            add(ERROR, "vendor data", "{} bytes (most: {})".format(len(module.vendor_data),
                                                                    L.SUPPLIER_DATA_LEN))
        return
    if vendor_layout.size > L.SUPPLIER_DATA_LEN:
        add(ERROR, vendor_layout.title, "the section takes {} bytes (most: {})".format(
            vendor_layout.size, L.SUPPLIER_DATA_LEN))
    for where, message in vendor_layout.check(module.vendor_values):
        add(ERROR, "{} {}".format(vendor_layout.title, where), message)
