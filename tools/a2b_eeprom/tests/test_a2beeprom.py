#!/usr/bin/env python3
"""Tests for the A2B EEPROM image tool's core (no GUI, no display needed).

Run from anywhere:  python3 tools/a2b_eeprom/tests/test_a2beeprom.py
                or: python3 -m unittest discover -s tools/a2b_eeprom/tests

Copyright 2026 by ClockWorks Signal Processing LLC
Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
SPDX-License-Identifier: BSD-3-Clause
"""

import json
import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.dirname(HERE)
ROOT = os.path.dirname(os.path.dirname(TOOL))
sys.path.insert(0, TOOL)

from a2beeprom import layout as L  # noqa: E402
from a2beeprom import project, registers, sigma_xml, validate  # noqa: E402
from a2beeprom.crc8 import crc8  # noqa: E402
from a2beeprom.decode import decode  # noqa: E402
from a2beeprom.encode import EncodeError, encode, split_units  # noqa: E402
from a2beeprom.model import Group, Module, Peripheral, Unit  # noqa: E402
from a2beeprom.vendor import clockworks  # noqa: E402

ADI_IMAGES = [os.path.join(ROOT, "adi", "module_info", "adi_a2b_node_ModuleInfo_1.3.0.0_{}.dat"
                           .format(n)) for n in ("RJ45", "XLR")]
AB0020_DIR = os.path.join(ROOT, "tools", "SS+reference", "2x_AB0020_EVM_with_2437")
EXAMPLES = os.path.join(TOOL, "examples")


def read(path):
    with open(path, "rb") as f:
        return f.read()


def errors(issues):
    return [i for i in issues if i.level == validate.ERROR]


def messages(issues, level=None):
    return " | ".join("{}: {}".format(i.where, i.message) for i in issues
                      if level is None or i.level == level)


class Crc8Test(unittest.TestCase):
    def test_check_value(self):
        # CRC-8 poly 0x07, init 0xFF, no reflection, no final XOR (a2bcpp/tests/unit/test_crc8.cpp).
        self.assertEqual(crc8(b"123456789"), 0xFB)

    def test_adi_header_crcs(self):
        self.assertEqual(crc8(read(ADI_IMAGES[0])[:0x1F]), 0xB8)
        self.assertEqual(crc8(read(ADI_IMAGES[1])[:0x1F]), 0x37)


class AdiImagesTest(unittest.TestCase):
    def test_decode_and_rebuild_byte_for_byte(self):
        for path in ADI_IMAGES:
            raw = read(path)
            decoded = decode(raw)
            self.assertTrue(decoded.ok, messages(validate_issues(decoded)))
            self.assertEqual(decoded.image_end, len(raw))
            self.assertEqual(decoded.module.reserved, {})
            self.assertEqual(encode(decoded.module).image, raw, path)

    def test_project_file_round_trip(self):
        for path in ADI_IMAGES:
            raw = read(path)
            module = decode(raw).module
            again = Module.from_json(json.loads(json.dumps(module.to_json())))
            self.assertEqual(encode(again).image, raw, path)

    def test_fields(self):
        m = decode(read(ADI_IMAGES[0])).module
        self.assertEqual((m.vendor_id, m.product_id, m.version_id), (0x00AD, 0x0037, 0x0010))
        self.assertEqual(m.sw_version, [1, 1, 0, 0])
        self.assertEqual((m.power_config, m.local_power), (4, False))
        self.assertEqual(m.device_name, "SRC")
        self.assertEqual(m.cable_type, 2)
        self.assertEqual(m.regs["i2sgcfg"], 0xE0)
        self.assertEqual(m.regs["i2scfg"], 0x19)
        self.assertEqual([(g.id, g.name, g.channels) for g in m.tx_groups], [(0, "ADAU1701", 3)])
        self.assertEqual(m.supplier_vendor_id, "000000AD")
        self.assertEqual(m.supplier_length, 0x29)  # ADI's; its CRC is still at 0x4FF.
        self.assertEqual([p.name for p in m.peripherals], ["ADAU1701", "BD28623", "Microphones"])
        adau = m.peripherals[0]
        self.assertEqual((adau.address, adau.addr_width, adau.data_width), (0x34, 2, 1))
        self.assertEqual(adau.units[0], Unit.delay(16))
        self.assertEqual([len(u.data) for u in adau.units[1:]], [4, 3792, 1332, 4090, 10, 26, 4])
        x = decode(read(ADI_IMAGES[1])).module
        self.assertEqual((x.max_power_mw, x.min_power_mw, x.max_current_ma), (5000, 500, 200))


