#!/usr/bin/env python3
"""A smoke test of the image tool's GUI: the window opens a project, shows every tab, takes
edits into the module, checks them, imports ADI's image, exports and saves. The file
dialogs are answered by the test. Skipped without Tkinter or a display; on a headless Linux
host run it under a virtual display:

    xvfb-run -a python3 tools/a2b_eeprom/tests/test_gui.py

Copyright 2026 by ClockWorks Signal Processing LLC
Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
SPDX-License-Identifier: BSD-3-Clause
"""

import os
import shutil
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
TOOL = os.path.dirname(HERE)
ROOT = os.path.dirname(os.path.dirname(TOOL))
sys.path.insert(0, TOOL)

try:
    import tkinter as tk
    from a2beeprom import gui
    tk.Tk().destroy()
    SKIP = None
except Exception as error:  # No Tkinter, or no display.
    SKIP = str(error)

from a2beeprom import project, validate  # noqa: E402
from a2beeprom.encode import encode  # noqa: E402

EXAMPLES = os.path.join(TOOL, "examples")
ADI_RJ45 = os.path.join(ROOT, "adi", "module_info", "adi_a2b_node_ModuleInfo_1.3.0.0_RJ45.dat")
EXERCISE = os.path.join(ROOT, "tools", "SS+reference", "2x_AB0020_EVM_with_2437",
                        "eeprom_exercise.xml")


def read(path):
    with open(path, "rb") as f:
        return f.read()


