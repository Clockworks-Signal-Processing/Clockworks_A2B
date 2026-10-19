"""Reads a module-info image (a .dat file, or the bytes read back from an EEPROM) into a
Module, block by block, checking each CRC.

It reads what the stack reads, as the stack reads it (a2bcpp/src/module_info.cpp,
module_script.cpp): the peripheral information block at 0x800 whatever 0x6E says, the
peripheral block count at 0x7A, units of type 0, 1 and 2. It also reads what the stack
doesn't (names, the vendor blocks) and accepts what ADI's own images do: the two vendor
blocks' CRC at their last byte whatever their length says. Bytes of 0x000-0x7FF that no field
gives are kept in Module.reserved, so encoding the result gives the same image back; an
EEPROM read past the image (0xFF) is not part of it.

Copyright 2026 by ClockWorks Signal Processing LLC
Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
SPDX-License-Identifier: BSD-3-Clause
"""

from . import layout as L
from . import vendor
from .crc8 import crc8
from .encode import EncodeError, encode, group_table
from .model import Group, Module, Peripheral, Unit
from .registers import REGISTERS, chip_name, guess_family

ERROR = "error"
WARNING = "warning"
NOTE = "note"


class Block:
    """A block found in the image."""

    def __init__(self, name, start, size, crc_ok=None, note=""):
        self.name = name
        self.start = start
        self.size = size  # Header included.
        self.crc_ok = crc_ok  # None: no CRC checked.
        self.note = note


class Decoded:
    """What decode() found."""

    def __init__(self):
        self.module = None  # None if the image isn't one.
        self.blocks = []
        self.issues = []  # (level, where, message).
        self.image_end = 0  # Bytes of the image proper.

    def add(self, level, where, message):
        self.issues.append((level, where, message))

    @property
    def ok(self):
        return self.module is not None and not any(i[0] == ERROR for i in self.issues)


def _text(data):
    end = data.find(b"\x00")
    return (data if end < 0 else data[:end]).decode("ascii", "replace")


