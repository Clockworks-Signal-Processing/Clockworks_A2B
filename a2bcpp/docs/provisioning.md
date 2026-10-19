# Peripherals and provisioning

A sub-node describes itself with a **module-info image**, normally kept in an I2C EEPROM on
the module. On the Clockworks boards the EEPROM is on the base board (AB0020, AB0310,
AB0312), and the transceiver is a plug-in A2B module with no EEPROM of its own. The image
holds:
- the module's identity and power data;
- its transceiver's register settings;
- its channel groups;
- the programs for its peripherals (a codec, for instance);
- the vendor's own data.

The stack reads the image when it finds the node ([main and sub-node
programming](main_and_sub_node_programming.md)).

This page covers:
- the format, as the stack and the image tool have it;
- how an image is made;
- how it gets into an EEPROM.

The tools:
- **The image tool**, `tools/a2b_eeprom` ([README](../../tools/a2b_eeprom/README.md)): it
  builds and reads images.
- **The bench console's `eeprom` commands** ([bench console](bench_console.md#eeproms)):
  they write and read EEPROMs.

## 1. The image

The format is ADI's ("Plug and Play Content (Module Information) Specification",
`docs/ADI_PnP/A2B_PnP_EEPROM_ContentSpec.pdf`), together with ADI's sample images
(`adi/module_info/*.dat`). Where the two disagree, the samples win: they are what SigmaStudio+
writes and what EEPROMs in the field hold (section 2).

| Address | Block (type) | What it holds | The stack |
|---|---|---|---|
| 0x000 | Header, 32 bytes | Marker 0xAA; module vendor, product and version IDs; the target chip (0x07, section 2); SW version; primary function at 0x20; CRC-8 at 0x1F | Checks the marker, CRC and 0x20; keeps the IDs; rejects a node whose chip isn't the named one |
| 0x020 | Module information (3) | Power configuration and figures; the B port's type and cable length; name; device type; pointers; peripheral block count (0x7A); minimum slot sizes; I2CCFG (0x7F); register settings (0x80-0x8F); SPI tunnel, branches and local micro (0x90-0x98) | Power, figures, B port, count, I2CCFG.DATARATE and 0x80-0x8F (`src/module_info.cpp`) |
| 0x100 | (in the block above) | Channel groups: TX count and records, RX count and records; the block's CRC right after them | The groups, up to 8 each way |
| 0x300 | Device Function Information (7), 256 bytes | Per-function vendor data | Not read |
| 0x400 | Supplier Information (8), 256 bytes | Vendor ID (8 characters), 32 reserved bytes, 212 bytes of the vendor's own (section 3) | Not read; apps: `ParseSupplierInfo()` |
| 0x800 | Peripheral information (4) | A 32-bit address per peripheral block | Read at 0x800 (`src/module_script.cpp`) |
| next | Peripheral blocks (5), each followed by its units | Interface, name, I2C address, group maps, pins, widths, unit count; units: writes (1) and waits (2) | Runs the I2C blocks' units |

The image ends with the last unit. The AB0020's, with its ADAU1761 program, is 8376 bytes.

**Blocks and units.** Each starts with three bytes:
- byte 0: the type (high nibble) and the top of a 12-bit length;
- byte 1: the length's low byte;
- byte 2: 0, except in a write unit, where it is the 7-bit I2C address.

The length counts the payload with its CRC-8, not the header. A block's CRC covers its
header and its payload up to the CRC.

**CRC-8:** polynomial 0x07, most significant bit first, starting value 0xFF, no final XOR
(`a2b/crc8.h`; its check value for "123456789" is 0xFB).

**Byte order.** Multi-byte fields are low byte first, except the 12-bit lengths and waits, a
peripheral block's unit count, the EEPROM's own addresses and the sub-address inside a
write, which are high byte first.

## 2. Where the specification and ADI's images differ

The image tool and the stack follow the images:

1. **The peripheral information block is at 0x800.** The specification gives 0x500 at 0x6E
   and 0x800 in its section 5.5; the images use 0x800, and the stack reads 0x800 whatever
   0x6E says.
2. **The header is 32 bytes** (its CRC at 0x1F). The specification says 16.
3. **A write unit's byte 2 is the I2C address.** The specification calls it reserved.
   Write units of type 0 (no CRC) are read too, but never written.
4. **The two vendor blocks have their CRC at their last byte** (base + 0xFF, over the 255
   bytes before it), whatever their length says. ADI's images give lengths 0 and 0x29. The
   image tool writes the length 0xFD, so both readings put the CRC at the same byte.
5. **A peripheral block's unit count is 12 bits, high byte first**, unlike its other 16-bit
   fields.
6. **The CRC's parameters** are not in the specification (section 1).
7. **"256KB" in the specification's key notes** is 256 Kbit: 2-byte addresses reach 64 KB,
   and its recommended part (M24512) is 64 KB.
8. **The cable type** is 0 XLR, 1 RJ45, 2 UTP in the specification; ADI's "RJ45" image stores
   2, as SigmaStudio+'s own numbering has it (`adi_a2b_busconfig.h`: 0 UTP, 1 XLR, 2 RJ45).
   The stack uses the specification's numbering (below).
