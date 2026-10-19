"""Builds a module-info image (the .dat file: the EEPROM's bytes from address 0) from a
Module.

Layout, as ADI's images have it (layout.py):
- 0x000 header (CRC at 0x1F), 0x020 module-info block with the channel groups at 0x100
  and its CRC right after them;
- 0x300 Device Function Information block, 0x400 Supplier Information block (the vendor
  section), each 256 bytes with its CRC at its last byte;
- 0x800 the peripheral information block, then each peripheral block followed by its units,
  one after the other. The image ends with the last unit.

Copyright 2026 by ClockWorks Signal Processing LLC
Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
SPDX-License-Identifier: BSD-3-Clause
"""

from . import layout as L
from .crc8 import crc8
from .registers import NOT_STATED, REGISTERS, chip_code


class EncodeError(Exception):
    """The module can't be laid out (validate.check() says why, in more words)."""


class Encoded:
    """An image and where its parts are."""

    def __init__(self, image, regions, coverage, crc_offsets):
        self.image = image
        self.regions = regions  # (start, end exclusive, name), in address order.
        self.coverage = coverage  # 0x800 flags: the byte comes from a field.
        self.crc_offsets = crc_offsets  # The CRCs of 0x000-0x7FF.


def text_bytes(text, size):
    """`text` as ASCII, NUL-padded to `size` bytes (cut if longer)."""
    data = str(text).encode("ascii", "replace")[:size]
    return data + bytes(size - len(data))


def group_table(module):
    """The channel-group table's bytes: TX count and records, RX count and records."""
    out = bytearray()
    for groups in (module.tx_groups, module.rx_groups):
        out.append(len(groups) & 0xFF)
        for g in groups:
            out.append(g.id & 0xFF)
            out += text_bytes(g.name, L.GROUP_NAME_LEN)
            out += (g.channels & 0xFFFFFFFF).to_bytes(4, "little")
    return bytes(out)