@unittest.skipIf(SKIP is not None, "no GUI: {}".format(SKIP))
class GuiTest(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.mkdtemp()
        self.root = tk.Tk()
        self.answers = []
        self.saved = {}
        self.patched = {}
        for module_, name, fake in (
                (gui.filedialog, "askopenfilename", self.answer),
                (gui.filedialog, "asksaveasfilename", self.answer),
                (gui.messagebox, "showinfo", lambda *a, **k: None),
                (gui.messagebox, "showwarning", lambda *a, **k: None),
                (gui.messagebox, "showerror", self.error),
                (gui.messagebox, "askyesnocancel", lambda *a, **k: False)):
            self.patched[(module_, name)] = getattr(module_, name)
            setattr(module_, name, fake)
        self.errors = []
        self.app = gui.App(self.root, os.path.join(EXAMPLES, "AB0020.json"))
        self.pump()

    def tearDown(self):
        for (module_, name), original in self.patched.items():
            setattr(module_, name, original)
        self.app.close()
        shutil.rmtree(self.folder)

    def answer(self, *args, **kwargs):
        return self.answers.pop(0)

    def error(self, title, message, **kwargs):
        self.errors.append(message)

    def pump(self):
        for _ in range(3):
            self.root.update()
        self.app.check()

    def tab(self, name):
        return next(t for t in self.app.tabs if type(t).__name__ == name)

    def errors_now(self):
        return [i for i in self.app.issues if i.level == validate.ERROR]

    def test_opens_the_example_and_every_tab(self):
        self.assertEqual(self.app.module.device_name, "AB0020")
        self.assertEqual(self.errors_now(), [])
        self.assertIn("0 error(s)", self.app.status.cget("text"))
        for t in self.app.tabs:
            self.app.notebook.select(t.frame)
            self.pump()
        self.assertEqual(encode(self.app.module).image,
                         read(os.path.join(EXAMPLES, "AB0020.dat")))
        self.assertFalse(self.app.dirty)

    def test_edits_reach_the_module_and_the_checks(self):
        self.tab("ModuleTab").name.set("AB0020 rev B")
        self.pump()
        self.assertEqual(self.app.module.device_name, "AB0020 rev B")
        self.assertTrue(self.app.dirty)
        hex_entry, fields = self.tab("TransceiverTab").widgets["i2srate"]
        hex_entry.var.set("0x01")  # A reduced rate.
        self.pump()
        self.assertEqual(self.app.module.regs["i2srate"], 0x01)
        self.assertTrue(any("reduced rate" in i.message for i in self.errors_now()))
        hex_entry.var.set("0x05")  # 2x: allowed.
        self.pump()
        self.assertEqual(self.errors_now(), [])
        # A field changes the byte: TDM4 in I2SGCFG.
        tdm = [var for field, var in self.tab("TransceiverTab").widgets["i2sgcfg"][1]
               if field.name == "TDMMODE"][0]
        tdm.set("1: TDM4")
        self.tab("TransceiverTab")._field_changed(
            gui.registers.find_register("i2sgcfg"),
            [f for f, v in self.tab("TransceiverTab").widgets["i2sgcfg"][1]
             if f.name == "TDMMODE"][0], 1)
        self.pump()
        self.assertEqual(self.app.module.regs["i2sgcfg"], 0xE1)
        # Another chip family: the SPI settings no longer apply.
        module_tab = self.tab("ModuleTab")
        module_tab.chip.set("AD2428")
        module_tab._chip()
        self.pump()
        self.assertEqual(self.app.module.target_chip, "AD2428")
        self.assertTrue(any(i.where == "SPICFG" for i in self.app.issues))

    def test_board_tab(self):
        tab = self.tab("ModuleTab")
        self.assertEqual(tab.title, "Board")
        self.assertEqual(self.app.notebook.tab(tab.frame, "text"), "Board")
        self.assertEqual(int(tab.notes.cget("height")), 6)
        self.assertTrue(self.app.module.local_power)  # The AB0020 has its own supply.

    def test_external_board_shares_its_settings(self):
        tab = self.tab("TransceiverTab")
        self.app.notebook.select(tab.frame)
        self.pump()
        names = {key: [field.name for field, _ in fields]
                 for key, (_, fields) in tab.widgets.items()}
        self.assertEqual(names["i2scfg"], ["RXBCLKINV", "RX", "TXBCLKINV", "TX"])
        self.assertNotIn("SYNCDIS", names["i2sgcfg"])
        self.assertIn("HPFCORNER", names["pdmctl2"])  # In the AD243x-only section.
        self.assertIn("SPIMODE", names["spicfg"])
        self.assertIn("both families", tab.summary.cget("text"))
        # Two TX pins: the families read it differently, an error.
        tab._value("i2scfg", 0x12)
        self.pump()
        self.assertTrue(any(i.where == "I2SCFG" for i in self.errors_now()))
        tab._value("i2scfg", 0x11)
        self.pump()
        self.assertEqual(self.errors_now(), [])

    def test_peripheral_pin_boxes(self):
        tab = self.tab("PeripheralsTab")
        self.app.notebook.select(tab.frame)
        p = self.app.module.peripherals[0]
        self.assertEqual((p.rx_interface, p.tx_interface), (0x21, 0x30))
        p.rx_interface = p.tx_interface = 0
        tab.show(keep=True)
        self.pump()
        self.assertTrue(any("on no A2B" in i.message for i in self.errors_now()))
        for attr, sio in (("rx_interface", 0), ("tx_interface", 4)):  # DRX0, DTX0.
            bits, update = tab.pin_boxes[attr]
            bits[sio].set(1)
            update()
        self.pump()
        self.assertEqual((p.rx_interface, p.tx_interface), (0x21, 0x30))
        self.assertEqual(self.errors_now(), [])

    def test_vendor_section(self):
        vendor_tab = self.tab("VendorTab")
        values = self.app.module.vendor_values
        self.assertEqual(values["id"], "AB0020")
        vendor_tab.vendor_id.set("CUSTOM01")
        self.pump()
        self.assertIsNone(self.app.module.vendor_layout())
        vendor_tab.vendor_id.set("CLKWORKS")
        self.pump()
        self.assertEqual(self.app.module.vendor_values["features"], [])
        self.assertEqual(self.errors_now(), [])

    def test_peripheral_program_from_xml(self):
        tab = self.tab("PeripheralsTab")
        self.app.notebook.select(tab.frame)
        tab.add()
        self.pump()
        self.assertEqual(len(self.app.module.peripherals), 2)
        p = self.app.module.peripherals[1]
        p_xml = project.stored_path(EXERCISE, self.app.project_dir())
        tab._edit("xml", p_xml, reread=True)
        self.pump()
        self.assertEqual(len(p.units), 16)  # 8 writes and 8 waits; the reads dropped.
        self.assertEqual(p.xml_report.reads_dropped, 8)
        tab.remove()
        self.pump()
        self.assertEqual(len(self.app.module.peripherals), 1)

    def test_import_export_and_save(self):
        self.answers = [ADI_RJ45]
        self.app.import_image()
        self.pump()
        self.assertEqual(self.app.module.device_name, "SRC")
        self.assertEqual(encode(self.app.module).image, read(ADI_RJ45))
        self.assertEqual(self.app.module.target_chip, "Not stated")  # 0x07: 0, as ADI's.
        self.assertEqual(self.app.module.view_family, "AD243x")  # From product 0x0037.
        exported = os.path.join(self.folder, "rj45.dat")
        self.answers = [exported]
        self.app.export_image()
        self.assertEqual(self.errors, [])
        self.assertEqual(read(exported), read(ADI_RJ45))
        saved = os.path.join(self.folder, "rj45.json")
        self.answers = [saved]
        self.assertTrue(self.app.save_as())
        module, problems = project.load(saved)
        self.assertEqual(problems, [])
        self.assertEqual(encode(module).image, read(ADI_RJ45))
        self.assertFalse(self.app.dirty)

    def test_export_refused_with_errors(self):
        self.app.module.regs["i2sgcfg"] = 0xE3  # TDM12.
        self.app.changed()
        self.answers = [os.path.join(self.folder, "never.dat")]
        self.app.export_image()
        self.assertTrue(self.errors and "fix the errors" in self.errors[0])
        self.assertFalse(os.path.exists(os.path.join(self.folder, "never.dat")))

    def test_save_as_keeps_the_xml_reachable(self):
        saved = os.path.join(self.folder, "sub", "AB0020.json")
        os.makedirs(os.path.dirname(saved))
        self.answers = [saved]
        self.assertTrue(self.app.save_as())
        module, problems = project.load(saved)
        self.assertEqual(problems, [])
        self.assertEqual(encode(module).image, read(os.path.join(EXAMPLES, "AB0020.dat")))


if __name__ == "__main__":
    unittest.main()
