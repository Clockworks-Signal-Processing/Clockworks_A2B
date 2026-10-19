"""A module-info image as fields: what the GUI edits, the project file holds, and the
encoder (encode.py) turns into bytes.

Copyright 2026 by ClockWorks Signal Processing LLC
Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
SPDX-License-Identifier: BSD-3-Clause
"""

import copy

from . import layout, registers, vendor
from .vendor import clockworks

PROJECT_FORMAT = "a2b-eeprom-project"
PROJECT_VERSION = 1
TOOL_VERSION = (1, 0, 0, 0)  # Written to the image's SW Version (0x09).


class Unit:
    """A peripheral configuration unit: a write (sub-address and data, one I2C transfer)
    or a wait."""

    def __init__(self, kind, address=0, data=b"", delay_ms=0, crc=True, note="",
                 sub_address_width=None):
        self.kind = kind  # "write" or "delay".
        self.address = address  # Write: the 7-bit I2C address.
        self.data = bytes(data)  # Write: sub-address, then data.
        self.delay_ms = delay_ms  # Delay: ms, 0-4095.
        self.crc = crc  # Write: type 1 (with a CRC-8); type 0 without.
        self.note = note  # Where it came from; not stored in the image.
        # Write: the sub-address's bytes, when known (from the XML); else the peripheral's
        # address width. Only splitting uses it; not stored in the image.
        self.sub_address_width = sub_address_width

    @staticmethod
    def write(address, data, crc=True, note="", sub_address_width=None):
        return Unit("write", address=address, data=data, crc=crc, note=note,
                    sub_address_width=sub_address_width)

    @staticmethod
    def delay(ms, note=""):
        return Unit("delay", delay_ms=ms, note=note)

    def to_json(self):
        if self.kind == "delay":
            return {"delay_ms": self.delay_ms}
        out = {"write": self.data.hex(" ").upper(), "address": "0x{:02X}".format(self.address)}
        if not self.crc:
            out["crc"] = False
        return out

    @staticmethod
    def from_json(data):
        if "delay_ms" in data:
            return Unit.delay(int(data["delay_ms"]))
        return Unit.write(_number(data.get("address", 0)), bytes.fromhex(data["write"]),
                          crc=bool(data.get("crc", True)))

    def __eq__(self, other):
        return (isinstance(other, Unit) and self.kind == other.kind and
                (self.delay_ms == other.delay_ms if self.kind == "delay" else
                 (self.address, self.data, self.crc) == (other.address, other.data, other.crc)))


