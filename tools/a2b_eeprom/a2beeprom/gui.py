"""The A2B EEPROM image tool's window (Tkinter, standard library only).

One tab per part of the image, in the order of ADI's specification: the module (header,
name, cable), power, the transceiver's settings, channel groups, peripherals, vendor data;
then the image itself (checks, memory map, bytes). Every edit goes into the Module at once
and the checks run again a moment later; the status bar counts errors and warnings, and an
image with errors isn't exported. Run it with a2b_eeprom_gui.py.

Copyright 2026 by ClockWorks Signal Processing LLC
Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
SPDX-License-Identifier: BSD-3-Clause
"""

import os
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from . import __version__
from . import layout as L
from . import project, registers, report, sigma_xml, validate, vendor
from .decode import decode
from .encode import EncodeError, encode
from .model import Group, Module, Peripheral

TITLE = "A2B EEPROM image tool"
CHECK_DELAY_MS = 300
EEPROM_SIZES = [4096, 8192, 16384, 32768, 65536]
# The Transceiver tab's order: the audio port first, then clocks and pins, SPI, I2C.
DISPLAY_ORDER = ["i2sgcfg", "i2scfg", "i2srate", "pdmctl", "pdmctl2", "clk1cfg", "clk2cfg",
                 "gpiooen", "gpioien", "gpiodat", "pinten", "pintinv", "pincfg", "spicfg",
                 "spickdiv", "spipincfg", "i2ccfg"]
# The target chip menu's entries that aren't a chip: what they show -> the project's value.
CHIP_CHOICES = {
    "External (not decided by the board with the EEPROM)": registers.EXTERNAL,
    "Not stated (as ADI's images)": registers.NOT_STATED,
}
CHIP_CHOICES_BY_VALUE = {value: text for text, value in CHIP_CHOICES.items()}
LEVEL_COLOURS = {validate.ERROR: "#c00000", validate.WARNING: "#a05a00", validate.NOTE: "#406080"}


def parse_number(text, minimum=0, maximum=0xFFFFFFFF):
    """An int from "123", "0x7B"; ValueError if it isn't one or is out of range."""
    value = int(text.strip(), 0)
    if not minimum <= value <= maximum:
        raise ValueError("{} isn't {}-{}".format(value, minimum, maximum))
    return value


def parse_ids(text, bits=16):
    """A bitmap from group IDs: "0", "0,2", "none"."""
    return report.parse_channels(text, bits)


class NumberEntry(ttk.Entry):
    """An entry for a whole number, shown in hex or decimal; calls `on_value(int)` with each
    valid value typed, and marks an invalid one."""

    def __init__(self, parent, on_value, minimum=0, maximum=0xFFFF, hex_digits=0, width=10):
        self.var = tk.StringVar()
        ttk.Entry.__init__(self, parent, textvariable=self.var, width=width)
        self.on_value = on_value
        self.minimum = minimum
        self.maximum = maximum
        self.hex_digits = hex_digits
        self.loading = False
        self.var.trace_add("write", self._changed)

    def set(self, value):
        self.loading = True
        if self.hex_digits:
            self.var.set("0x{:0{}X}".format(value, self.hex_digits))
        else:
            self.var.set(str(value))
        self.loading = False
        self.configure(style="TEntry")

    def _changed(self, *_):
        if self.loading:
            return
        try:
            value = parse_number(self.var.get(), self.minimum, self.maximum)
        except ValueError:
            self.configure(style="Bad.TEntry")
            return
        self.configure(style="TEntry")
        self.on_value(value)


class ScrollFrame(ttk.Frame):
    """A frame that scrolls vertically; put widgets in .inner."""

    def __init__(self, parent):
        ttk.Frame.__init__(self, parent)
        self.canvas = tk.Canvas(self, highlightthickness=0)
        bar = ttk.Scrollbar(self, orient="vertical", command=self.canvas.yview)
        self.inner = ttk.Frame(self.canvas)
        self.inner.bind("<Configure>", lambda e: self.canvas.configure(
            scrollregion=self.canvas.bbox("all")))
        self.window = self.canvas.create_window((0, 0), window=self.inner, anchor="nw")
        self.canvas.bind("<Configure>", lambda e: self.canvas.itemconfigure(self.window,
                                                                            width=e.width))
        self.canvas.configure(yscrollcommand=bar.set)
        self.canvas.pack(side="left", fill="both", expand=True)
        bar.pack(side="right", fill="y")
        self.canvas.bind("<Enter>", lambda e: self.canvas.bind_all("<MouseWheel>", self._wheel))
        self.canvas.bind("<Leave>", lambda e: self.canvas.unbind_all("<MouseWheel>"))

    def _wheel(self, event):
        self.canvas.yview_scroll(int(-event.delta / 120) or (-1 if event.delta > 0 else 1),
                                 "units")

    def clear(self):
        for child in self.inner.winfo_children():
            child.destroy()


def row(parent, r, label, widget, note=""):
    """A label, a widget and a note on grid row r."""
    ttk.Label(parent, text=label).grid(row=r, column=0, sticky="w", padx=(0, 8), pady=2)
    widget.grid(row=r, column=1, sticky="w", pady=2)
    if note:
        ttk.Label(parent, text=note, foreground="#555555").grid(row=r, column=2, sticky="w",
                                                               padx=(8, 0))
    return widget


class Dialog(tk.Toplevel):
    """A small modal form: fields of (label, initial text, check(text) -> value)."""

    def __init__(self, parent, title, fields, extra=None):
        tk.Toplevel.__init__(self, parent)
        self.title(title)
        self.transient(parent)
        self.result = None
        self.fields = fields
        self.vars = []
        body = ttk.Frame(self, padding=10)
        body.pack(fill="both", expand=True)
        for n, (label, initial, _) in enumerate(fields):
            var = tk.StringVar(value=initial)
            entry = ttk.Entry(body, textvariable=var, width=40)
            row(body, n, label, entry)
            self.vars.append(var)
            if n == 0:
                entry.focus_set()
        if extra:
            extra(body, len(fields), self.vars)
        self.error = ttk.Label(body, text="", foreground=LEVEL_COLOURS[validate.ERROR])
        self.error.grid(row=len(fields) + 1, column=0, columnspan=3, sticky="w")
        buttons = ttk.Frame(body)
        buttons.grid(row=len(fields) + 2, column=0, columnspan=3, sticky="e", pady=(8, 0))
        ttk.Button(buttons, text="OK", command=self.ok).pack(side="left", padx=4)
        ttk.Button(buttons, text="Cancel", command=self.destroy).pack(side="left")
        self.bind("<Return>", lambda e: self.ok())
        self.bind("<Escape>", lambda e: self.destroy())
        try:  # Modal, once it is on the screen.
            self.wait_visibility()
            self.grab_set()
        except tk.TclError:
            pass

    def ok(self):
        values = []
        for (label, _, check), var in zip(self.fields, self.vars):
            try:
                values.append(check(var.get()))
            except ValueError as error:
                self.error.configure(text="{}: {}".format(label, error))
                return
        self.result = values
        self.destroy()


