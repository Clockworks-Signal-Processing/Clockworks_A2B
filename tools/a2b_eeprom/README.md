# A2B EEPROM image tool

Builds and reads the module-info image an A2B sub-node keeps in its EEPROM: ADI's PnP
format ([the specification](../../docs/ADI_PnP/A2B_PnP_EEPROM_ContentSpec.pdf), as ADI's
own images and the stack have it: [provisioning.md](../../a2bcpp/docs/provisioning.md)).
The stack reads it when it finds the node. It gives:
- the node's identity and power data;
- its transceiver's I2S/TDM, PDM, clock-out and GPIO settings;
- its channel groups;
- the programs for its peripherals, a codec's for instance.

Two programs share one core (`a2beeprom/`):

| Program | What it does |
|---|---|
| `a2b_eeprom.py` | Command line: `build` an image from a project file, `check` a project, `decode` an image. |
| `a2b_eeprom_gui.py` | The same in a window: edit every field, import and export images. |

They need Python 3.8 or later and nothing else. The GUI uses Tkinter, part of Python on
Windows; on Debian or Raspberry Pi OS install it with `sudo apt install python3-tk`.

## Files

| File | What it is |
|---|---|
| Project (`.json`) | Every setting of one module, as you edit it. Peripheral programs are named by path, relative to the project file, and read again at each build. |
| Image (`.dat`) | The EEPROM's bytes from address 0, as ADI's tools write them. The bench console writes it to a node (`eeprom write`) and reads one back (`eeprom read`). |
| Program (`.xml`) | A SigmaStudio XML export (a codec's `<ROM>` file), or a sequence written by hand in the same form: `writeXbytes`, `delay`, `read`. |

## The GUI

```
python3 tools/a2b_eeprom/a2b_eeprom_gui.py [PROJECT.json]
```

(`py tools\a2b_eeprom\a2b_eeprom_gui.py` on Windows.)

The tabs follow ADI's specification:

| Tab | What it holds |
|---|---|
| **Board** | The board that holds the EEPROM (ADI's spec calls it the module): the EEPROM's size (the project's, not in the image); the header's IDs, SW version and target chip; the name, device type, and the B port's type and cable length; notes (the project's, not in the image) |
| **Power** | Power configuration (CFG0, CFG4), local power, most and least power, most current |
| **Transceiver I/O** | The register settings, field by field for the target chip, and the port they give ("TDM2 32-bit, 1 TX pin, ..."). Each register's byte is beside its fields; editing either changes the other. Also the slot sizes and other fields the stack doesn't use. For a plug-in module (External), the settings both families share, then the AD243x-only ones (below) |
| **Channel groups** | TX and RX groups: ID, name, channels (`0-1`, `0,2,4-7`) |
| **Peripherals** | Each peripheral block: name, interface, I2C address, groups, A2B pins, widths, and its program. The pins are check boxes named by their stream, "SIO0 (DRX0)", "SIO4 (DTX0)" (below). Pick a SigmaStudio XML file with "...": the dialog starts in the project's folder (the current directory for a new project), and the path is kept relative to the project. Its units are listed; "Read again" picks up a changed file |
| **Vendor data** | The vendor ID, and the vendor's section as a form (Clockworks': an ID and features, with "Generate a UID"), or hex for a vendor the tool doesn't know |
| **Image** | The checks, the memory map and the image's bytes; "Describe" shows it in words |

The status bar counts errors and warnings as you type; click it for the list.

**File menu:**
- New, Open, Save and Save As a project (`.json`). XML paths follow the project when it is
  saved elsewhere.
- **Import image (.dat):** reads an image, from the tool or from an EEPROM (`eeprom read`), into
  an editable project. It shows what was found, block by block, with each block's CRC. The
  target chip is the image's (0x07); an image that doesn't state one (ADI's) shows its
  settings as the product ID's chip family.
- **Export image (.dat):** refused while there are errors.

## Command line

From the repository root:

```
python3 tools/a2b_eeprom/a2b_eeprom.py build tools/a2b_eeprom/examples/AB0020.json
python3 tools/a2b_eeprom/a2b_eeprom.py check MY_MODULE.json
python3 tools/a2b_eeprom/a2b_eeprom.py decode BACKUP_NODE0.dat --chip AD2437
python3 tools/a2b_eeprom/a2b_eeprom.py decode BACKUP_NODE0.dat --project NODE0.json
```

- `build` checks the project, then writes the image: `-o FILE`, or the project's name with
  `.dat`. It prints the memory map, and refuses while there are errors.
- `decode` lists every block with its CRC and every field in words; `--all` lists every
  unit. `--project` saves what it read as a project, with the units in it, to edit.

Exit status: 0, or 1 when there are errors.

## The checks

Each finding has a level:
- **Error:** no image is written. The format can't hold it, or the stack would reject the
  node or fail its set-up. Examples:
  - a reduced I2S rate, TDM12, more than 8 peripherals, a unit too long, groups past 0x2FF,
    an image bigger than the EEPROM;
  - with a plug-in module (External): an I2SCFG that reads differently on the two families
    (more than one data pin a way), or I2SGCFG bit 3 set (below);
  - a peripheral's pins: a pin count that doesn't match its pins; channel groups to or from a
    peripheral on no pins; a pin the transceiver's settings don't turn on.
- **Warning:** it works, but probably not as meant. Examples:
  - a register the target chip hasn't got;
  - CLKOUT2 with SPI left on (AD243x);
  - more than 8 groups in a direction (the stack keeps 8);
  - a write that would take over 0.25 s behind an ADI USBi at the image's I2C rate (measured:
    217 µs per byte at 100 kHz, 162 µs at 400 kHz; about 1150 and 1540 bytes), near the USBi's
    0.3 s status window;
  - an image's power configuration that isn't its named chip's (the stack uses the chip's);
  - I2CCFG bits other than DATARATE (the stack doesn't apply them);
  - a B port type the stack can't run yet (RJ45-CUSTOM, RJ45-CUSTOM-INV, CUSTOM): the chain
    ends at that node;
  - a B-side cable over 15 m with an AD242x or a plug-in module;
  - a group's channel on a pin its peripheral isn't on.
- **Note:** worth knowing. Examples:
  - fields the stack stores but doesn't use (slot sizes);
  - an XLR or RJ45 B port: two-step on an AD2437, not run yet, UTP on other chips;
  - with a plug-in module (External), each AD243x-only setting: the stack leaves it out on an
    AD242x module; and that the stack uses each chip's own power configuration (CFG4 on an
    AD2437, CFG0 on an AD2433 or AD242x), not the image's;
  - a pin the transceiver turns on that no peripheral is on (fine for a header, as the
    AB0312's);
  - a Clockworks product ID that isn't a board number;
  - I2CCFG.DATARATE set: the node's I2C port moves to 400 kHz after the image's start is read,
    so every device on it must take 400 kHz;
  - XML reads dropped.

**The target chip** (header 0x07) says which transceiver the settings are for, and how they
read: the AD242x and AD243x differ in I2SCFG, I2SGCFG bit 3, PDMCTL2, PINCFG, I2CCFG and SPI.
The stack rejects a node whose chip isn't the named one.
- A chip: its part number, 0x2437 for the AD2437. Use it for a board with the chip soldered
  down.
- **External** (0x7FFF): the transceiver is a plug-in module, so the board with the EEPROM
  doesn't decide the chip. The image holds one set of settings, and they must mean the same
  on both families. The Transceiver I/O tab shows two sections:
  - **Both families:** everything the families share: the format, rate, PDM, clock outputs,
    GPIO and pin interrupts, drive strength, and I2SCFG as off or one data pin each way
    (DTX0, DRX0) with the BCLK edges. More pins read differently (pin enables on the AD242x,
    pin counts on the AD243x): an error. I2SGCFG bit 3 is RXONDTX1 on the AD242x and SYNCDIS
    on the AD243x, so it must be 0.
  - **AD243x only:** SPI, PDMCTL2's HPF corner and PINCFG's GPIO mode. On an AD242x module
    the stack leaves SPI out and writes those bits clear (the AD242x TRM marks them unused).
- **Not stated** (0x0000): ADI's images.

**The B port** (0x28) is the module's downstream connector, and the stack sets the link to
the next node from it: 0 XLR, 1 RJ45 (ADI's: two-step on an AD2437, B side inverted), 2 UTP,
3 RJ45-CUSTOM, 4 RJ45-CUSTOM-INV, 5 CUSTOM ([provisioning.md](../../a2bcpp/docs/provisioning.md)).
Its cable length (0x6C) is in metres, for the response cycles; 0 leaves the board's 4 m.

**Clockworks' module IDs:** vendor 0x0C54, the new project's default; product the board's
number without "AB" (AB0020: 0x0020).

**A peripheral's A2B pins** (its RX and TX interface fields) say which of the transceiver's
data pins it is on: bits 7:5 the number of pins, bits 4:0 the pins by the AD243x's SIO
numbers. From the AD2437 TRM (Table 5-5):

| Pin | SIO4 | SIO3 | SIO2 | SIO1 | SIO0 |
|---|---|---|---|---|---|
| TX | DTX0 | DTX1 | DTX2 | DTX3 | |
| RX, no PDM | | DRX3 | DRX2 | DRX1 | DRX0 |

SIO1-3 each carry an RX or a TX stream, as the pin counts decide. With PDM on, the RX
streams move to the pins PDM leaves free (PDM0: SIO1-3; PDM1: SIO0, 2,
3; both: SIO2, 3), and the GUI names them so. An AD242x's pins are named the same way. ADI's
images put their codec on RX 0x21 (one pin, SIO0: DRX0) and TX 0x30 (one pin, SIO4: DTX0),
and so does `AB0020.json`. Channel n of a group goes on pin n / channels-per-pin.

## Peripheral programs

Each peripheral block has a program: an XML file, or units held in the project (an image
read back keeps its units this way).

**Reads and waits.** The format has writes and waits only, so XML `read` actions are dropped
(and counted). A wait for something, such as a PLL lock, becomes a fixed `delay`.

**Which I2C address a write goes to:**
1. the action's own `i2caddr` (7-bit);
2. else the file's `IC_Address`, which SigmaStudio writes in 8-bit form (114 is 7-bit 0x39);
3. else the peripheral's address.

**Splitting long writes.** A long write may time out behind an ADI USBi (the warning above).
A peripheral can have such writes split: give the longest write, sub-address included, and
the data bytes per sub-address step. Splitting is off by default: the XML doesn't reliably say
how the sub-address steps. For example, an ADAU1761's program memory takes 5 bytes per
address and its parameters 4. `AB0020.json` splits at 1022 bytes, 5 per address: its program's
one 1732-byte write, to program memory, becomes 1022 + 712 bytes.

## Vendor sections

The Supplier Information block (0x400) holds a vendor ID ("CLKWORKS"), then 212 bytes that
are the vendor's own. `a2beeprom/vendor/clockworks.py` defines Clockworks' section:
- an ID of up to 15 characters;
- up to six features, each with a 64-bit UID, a name, two signed 32-bit values and a text.

To add your own section:
1. Copy `clockworks.py` and change the vendor ID and fields.
2. List the new layout in `vendor/__init__.py`.

The GUI's form, the checks, the project file and the bytes all follow from the layout.

## Examples

`examples/` has projects for the Clockworks boards as sub-nodes, and the images they build.
The values are the bench console's board profiles (`a2bcpp/examples/clockworks/clockworks_boards.cpp`),
except two things the bench runs settled: the AB0020 and AB0312 set I2CCFG.DATARATE (400 kHz
once the image's start is read), and the AB0020 splits its long program write (above):

| Project | Board | Module | Target chip | B port | Power |
|---|---|---|---|---|---|
| `AB0020.json` | ADAU1761 codec with its SigmaStudio program, on DRX0 and DTX0 | 0C54:0020 | External | UTP | Local (can be set for bus power) |
| `AB0312.json` | I2S on a header | 0C54:0312 | External | RJ45 | Local (can be set for bus power) |
| `AB0204.json` | Four PDM microphones (no EEPROM on the board: for `module N file`) | 0C54:0204 | AD2427 | UTP | Bus, CFG0 (every AD2427 microphone board) |

## Tests

```
python3 tools/a2b_eeprom/tests/test_a2beeprom.py
xvfb-run -a python3 tools/a2b_eeprom/tests/test_gui.py    # with a display: no xvfb-run
```

`test_gui.py` drives the window: the example project, every tab, edits, the checks, import,
export and save; the Board tab; an External board's two sections; the peripheral pin boxes. It is skipped without Tkinter or a display.

`test_a2beeprom.py` covers:
- ADI's two sample images, decoded and built again byte for byte;
- the AB0020 program read as the bench console reads it;
- the Clockworks section's layout and limits;
- the target chip field, the B port and the Clockworks IDs;
- the checks, with an External board's shared settings and the peripheral pins (ADI's
  samples pass them);
- the example images, which must match their projects.

After changing an example project, build it again. A C++ test checks that the stack reads
the example images too (`a2bcpp/tests/unit/test_module_info.cpp`).