def validate_issues(decoded):
    return [validate.Issue(*i) for i in decoded.issues]


class SigmaXmlTest(unittest.TestCase):
    def test_ab0020_codec_program(self):
        program = sigma_xml.read_file(os.path.join(AB0020_DIR, "AB0020-sub-node-ADAU1761.xml"))
        # a2bcpp/docs/hardware_notes.md: 38 writes of 6076 bytes, the largest 1732, one 100 ms
        # delay and one read; the bench console's reader (test_sigma_xml) gets the same.
        self.assertEqual(program.writes, 38)
        self.assertEqual(program.write_bytes, 6076)
        self.assertEqual(program.largest_write, 1732)
        self.assertEqual([u.delay_ms for u in program.units if u.kind == "delay"], [100])
        self.assertEqual(program.reads_dropped, 1)
        self.assertEqual(program.rom_address, 0x39)  # IC_Address 114 is 8-bit 0x72.
        first = program.units[0]
        self.assertEqual((first.address, first.data), (0x39, bytes([0x40, 0xEB, 0x7F])))

    def test_hand_written_sequence(self):
        # Self-closing reads, run-together hex ("00FF"), per-action 7-bit i2caddr.
        program = sigma_xml.read_file(os.path.join(AB0020_DIR, "eeprom_exercise.xml"))
        self.assertEqual((program.writes, program.delays, program.reads_dropped), (8, 8, 8))
        self.assertEqual(program.addresses, {0x50})
        self.assertEqual(program.units[1].delay_ms, 0xFF)
        self.assertEqual(program.units[0].data, bytes([0, 0, 0]))

    def test_refusals(self):
        bad = {
            "move": '<ROM><action instr="move" addr="1" len="2">00</action></ROM>',
            "no address": '<ROM><action instr="writeXbytes" addr="1" len="2">00</action></ROM>',
            "long delay": '<ROM><action instr="delay">10 00</action></ROM>',
            "not hex": '<ROM><action instr="delay">zz</action></ROM>',
            "bad len": '<ROM><action instr="writeXbytes" addr_width="2" len="4" addr="0" '
                       'i2caddr="80">00</action></ROM>',
            "empty": "<ROM></ROM>",
        }
        for name, text in bad.items():
            with self.assertRaises(sigma_xml.XmlError, msg=name):
                sigma_xml.parse(text.encode())

    def test_default_address(self):
        text = b'<ROM><action instr="writeXbytes" addr="16" len="2">AB</action></ROM>'
        program = sigma_xml.parse(text, default_address=0x38)
        self.assertEqual(program.units[0].address, 0x38)
        self.assertEqual(program.units[0].data, bytes([0x10, 0xAB]))