class Peripheral:
    """A peripheral configuration block: the peripheral and its units."""

    def __init__(self):
        self.name = ""
        self.interface = layout.INTERFACE_I2C
        self.address = 0  # 7-bit I2C address (or SPI slave select).
        self.tx_groups = 0  # Bit n: TX group ID n feeds it.
        self.rx_groups = 0  # Bit n: it feeds RX group ID n.
        self.rx_interface = 0  # Bits 7:5 the number of the A2B's RX pins it is on, 4:0 which.
        self.tx_interface = 0
        self.addr_width = 1  # Sub-address bytes of its registers.
        self.data_width = 1
        self.cmd_width = 0
        self.xml = ""  # Its program, a SigmaStudio XML file (relative to the project file).
        self.units = []  # Its units: read from the XML, or held in the project.
        self.split_max = 0  # Longest write when splitting; 0: no splitting.
        self.split_bytes_per_address = 1  # Data bytes per sub-address step.
        self.xml_report = None  # What the XML reader found (sigma_xml.XmlProgram).

    def to_json(self):
        out = {
            "name": self.name,
            "interface": {layout.INTERFACE_I2C: "i2c", layout.INTERFACE_SPI: "spi",
                          layout.INTERFACE_NONE: "none"}.get(self.interface, self.interface),
            "address": "0x{:02X}".format(self.address),
            "tx_groups": "0x{:04X}".format(self.tx_groups),
            "rx_groups": "0x{:04X}".format(self.rx_groups),
            "rx_interface": "0x{:04X}".format(self.rx_interface),
            "tx_interface": "0x{:04X}".format(self.tx_interface),
            "addr_width": self.addr_width,
            "data_width": self.data_width,
            "cmd_width": self.cmd_width,
        }
        if self.split_max:
            out["split"] = {"max_bytes": self.split_max,
                            "bytes_per_address": self.split_bytes_per_address}
        if self.xml:
            out["xml"] = self.xml
        else:
            out["units"] = [u.to_json() for u in self.units]
        return out

    @staticmethod
    def from_json(data):
        p = Peripheral()
        p.name = data.get("name", "")
        interface = data.get("interface", "i2c")
        p.interface = {"i2c": layout.INTERFACE_I2C, "spi": layout.INTERFACE_SPI,
                       "none": layout.INTERFACE_NONE}.get(interface, None)
        if p.interface is None:
            p.interface = _number(interface)
        p.address = _number(data.get("address", 0))
        p.tx_groups = _number(data.get("tx_groups", 0))
        p.rx_groups = _number(data.get("rx_groups", 0))
        p.rx_interface = _number(data.get("rx_interface", 0))
        p.tx_interface = _number(data.get("tx_interface", 0))
        p.addr_width = _number(data.get("addr_width", 1))
        p.data_width = _number(data.get("data_width", 1))
        p.cmd_width = _number(data.get("cmd_width", 0))
        split = data.get("split")
        if split:
            p.split_max = _number(split.get("max_bytes", 0))
            p.split_bytes_per_address = _number(split.get("bytes_per_address", 1))
        p.xml = data.get("xml", "")
        p.units = [Unit.from_json(u) for u in data.get("units", [])]
        return p


class Group:
    """A channel group: its ID, its name and its TDM channels (bit n: channel n)."""

    def __init__(self, group_id=0, name="", channels=0):
        self.id = group_id
        self.name = name
        self.channels = channels

    def to_json(self):
        return {"id": self.id, "name": self.name, "channels": "0x{:08X}".format(self.channels)}

    @staticmethod
    def from_json(data):
        return Group(_number(data["id"]), data.get("name", ""), _number(data.get("channels", 0)))