def decode(image):
    """Reads `image` (bytes). Decoded.module is None if it has no module info at all."""
    image = bytes(image)
    out = Decoded()
    if len(image) < L.REGS + 0x10:
        out.add(ERROR, "image", "only {} bytes: the start of an image is 0x90".format(len(image)))
        return out
    if image[0] != L.MARKER:
        what = "blank (0xFF)" if image[0] == 0xFF else "0x{:02X}".format(image[0])
        out.add(ERROR, "0x000", "no marker 0xAA: byte 0 is {}".format(what))
        return out
    m = Module()
    m.peripherals = []
    m.vendor_values = {}
    low = bytearray(image[:L.PERIPHERAL_INFO])
    low += bytes(L.PERIPHERAL_INFO - len(low))

    # Header.
    header_ok = crc8(image[:L.HEADER_CRC]) == image[L.HEADER_CRC]
    out.blocks.append(Block("Header", 0, 0x20, header_ok))
    if not header_ok:
        out.add(ERROR, "header", "CRC 0x{:02X}, should be 0x{:02X}".format(
            image[L.HEADER_CRC], crc8(image[:L.HEADER_CRC])))
    m.vendor_id = L.get16(low, L.VENDOR_ID)
    m.product_id = L.get16(low, L.PRODUCT_ID)
    m.version_id = L.get16(low, L.VERSION_ID)
    m.target_chip = chip_name(L.get16(low, L.TARGET_CHIP))
    if L.get16(low, L.TARGET_CHIP) == 0xFFFF:
        m.target_chip_unstated = 0xFFFF
    if m.chip() is None:  # External or not stated: show the settings as the product ID's chip.
        m.view_family = guess_family(m.product_id) or m.view_family
    m.sw_version = list(low[L.SW_VERSION:L.SW_VERSION + 4])
    m.secondary_function = L.get32(low, L.SECONDARY_FUNCTION)
    if L.get32(low, L.PRIMARY_FUNCTION) != L.MODULE_INFO:
        out.add(ERROR, "header", "primary function at 0x{:X}, not 0x20".format(
            L.get32(low, L.PRIMARY_FUNCTION)))

    # Module-info block.
    block_type = L.header_type(low[L.MODULE_INFO:])
    length = L.header_length(low[L.MODULE_INFO:])
    crc_at = L.MODULE_INFO + 3 + length - 1
    if block_type != L.TYPE_MODULE_INFO:
        out.add(ERROR, "0x020", "block type {}, not 3 (module information)".format(block_type))
    if length == 0 or crc_at >= L.PERIPHERAL_INFO:
        out.add(ERROR, "0x020", "module-info block length 0x{:X}".format(length))
        crc_at = None
    m.power_config = low[L.POWER_CONFIG] >> 4
    m.local_power = bool(low[L.POWER_CONFIG] & 0x01)
    m.max_power_mw = L.get16(low, L.MAX_POWER)
    m.min_power_mw = L.get16(low, L.MIN_POWER)
    m.cable_type = low[L.CABLE_TYPE]
    m.max_current_ma = L.get16(low, L.MAX_CURRENT)
    m.device_name = _text(low[L.DEVICE_NAME:L.DEVICE_NAME + L.DEVICE_NAME_LEN])
    m.device_type = L.get32(low, L.DEVICE_TYPE)
    m.cable_length = L.get16(low, L.CABLE_LENGTH)
    count = low[L.PERIPHERAL_COUNT]
    m.min_down_slot = low[L.MIN_DOWN_SLOT]
    m.min_up_slot = low[L.MIN_UP_SLOT]
    for reg in REGISTERS:
        m.regs[reg.key] = low[reg.offset]
    m.spi_tunnel = low[L.SPI_TUNNEL]
    m.spi_tunnel_down = list(low[L.SPI_TUNNEL_DOWN:L.SPI_TUNNEL_DOWN + 2])
    m.spi_tunnel_up = list(low[L.SPI_TUNNEL_UP:L.SPI_TUNNEL_UP + 2])
    m.branches = low[L.BRANCHES]
    m.local_micro = low[L.LOCAL_MICRO]
    m.local_micro_mode = low[L.LOCAL_MICRO_MODE]
    m.local_micro_peripherals = low[L.LOCAL_MICRO_PERIPHERALS]
    for name, at, want in (("peripheral information", L.PERIPHERAL_INFO_PTR, L.PERIPHERAL_INFO),
                           ("device function", L.DEVICE_FUNCTION_PTR, L.DEVICE_FUNCTION),
                           ("supplier information", L.SUPPLIER_INFO_PTR, L.SUPPLIER_INFO)):
        if L.get32(low, at) != want:
            out.add(NOTE, "0x{:03X}".format(at), "the {} block's address is 0x{:X}; the stack "
                    "reads it at 0x{:X}".format(name, L.get32(low, at), want))
    if crc_at is not None:
        block_ok = crc8(low[L.MODULE_INFO:crc_at]) == low[crc_at]
        out.blocks.append(Block("Module information", L.MODULE_INFO, crc_at + 1 - L.MODULE_INFO,
                                block_ok))
        if not block_ok:
            out.add(WARNING, "module information", "block CRC 0x{:02X}, should be 0x{:02X} (the "
                    "stack doesn't check it)".format(low[crc_at], crc8(low[L.MODULE_INFO:crc_at])))
        _read_groups(low, crc_at, m, out)
        natural = L.GROUP_TABLE + len(group_table(m)) + 1 - (L.MODULE_INFO + 3)
        if length != natural:
            m.module_info_length = length

    # The two vendor blocks.
    if len(image) >= L.SUPPLIER_INFO + L.FIXED_BLOCK_LEN:
        _read_fixed_block(low, L.DEVICE_FUNCTION, L.TYPE_DEVICE_FUNCTION,
                          "Device function information", m, out)
        _read_fixed_block(low, L.SUPPLIER_INFO, L.TYPE_SUPPLIER_INFO, "Supplier information", m,
                          out)
    else:
        out.add(WARNING, "0x300", "the image ends at 0x{:X}, before the vendor blocks".format(
            len(image)))
        m.device_function_length = None
        m.supplier_vendor_id = ""
        m.vendor_data = b""
    out.image_end = min(len(image), L.SUPPLIER_INFO + L.FIXED_BLOCK_LEN)

    # Peripherals.
    if count:
        _read_peripherals(image, count, m, out)
    elif len(image) >= L.PERIPHERAL_INFO + 4 and L.header_type(image[L.PERIPHERAL_INFO:]) == \
            L.TYPE_PERIPHERAL_INFO:
        info_len = L.header_length(image[L.PERIPHERAL_INFO:])
        out.image_end = max(out.image_end, L.PERIPHERAL_INFO + 3 + info_len)

    # What no field gave.
    try:
        encoded = encode(m, with_reserved=False)
    except EncodeError as error:
        out.add(WARNING, "image", "can't be built again as it is: {}".format(error))
        encoded = None
    for at in range(L.PERIPHERAL_INFO if encoded else 0):
        if not encoded.coverage[at] and low[at] != encoded.image[at]:
            m.reserved[at] = low[at]
    if m.reserved:
        out.add(NOTE, "reserved", "{} byte(s) outside the known fields are not 0 (kept)".format(
            len(m.reserved)))
    out.module = m
    return out