class ClockworksSectionTest(unittest.TestCase):
    def values(self, n):
        return {"id": "AB0020-0001", "features": [
            {"uid": 0x0102030405060708 + i, "name": "F{}".format(i), "value0": -i,
             "value1": 1 << 30, "text": "TEXT{}".format(i)} for i in range(n)]}

    def test_layout_fits_and_offsets(self):
        self.assertEqual(clockworks.LAYOUT.size, 16 + 1 + 6 * 32)
        self.assertLessEqual(clockworks.LAYOUT.size, L.SUPPLIER_DATA_LEN)
        m = Module()
        m.vendor_values = self.values(2)
        image = encode(m).image
        self.assertEqual(image[0x403:0x40B], b"CLKWORKS")
        self.assertEqual(image[0x42B:0x43B], b"AB0020-0001" + bytes(5))
        self.assertEqual(image[0x43B], 2)
        self.assertEqual(image[0x43C:0x444], (0x0102030405060708).to_bytes(8, "little"))
        self.assertEqual(image[0x444:0x44C], b"F0" + bytes(6))
        self.assertEqual(image[0x450:0x454], (1 << 30).to_bytes(4, "little"))
        self.assertEqual(image[0x401], L.FIXED_BLOCK_PAYLOAD)  # Length 0xFD: CRC at 0x4FF.
        self.assertEqual(image[0x4FF], crc8(image[0x400:0x4FF]))

    def test_round_trip(self):
        m = Module()
        m.vendor_values = self.values(6)
        decoded = decode(encode(m).image)
        self.assertTrue(decoded.ok, decoded.issues)
        self.assertEqual(decoded.module.vendor_values["id"], "AB0020-0001")
        self.assertEqual(decoded.module.vendor_values["features"], self.values(6)["features"])
        self.assertEqual(decoded.module.reserved, {})

    def test_limits(self):
        m = Module()
        m.vendor_values = self.values(7)
        self.assertIn("more than 6", messages(validate.check(m)))
        m.vendor_values = self.values(2)
        m.vendor_values["id"] = "X" * 16  # 15 characters and the NUL.
        self.assertIn("longer than 15", messages(validate.check(m)))
        m.vendor_values = self.values(2)
        m.vendor_values["features"][1]["uid"] = m.vendor_values["features"][0]["uid"]
        self.assertIn("same uid", messages(validate.check(m)))
        m.vendor_values = self.values(1)
        m.vendor_values["features"][0]["value0"] = 1 << 31
        self.assertIn("outside", messages(validate.check(m)))
        m.vendor_values = self.values(1)
        m.vendor_values["features"][0]["name"] = "EIGHTCHR"  # All 8 bytes, no NUL.
        self.assertEqual(errors(validate.check(m)), [])
        self.assertEqual(decode(encode(m).image).module.vendor_values["features"][0]["name"],
                         "EIGHTCHR")


class ExamplesTest(unittest.TestCase):
    def test_examples_build_as_committed(self):
        for name in ("AB0020", "AB0312", "AB0204"):
            path = os.path.join(EXAMPLES, name + ".json")
            module, problems = project.load(path)
            self.assertEqual(problems, [])
            self.assertEqual(errors(validate.check(module)), [], name)
            image = encode(module).image
            self.assertEqual(image, read(os.path.join(EXAMPLES, name + ".dat")),
                             "{}.dat is out of date: a2b_eeprom.py build {}".format(name, path))
            decoded = decode(image)
            self.assertTrue(decoded.ok, decoded.issues)
            self.assertEqual(decoded.image_end, len(image))

    def test_ab0020_image(self):
        module, _ = project.load(os.path.join(EXAMPLES, "AB0020.json"))
        image = encode(module).image
        self.assertLess(len(image), 0x8000)  # Fits a 24FC256.
        m = decode(image).module
        self.assertEqual(m.regs["clk2cfg"], 0x81)
        self.assertEqual(m.peripherals[0].address, 0x39)
        self.assertEqual(m.regs["i2ccfg"], 0x01)  # DATARATE: 400 kHz.
        units = m.peripherals[0].units
        self.assertEqual(len(units), 40)  # 39 writes and the wait.
        # The 1730-byte program write split at 1020 data bytes, 5 per address.
        self.assertEqual(max(len(u.data) for u in units if u.kind == "write"), 1022)
        self.assertIn(b"\x08\xCC" + bytes([0x01, 0x65]),
                      [u.data[:4] for u in units if u.kind == "write"])

    def test_readback_with_blank_tail(self):
        image = read(os.path.join(EXAMPLES, "AB0204.dat"))
        decoded = decode(image + b"\xFF" * (0x8000 - len(image)))
        self.assertTrue(decoded.ok)
        self.assertEqual(decoded.image_end, len(image))
        self.assertEqual(encode(decoded.module).image, image)