class Module:
    """Everything a module-info image holds, and the project's own settings."""

    def __init__(self):
        # The project's own (not in the image).
        self.eeprom_size = layout.DEFAULT_EEPROM_SIZE
        self.notes = ""
        # With no chip named (External, not stated): the family the GUI shows the settings as.
        self.view_family = registers.AD243X
        # Header.
        # The transceiver the settings are for (0x07): a chip's name, registers.EXTERNAL (a
        # plug-in transceiver module) or registers.NOT_STATED (ADI's images).
        self.target_chip = registers.EXTERNAL
        # An imported image's field when it is not stated but not 0 (0xFFFF), so it rebuilds
        # the same; None otherwise.
        self.target_chip_unstated = None
        self.vendor_id = layout.CLOCKWORKS_MODULE_VENDOR
        self.product_id = 0x0000  # Clockworks: the board's number (AB0020: 0x0020).
        self.version_id = 0x0010
        self.sw_version = list(TOOL_VERSION)
        self.secondary_function = 0
        # Module-info block.
        self.power_config = 4  # CFG4.
        self.local_power = False
        self.max_power_mw = 0
        self.min_power_mw = 0
        self.max_current_ma = 0
        self.cable_type = 2  # The B port: UTP.
        self.cable_length = 0  # The B-side cable in metres; 0: not given.
        self.device_name = ""
        self.device_type = 0
        self.min_down_slot = 7  # No preference.
        self.min_up_slot = 7
        self.regs = registers.default_registers()
        self.spi_tunnel = 0
        self.spi_tunnel_down = [0, 0]
        self.spi_tunnel_up = [0, 0]
        self.branches = 0
        self.local_micro = 0
        self.local_micro_mode = 0
        self.local_micro_peripherals = 0
        self.tx_groups = []
        self.rx_groups = []
        self.module_info_length = None  # None: just what the groups need.
        # Device Function Information block (0x300).
        self.device_function_length = None  # None: 0xFD.
        self.device_function_data = b""  # From 0x303.
        # Supplier Information block (0x400).
        self.supplier_length = None  # None: 0xFD.
        self.supplier_vendor_id = clockworks.VENDOR_ID
        self.vendor_values = clockworks.LAYOUT.defaults()  # When the layout is known.
        self.vendor_data = b""  # Raw, when it isn't (from 0x42B).
        # Peripherals.
        self.peripherals = []
        # Bytes of 0x000-0x7FF the fields above don't give (reserved, kept from an imported
        # image): offset -> value.
        self.reserved = {}

    def chip(self):
        return registers.find_chip(self.target_chip)

    def family(self):
        """The family the settings read as: the target chip's, else the view family."""
        chip = self.chip()
        return chip.family if chip else self.view_family

    def external(self):
        """The image names no chip because the transceiver is a plug-in module."""
        return self.target_chip == registers.EXTERNAL

    def vendor_layout(self):
        return vendor.find(self.supplier_vendor_id)

    def copy(self):
        return copy.deepcopy(self)

    def to_json(self):
        layout_ = self.vendor_layout()
        out = {
            "format": PROJECT_FORMAT,
            "version": PROJECT_VERSION,
            "target_chip": self.target_chip,
            "view_as": self.view_family,
            "eeprom_size": self.eeprom_size,
            "notes": self.notes,
            "header": {
                "vendor_id": "0x{:04X}".format(self.vendor_id),
                "product_id": "0x{:04X}".format(self.product_id),
                "version_id": "0x{:04X}".format(self.version_id),
                "sw_version": ".".join(str(v) for v in self.sw_version),
                "secondary_function": "0x{:08X}".format(self.secondary_function),
            },
            "module": {
                "device_name": self.device_name,
                "device_type": "0x{:08X}".format(self.device_type),
                "power_config": self.power_config,
                "local_power": self.local_power,
                "max_power_mw": self.max_power_mw,
                "min_power_mw": self.min_power_mw,
                "max_current_ma": self.max_current_ma,
                "cable_type": self.cable_type,
                "cable_length": self.cable_length,
                "min_down_slot": self.min_down_slot,
                "min_up_slot": self.min_up_slot,
                "spi_tunnel": self.spi_tunnel,
                "spi_tunnel_down": list(self.spi_tunnel_down),
                "spi_tunnel_up": list(self.spi_tunnel_up),
                "branches": self.branches,
                "local_micro": self.local_micro,
                "local_micro_mode": self.local_micro_mode,
                "local_micro_peripherals": self.local_micro_peripherals,
            },
            "registers": {key: "0x{:02X}".format(value) for key, value in self.regs.items()},
            "groups": {
                "tx": [g.to_json() for g in self.tx_groups],
                "rx": [g.to_json() for g in self.rx_groups],
            },
            "peripherals": [p.to_json() for p in self.peripherals],
            "supplier": {"vendor_id": self.supplier_vendor_id},
        }
        if self.module_info_length is not None:
            out["module"]["block_length"] = self.module_info_length
        if layout_ is not None:
            out["supplier"]["values"] = layout_.to_json(self.vendor_values)
        else:
            out["supplier"]["data"] = self.vendor_data.hex(" ").upper()
        if self.supplier_length is not None:
            out["supplier"]["block_length"] = self.supplier_length
        if self.device_function_length is not None or self.device_function_data:
            out["device_function"] = {"data": self.device_function_data.hex(" ").upper()}
            if self.device_function_length is not None:
                out["device_function"]["block_length"] = self.device_function_length
        if self.reserved:
            out["reserved"] = {"0x{:03X}".format(k): "0x{:02X}".format(v)
                               for k, v in sorted(self.reserved.items())}
        return out

    @staticmethod
    def from_json(data):
        if data.get("format") != PROJECT_FORMAT:
            raise ValueError("not an A2B EEPROM project file")
        if data.get("version", 0) > PROJECT_VERSION:
            raise ValueError("made by a newer version of this tool")
        m = Module()
        m.target_chip = data.get("target_chip", m.target_chip)
        m.view_family = data.get("view_as", m.view_family)
        if m.view_family not in registers.BOTH:
            raise ValueError("view_as '{}' isn't {} or {}".format(m.view_family, *registers.BOTH))
        m.eeprom_size = _number(data.get("eeprom_size", m.eeprom_size))
        m.notes = data.get("notes", "")
        header = data.get("header", {})
        m.vendor_id = _number(header.get("vendor_id", m.vendor_id))
        m.product_id = _number(header.get("product_id", m.product_id))
        m.version_id = _number(header.get("version_id", m.version_id))
        if "sw_version" in header:
            m.sw_version = [int(v) for v in str(header["sw_version"]).split(".")]
        m.secondary_function = _number(header.get("secondary_function", 0))
        mod = data.get("module", {})
        m.device_name = mod.get("device_name", "")
        m.device_type = _number(mod.get("device_type", 0))
        m.power_config = _number(mod.get("power_config", m.power_config))
        m.local_power = bool(mod.get("local_power", False))
        m.max_power_mw = _number(mod.get("max_power_mw", 0))
        m.min_power_mw = _number(mod.get("min_power_mw", 0))
        m.max_current_ma = _number(mod.get("max_current_ma", 0))
        m.cable_type = _number(mod.get("cable_type", m.cable_type))
        m.cable_length = _number(mod.get("cable_length", 0))
        m.min_down_slot = _number(mod.get("min_down_slot", m.min_down_slot))
        m.min_up_slot = _number(mod.get("min_up_slot", m.min_up_slot))
        m.spi_tunnel = _number(mod.get("spi_tunnel", 0))
        m.spi_tunnel_down = [_number(v) for v in mod.get("spi_tunnel_down", [0, 0])]
        m.spi_tunnel_up = [_number(v) for v in mod.get("spi_tunnel_up", [0, 0])]
        m.branches = _number(mod.get("branches", 0))
        m.local_micro = _number(mod.get("local_micro", 0))
        m.local_micro_mode = _number(mod.get("local_micro_mode", 0))
        m.local_micro_peripherals = _number(mod.get("local_micro_peripherals", 0))
        if "block_length" in mod:
            m.module_info_length = _number(mod["block_length"])
        for key, value in data.get("registers", {}).items():
            if registers.find_register(key) is None:
                raise ValueError("unknown register setting '{}'".format(key))
            m.regs[key] = _number(value)
        groups = data.get("groups", {})
        m.tx_groups = [Group.from_json(g) for g in groups.get("tx", [])]
        m.rx_groups = [Group.from_json(g) for g in groups.get("rx", [])]
        m.peripherals = [Peripheral.from_json(p) for p in data.get("peripherals", [])]
        supplier = data.get("supplier", {})
        m.supplier_vendor_id = supplier.get("vendor_id", m.supplier_vendor_id)
        layout_ = m.vendor_layout()
        if layout_ is not None:
            m.vendor_values = layout_.from_json(supplier.get("values", {}))
            m.vendor_data = b""
        else:
            m.vendor_values = {}
            m.vendor_data = bytes.fromhex(supplier.get("data", ""))
        if "block_length" in supplier:
            m.supplier_length = _number(supplier["block_length"])
        function = data.get("device_function")
        if function:
            m.device_function_data = bytes.fromhex(function.get("data", ""))
            if "block_length" in function:
                m.device_function_length = _number(function["block_length"])
        m.reserved = {_number(k): _number(v) for k, v in data.get("reserved", {}).items()}
        return m


def _number(value):
    """An int from a JSON number or a "0x.." string."""
    if isinstance(value, bool):
        return int(value)
    if isinstance(value, int):
        return value
    return int(str(value), 0)