class App:
    """The window: the module being edited, its project file, the tabs."""

    def __init__(self, root, path=None):
        self.root = root
        self.module = Module()
        self.project_path = None
        self.imported_from = None
        self.dirty = False
        self.loading = False
        self.check_job = None
        self.issues = []
        style = ttk.Style(root)
        style.configure("Bad.TEntry", fieldbackground="#ffd0d0")
        style.configure("Bad.TCombobox", fieldbackground="#ffd0d0")
        root.title(TITLE)
        root.geometry("1000x720")
        root.protocol("WM_DELETE_WINDOW", self.quit)
        self._menu()
        self.status = ttk.Label(root, text="", anchor="w", padding=(8, 2))
        self.status.pack(fill="x", side="bottom")  # Before the notebook, which takes the rest.
        self.notebook = ttk.Notebook(root)
        self.notebook.pack(fill="both", expand=True)
        self.tabs = [ModuleTab(self), PowerTab(self), TransceiverTab(self), GroupsTab(self),
                     PeripheralsTab(self), VendorTab(self), ImageTab(self)]
        for tab in self.tabs:
            self.notebook.add(tab.frame, text=tab.title)
        self.status.bind("<Button-1>", lambda e: self.notebook.select(self.tabs[-1].frame))
        if path:
            self.open_path(path)
        else:
            self.load_all()

    # The menu and the title.

    def _menu(self):
        bar = tk.Menu(self.root)
        files = tk.Menu(bar, tearoff=False)
        files.add_command(label="New", command=self.new, accelerator="Ctrl+N")
        files.add_command(label="Open project...", command=self.open, accelerator="Ctrl+O")
        files.add_command(label="Save project", command=self.save, accelerator="Ctrl+S")
        files.add_command(label="Save project as...", command=self.save_as)
        files.add_separator()
        files.add_command(label="Import image (.dat)...", command=self.import_image,
                          accelerator="Ctrl+I")
        files.add_command(label="Export image (.dat)...", command=self.export_image,
                          accelerator="Ctrl+E")
        files.add_separator()
        files.add_command(label="Quit", command=self.quit)
        bar.add_cascade(label="File", menu=files)
        helps = tk.Menu(bar, tearoff=False)
        helps.add_command(label="About", command=self.about)
        bar.add_cascade(label="Help", menu=helps)
        self.root.configure(menu=bar)
        for key, command in (("n", self.new), ("o", self.open), ("s", self.save),
                             ("i", self.import_image), ("e", self.export_image)):
            self.root.bind_all("<Control-{}>".format(key), lambda e, c=command: c())

    def update_title(self):
        if self.project_path:
            name = os.path.basename(self.project_path)
        elif self.imported_from:
            name = "imported from " + os.path.basename(self.imported_from)
        else:
            name = "new board"
        self.root.title("{}{} - {}".format(name, " *" if self.dirty else "", TITLE))

    def project_dir(self):
        """Where file dialogs start and XML paths are relative to: the project's folder, or
        the current directory for a project not saved yet."""
        if self.project_path:
            return os.path.dirname(os.path.abspath(self.project_path))
        return os.getcwd()

    # Edits and checks.

    def changed(self, reload_tabs=()):
        """An edit went into the module: mark it, reload the tabs named, check again soon."""
        if self.loading:
            return
        self.dirty = True
        self.update_title()
        for tab in self.tabs:
            if type(tab).__name__ in reload_tabs:
                tab.load()
        self.schedule_check()

    def schedule_check(self):
        if self.check_job is not None:
            self.root.after_cancel(self.check_job)
        self.check_job = self.root.after(CHECK_DELAY_MS, self.check)

    def check(self):
        if self.check_job is not None:
            self.root.after_cancel(self.check_job)
        self.check_job = None
        self.issues = validate.check(self.module)
        errors = sum(1 for i in self.issues if i.level == validate.ERROR)
        warnings = sum(1 for i in self.issues if i.level == validate.WARNING)
        try:
            size = "{} bytes".format(len(encode(self.module).image))
        except EncodeError:
            size = "can't be built"
        self.status.configure(
            text="{} error(s), {} warning(s); image {} of a {}-byte EEPROM ({}). Click for "
                 "the list.".format(errors, warnings, size, self.module.eeprom_size,
                                    self.module.target_chip),
            foreground=LEVEL_COLOURS[validate.ERROR] if errors else "")
        self.tabs[-1].show(self.issues)

    def load_all(self):
        """Every tab from the module."""
        self.loading = True
        try:
            for tab in self.tabs:
                tab.load()
        finally:
            self.loading = False
        self.update_title()
        self.check()

    # Files.

    def ask_discard(self):
        if not self.dirty:
            return True
        answer = messagebox.askyesnocancel(TITLE, "Save the changes first?", parent=self.root)
        if answer is None:
            return False
        return self.save() if answer else True

    def new(self):
        if not self.ask_discard():
            return
        self.module = Module()
        self.project_path = None
        self.imported_from = None
        self.dirty = False
        self.load_all()

    def open(self):
        if not self.ask_discard():
            return
        path = filedialog.askopenfilename(
            parent=self.root, title="Open project", initialdir=self.project_dir(),
            filetypes=[("A2B EEPROM projects", "*.json"), ("All files", "*.*")])
        if path:
            self.open_path(path)

    def open_path(self, path):
        try:
            module, problems = project.load(path)
        except project.ProjectError as error:
            messagebox.showerror(TITLE, str(error), parent=self.root)
            return
        self.module = module
        self.project_path = os.path.abspath(path)
        self.imported_from = None
        self.dirty = False
        self.load_all()
        if problems:
            messagebox.showwarning(TITLE, "Programs that couldn't be read:\n\n" + "\n".join(
                "Peripheral {}: {}".format(n + 1, message) for n, message in problems),
                parent=self.root)

    def save(self):
        if not self.project_path:
            return self.save_as()
        try:
            project.save(self.module, self.project_path)
        except OSError as error:
            messagebox.showerror(TITLE, "Can't save: {}".format(error), parent=self.root)
            return False
        self.dirty = False
        self.update_title()
        return True

    def save_as(self):
        path = filedialog.asksaveasfilename(
            parent=self.root, title="Save project", initialdir=self.project_dir(),
            defaultextension=".json", filetypes=[("A2B EEPROM projects", "*.json")])
        if not path:
            return False
        old_dir = self.project_dir()
        new_dir = os.path.dirname(os.path.abspath(path))
        for p in self.module.peripherals:  # The XML paths, relative to the new folder.
            if p.xml:
                p.xml = project.stored_path(project.full_path(p.xml, old_dir), new_dir)
        self.project_path = os.path.abspath(path)
        self.imported_from = None
        if self.save():
            self.tabs[4].load()
            return True
        return False

    def import_image(self):
        if not self.ask_discard():
            return
        path = filedialog.askopenfilename(
            parent=self.root, title="Import image", initialdir=self.project_dir(),
            filetypes=[("Module-info images", "*.dat *.bin"), ("All files", "*.*")])
        if not path:
            return
        try:
            with open(path, "rb") as f:
                data = f.read()
        except OSError as error:
            messagebox.showerror(TITLE, str(error), parent=self.root)
            return
        decoded = decode(data)
        if decoded.module is None:
            messagebox.showerror(TITLE, "{} is not a module-info image:\n\n{}".format(
                os.path.basename(path), "\n".join(i[2] for i in decoded.issues)),
                parent=self.root)
            return
        module = decoded.module  # Its target chip from 0x07 (decode()).
        self.module = module
        self.project_path = None
        self.imported_from = path
        self.dirty = True
        self.load_all()
        TextWindow(self.root, "Imported {}".format(os.path.basename(path)),
                   report.describe(module, decoded, units_shown=50))

    def export_image(self):
        self.check()
        errors = [i for i in self.issues if i.level == validate.ERROR]
        if errors:
            messagebox.showerror(TITLE, "Not exported: fix the errors first.\n\n" + "\n".join(
                "{}: {}".format(i.where, i.message) for i in errors[:15]), parent=self.root)
            self.notebook.select(self.tabs[-1].frame)
            return
        base = os.path.splitext(os.path.basename(self.project_path or self.imported_from or
                                                 "board"))[0]
        path = filedialog.asksaveasfilename(
            parent=self.root, title="Export image", initialdir=self.project_dir(),
            initialfile=base + ".dat", defaultextension=".dat",
            filetypes=[("Module-info images", "*.dat")])
        if not path:
            return
        image = encode(self.module).image
        try:
            with open(path, "wb") as f:
                f.write(image)
        except OSError as error:
            messagebox.showerror(TITLE, "Can't write: {}".format(error), parent=self.root)
            return
        messagebox.showinfo(TITLE, "Wrote {}: {} bytes.\n\nThe bench console writes it to a "
                            "sub-node: eeprom write N {}".format(
                                os.path.basename(path), len(image), os.path.basename(path)),
                            parent=self.root)

    def about(self):
        messagebox.showinfo(TITLE, "{} {}\n\nBuilds and reads the module-info image of an A2B "
                            "sub-node's EEPROM (ADI's PnP format).\n\ntools/a2b_eeprom/README.md"
                            .format(TITLE, __version__), parent=self.root)

    def quit(self):
        if self.ask_discard():
            self.close()

    def close(self):
        """Closes the window, with no check left to run."""
        if self.check_job is not None:
            self.root.after_cancel(self.check_job)
            self.check_job = None
        self.root.destroy()