class ValidateTest(unittest.TestCase):
    def module(self, chip="AD2437"):
        m = Module()
        m.target_chip = chip
        m.regs["i2sgcfg"] = 0xE0
        m.regs["i2scfg"] = 0x11
        m.tx_groups = [Group(0, "out", 3)]
        m.rx_groups = [Group(0, "in", 3)]
        return m

    def test_clean(self):
        self.assertEqual(errors(validate.check(self.module())), [])

    def test_power_config_is_the_chips(self):
        # Each chip has one power configuration, which the stack uses in place of the image's.
        self.assertNotIn("power", messages(validate.check(self.module("AD2437"))))  # CFG4.
        for chip in ("AD2433", "AD2428", "AD2427"):
            m = self.module(chip)
            m.regs["i2scfg"] = 0x00 if chip == "AD2427" else 0x11
            self.assertIn("CFG4 for an {}, which has CFG0 only: the stack uses CFG0".format(chip),
                          messages(validate.check(m), validate.WARNING))
            m.power_config = 0
            self.assertNotIn("CFG", messages(validate.check(m)))
        m = self.module("AD2437")
        m.power_config = 0
        self.assertIn("CFG0 for an AD2437, which has CFG4 only",
                      messages(validate.check(m), validate.WARNING))
        m = self.module("External")
        m.power_config = 0  # Either value: a note, never a warning.
        self.assertNotIn("CFG", messages(validate.check(m), validate.WARNING))
        self.assertIn("CFG4 on an AD2437, CFG0 on an AD2433 or AD242x",
                      messages(validate.check(m), validate.NOTE))

    def test_stack_limits(self):
        m = self.module()
        m.regs["i2srate"] = 0x01  # SFF / 2: the stack refuses a reduced rate.
        self.assertIn("reduced rate", messages(validate.check(m), validate.ERROR))
        m = self.module()
        m.regs["i2sgcfg"] = 0xE3  # TDM12.
        self.assertIn("TDM12", messages(validate.check(m), validate.ERROR))
        m = self.module()
        m.rx_groups = [Group(i, "g", 1 << i) for i in range(9)]
        self.assertIn("keeps the first 8", messages(validate.check(m), validate.WARNING))
        m.tx_groups = [Group(i, "g", 1 << i) for i in range(5)]
        self.assertIn("past 0x2FF", messages(validate.check(m), validate.ERROR))
        m = self.module()
        m.peripherals = [Peripheral() for _ in range(9)]
        self.assertIn("at most 8", messages(validate.check(m), validate.ERROR))

    def test_i2ccfg(self):
        m = self.module()
        self.assertNotIn("I2CCFG", messages(validate.check(m)))
        m.regs["i2ccfg"] = 0x01
        self.assertIn("400 kHz", messages(validate.check(m), validate.NOTE))
        self.assertNotIn("I2CCFG", messages(validate.check(m), validate.WARNING))
        m.regs["i2ccfg"] = 0x8F  # DISI2C, FMPLUS, FRAMERATE and EACK too.
        found = messages(validate.check(m), validate.WARNING)
        for name in ("DISI2C", "FMPLUS", "FRAMERATE", "EACK"):
            self.assertIn(name, found)
        m = self.module("AD2428")
        m.regs["i2ccfg"] = 0x06
        self.assertIn("FRAMERATE, EACK set", messages(validate.check(m), validate.WARNING))

    def test_chip(self):
        m = self.module("AD2428")
        m.regs["spicfg"] = 0x02
        self.assertIn("has no SPICFG", messages(validate.check(m), validate.WARNING))
        m = self.module("AD2427")
        self.assertIn("no I2S/TDM port", messages(validate.check(m), validate.WARNING))
        m = self.module()
        m.regs["clk2cfg"] = 0x81  # CLKOUT2 with SPI left on.
        self.assertIn("CLKOUT2", messages(validate.check(m), validate.WARNING))
        m.regs["spicfg"] = 0x02
        self.assertNotIn("CLKOUT2", messages(validate.check(m)))

    def test_eeprom_size(self):
        m = self.module()
        p = Peripheral()
        p.name = "big"
        p.address = 0x39
        p.units = [Unit.write(0x39, bytes(4000)) for _ in range(9)]
        m.peripherals = [p]
        self.assertIn("more than the EEPROM", messages(validate.check(m), validate.ERROR))
        self.assertIn("a write of 4000 bytes", messages(validate.check(m), validate.WARNING))

    def test_long_writes_by_measured_usbi_time(self):
        # 217 us per byte at 100 kHz, 162 at 400 kHz (DATARATE): warned above 0.25 s.
        m = self.module()
        p = Peripheral()
        p.address = 0x39
        m.peripherals = [p]
        for size, fast, warned in ((1150, False, False), (1160, False, True),
                                   (1540, True, False), (1550, True, True)):
            p.units = [Unit.write(0x39, bytes(size))]
            m.regs["i2ccfg"] = 0x01 if fast else 0x00
            text = messages(validate.check(m), validate.WARNING)
            self.assertEqual("a write of {} bytes".format(size) in text, warned, (size, fast))
        self.assertIn("about 0.25 s through an ADI USBi at 400 kHz", text)
        # Split at 1022 bytes, 5 per address, as the AB0020's ADAU1761 program: no warning.
        p.units = [Unit.write(0x39, b"\x08\x00" + bytes(1730))]
        p.addr_width = 2
        p.split_max = 1022
        p.split_bytes_per_address = 5
        m.regs["i2ccfg"] = 0x00
        self.assertNotIn("a write of", messages(validate.check(m)))

    def test_units(self):
        m = self.module()
        p = Peripheral()
        p.address = 0x39
        p.units = [Unit.write(0x80, b"\x00"), Unit.delay(5000), Unit.write(0x39, bytes(4095))]
        m.peripherals = [p]
        text = messages(validate.check(m), validate.ERROR)
        self.assertIn("I2C address 0x80", text)
        self.assertIn("5000 ms", text)
        self.assertIn("4095 bytes", text)