9. **CFG0** is "not supported" in the specification; the stack runs each node in its chip's
   own power configuration, CFG0 on an AD2433 and the AD242x included, whatever the image's
   byte says (section 3; [decisions](decisions.md#policy-decisions)).
10. **Channel groups:** the specification allows 15 each way, but the table must end before
    0x300 (13 groups in all), and the stack keeps the first 8 each way.

**I2CCFG (0x7F): only DATARATE is used.** ADI's stack ignored the whole byte. A sub-node's
I2C port comes out of reset at 100 kHz, so the stack reads the image's start and groups at
100 kHz, whatever EEPROM is fitted. If the image sets DATARATE (bit 0), the stack then writes
I2CCFG.DATARATE with the node's other settings, before the peripheral programs. Those
programs, and any later EEPROM access, run at 400 kHz: remote writes take about 45 µs a byte
instead of 113 µs (TRM, remote I2C latency).
- Set DATARATE only if the module's whole I2C port takes 400 kHz: the EEPROM, every
  peripheral and the pull-ups.
- The other bits are not applied:
  - FRAMERATE: the stack sets it itself on a 44.1 kHz bus;
  - EACK: the main node's;
  - FMPLUS: out of scope;
  - DISI2C: would cut the node off from its own EEPROM.
- The image tool notes DATARATE and warns about the other bits.
- ADI's RJ45 sample sets DATARATE, and so do the Clockworks AB0020 and AB0312 examples:
  every AB0020 has run clean at 400 kHz with AD2428 and AD2437 modules.

**The B port: the link to the next node.** The B-side cable type (0x28) says what the
module's B port is, and the stack sets the link from this node to the next from it
(`LinkProfile::ForBPort()`, `src/link_profile.cpp`):

| Type | Name | The link |
|---|---|---|
| 0 | XLR | ADI's XLR: two-step on an AD2437; UTP on any other chip |
| 1 | RJ45 | ADI's RJ45: two-step, B side inverted (XCVRBINV), on an AD2437; UTP on any other chip |
| 2 | UTP | Single-step, power on the A2B pair, B side straight |
| 3 | RJ45-CUSTOM | An RJ45 that isn't ADI's, B side straight |
| 4 | RJ45-CUSTOM-INV | An RJ45 that isn't ADI's, B side inverted |
| 5 | CUSTOM | Something else |

- **Two-step needs the AD2437's 24 V switch**, so XLR and RJ45 give UTP on every other chip.
  An AB0312's RJ45 port, for instance, is two-step with an AD2437 module and UTP with an
  AD2428 module.
- **Two-step links aren't run** ([roadmap](../../ROADMAP.md#features-not-included)):
  an XLR or RJ45 port on an AD2437 ends the chain at that node, with a log line. The app (the bench's `link N rj45`) can set the
  link instead.
- **Types 3-5 have no handler yet.** `CustomBPortLink()` (`src/link_profile.cpp`) is a
  placeholder that answers "Unsupported B port type", and the chain ends at that node. Types
  6 and up are reserved and get the same answer.
- **Which link wins:** the app's (`Network::SetLinkProfile()`), then the node object's
  `SubNode::DownstreamLink()`, whose default is the image's B port. `BoardConfig::defaultLink`
  is the main node's link, to sub-node 0. An app whose links are all alike can give every
  position the board's link: the minimal and fixed examples do, and the bench's
  `link all board`.

**The B-side cable length** (0x6C, 16 bits) is in metres; ADI's images store 4, the TRMs'
4 m default, and 0 means not given. The stack takes it as the length of the link to the next
node, for the response cycles (`Network::CableLength()`), whatever its length (the image tool
warns past the data sheets' 15 m on an AD242x and 30 m on an AD2437). The app's length
(`Network::SetCableLength()`, a measured one for instance) wins; with neither, the board's
(`BoardRouting::cableDm`, 4 m) applies.

**The target chip** (header 0x07-0x08, which the specification reserves; ADI's images have
0): the transceiver the image's settings are for.
- A chip's part number in hex: 0x2437 for the AD2437. A node with another chip is rejected
  (`RejectReason::kWrongChip`).
- **0x7FFF, External:** the transceiver is a plug-in module, so the board with the EEPROM
  doesn't decide the chip, and any chip takes the image. It has one set of settings, which
  must mean the same on both families (below).
- **0x0000, not stated** (ADI's images), and 0xFFFF (unprogrammed), take any chip.

**One set of settings for both families (External).** In 0x7F-0x8F only these differ
between the AD242x and the AD243x (the two TRMs):

| Register | AD242x | AD243x | In an External image |
|---|---|---|---|
| I2SCFG | Pin enables (TX0EN, TX1EN, RX0EN, RX1EN, two-pin interleave) | Pin counts (TXPINS, RXPINS) | Off, or one data pin each way (DTX0, DRX0), which read the same on both: 0x00, 0x01, 0x10, 0x11, and the BCLK edge bits. More pins: an error in the image tool |
| I2SGCFG bit 3 | RXONDTX1 | SYNCDIS | 0: an error in the image tool |
| PDMCTL2 bits 7:6 | Unused | HPFCORNER | AD243x only: the stack writes them clear on an AD242x |
| PINCFG bits 7:6 | Unused | GPIOSEL | AD243x only: the stack writes them clear on an AD242x |
| SPICFG, SPICKDIV, SPIPINCFG | None | SPI | AD243x only: the stack leaves them out on a chip without SPI |
| I2CCFG bits 7 and 3 | None | DISI2C, FMPLUS | Not applied by the stack |

Everything else is common: the format, rate, PDM, clock outputs, GPIO and pin interrupts,
and drive strength. The stack's part is `NodeSetup::WriteRegisters()` (`src/node_setup.cpp`),
which masks the AD243x-only bits by chip family whatever the image's target chip, so an
image with an HPF corner or a GPIO mode set for an AD243x module is safe on an AD2428 module.
A base board that needs two or more data pins a way, or different settings per family, needs
a second register set, which the format hasn't got (section 8).

**The power configuration** (0x23 bits 7:4) is the chip's, not the image's. Each supported
chip has one: CFG-4 on the AD2437 (its data sheet), CFG-0 on the AD2433 (AD2431W-35W data
sheet, Table 1) and on the AD242x, which have none. Once a node is identified the stack uses
its chip's (`ChipCaps::powerConfig`) for everything after: the app's review, the console,
and the check of the configuration the node detected, which would otherwise override an
AD2433 to CFG-4. An External image can't know the chip, so its value is ignored silently;
a named or unstated image that disagrees is logged. The image tool notes the first and warns
about the second.

**Stored, not used by the stack:** the minimum slot sizes (the bus's slot format is the main
node's), the device type, the SPI tunnel, branch and local-micro fields, and the names. The
image tool marks them so.

**Not in the format:** the AD2437's second I2S/TDM port (the specification notes it at
0x84), the I2S offsets, and reduced rates (the stack refuses SHARE and REDUCE).
`SubNode::TweakRegisters()` covers what a module needs beyond the image.

## 3. Clockworks' images and vendor section

**Module IDs.** A Clockworks image's module vendor ID (0x01) is 0x0C54 ("CSP"), and its
product ID (0x03) is the board's number without "AB": AB0020 is 0x0020, AB0312 0x0312. An
app's `NodeFactory` matches on them (`ByModule(0x0C54, 0x0020, ...)`).

**The example images** (`tools/a2b_eeprom/examples`):

| Board | Module | Target chip | B port | Power |
|---|---|---|---|---|
| AB0020 | 0C54:0020 | External (a plug-in AD2437, AD2433 or AD2428) | UTP | Local power (0x23 bit 0); the board can be set for bus power |
| AB0312 | 0C54:0312 | External | RJ45: two-step with an AD2437 module, UTP with any other | Local power; the board can be set for bus power |
| AB0204 | 0C54:0204 | AD2427 (soldered down) | UTP | Bus powered only, CFG0, as every AD2427 microphone board |

The AB0204 has no EEPROM: its image is for `module N file`, or to compile in. The stack uses
local power only in its power budget: a node with its own supply isn't counted against the
bus's.

**The vendor section.** The Supplier Information block's vendor ID says how its 212 bytes of vendor data read. ADI's
images say "000000AD". Clockworks' section, "CLKWORKS", is an example of a vendor's own data:

| Offset | Bytes | Field |
|---|---|---|
| 0x42B | 16 | ID: up to 15 characters and a NUL (a model or serial number) |
| 0x43B | 1 | Number of features, 0-6 |
| 0x43C | 6 x 32 | Features: UID (8 bytes), name (8), value 0 (4, signed), value 1 (4, signed), text (8) |
| 0x4FC | 3 | Spare (0) |

Numbers are low byte first, text is ASCII padded with NULs, and a feature's name and text may
use all 8 bytes. A feature's UID names the kind of feature, the same on every board that has
it. The section has room for six features: eight would need 273 bytes.

Readers:
- `tools/a2b_eeprom/a2beeprom/vendor/clockworks.py` reads it in Python;
- `clockworks::ParseClockworksSection()` (`examples/clockworks/clockworks_boards.h`) in C++;
- the bench console's `eeprom info` prints it.

A vendor adds its own section by copying the Python layout (the tool's README says how) and
writing a C++ reader like Clockworks'.

## 4. Peripheral programs

A peripheral block's units run in order when the node is set up, after its register
settings: each write as one I2C transfer, each wait as a wait. A failure stops them and
rejects the node.

The image tool reads them from a SigmaStudio XML export (a codec's `<ROM>` file), or from a
sequence written by hand in the same form (`writeXbytes`, `delay`, `read`):
- **`read` actions are dropped.** The format has no read unit, so a wait for a codec's PLL to
  lock becomes a fixed delay.
- **Each write goes to its own `i2caddr` (7-bit),** else to the file's `IC_Address`, which
  SigmaStudio writes in 8-bit form (114 is 0x39), else to the peripheral's address.
- **Limits:** a write may be 4094 bytes, sub-address included, and a wait 4095 ms.

**A peripheral's A2B pins.** A peripheral block's RX and TX interface fields (payload
70-73, 16 bits each) say which of the transceiver's data pins the peripheral is on: bits 7:5
the number of pins, bits 4:0 the pins as the AD243x's SIO numbers. The stack doesn't read
them; the image tool checks them against the register settings and the channel groups. From
the AD2437 TRM's Table 5-5:

| Stream | SIO4 | SIO3 | SIO2 | SIO1 | SIO0 |
|---|---|---|---|---|---|
| TX | DTX0 | DTX1 | DTX2 | DTX3 | |
| RX, no PDM | | DRX3 | DRX2 | DRX1 | DRX0 |
| RX, PDM0 on | | DRX2 | DRX1 | DRX0 | (PDM0) |
| RX, PDM1 on | | DRX2 | DRX1 | (PDM1) | DRX0 |
| RX, both on | | DRX1 | DRX0 | (PDM1) | (PDM0) |

SIO1-3 each carry an RX or a TX stream, as the pin counts in I2SCFG decide. ADI's images
put their ADAU1701 on RX 0x21 (one pin, SIO0: DRX0) and TX 0x30 (one pin, SIO4:
DTX0); the AB0020's ADAU1761 has the same, in the example image and the bench's. An AD242x's
pins are named the same way, by stream.

**Long writes.** The stack reads each unit in one transfer and writes it in one. Behind an
ADI USBi a remote write takes about 217 µs per byte at a sub-node's 100 kHz and 162 µs at
400 kHz (measured; [timing](timing.md)), and the USBi checks a transfer's status for about
0.3 s ([porting](porting.md)). The tool warns about a write that would take over 0.25 s at
the image's rate: about 1150 bytes at 100 kHz, 1540 at 400 kHz. It can split long writes,
given the peripheral's data bytes per sub-address step: SigmaStudio's XML doesn't say
reliably (ADI's own image steps one of its program writes by bytes). The AB0020's program
has one 1732-byte write, to the ADAU1761's program memory (5 bytes per address):
`AB0020.json` splits writes over 1022 bytes, so it becomes 1022 + 712 bytes (0x0800 and
0x08CC, the step SigmaStudio's own 1020-byte writes use), each under 0.25 s at 100 kHz.

## 5. Making an image

The image tool (`tools/a2b_eeprom`) keeps a module in a project file (`.json`):
- every field of the image;
- the transceiver it is for, which sets how the register settings read (AD242x and AD243x
  differ);
- the EEPROM's size;
- each peripheral's XML program, named by a path relative to the project.

**GUI.** It has a tab per part of the image, and it imports and exports `.dat` files.

**Command line.**
- `build` checks a project and writes its image.
- `decode` says what an image holds, block by block with each CRC, and can save it as a
  project to edit.

**Checks.** Every build is checked against:
- the format;
- what the stack does with an image (a reduced I2S rate, TDM12, more than 8 peripheral
  blocks, a unit too long: errors);
- the target chip (a register it hasn't got, CLKOUT2 with SPI on: warnings);
- the EEPROM's size.

An image with errors isn't written.

**Examples.** `tools/a2b_eeprom/examples/` has the Clockworks boards' projects and images:
AB0020 with its codec program, AB0312, and AB0204 (four PDM microphones). Their values are the
bench console's board profiles.

## 6. Writing an EEPROM

**The EEPROM.** The Clockworks boards' EEPROM is a Microchip 24FC256 or equivalent: 32 KB,
64-byte write pages, 5 ms write cycle, 16-bit addresses, at 0x50. The stack looks for an image
at 0x50-0x54 and takes the first whose byte 0 is the marker.

**The commands.** The bench console's `eeprom` commands ([bench console](bench_console.md#eeproms))
run on Windows (USBi) and Linux (not on the RP2040, which has no file system):
- `read`, `write`, `verify`, `info`, `erase` and `abort`;
- `config` for the EEPROM's address, size, page, write cycle and read size.

**Provisioning a chain of blank boards:**
1. `ignoreEEPROM 1`, with each link's profile set (`link N ...`): every node joins on the
   console's module info.
2. `eeprom read N FILE` for each node first, to keep what was there.
3. `eeprom write N IMAGE` for each node.
4. `ignoreEEPROM 0` and `start`: the nodes are found from their EEPROMs.

`module N file IMAGE` tries an image on a node without writing it.

**`eeprom write`, step by step** (`EepromJob`, `examples/bench/eeprom_job.cpp`):
1. The image is checked (`CheckImage()`):
   - every block and unit sound;
   - the CRCs the stack checks right (the header's, the peripheral blocks' and units'). The
     others are only noted, so a module's image read back can always be written back;
   - the start as the stack's own `ParseModuleInfo()` takes it;
   - the size against the EEPROM.
2. The EEPROM is read over the image's length.
3. If it holds a marker and the image differs, 0xFF goes over the marker. From then on a
   stopped write (power, cable, `eeprom abort`, which is taken at once) leaves a node with no
   module info, never a half image, and the console says so.
4. Each page that differs is written, byte 0 aside, with the write-cycle wait (6 ms) after
   it. A transfer that fails (a busy EEPROM NAKs) is tried again every 2 ms, up to 8 times.
   On the Pi that happens at nearly every page: the EEPROM is still busy 11 ms after the
   previous page started and free at about 13 ms ([timing](timing.md)); `eeprom config cycle`
   sets a longer wait.
5. Everything is read back and compared.
6. The marker is written last and read back.

One transfer is made per poll, so the bus is served meanwhile. The AB0020's image takes about
1.5 s to write at 100 kHz, after the 1.3 s first read and before the 1.3 s read-back. A
32 KB file takes 5 s for each read. A write stopped in steps 3 to 5 leaves the node with no
module info until a second write restores it. The node is not blank during step 2, so a
failure there (a cable pulled in the first read) leaves the image as it was.

**Reads.** Reads go in 256-byte transfers. A read "to the image's end" follows the image's
blocks as they come in (`WalkImage()`); an EEPROM with no image is read whole.

**After a write.** The stack reads a node's module info only when it finds the node, so start
the bus again.

## 7. Building blocks for an app

| Part | Where | What it does |
|---|---|---|
| `ImageModuleInfoSource` | the stack (`a2b/image_source.h`) | Module info from images in memory, per position or by default, with a fallback source |
| `EepromModuleInfoSource` | the stack (`a2b/eeprom_source.h`) | Module info from the EEPROMs; `EepromAddr()` tells where an image was found |
| `EepromJob` | `examples/bench/eeprom_job.h` | Reads, writes, verifies or erases an EEPROM, a transfer per poll |
| `CheckImage()`, `WalkImage()`, `ParseSupplierInfo()`, `DescribeImage()` | `examples/bench/eeprom_image.h` | An image checked before it is written, its end found, its vendor block read, described |
| `clockworks::ParseClockworksSection()` | `examples/clockworks/clockworks_boards.h` | Clockworks' vendor section |

Writing EEPROMs stays application code ([decisions](decisions.md)). Copy these into an app
that provisions modules itself.

## 8. Open questions

- **Two register sets.** An External image holds one set of settings, shared by both chip
  families (section 2). A base board that needs more than one data pin a way, or settings
  that differ by family, would need a second set in the format.
- **Power-good and write protect** on the Clockworks boards: which sub-node GPIO carries
  power-good (for two-step links, not built), and whether an EEPROM's WP pin is wired. A
  write-protected EEPROM fails `eeprom write` at the read-back.
- **A remote EEPROM's NAK on hardware**, during its write cycle: whether the main node
  reports it as an I2C error interrupt as well as a failed transfer. On the Pi, at the
  default 6 ms wait, about one NAK a page was retried and the main node reported no interrupt
  (`Interrupts: quiet` after 48 retries). The run with no wait (`eeprom config cycle 0`),
  where each page would be tried about four times, was not made.