class TextWindow(tk.Toplevel):
    """Text to read, in its own window."""

    def __init__(self, parent, title, lines):
        tk.Toplevel.__init__(self, parent)
        self.title(title)
        self.geometry("900x600")
        text = tk.Text(self, wrap="none", font=("Courier", 10))
        bar = ttk.Scrollbar(self, orient="vertical", command=text.yview)
        text.configure(yscrollcommand=bar.set)
        bar.pack(side="right", fill="y")
        text.pack(fill="both", expand=True)
        text.insert("1.0", "\n".join(lines))
        text.configure(state="disabled")


class Tab:
    """A notebook page. load() fills it from app.module."""

    title = ""

    def __init__(self, app):
        self.app = app
        self.frame = ttk.Frame(app.notebook, padding=10)

    @property
    def m(self):
        return self.app.module

    def load(self):
        pass

    def set(self, setter):
        """Applies an edit to the module, then tells the app."""
        if self.app.loading:
            return
        setter(self.m)
        self.app.changed()

    def number(self, parent, attr, minimum=0, maximum=0xFFFF, hex_digits=0, width=10,
               after=None):
        """A NumberEntry bound to the module's attribute `attr`."""
        def on_value(v):
            def setter(m):
                setattr(m, attr, v)
            self.set(setter)
            if after:
                after()
        entry = NumberEntry(parent, on_value, minimum, maximum, hex_digits, width)
        entry.attr = attr
        return entry