class TargetChipAndBPortTest(unittest.TestCase):
    """The header's target chip (0x07) and the B port (0x28, 0x6C), as the stack reads them."""

    def module(self, chip=registers.EXTERNAL, cable=2, length=0):
        m = ValidateTest().module(chip)
        m.product_id = 0x0020
        m.cable_type = cable
        m.cable_length = length
        return m

    def test_chip_field(self):
        for chip, code in (("AD2437", 0x2437), ("AD2427", 0x2427),
                           (registers.EXTERNAL, 0x7FFF), (registers.NOT_STATED, 0)):
            image = encode(self.module(chip)).image
            self.assertEqual(L.get16(image, L.TARGET_CHIP), code)
            self.assertEqual(image[L.HEADER_CRC], crc8(image[:L.HEADER_CRC]))
            decoded = decode(image)
            self.assertEqual(decoded.module.target_chip, chip)
            self.assertEqual(decoded.module.reserved, {})
        # Unprogrammed (0xFFFF) reads as not stated, and the bytes are kept.
        image = bytearray(encode(self.module(registers.NOT_STATED)).image)
        image[L.TARGET_CHIP:L.TARGET_CHIP + 2] = b"\xFF\xFF"
        image[L.HEADER_CRC] = crc8(bytes(image[:L.HEADER_CRC]))
        decoded = decode(bytes(image))
        self.assertEqual(decoded.module.target_chip, registers.NOT_STATED)
        self.assertEqual(encode(decoded.module).image, bytes(image))

    def test_external_checks_both_families(self):
        m = self.module()
        m.regs["spicfg"] = 0x02
        m.regs["pdmctl2"] = 0x40  # HPFCORNER 60 Hz: AD243x only, on bits the AD242x hasn't got.
        issues = validate.check(m)
        self.assertEqual(errors(issues), [])
        self.assertNotIn("reserved", messages(issues))  # The stack clears them on an AD242x.
        notes = messages(issues, validate.NOTE)
        self.assertIn("SPICFG: 0x02: AD243x only; the stack doesn't write it on an AD242x", notes)
        self.assertIn("PDMCTL2: HPFCORNER: AD243x only; the stack writes these bits clear", notes)
        self.assertNotIn("I2SCFG", messages(issues))  # 0x11: one pin each way, alike.
        self.assertIn("the stack uses the chip's own power configuration", notes)
        for i2scfg in (0x22, 0x33, 0x15):
            m.regs["i2scfg"] = i2scfg
            self.assertIn("I2SCFG: 0x{:02X} reads differently".format(i2scfg),
                          messages(validate.check(m), validate.ERROR))
        self.assertIn("pin count of 5 or 6", messages(validate.check(m), validate.ERROR))
        m.regs["i2scfg"] = 0x11
        m.regs["i2sgcfg"] = 0xE8
        self.assertIn("RXONDTX1 on the AD242x and SYNCDIS on the AD243x",
                      messages(validate.check(m), validate.ERROR))
        # A named chip: its own fields, no External rules.
        m = self.module("AD2437")
        m.regs["i2scfg"] = 0x22
        self.assertNotIn("reads differently", messages(validate.check(m)))
        m = self.module("AD2410")
        self.assertIn("isn't one the stack supports", messages(validate.check(m), validate.ERROR))

    def test_b_port(self):
        self.assertNotIn("B port", messages(validate.check(self.module(cable=2))))
        rj45 = messages(validate.check(self.module(cable=1)), validate.NOTE)
        self.assertIn("RJ45: two-step on an AD2437", rj45)
        self.assertIn("XLR on an AD2428: UTP",
                      messages(validate.check(self.module("AD2428", cable=0)), validate.NOTE))
        for cable in (3, 4, 5):
            self.assertIn("Unsupported B port type",
                          messages(validate.check(self.module(cable=cable)), validate.WARNING))
        self.assertIn("B port type 6 is reserved",
                      messages(validate.check(self.module(cable=6)), validate.ERROR))
        self.assertIn("longer than the data sheets' longest link, 30 m",
                      messages(validate.check(self.module(length=31)), validate.WARNING))
        self.assertIn("an AD242x link is at most 15 m",
                      messages(validate.check(self.module(length=20)), validate.WARNING))
        self.assertNotIn("15 m", messages(validate.check(self.module("AD2437", length=20))))
        image = encode(self.module(cable=4, length=12)).image
        self.assertEqual((image[L.CABLE_TYPE], L.get16(image, L.CABLE_LENGTH)), (4, 12))

    def test_clockworks_ids(self):
        m = self.module()
        self.assertEqual(m.vendor_id, L.CLOCKWORKS_MODULE_VENDOR)
        self.assertNotIn("board number", messages(validate.check(m)))
        m.product_id = 0x002A
        self.assertIn("isn't a board number", messages(validate.check(m), validate.NOTE))