def _read_groups(low, crc_at, m, out):
    """The channel-group table from 0x100 to the module-info block's CRC."""
    at = L.GROUP_TABLE
    for direction in ("tx", "rx"):
        if at >= crc_at:
            out.add(ERROR, "channel groups", "the table runs past the block's end")
            return
        n = low[at]
        at += 1
        if n > L.MAX_GROUPS:
            out.add(ERROR, "channel groups", "{} {} groups (most: 15)".format(n, direction.upper()))
            return
        groups = []
        for _ in range(n):
            if at + L.GROUP_RECORD_LEN > crc_at:
                out.add(ERROR, "channel groups", "the table runs past the block's end")
                return
            record = low[at:at + L.GROUP_RECORD_LEN]
            groups.append(Group(record[0], _text(record[1:1 + L.GROUP_NAME_LEN]),
                                L.get32(record, 1 + L.GROUP_NAME_LEN)))
            at += L.GROUP_RECORD_LEN
        if direction == "tx":
            m.tx_groups = groups
        else:
            m.rx_groups = groups


def _read_fixed_block(low, base, block_type, name, m, out):
    """One of the two 256-byte vendor blocks, its CRC at its last byte."""
    found = L.header_type(low[base:])
    length = L.header_length(low[base:])
    crc_ok = crc8(low[base:base + 0xFF]) == low[base + 0xFF]
    note = ""
    if length != L.FIXED_BLOCK_PAYLOAD:
        note = "length 0x{:X}; CRC taken at 0x{:X}".format(length, base + 0xFF)
    out.blocks.append(Block(name, base, L.FIXED_BLOCK_LEN, crc_ok, note))
    if found != block_type:
        out.add(WARNING, "0x{:03X}".format(base), "block type {}, not {} ({})".format(
            found, block_type, name.lower()))
    if not crc_ok:
        out.add(WARNING, name.lower(), "CRC 0x{:02X} at 0x{:X}, should be 0x{:02X}".format(
            low[base + 0xFF], base + 0xFF, crc8(low[base:base + 0xFF])))
    if base == L.DEVICE_FUNCTION:
        m.device_function_length = None if length == L.FIXED_BLOCK_PAYLOAD else length
        m.device_function_data = bytes(low[base + 3:base + 0xFF]).rstrip(b"\x00")
        return
    m.supplier_length = None if length == L.FIXED_BLOCK_PAYLOAD else length
    raw_id = bytes(low[L.SUPPLIER_VENDOR_ID:L.SUPPLIER_VENDOR_ID + L.SUPPLIER_VENDOR_ID_LEN])
    m.supplier_vendor_id = _text(raw_id)
    data = bytes(low[L.SUPPLIER_DATA:L.SUPPLIER_DATA + L.SUPPLIER_DATA_LEN])
    layout_ = vendor.find(m.supplier_vendor_id)
    if layout_ is not None and raw_id == m.supplier_vendor_id.encode("ascii"):
        m.vendor_values, problems = layout_.decode(data)
        m.vendor_data = b""
        for problem in problems:
            out.add(WARNING, layout_.title + " section", problem)
    else:
        m.vendor_values = {}
        m.vendor_data = data.rstrip(b"\x00")