class ModuleTab(Tab):
    """The board that holds the EEPROM: the header and the module-info block's fields (ADI's
    specification calls the board a module)."""

    title = "Board"

    def __init__(self, app):
        Tab.__init__(self, app)
        f = ttk.LabelFrame(self.frame, text="The project (not in the image)", padding=8)
        f.pack(fill="x")
        self.size = tk.StringVar()
        size = ttk.Combobox(f, textvariable=self.size, width=10,
                            values=[str(s) for s in EEPROM_SIZES])
        row(f, 0, "EEPROM size (bytes)", size, "24FC256: 32768")
        self.size.trace_add("write", lambda *a: self._size())
        notes = ttk.Frame(f)
        self.notes = tk.Text(notes, width=70, height=6, wrap="word")
        bar = ttk.Scrollbar(notes, orient="vertical", command=self.notes.yview)
        self.notes.configure(yscrollcommand=bar.set)
        self.notes.pack(side="left", fill="both", expand=True)
        bar.pack(side="left", fill="y")
        row(f, 1, "Notes", notes)
        notes.grid(sticky="ew", columnspan=2)  # Widens with the window.
        f.columnconfigure(1, weight=1)
        self.notes.bind("<<Modified>>", self._notes)

        h = ttk.LabelFrame(self.frame, text="Header (0x00)", padding=8)
        h.pack(fill="x", pady=(10, 0))
        self.entries = [
            row(h, 0, "Board vendor ID", self.number(h, "vendor_id", hex_digits=4),
                "ADI: Module Vendor ID. Clockworks: 0x0C54; 0x00AD is ADI's"),
            row(h, 1, "Board product ID", self.number(h, "product_id", hex_digits=4),
                "ADI: Module Product ID. Clockworks: the board's number (AB0020: 0x0020)"),
            row(h, 2, "Board version ID", self.number(h, "version_id", hex_digits=4),
                "ADI: Module Version ID. The stack reads its low byte as the silicon version"),
        ]
        self.sw = tk.StringVar()
        sw = ttk.Entry(h, textvariable=self.sw, width=12)
        row(h, 3, "SW version", sw, "Four numbers 0-255: the tool's own by default")
        self.sw.trace_add("write", lambda *a: self._sw(sw))
        self.chip = tk.StringVar()
        chip = ttk.Combobox(h, textvariable=self.chip, state="readonly", width=48,
                            values=[c.name for c in registers.CHIPS] + list(CHIP_CHOICES))
        row(h, 4, "Target chip (0x07)", chip, "The stack rejects a node with another chip")
        chip.bind("<<ComboboxSelected>>", lambda e: self._chip())

        b = ttk.LabelFrame(self.frame, text="Board information (0x20; ADI: Module Information)",
                           padding=8)
        b.pack(fill="x", pady=(10, 0))
        self.name = tk.StringVar()
        row(b, 0, "Device name", ttk.Entry(b, textvariable=self.name, width=40),
            "Up to 59 characters")
        self.name.trace_add("write", lambda *a: self.set(
            lambda m: setattr(m, "device_name", self.name.get())))
        types = ttk.Frame(b)
        self.type_bits = {}
        for n, (bit, label) in enumerate(sorted(L.DEVICE_TYPE_BITS.items())):
            var = tk.IntVar()
            ttk.Checkbutton(types, text=label, variable=var,
                            command=self._device_type).grid(row=n // 3, column=n % 3, sticky="w")
            self.type_bits[bit] = var
        row(b, 1, "Device type", types)
        self.cable = tk.StringVar()
        cable = ttk.Combobox(b, textvariable=self.cable, state="readonly", width=20,
                             values=["{}: {}".format(k, v) for k, v in L.CABLE_TYPES.items()])
        row(b, 2, "B-side cable type", cable, "Sets the link to the next node")
        cable.bind("<<ComboboxSelected>>", lambda e: self._cable())
        self.cable_hint = ttk.Label(b, foreground="#555555")
        self.cable_hint.grid(row=3, column=1, columnspan=2, sticky="w")
        self.entries.append(row(b, 4, "B-side cable length (m)",
                                self.number(b, "cable_length", 0, 0xFFFF),
                                "For the response cycles; 0: the board's (4 m)"))

    def load(self):
        m = self.m
        self.chip.set(CHIP_CHOICES_BY_VALUE.get(m.target_chip, m.target_chip))
        self.size.set(str(m.eeprom_size))
        self.notes.delete("1.0", "end")
        self.notes.insert("1.0", m.notes)
        self.notes.edit_modified(False)
        for entry in self.entries:
            entry.set(getattr(m, entry.attr))
        self.sw.set(".".join(str(v) for v in m.sw_version))
        self.name.set(m.device_name)
        for bit, var in self.type_bits.items():
            var.set(m.device_type >> bit & 1)
        self.cable.set("{}: {}".format(m.cable_type, L.CABLE_TYPES.get(m.cable_type, "reserved")))
        self.cable_hint.config(text=L.CABLE_TYPE_HINTS.get(m.cable_type, "Reserved: not probed "
                                                                          "past this node"))

    def _chip(self):
        value = CHIP_CHOICES.get(self.chip.get(), self.chip.get())
        self.set(lambda m: setattr(m, "target_chip", value))
        self.app.changed(reload_tabs=("TransceiverTab",))

    def _cable(self):
        kind = int(self.cable.get().split(":")[0])
        self.set(lambda m: setattr(m, "cable_type", kind))
        self.cable_hint.config(text=L.CABLE_TYPE_HINTS.get(kind, ""))

    def _size(self):
        try:
            value = parse_number(self.size.get(), 256, L.MAX_IMAGE)
        except ValueError:
            return
        self.set(lambda m: setattr(m, "eeprom_size", value))

    def _notes(self, _):
        if self.notes.edit_modified():
            self.set(lambda m: setattr(m, "notes", self.notes.get("1.0", "end-1c")))
            self.notes.edit_modified(False)

    def _sw(self, entry):
        try:
            parts = [int(p) for p in self.sw.get().split(".")]
            if len(parts) != 4 or any(not 0 <= p <= 255 for p in parts):
                raise ValueError
        except ValueError:
            entry.configure(style="Bad.TEntry")
            return
        entry.configure(style="TEntry")
        self.set(lambda m: setattr(m, "sw_version", parts))

    def _device_type(self):
        def setter(m):
            value = m.device_type & ~sum(1 << b for b in self.type_bits)
            for bit, var in self.type_bits.items():
                value |= var.get() << bit
            m.device_type = value
        self.set(setter)


class PowerTab(Tab):
    title = "Power"

    def __init__(self, app):
        Tab.__init__(self, app)
        f = ttk.LabelFrame(self.frame, text="Power (0x23-0x2A)", padding=8)
        f.pack(fill="x")
        self.config = tk.StringVar()
        config = ttk.Combobox(f, textvariable=self.config, width=24, values=[
            "0: CFG0 (standard power)", "4: CFG4 (high power, 24 V)"])
        row(f, 0, "Power configuration", config,
            "Bits 7:4 of 0x23; the stack applies it on AD243x nodes")
        self.config.trace_add("write", lambda *a: self._config(config))
        self.local = tk.IntVar()
        row(f, 1, "Local power", ttk.Checkbutton(f, variable=self.local, command=lambda: self.set(
            lambda m: setattr(m, "local_power", bool(self.local.get())))),
            "The board has its own supply: not counted in the power budget")
        self.entries = [
            row(f, 2, "Most power (mW)", self.number(f, "max_power_mw"),
                "The power budget counts it for a bus-powered board"),
            row(f, 3, "Least power (mW)", self.number(f, "min_power_mw")),
            row(f, 4, "Most current (mA)", self.number(f, "max_current_ma")),
        ]

    def load(self):
        m = self.m
        self.config.set({0: "0: CFG0 (standard power)", 4: "4: CFG4 (high power, 24 V)"}.get(
            m.power_config, str(m.power_config)))
        self.local.set(int(m.local_power))
        for entry in self.entries:
            entry.set(getattr(m, entry.attr))

    def _config(self, widget):
        try:
            value = parse_number(self.config.get().split(":")[0], 0, 15)
        except ValueError:
            widget.configure(style="Bad.TCombobox")
            return
        widget.configure(style="TCombobox")
        self.set(lambda m: setattr(m, "power_config", value))


class TransceiverTab(Tab):
    """The register settings, field by field: for the target chip's family; for a board with a
    plug-in module (External), the fields both families share, then the AD243x's own."""

    title = "Transceiver I/O"

    def __init__(self, app):
        Tab.__init__(self, app)
        self.summary = ttk.Label(self.frame, text="", font=("TkDefaultFont", 10, "bold"))
        self.summary.pack(fill="x", pady=(0, 6))
        self.scroll = ScrollFrame(self.frame)
        self.scroll.pack(fill="both", expand=True)
        self.updating = False

    def load(self):
        self.scroll.clear()
        m = self.m
        family = m.family()
        inner = self.scroll.inner
        self.widgets = {}
        r = 0
        if m.external():
            r = self._external(inner, r)
        else:
            if m.chip() is None:
                # Not stated (ADI's images): which family's fields to show.
                view = ttk.Frame(inner)
                view.grid(row=r, column=0, sticky="w", pady=4)
                var = tk.StringVar(value=m.view_family)
                box = ttk.Combobox(view, textvariable=var, values=list(registers.BOTH),
                                   state="readonly", width=8)
                row(view, 0, "View fields as", box, "The target chip is not stated")
                box.bind("<<ComboboxSelected>>", lambda e, v=var: self._view(v.get()))
                r += 1
            for key in DISPLAY_ORDER:
                self._register(inner, r, registers.find_register(key), family)
                r += 1
        slots = ttk.LabelFrame(inner, text="Slots and extras (stored; this stack doesn't use "
                               "them)", padding=6)
        slots.grid(row=r, column=0, sticky="ew", pady=4)
        values = ["{}: {}".format(k, v) for k, v in L.SLOT_SIZES.items()]
        for n, (attr, label) in enumerate((("min_down_slot", "Minimum downstream slot (0x7D)"),
                                           ("min_up_slot", "Minimum upstream slot (0x7E)"))):
            var = tk.StringVar(value="{}: {}".format(getattr(m, attr),
                                                      L.SLOT_SIZES.get(getattr(m, attr), "?")))
            box = ttk.Combobox(slots, textvariable=var, values=values, state="readonly", width=18)
            row(slots, n, label, box)
            box.bind("<<ComboboxSelected>>", lambda e, a=attr, v=var: self.set(
                lambda mm: setattr(mm, a, int(v.get().split(":")[0]))))
        for n, (attr, label) in enumerate((("spi_tunnel", "SPI data tunnel (0x90)"),
                                           ("branches", "Branches (0x95)"),
                                           ("local_micro", "Local micro (0x96)"),
                                           ("local_micro_mode", "Local micro mode (0x97)"),
                                           ("local_micro_peripherals",
                                            "Local micro does peripherals (0x98)"))):
            entry = self.number(slots, attr, 0, 0xFF)
            row(slots, 2 + n, label, entry)
            entry.set(getattr(m, attr))
        inner.columnconfigure(0, weight=1)
        self._summary()

    def _external(self, inner, r):
        """A plug-in module's board: the shared fields, then the AD243x's own. Returns the next
        row."""
        both = ttk.LabelFrame(inner, text="Both families: the same on an AD242x and an AD243x "
                              "module", padding=6)
        both.grid(row=r, column=0, sticky="ew", pady=4)
        both.columnconfigure(0, weight=1)
        n = 0
        for key in DISPLAY_ORDER:
            reg = registers.find_register(key)
            fields = registers.shared_fields(reg)
            if fields:
                self._register(both, n, reg, None, fields)
                n += 1
        only = ttk.LabelFrame(inner, text="AD243x only: an AD242x module doesn't get these (the "
                              "stack leaves out SPI there, and writes these bits clear)", padding=6)
        only.grid(row=r + 1, column=0, sticky="ew", pady=4)
        only.columnconfigure(0, weight=1)
        n = 0
        for key in DISPLAY_ORDER:
            reg = registers.find_register(key)
            fields = registers.ad243x_only_fields(reg)
            if not fields:
                continue
            if reg.families == (registers.AD243X,):
                self._register(only, n, reg, None, fields)
            else:
                self._fields_only(only, n, reg, fields)
            n += 1
        return r + 2

    def _fields_only(self, parent, r, reg, fields):
        """Fields of a register shown elsewhere with its byte: only the fields here."""
        frame = ttk.LabelFrame(parent, text="0x{:02X}  {}: its AD243x bits".format(
            reg.offset, reg.name), padding=6)
        frame.grid(row=r, column=0, sticky="ew", pady=4)
        fields_frame = ttk.Frame(frame)
        fields_frame.grid(row=0, column=0, sticky="w")
        hex_entry, shown = self.widgets.setdefault(reg.key, (None, []))
        for n, field in enumerate(fields):
            shown.append(self._field(fields_frame, n, reg, field))
        self.widgets[reg.key] = (hex_entry, shown)

    def _register(self, parent, r, reg, family, fields=None):
        on_chip = fields is not None or family in reg.families
        title = "0x{:02X}  {}: {}".format(reg.offset, reg.name, reg.label)
        if not on_chip:
            chip = self.m.chip()
            title += "  (not on the {}: keep it at 0x{:02X})".format(
                chip.name if chip else family, reg.power_on)
        if fields is None:
            fields = reg.fields_for(family) if on_chip else []
        frame = ttk.LabelFrame(parent, text=title, padding=6)
        frame.grid(row=r, column=0, sticky="ew", pady=4)
        value = self.m.regs[reg.key]
        hex_entry = NumberEntry(frame, lambda v, k=reg.key: self._value(k, v), 0, 0xFF, 2, 6)
        hex_entry.grid(row=0, column=0, sticky="nw", padx=(0, 12))
        hex_entry.set(value)
        shown = []
        if fields:
            fields_frame = ttk.Frame(frame)
            fields_frame.grid(row=0, column=1, sticky="w")
            for n, field in enumerate(fields):
                shown.append(self._field(fields_frame, n, reg, field))
        self.widgets[reg.key] = (hex_entry, shown)

    def _field(self, parent, n, reg, field):
        r, c = n, 0
        ttk.Label(parent, text="{} ({})".format(field.label, field.name)).grid(
            row=r, column=2 * c, sticky="w", padx=(0, 6))
        current = field.get(self.m.regs[reg.key])
        if field.width == 1 and field.values is None:
            var = tk.IntVar(value=current)
            widget = ttk.Checkbutton(parent, variable=var,
                                     command=lambda: self._field_changed(reg, field, var.get()))
        elif field.values is not None:
            choices = ["{}: {}".format(k, v) for k, v in sorted(field.values.items())]
            var = tk.StringVar(value="{}: {}".format(current, field.values.get(
                current, "reserved")))
            widget = ttk.Combobox(parent, textvariable=var, values=choices, state="readonly",
                                  width=34)
            widget.bind("<<ComboboxSelected>>", lambda e: self._field_changed(
                reg, field, int(var.get().split(":")[0])))
        else:
            var = tk.StringVar(value=str(current))
            widget = ttk.Spinbox(parent, from_=0, to=(1 << field.width) - 1, textvariable=var,
                                 width=6, command=lambda: self._spin(reg, field, var))
            var.trace_add("write", lambda *a: self._spin(reg, field, var))
        widget.grid(row=r, column=2 * c + 1, sticky="w", pady=1)
        return field, var

    def _spin(self, reg, field, var):
        try:
            value = int(var.get(), 0)
        except ValueError:
            return
        if 0 <= value < 1 << field.width:
            self._field_changed(reg, field, value)

    def _field_changed(self, reg, field, field_value):
        if self.updating:
            return
        new = field.put(self.m.regs[reg.key], field_value)
        if new != self.m.regs[reg.key]:
            self._value(reg.key, new)

    def _value(self, key, value):
        if self.updating or self.app.loading:
            return
        self.m.regs[key] = value
        self.updating = True
        try:
            hex_entry, fields = self.widgets[key]
            if hex_entry is not None and hex_entry.var.get().lower() != "0x{:02x}".format(value):
                hex_entry.set(value)
            for field, var in fields:
                current = field.get(value)
                if isinstance(var, tk.IntVar):
                    var.set(current)
                elif field.values is not None:
                    var.set("{}: {}".format(current, field.values.get(current, "reserved")))
                else:
                    var.set(str(current))
        finally:
            self.updating = False
        self._summary()
        self.app.changed()

    def _summary(self):
        chip = self.m.chip()
        regs = self.m.regs
        if self.m.external():
            text = ("Port (both families): " + registers.port_summary(regs, registers.AD243X)
                    if registers.i2scfg_shared(regs["i2scfg"]) else
                    "Port: I2SCFG reads differently on the two families (see the checks)")
        else:
            text = "Port ({}): {}".format(chip.name if chip else "as the " + self.m.family(),
                                          registers.port_summary(regs, self.m.family()))
        self.summary.configure(text=text)

    def _view(self, family):
        """Shows the fields as @p family's (no chip named)."""
        self.set(lambda m: setattr(m, "view_family", family))
        self.app.changed(reload_tabs=("TransceiverTab",))


class GroupsTab(Tab):
    """The TX and RX channel groups (0x100)."""

    title = "Channel groups"

    def __init__(self, app):
        Tab.__init__(self, app)
        ttk.Label(self.frame, wraplength=900, text=(
            "RX groups: channels the board sends onto the bus (its DRX pins, its PDM "
            "microphones). TX groups: channels it plays from the bus (its DTX pins). Channel n "
            "is the n-th TDM channel of the port, across its pins. Connections name these "
            "groups; the stack keeps 8 each way, and the table holds 13 in all.")).pack(
            fill="x", pady=(0, 8))
        self.trees = {}
        for direction in ("tx", "rx"):
            frame = ttk.LabelFrame(self.frame, text="{} groups".format(direction.upper()),
                                   padding=6)
            frame.pack(fill="both", expand=True, pady=4)
            tree = ttk.Treeview(frame, columns=("id", "name", "channels"), show="headings",
                                height=6)
            for column, width in (("id", 60), ("name", 300), ("channels", 300)):
                tree.heading(column, text=column.capitalize())
                tree.column(column, width=width, anchor="w")
            tree.pack(side="left", fill="both", expand=True)
            tree.bind("<Double-1>", lambda e, d=direction: self.edit(d))
            buttons = ttk.Frame(frame)
            buttons.pack(side="left", fill="y", padx=6)
            for label, command in (("Add", self.add), ("Edit", self.edit),
                                   ("Remove", self.remove)):
                ttk.Button(buttons, text=label, command=lambda c=command, d=direction: c(d)).pack(
                    fill="x", pady=2)
            self.trees[direction] = tree

    def groups(self, direction):
        return self.m.tx_groups if direction == "tx" else self.m.rx_groups

    def load(self):
        for direction, tree in self.trees.items():
            tree.delete(*tree.get_children())
            for n, g in enumerate(self.groups(direction)):
                tree.insert("", "end", iid=str(n), values=(g.id, g.name,
                                                           report.channel_list(g.channels)))

    def _dialog(self, title, group):
        def check_id(text):
            return parse_number(text, 0, 255)

        def check_name(text):
            if len(text) > L.GROUP_NAME_LEN - 1:
                raise ValueError("up to {} characters".format(L.GROUP_NAME_LEN - 1))
            return text
        dialog = Dialog(self.frame, title, [
            ("ID (0-255)", str(group.id), check_id),
            ("Name", group.name, check_name),
            ("Channels (e.g. 0-1, 0,2,4-7)", report.channel_list(group.channels),
             report.parse_channels),
        ])
        self.frame.wait_window(dialog)
        return dialog.result

    def add(self, direction):
        groups = self.groups(direction)
        used = {g.id for g in groups}
        new_id = next(i for i in range(256) if i not in used)
        result = self._dialog("Add a {} group".format(direction.upper()), Group(new_id, "", 0x3))
        if result:
            self.set(lambda m: self.groups(direction).append(Group(*result)))
            self.load()

    def edit(self, direction):
        tree = self.trees[direction]
        selected = tree.selection()
        if not selected:
            return
        n = int(selected[0])
        result = self._dialog("Edit a {} group".format(direction.upper()),
                              self.groups(direction)[n])

        def setter(m):
            self.groups(direction)[n] = Group(*result)
        if result:
            self.set(setter)
            self.load()

    def remove(self, direction):
        selected = self.trees[direction].selection()
        if selected:
            n = int(selected[0])
            self.set(lambda m: self.groups(direction).pop(n))
            self.load()


class PeripheralsTab(Tab):
    """The peripheral blocks: each one's fields and program."""

    title = "Peripherals"

    def __init__(self, app):
        Tab.__init__(self, app)
        left = ttk.Frame(self.frame)
        left.pack(side="left", fill="y")
        self.list = tk.Listbox(left, width=28, height=12, exportselection=False)
        self.list.pack(fill="y", expand=True)
        self.list.bind("<<ListboxSelect>>", lambda e: self.show())
        buttons = ttk.Frame(left)
        buttons.pack(fill="x", pady=4)
        for label, command in (("Add", self.add), ("Remove", self.remove), ("Up", self.up),
                               ("Down", self.down)):
            ttk.Button(buttons, text=label, width=7, command=command).pack(side="left")
        self.detail = ttk.Frame(self.frame, padding=(12, 0, 0, 0))
        self.detail.pack(side="left", fill="both", expand=True)
        self.index = None
        self.pin_boxes = {}  # "rx_interface"/"tx_interface": the selected one's boxes, update.

    def load(self):
        self.list.delete(0, "end")
        for n, p in enumerate(self.m.peripherals):
            self.list.insert("end", "{}: {}".format(n + 1, p.name or "(no name)"))
        count = len(self.m.peripherals)
        if self.index is None or self.index >= count:
            self.index = 0 if count else None
        if self.index is not None:
            self.list.selection_set(self.index)
        self.show(keep=True)

    def selected(self):
        return self.m.peripherals[self.index] if self.index is not None else None

    def show(self, keep=False):
        if not keep:
            selection = self.list.curselection()
            self.index = selection[0] if selection else None
        for child in self.detail.winfo_children():
            child.destroy()
        p = self.selected()
        if p is None:
            ttk.Label(self.detail, text="No peripherals: Add one for a codec, an amplifier, ...")\
                .pack(anchor="w")
            return
        f = ttk.LabelFrame(self.detail, text="Peripheral block", padding=8)
        f.pack(fill="x")
        name = tk.StringVar(value=p.name)
        row(f, 0, "Name", ttk.Entry(f, textvariable=name, width=40), "Up to 63 characters")
        name.trace_add("write", lambda *a: self._edit("name", name.get(), relist=True))
        interface = tk.StringVar(value={0: "I2C", 1: "SPI", 0xFF: "Not programmable"}.get(
            p.interface, str(p.interface)))
        box = ttk.Combobox(f, textvariable=interface, state="readonly", width=18,
                           values=["I2C", "SPI", "Not programmable"])
        row(f, 1, "Interface", box, "The stack skips SPI's units (SPI is not supported)")
        box.bind("<<ComboboxSelected>>", lambda e: self._edit(
            "interface", {"I2C": 0, "SPI": 1}.get(interface.get(), 0xFF)))
        entry = NumberEntry(f, lambda v: self._edit("address", v, reread=True), 0, 0x7F, 2, 6)
        entry.set(p.address)
        row(f, 2, "I2C address (7-bit)", entry, "For writes without their own")
        for n, (attr, label) in enumerate((("tx_groups", "Fed by TX groups"),
                                           ("rx_groups", "Feeds RX groups"))):
            var = tk.StringVar(value=report.channel_list(getattr(p, attr)))
            widget = ttk.Entry(f, textvariable=var, width=20)
            row(f, 3 + n, label, widget, "Group IDs, e.g. 0 or 0,1")
            var.trace_add("write", lambda *a, a_=attr, v=var, w=widget: self._ids(a_, v, w))
        for n, (attr, label) in enumerate((("rx_interface", "On the A2B's RX pins"),
                                           ("tx_interface", "On the A2B's TX pins"))):
            self._pins(f, 5 + n, attr, label)
        for n, (attr, label) in enumerate((("addr_width", "Sub-address bytes"),
                                           ("data_width", "Data bytes"),
                                           ("cmd_width", "Command bytes"))):
            entry = NumberEntry(f, lambda v, a=attr: self._edit(a, v), 0, 4)
            entry.set(getattr(p, attr))
            row(f, 7 + n, label, entry)

        g = ttk.LabelFrame(self.detail, text="Program", padding=8)
        g.pack(fill="both", expand=True, pady=(8, 0))
        path = tk.StringVar(value=p.xml)
        line = ttk.Frame(g)
        ttk.Entry(line, textvariable=path, width=46).pack(side="left", fill="x", expand=True)
        ttk.Button(line, text="...", width=3, command=lambda: self._browse(path)).pack(
            side="left", padx=2)
        ttk.Button(line, text="Read again", command=lambda: self._read(p.xml)).pack(side="left")
        row(g, 0, "SigmaStudio XML", line)
        path.trace_add("write", lambda *a: self._edit("xml", path.get().strip(), reread=True))
        split = ttk.Frame(g)
        split_max = NumberEntry(split, lambda v: self._edit("split_max", v), 0, L.MAX_WRITE,
                                width=6)
        split_max.set(p.split_max)
        split_max.pack(side="left")
        ttk.Label(split, text=" bytes at most (0: no split), ").pack(side="left")
        step = NumberEntry(split, lambda v: self._edit("split_bytes_per_address", v), 1, 64,
                           width=4)
        step.set(p.split_bytes_per_address)
        step.pack(side="left")
        ttk.Label(split, text=" data bytes per address step").pack(side="left")
        row(g, 1, "Split long writes", split)
        self.summary = ttk.Label(g, text="")
        self.summary.grid(row=2, column=0, columnspan=3, sticky="w", pady=4)
        self.units = ttk.Treeview(g, columns=("n", "what", "address", "bytes", "data", "from"),
                                  show="headings", height=10)
        for column, width in (("n", 40), ("what", 60), ("address", 70), ("bytes", 60),
                              ("data", 280), ("from", 260)):
            self.units.heading(column, text=column.capitalize())
            self.units.column(column, width=width, anchor="w")
        self.units.grid(row=3, column=0, columnspan=3, sticky="nsew")
        g.rowconfigure(3, weight=1)
        g.columnconfigure(1, weight=1)
        self._show_units()

    def _pins(self, parent, r, attr, label):
        """The A2B pins it is on: a box per SIO pin, named by the stream it carries with the
        transceiver's settings (AD2437 TRM, Table 5-5); the pin count follows the boxes."""
        p = self.selected()
        value = getattr(p, attr)
        streams = registers.sio_streams(self.m.regs)["rx" if attr == "rx_interface" else "tx"]
        frame = ttk.Frame(parent)
        bits = []
        for n in range(5):
            var = tk.IntVar(value=value >> n & 1)
            ttk.Checkbutton(frame, text="SIO{} ({})".format(n, streams.get(n, "-")),
                            variable=var, command=lambda: update()).pack(side="left")
            bits.append(var)

        def update():
            sio = sum(v.get() << n for n, v in enumerate(bits))
            self._edit(attr, (getattr(p, attr) & 0xFF00) | (bin(sio).count("1") << 5) | sio)
        self.pin_boxes[attr] = (bits, update)
        row(parent, r, label, frame, "SIO numbering as the AD243x's; an AD242x's pins by name")

    def _ids(self, attr, var, widget):
        try:
            value = parse_ids(var.get())
        except ValueError:
            widget.configure(style="Bad.TEntry")
            return
        widget.configure(style="TEntry")
        self._edit(attr, value)

    def _edit(self, attr, value, relist=False, reread=False):
        p = self.selected()
        if p is None or getattr(p, attr) == value:
            return
        self.set(lambda m: setattr(p, attr, value))
        if relist:
            self.list.delete(self.index)
            self.list.insert(self.index, "{}: {}".format(self.index + 1, p.name or "(no name)"))
            self.list.selection_set(self.index)
        if reread:
            self._read(p.xml)

    def _browse(self, var):
        start = self.app.project_dir()
        current = project.full_path(var.get(), start)
        if current and os.path.isdir(os.path.dirname(current)):
            start = os.path.dirname(current)
        path = filedialog.askopenfilename(
            parent=self.frame, title="SigmaStudio XML program", initialdir=start,
            filetypes=[("SigmaStudio XML", "*.xml"), ("All files", "*.*")])
        if path:
            var.set(project.stored_path(path, self.app.project_dir()))

    def _read(self, xml):
        p = self.selected()
        if p is None:
            return
        if xml:
            problems = project.read_programs(_Only(self.m, p), self.app.project_dir())
            if problems:
                p.units = []
                p.xml_report = None
                self.summary.configure(text="Can't read it: " + problems[0][1],
                                       foreground=LEVEL_COLOURS[validate.ERROR])
                self._fill_units()
                self.app.schedule_check()
                return
        self._show_units()
        self.app.schedule_check()

    def _show_units(self):
        p = self.selected()
        if p.xml and p.xml_report is not None:
            text = "From the XML: " + p.xml_report.summary()
        elif p.xml:
            text = "The XML hasn't been read (Read again)"
        else:
            text = "{} unit(s) held in the project (from an imported image)".format(len(p.units))
        self.summary.configure(text=text, foreground="")
        self._fill_units()

    def _fill_units(self):
        self.units.delete(*self.units.get_children())
        for n, u in enumerate(self.selected().units):
            if u.kind == "delay":
                values = (n + 1, "wait", "", "", "{} ms".format(u.delay_ms), u.note)
            else:
                data = u.data[:12].hex(" ").upper() + (" ..." if len(u.data) > 12 else "")
                values = (n + 1, "write", "0x{:02X}".format(u.address), len(u.data), data,
                          u.note)
            self.units.insert("", "end", values=values)

    def add(self):
        p = Peripheral()
        p.name = "Peripheral {}".format(len(self.m.peripherals) + 1)
        self.set(lambda m: m.peripherals.append(p))
        self.index = len(self.m.peripherals) - 1
        self.load()

    def remove(self):
        if self.index is None:
            return
        n = self.index
        self.set(lambda m: m.peripherals.pop(n))
        self.index = max(0, n - 1)
        self.load()

    def _move(self, step):
        n = self.index
        if n is None or not 0 <= n + step < len(self.m.peripherals):
            return

        def setter(m):
            m.peripherals[n], m.peripherals[n + step] = m.peripherals[n + step], m.peripherals[n]
        self.set(setter)
        self.index = n + step
        self.load()

    def up(self):
        self._move(-1)

    def down(self):
        self._move(1)


class _Only:
    """A module with one peripheral, for project.read_programs()."""

    def __init__(self, module, peripheral):
        self.peripherals = [peripheral]


class VendorTab(Tab):
    """The Supplier Information block: the vendor ID and the vendor's section."""

    title = "Vendor data"

    def __init__(self, app):
        Tab.__init__(self, app)
        top = ttk.Frame(self.frame)
        top.pack(fill="x")
        self.vendor_id = tk.StringVar()
        box = ttk.Combobox(top, textvariable=self.vendor_id, width=12,
                           values=sorted(vendor.LAYOUTS))
        row(top, 0, "Vendor ID (0x403)", box,
            "8 characters: says how the 212 bytes from 0x42B read")
        self.vendor_id.trace_add("write", lambda *a: self._vendor_id())
        self.body = ttk.Frame(self.frame)
        self.body.pack(fill="both", expand=True, pady=(8, 0))

    def load(self):
        self.vendor_id.set(self.m.supplier_vendor_id)
        self._body()

    def _vendor_id(self):
        text = self.vendor_id.get()
        if self.app.loading or text == self.m.supplier_vendor_id:
            return
        old = self.m.vendor_layout()

        def setter(m):
            m.supplier_vendor_id = text
            new = m.vendor_layout()
            if new is not None and new is not old:
                m.vendor_values = new.defaults()
            elif new is None:
                m.vendor_values = {}
        self.set(setter)
        if self.m.vendor_layout() is not old:
            self._body()

    def _body(self):
        for child in self.body.winfo_children():
            child.destroy()
        layout_ = self.m.vendor_layout()
        if layout_ is None:
            ttk.Label(self.body, text="No layout known for this vendor ID: its data in hex "
                      "(up to 212 bytes).").pack(anchor="w")
            text = tk.Text(self.body, height=10, width=100, font=("Courier", 10))
            text.pack(fill="both", expand=True)
            text.insert("1.0", self.m.vendor_data.hex(" ").upper())
            text.edit_modified(False)
            text.bind("<<Modified>>", lambda e: self._raw(text))
            return
        ttk.Label(self.body, text="{}: {}".format(layout_.title, layout_.description),
                  wraplength=900).pack(anchor="w", pady=(0, 6))
        form = ttk.Frame(self.body)
        form.pack(fill="both", expand=True)
        r = 0
        for item in layout_.items:
            if isinstance(item, vendor.Repeat):
                self._repeat(form, r, item)
            else:
                self._simple(form, r, item)
            r += 1
        form.columnconfigure(1, weight=1)

    def _raw(self, text):
        if not text.edit_modified():
            return
        text.edit_modified(False)
        try:
            data = bytes.fromhex(text.get("1.0", "end-1c"))
        except ValueError:
            return
        self.set(lambda m: setattr(m, "vendor_data", data))

    def _simple(self, form, r, item):
        var = tk.StringVar(value=str(item.to_json(self.m.vendor_values.get(item.name,
                                                                          item.default()))))
        widget = ttk.Entry(form, textvariable=var, width=30)
        row(form, r, item.label, widget, item.help_text)

        def changed(*_):
            try:
                value = item.from_json(var.get())
            except ValueError:
                widget.configure(style="Bad.TEntry")
                return
            bad = item.check(value)
            widget.configure(style="Bad.TEntry" if bad else "TEntry")
            self.set(lambda m: m.vendor_values.__setitem__(item.name, value))
        var.trace_add("write", changed)

    def _repeat(self, form, r, item):
        frame = ttk.LabelFrame(form, text="{}s (up to {})".format(item.label, item.max_count),
                               padding=6)
        frame.grid(row=r, column=0, columnspan=3, sticky="nsew", pady=4)
        form.rowconfigure(r, weight=1)
        columns = [f.name for f in item.fields]
        tree = ttk.Treeview(frame, columns=columns, show="headings", height=item.max_count)
        for f in item.fields:
            tree.heading(f.name, text=f.label)
            tree.column(f.name, width=170 if f.kind == "uid" else 110, anchor="w")
        tree.pack(side="left", fill="both", expand=True)
        buttons = ttk.Frame(frame)
        buttons.pack(side="left", fill="y", padx=6)

        def records():
            return self.m.vendor_values.setdefault(item.name, [])

        def fill():
            tree.delete(*tree.get_children())
            for n, record in enumerate(records()):
                tree.insert("", "end", iid=str(n), values=[
                    f.to_json(record.get(f.name, f.default())) for f in item.fields])

        def dialog(title, record):
            fields = []
            for f in item.fields:
                def check(text, f=f):
                    value = f.from_json(text) if text.strip() else f.default()
                    problems = f.check(value)
                    if problems:
                        raise ValueError(problems[0])
                    return value
                fields.append((f.label, str(f.to_json(record.get(f.name, f.default()))), check))

            def extra(body, r_, vars_):
                uid = [n for n, f in enumerate(item.fields) if f.kind == "uid"]
                if uid:
                    var = vars_[uid[0]]
                    ttk.Button(body, text="Generate a UID", command=lambda: var.set(
                        "0x{:016X}".format(vendor.UidField.generate()))).grid(
                        row=r_, column=1, sticky="w")
            d = Dialog(self.frame, title, fields, extra)
            self.frame.wait_window(d)
            if d.result is None:
                return None
            return {f.name: v for f, v in zip(item.fields, d.result)}

        def add():
            if len(records()) >= item.max_count:
                messagebox.showinfo(TITLE, "{} {}s at most.".format(item.max_count, item.label),
                                    parent=self.frame)
                return
            record = item.default_record()
            for f in item.fields:
                if f.kind == "uid":
                    record[f.name] = vendor.UidField.generate()
            new = dialog("Add a {}".format(item.label.lower()), record)
            if new:
                self.set(lambda m: records().append(new))
                fill()

        def edit():
            selected = tree.selection()
            if not selected:
                return
            n = int(selected[0])
            new = dialog("Edit a {}".format(item.label.lower()), records()[n])
            if new:
                self.set(lambda m: records().__setitem__(n, new))
                fill()

        def remove():
            selected = tree.selection()
            if selected:
                n = int(selected[0])
                self.set(lambda m: records().pop(n))
                fill()
        for label, command in (("Add", add), ("Edit", edit), ("Remove", remove)):
            ttk.Button(buttons, text=label, command=command).pack(fill="x", pady=2)
        tree.bind("<Double-1>", lambda e: edit())
        fill()


class ImageTab(Tab):
    """The image: the checks' findings, the memory map, the bytes."""

    title = "Image"

    def __init__(self, app):
        Tab.__init__(self, app)
        top = ttk.Frame(self.frame)
        top.pack(fill="x")
        ttk.Button(top, text="Check now", command=app.check).pack(side="left")
        ttk.Button(top, text="Describe", command=self.describe).pack(side="left", padx=4)
        ttk.Button(top, text="Export image...", command=app.export_image).pack(side="left")
        self.size = ttk.Label(top, text="")
        self.size.pack(side="left", padx=12)
        panes = ttk.PanedWindow(self.frame, orient="vertical")
        panes.pack(fill="both", expand=True, pady=(8, 0))
        issues = ttk.LabelFrame(panes, text="Checks", padding=4)
        self.issues = ttk.Treeview(issues, columns=("level", "where", "message"),
                                   show="headings", height=8)
        for column, width in (("level", 70), ("where", 220), ("message", 650)):
            self.issues.heading(column, text=column.capitalize())
            self.issues.column(column, width=width, anchor="w")
        for level, colour in LEVEL_COLOURS.items():
            self.issues.tag_configure(level, foreground=colour)
        self.issues.pack(fill="both", expand=True)
        panes.add(issues, weight=2)
        lower = ttk.Frame(panes)
        self.map = ttk.Treeview(lower, columns=("from", "to", "what"), show="headings",
                                height=8)
        for column, width in (("from", 70), ("to", 70), ("what", 260)):
            self.map.heading(column, text=column.capitalize())
            self.map.column(column, width=width, anchor="w")
        self.map.pack(side="left", fill="y")
        self.hex = tk.Text(lower, wrap="none", font=("Courier", 9), height=12)
        bar = ttk.Scrollbar(lower, orient="vertical", command=self.hex.yview)
        self.hex.configure(yscrollcommand=bar.set)
        self.hex.pack(side="left", fill="both", expand=True, padx=(8, 0))
        bar.pack(side="left", fill="y")
        panes.add(lower, weight=3)

    def show(self, issues):
        self.issues.delete(*self.issues.get_children())
        for issue in issues:
            self.issues.insert("", "end", values=(issue.level, issue.where, issue.message),
                               tags=(issue.level,))
        self.map.delete(*self.map.get_children())
        self.hex.configure(state="normal")
        self.hex.delete("1.0", "end")
        try:
            encoded = encode(self.m)
        except EncodeError as error:
            self.size.configure(text="Can't be built: {}".format(error))
            self.hex.configure(state="disabled")
            return
        image = encoded.image
        self.size.configure(text="{} bytes of a {}-byte EEPROM".format(len(image),
                                                                       self.m.eeprom_size))
        for start, end, name in encoded.regions:
            self.map.insert("", "end", values=("0x{:04X}".format(start),
                                               "0x{:04X}".format(end - 1), name))
        lines = []
        for at in range(0, len(image), 16):
            chunk = image[at:at + 16]
            text = "".join(chr(b) if 0x20 <= b < 0x7F else "." for b in chunk)
            lines.append("{:04X}  {:<48} {}".format(at, chunk.hex(" ").upper(), text))
        self.hex.insert("1.0", "\n".join(lines))
        self.hex.configure(state="disabled")

    def describe(self):
        try:
            decoded = decode(encode(self.m).image)
        except EncodeError as error:
            messagebox.showerror(TITLE, str(error), parent=self.frame)
            return
        module = decoded.module
        module.target_chip = self.m.target_chip
        TextWindow(self.frame, "The image in words", report.describe(module, decoded,
                                                                     units_shown=200))


def main(argv):
    root = tk.Tk()
    try:  # Sharp text on high-DPI Windows screens.
        import ctypes
        ctypes.windll.shcore.SetProcessDpiAwareness(1)
    except (ImportError, AttributeError, OSError):
        pass
    App(root, argv[1] if len(argv) > 1 else None)
    root.mainloop()
    return 0