class PeripheralPinsTest(unittest.TestCase):
    """A peripheral's A2B pins (RX/TX interface: count in bits 7:5, SIO pins in 4:0)."""

    def module(self, chip="AD2437", rx=0x21, tx=0x30):
        m = ValidateTest().module(chip)  # I2SCFG 0x11: DTX0 and DRX0; groups on channels 0-1.
        p = Peripheral()
        p.name = "codec"
        p.address = 0x39
        p.tx_groups = p.rx_groups = 1
        p.rx_interface, p.tx_interface = rx, tx
        m.peripherals = [p]
        return m

    def pins(self, m, level=None):
        return " | ".join(i.message for i in validate.check(m)
                          if (level is None or i.level == level) and
                          i.where.startswith("peripheral") and "XML" not in i.message)

    def test_as_adi_has_them(self):
        for chip in ("AD2437", "AD2428", registers.EXTERNAL):
            self.assertEqual(self.pins(self.module(chip)), "", chip)
        for path in ADI_IMAGES:
            self.assertEqual(self.pins(decode(read(path)).module, validate.ERROR), "")

    def test_errors(self):
        both = self.pins(self.module(rx=0, tx=0), validate.ERROR)
        self.assertIn("feeds RX group(s) 0, but it is on no A2B RX pin", both)
        self.assertIn("fed by TX group(s) 0, but it is on no A2B TX pin", both)
        self.assertIn("its RX pin count is 2, but it names 1 pin(s)",
                      self.pins(self.module(rx=0x41), validate.ERROR))
        self.assertIn("on DTX1, which the transceiver's settings don't turn on",
                      self.pins(self.module(tx=0x28), validate.ERROR))
        self.assertIn("on SIO4, which",  # SIO4 is DTX0: not an RX pin.
                      self.pins(self.module(rx=0x30), validate.ERROR))
        m = self.module(registers.EXTERNAL, tx=0x28)
        self.assertIn("don't turn on for both families", self.pins(m, validate.ERROR))

    def test_groups_and_unused_pins(self):
        m = self.module()
        m.tx_groups = [Group(0, "out", 0xC)]  # Channels 2-3: DTX1 at TDM2.
        self.assertIn("TX group 0's channel 2 is on DTX1, which it isn't on",
                      self.pins(m, validate.WARNING))
        m = self.module(rx=0)
        m.peripherals[0].rx_groups = 0
        self.assertIn("DRX0 is on, but no peripheral block is on it",
                      self.pins(m, validate.NOTE))
        # With PDM on, the RX pins move (AD2437 TRM, Table 5-5): DRX0 is SIO1.
        m = self.module(rx=0x22)
        m.regs["pdmctl"] = 0x01
        self.assertNotIn("SIO", self.pins(m, validate.ERROR))