def _read_peripherals(image, count, m, out):
    """The peripheral information block at 0x800 and the blocks and units it points to."""
    if len(image) < L.PERIPHERAL_INFO + 3:
        out.add(ERROR, "0x800", "{} peripheral block(s), but the image ends at 0x{:X}".format(
            count, len(image)))
        return
    head = image[L.PERIPHERAL_INFO:L.PERIPHERAL_INFO + 3]
    info_len = L.header_length(head)
    end = L.PERIPHERAL_INFO + 3 + info_len
    if L.header_type(head) != L.TYPE_PERIPHERAL_INFO or end > len(image) or info_len == 0:
        out.add(ERROR, "0x800", "no peripheral information block (type {}, length {})".format(
            L.header_type(head), info_len))
        return
    info = image[L.PERIPHERAL_INFO:end]
    crc_ok = crc8(info[:-1]) == info[-1]
    out.blocks.append(Block("Peripheral information", L.PERIPHERAL_INFO, len(info), crc_ok))
    out.image_end = max(out.image_end, end)
    if not crc_ok:
        out.add(ERROR, "peripheral information", "CRC 0x{:02X}, should be 0x{:02X}".format(
            info[-1], crc8(info[:-1])))
    if info_len < 4 * count + 1:
        out.add(ERROR, "peripheral information", "{} addresses for {} blocks (0x7A)".format(
            (info_len - 1) // 4, count))
        return
    if info_len != 4 * count + 1:
        out.add(WARNING, "peripheral information", "room for {} addresses; 0x7A says {}".format(
            (info_len - 1) // 4, count))
    for i in range(count):
        address = L.get32(info, 3 + 4 * i)
        p = _read_peripheral(image, address, i, out)
        if p is None:
            return
        m.peripherals.append(p)


def _read_peripheral(image, address, index, out):
    where = "peripheral {} (0x{:X})".format(index + 1, address)
    size = 3 + L.PERIPHERAL_BLOCK_PAYLOAD
    if address + size > len(image):
        out.add(ERROR, where, "past the image's end (0x{:X})".format(len(image)))
        return None
    block = image[address:address + size]
    if L.header_type(block) != L.TYPE_PERIPHERAL or L.header_length(block) != \
            L.PERIPHERAL_BLOCK_PAYLOAD:
        out.add(ERROR, where, "not a peripheral block (type {}, length {})".format(
            L.header_type(block), L.header_length(block)))
        return None
    payload = block[3:]
    p = Peripheral()
    p.interface = payload[0]
    p.name = _text(payload[1:1 + L.PERIPHERAL_NAME_LEN])
    p.address = payload[65]
    p.tx_groups = L.get16(payload, 66)
    p.rx_groups = L.get16(payload, 68)
    p.rx_interface = L.get16(payload, 70)
    p.tx_interface = L.get16(payload, 72)
    p.addr_width = payload[74]
    p.data_width = payload[75]
    p.cmd_width = payload[76]
    units = ((payload[77] & 0x0F) << 8) | payload[78]
    crc_ok = crc8(block[:-1]) == block[-1]
    out.blocks.append(Block("Peripheral '{}'".format(p.name), address, size, crc_ok,
                            "{} unit(s)".format(units)))
    out.image_end = max(out.image_end, address + size)
    if not crc_ok:
        out.add(ERROR, where, "CRC 0x{:02X}, should be 0x{:02X}".format(block[-1],
                                                                      crc8(block[:-1])))
    at = address + size
    for n in range(units):
        unit_where = "{} unit {} (0x{:X})".format(where, n + 1, at)
        if at + 3 > len(image):
            out.add(ERROR, unit_where, "past the image's end")
            return p
        head = image[at:at + 3]
        kind = L.header_type(head)
        length = L.header_length(head)
        if kind == L.TYPE_DELAY:
            if crc8(head[:2]) != head[2]:
                out.add(ERROR, unit_where, "wait CRC 0x{:02X}, should be 0x{:02X}".format(
                    head[2], crc8(head[:2])))
            p.units.append(Unit.delay(length))
            at += 3
        elif kind in (L.TYPE_WRITE, L.TYPE_WRITE_NO_CRC):
            if at + 3 + length > len(image):
                out.add(ERROR, unit_where, "a write of {} bytes past the image's end".format(
                    length))
                return p
            body = image[at + 3:at + 3 + length]
            if kind == L.TYPE_WRITE:
                if crc8(image[at:at + 3 + length - 1]) != body[-1]:
                    out.add(ERROR, unit_where, "write CRC 0x{:02X}, should be 0x{:02X}".format(
                        body[-1], crc8(image[at:at + 3 + length - 1])))
                data = body[:-1]
            else:
                data = body
            if head[2] > 0x7F:
                out.add(ERROR, unit_where, "I2C address 0x{:02X} isn't 7-bit".format(head[2]))
            p.units.append(Unit.write(head[2], data, crc=(kind == L.TYPE_WRITE)))
            at += 3 + length
        else:
            out.add(ERROR, unit_where, "unit type {} (the stack runs 0, 1 and 2)".format(kind))
            return p
        out.image_end = max(out.image_end, at)
    return p