def split_units(peripheral):
    """The peripheral's units, with writes longer than its split size cut into several,
    each with its own sub-address (peripheral.split_max; 0 keeps them whole)."""
    if not peripheral.split_max:
        return list(peripheral.units)
    out = []
    for unit in peripheral.units:
        width = unit.sub_address_width
        if width is None:
            width = peripheral.addr_width
        if unit.kind != "write" or len(unit.data) <= peripheral.split_max or width < 1:
            out.append(unit)
            continue
        step = peripheral.split_bytes_per_address
        chunk = ((peripheral.split_max - width) // step) * step
        if chunk <= 0:
            raise EncodeError("split size {} is too small for a {}-byte sub-address and {} "
                              "bytes per address".format(peripheral.split_max, width, step))
        base = int.from_bytes(unit.data[:width], "big")
        payload = unit.data[width:]
        for at in range(0, len(payload), chunk):
            address = base + at // step
            if address >= 1 << (8 * width):
                raise EncodeError("splitting a write runs its sub-address past {} bytes"
                                  .format(width))
            out.append(type(unit).write(unit.address,
                                        address.to_bytes(width, "big") + payload[at:at + chunk],
                                        crc=unit.crc, note=unit.note,
                                        sub_address_width=width))
    return out


def unit_bytes(unit):
    """A unit's bytes: header, payload, CRC."""
    if unit.kind == "delay":
        if not 0 <= unit.delay_ms <= L.MAX_DELAY_MS:
            raise EncodeError("a wait of {} ms (most: {})".format(unit.delay_ms, L.MAX_DELAY_MS))
        head = bytes([(L.TYPE_DELAY << 4) | ((unit.delay_ms >> 8) & 0x0F), unit.delay_ms & 0xFF])
        return head + bytes([crc8(head)])
    if not unit.data or len(unit.data) > L.MAX_WRITE:
        raise EncodeError("a write of {} bytes (1 to {})".format(len(unit.data), L.MAX_WRITE))
    if not 0 < unit.address <= 0x7F:
        raise EncodeError("a write to I2C address 0x{:02X}".format(unit.address))
    if unit.crc:
        out = L.block_header(L.TYPE_WRITE, len(unit.data) + 1, unit.address) + unit.data
        return out + bytes([crc8(out)])
    return L.block_header(L.TYPE_WRITE_NO_CRC, len(unit.data), unit.address) + unit.data


def peripheral_block(peripheral, unit_count):
    """A peripheral block's 83 bytes."""
    payload = bytearray(L.PERIPHERAL_BLOCK_PAYLOAD - 1)
    payload[0] = peripheral.interface & 0xFF
    payload[1:1 + L.PERIPHERAL_NAME_LEN] = text_bytes(peripheral.name, L.PERIPHERAL_NAME_LEN)
    payload[65] = peripheral.address & 0xFF
    L.put16(payload, 66, peripheral.tx_groups & 0xFFFF)
    L.put16(payload, 68, peripheral.rx_groups & 0xFFFF)
    L.put16(payload, 70, peripheral.rx_interface & 0xFFFF)
    L.put16(payload, 72, peripheral.tx_interface & 0xFFFF)
    payload[74] = peripheral.addr_width & 0xFF
    payload[75] = peripheral.data_width & 0xFF
    payload[76] = peripheral.cmd_width & 0xFF
    if unit_count > L.MAX_LENGTH:
        raise EncodeError("{} units in one peripheral block (most: {})".format(
            unit_count, L.MAX_LENGTH))
    payload[77] = (unit_count >> 8) & 0x0F  # 12 bits, high byte first.
    payload[78] = unit_count & 0xFF
    out = L.block_header(L.TYPE_PERIPHERAL, L.PERIPHERAL_BLOCK_PAYLOAD) + bytes(payload)
    return out + bytes([crc8(out)])


def encode(module, with_reserved=True):
    """The image of `module`. Its peripherals' units must be read already (project.py)."""
    img = bytearray(L.PERIPHERAL_INFO)
    covered = bytearray(L.PERIPHERAL_INFO)
    regions = []

    def put(at, data):
        img[at:at + len(data)] = data
        covered[at:at + len(data)] = b"\x01" * len(data)

    # Header.
    put(0, bytes([L.MARKER]))
    put(L.VENDOR_ID, (module.vendor_id & 0xFFFF).to_bytes(2, "little"))
    put(L.PRODUCT_ID, (module.product_id & 0xFFFF).to_bytes(2, "little"))
    put(L.VERSION_ID, (module.version_id & 0xFFFF).to_bytes(2, "little"))
    chip = chip_code(module.target_chip)
    if module.target_chip == NOT_STATED and module.target_chip_unstated is not None:
        chip = module.target_chip_unstated
    put(L.TARGET_CHIP, (chip & 0xFFFF).to_bytes(2, "little"))
    sw = (list(module.sw_version) + [0, 0, 0, 0])[:4]
    put(L.SW_VERSION, bytes(v & 0xFF for v in sw))
    put(L.PRIMARY_FUNCTION, L.MODULE_INFO.to_bytes(4, "little"))
    put(L.SECONDARY_FUNCTION, (module.secondary_function & 0xFFFFFFFF).to_bytes(4, "little"))
    regions.append((0x000, 0x020, "Header"))

    # Module-info block, with the channel groups.
    table = group_table(module)
    table_end = L.GROUP_TABLE + len(table)
    natural = table_end + 1 - (L.MODULE_INFO + 3)
    length = module.module_info_length if module.module_info_length is not None else natural
    if length < natural:
        raise EncodeError("the module-info block's length 0x{:X} is shorter than its channel "
                          "groups need (0x{:X})".format(length, natural))
    crc_at = L.MODULE_INFO + 3 + length - 1
    if crc_at >= L.DEVICE_FUNCTION:
        raise EncodeError("the channel groups run to 0x{:X}, past 0x{:X}: too many groups"
                          .format(crc_at, L.DEVICE_FUNCTION - 1))
    put(L.MODULE_INFO, L.block_header(L.TYPE_MODULE_INFO, length))
    put(L.POWER_CONFIG, bytes([((module.power_config & 0x0F) << 4) |
                               (1 if module.local_power else 0)]))
    put(L.MAX_POWER, (module.max_power_mw & 0xFFFF).to_bytes(2, "little"))
    put(L.MIN_POWER, (module.min_power_mw & 0xFFFF).to_bytes(2, "little"))
    put(L.CABLE_TYPE, bytes([module.cable_type & 0xFF]))
    put(L.MAX_CURRENT, (module.max_current_ma & 0xFFFF).to_bytes(2, "little"))
    put(L.DEVICE_NAME, text_bytes(module.device_name, L.DEVICE_NAME_LEN))
    put(L.DEVICE_TYPE, (module.device_type & 0xFFFFFFFF).to_bytes(4, "little"))
    put(L.CABLE_LENGTH, (module.cable_length & 0xFFFF).to_bytes(2, "little"))
    put(L.PERIPHERAL_INFO_PTR, L.PERIPHERAL_INFO.to_bytes(4, "little"))
    put(L.DEVICE_FUNCTION_PTR, L.DEVICE_FUNCTION.to_bytes(4, "little"))
    put(L.PERIPHERAL_COUNT, bytes([len(module.peripherals) & 0xFF]))
    put(L.MIN_DOWN_SLOT, bytes([module.min_down_slot & 0xFF]))
    put(L.MIN_UP_SLOT, bytes([module.min_up_slot & 0xFF]))
    for reg in REGISTERS:
        put(reg.offset, bytes([module.regs.get(reg.key, reg.power_on) & 0xFF]))
    put(L.SPI_TUNNEL, bytes([module.spi_tunnel & 0xFF]))
    put(L.SPI_TUNNEL_DOWN, bytes(v & 0xFF for v in module.spi_tunnel_down[:2]))
    put(L.SPI_TUNNEL_UP, bytes(v & 0xFF for v in module.spi_tunnel_up[:2]))
    put(L.BRANCHES, bytes([module.branches & 0xFF]))
    put(L.LOCAL_MICRO, bytes([module.local_micro & 0xFF]))
    put(L.LOCAL_MICRO_MODE, bytes([module.local_micro_mode & 0xFF]))
    put(L.LOCAL_MICRO_PERIPHERALS, bytes([module.local_micro_peripherals & 0xFF]))
    put(L.SUPPLIER_INFO_PTR, L.SUPPLIER_INFO.to_bytes(4, "little"))
    put(L.GROUP_TABLE, table)
    covered[table_end:crc_at + 1] = b"\x01" * (crc_at + 1 - table_end)
    regions.append((L.MODULE_INFO, L.GROUP_TABLE, "Module information"))
    regions.append((L.GROUP_TABLE, crc_at + 1, "Channel groups"))

    # Device Function Information block.
    base = L.DEVICE_FUNCTION
    function_length = (module.device_function_length
                       if module.device_function_length is not None else L.FIXED_BLOCK_PAYLOAD)
    put(base, L.block_header(L.TYPE_DEVICE_FUNCTION, function_length))
    data = bytes(module.device_function_data)[:L.FIXED_BLOCK_LEN - 4]
    put(base + 3, data + bytes(L.FIXED_BLOCK_LEN - 4 - len(data)))
    regions.append((base, base + L.FIXED_BLOCK_LEN, "Device function information"))

    # Supplier Information block: the vendor ID, then the vendor's section.
    base = L.SUPPLIER_INFO
    supplier_length = (module.supplier_length
                       if module.supplier_length is not None else L.FIXED_BLOCK_PAYLOAD)
    put(base, L.block_header(L.TYPE_SUPPLIER_INFO, supplier_length))
    put(L.SUPPLIER_VENDOR_ID, text_bytes(module.supplier_vendor_id, L.SUPPLIER_VENDOR_ID_LEN))
    vendor_layout = module.vendor_layout()
    if vendor_layout is not None:
        try:
            section = vendor_layout.encode(module.vendor_values)
        except (OverflowError, ValueError, TypeError) as error:
            raise EncodeError("the {} section: {}".format(vendor_layout.title, error))
        if len(section) > L.SUPPLIER_DATA_LEN:
            raise EncodeError("the {} section is {} bytes (most: {})".format(
                vendor_layout.title, len(section), L.SUPPLIER_DATA_LEN))
        put(L.SUPPLIER_DATA, section)
    else:
        section = bytes(module.vendor_data)[:L.SUPPLIER_DATA_LEN]
        put(L.SUPPLIER_DATA, section + bytes(L.SUPPLIER_DATA_LEN - len(section)))
    regions.append((base, base + L.FIXED_BLOCK_LEN, "Supplier information"))

    # Bytes the fields don't give (from an imported image), then the CRCs.
    crc_offsets = (L.HEADER_CRC, crc_at, L.DEVICE_FUNCTION + 0xFF, L.SUPPLIER_INFO + 0xFF)
    if with_reserved:
        for at, value in module.reserved.items():
            if 0 <= at < L.PERIPHERAL_INFO and at not in crc_offsets:
                img[at] = value & 0xFF
    img[L.HEADER_CRC] = crc8(img[0:L.HEADER_CRC])
    img[crc_at] = crc8(img[L.MODULE_INFO:crc_at])
    for block in (L.DEVICE_FUNCTION, L.SUPPLIER_INFO):
        img[block + 0xFF] = crc8(img[block:block + 0xFF])
    for at in crc_offsets:
        covered[at] = 1

    # Peripheral information block, then each peripheral block and its units.
    blocks = []
    for p in module.peripherals:
        units = split_units(p)
        blocks.append(peripheral_block(p, len(units)) + b"".join(unit_bytes(u) for u in units))
    info_len = 4 * len(blocks) + 1
    address = L.PERIPHERAL_INFO + 3 + info_len
    info = bytearray(L.block_header(L.TYPE_PERIPHERAL_INFO, info_len))
    for block in blocks:
        info += address.to_bytes(4, "little")
        address += len(block)
    info.append(crc8(info))
    tail = bytes(info)
    regions.append((L.PERIPHERAL_INFO, L.PERIPHERAL_INFO + len(info), "Peripheral information"))
    at = L.PERIPHERAL_INFO + len(info)
    for p, block in zip(module.peripherals, blocks):
        regions.append((at, at + len(block), "Peripheral '{}' and its units".format(p.name)))
        at += len(block)
        tail += block
    image = bytes(img) + tail
    return Encoded(image, regions, bytes(covered), crc_offsets)