class SplitTest(unittest.TestCase):
    def test_split_advances_the_sub_address(self):
        p = Peripheral()
        p.addr_width = 2
        p.split_max = 40
        p.split_bytes_per_address = 4
        data = bytes(range(100))
        p.units = [Unit.write(0x39, bytes([0x08, 0x00]) + data), Unit.delay(1)]
        units = split_units(p)
        writes = [u for u in units if u.kind == "write"]
        self.assertEqual([len(u.data) - 2 for u in writes], [36, 36, 28])
        self.assertEqual([u.data[:2] for u in writes], [b"\x08\x00", b"\x08\x09", b"\x08\x12"])
        self.assertEqual(b"".join(u.data[2:] for u in writes), data)
        self.assertEqual(units[-1], Unit.delay(1))

    def test_too_small(self):
        p = Peripheral()
        p.addr_width = 2
        p.split_max = 4
        p.split_bytes_per_address = 5
        p.units = [Unit.write(0x39, bytes(20))]
        with self.assertRaises(EncodeError):
            split_units(p)


class DecodeTest(unittest.TestCase):
    def test_blank_and_short(self):
        self.assertIsNone(decode(b"\xFF" * 0x8000).module)
        self.assertIn("blank", decode(b"\xFF" * 0x8000).issues[0][2])
        self.assertIsNone(decode(b"\xAA" * 10).module)

    def test_corrupt_crcs(self):
        image = bytearray(read(os.path.join(EXAMPLES, "AB0020.dat")))
        image[0x10] ^= 1  # In the header.
        decoded = decode(bytes(image))
        self.assertFalse(decoded.ok)
        self.assertIn("header", [i[1] for i in decoded.issues])
        image = bytearray(read(os.path.join(EXAMPLES, "AB0020.dat")))
        image[0x900] ^= 1  # In a unit.
        self.assertIn("write CRC", " ".join(i[2] for i in decode(bytes(image)).issues))

    def test_truncated(self):
        image = read(os.path.join(EXAMPLES, "AB0020.dat"))
        decoded = decode(image[:0x1000])
        self.assertFalse(decoded.ok)
        self.assertIn("past the image's end", " ".join(i[2] for i in decoded.issues))

    def test_reserved_bytes_kept(self):
        image = bytearray(read(os.path.join(EXAMPLES, "AB0312.dat")))
        image[0x9D] = 0x5A  # Reserved.
        image[0x14C] = 0  # Recompute the module-info block's CRC.
        crc_at = 0x23 + L.header_length(image[0x20:]) - 1
        image[crc_at] = crc8(image[0x20:crc_at])
        decoded = decode(bytes(image))
        self.assertEqual(decoded.module.reserved, {0x9D: 0x5A})
        self.assertEqual(encode(decoded.module).image, bytes(image))


class ProjectTest(unittest.TestCase):
    def test_save_and_load_keep_the_xml_path(self):
        folder = tempfile.mkdtemp()
        try:
            module, _ = project.load(os.path.join(EXAMPLES, "AB0020.json"))
            xml = project.full_path(module.peripherals[0].xml, EXAMPLES)
            module.peripherals[0].xml = project.stored_path(xml, folder)
            path = os.path.join(folder, "copy.json")
            project.save(module, path)
            again, problems = project.load(path)
            self.assertEqual(problems, [])
            self.assertEqual(encode(again).image, encode(module).image)
        finally:
            shutil.rmtree(folder)

    def test_missing_xml_is_reported(self):
        folder = tempfile.mkdtemp()
        try:
            module = Module()
            p = Peripheral()
            p.name = "codec"
            p.address = 0x39
            p.xml = "nowhere.xml"
            module.peripherals = [p]
            path = os.path.join(folder, "p.json")
            project.save(module, path)
            _, problems = project.load(path)
            self.assertEqual(len(problems), 1)
        finally:
            shutil.rmtree(folder)

    def test_registers_cover_0x7f_to_0x8f(self):
        self.assertEqual(sorted(r.offset for r in registers.REGISTERS), list(range(0x7F, 0x90)))


if __name__ == "__main__":
    unittest.main()
