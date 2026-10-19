# A2B PnP C++ stack: roadmap

What the stack does, what it has been tested on, what it doesn't include, and what still needs a
hardware test. Why it is built the way it is: [design decisions](a2bcpp/docs/decisions.md),
which also holds the designs for the features below that are designed but not built. What the
hardware taught us: [hardware notes](a2bcpp/docs/hardware_notes.md).

## What it does

| Area | What is built |
|---|---|
| Main node | Start-up from a SigmaStudio+ export (compiled in or loaded at run time), host scripts before or after the transceiver, restarts |
| Chains | Blind discovery, hot-plug, drops and re-plug, rejections and the node review, the power budget, up to 16 sub-nodes (15 by default) |
| Sub-nodes | Module info from the EEPROM or from images in memory, peripheral scripts, 400 kHz I2C when the image asks, `NodeFactory` and the `SubNode` hooks |
| Links | Single-step UTP, RJ45 (B side inverted) and Custom (own supply, SWBYP) links, per position; the link from an image's B port; cable lengths per link |
| Audio | Routing on AD243x crossbars and AD242x TDM order, mixed chains, the routing mutes and `MuteNetwork()`, bandwidth, 44.1/48/96/192 kHz |
| Access | Raw register access, peripheral read/write/write-read on I2C |
| Several buses | One `Network` per bus on one host |
| Provisioning | The image tool (`tools/a2b_eeprom`), `ImageModuleInfoSource`, the bench console's `eeprom` commands |
| Platforms | Linux (i2c-dev), RP2040 (Pico SDK), Windows over the ADI USBi (libusb) |
| Examples | The minimal example, the fixed app and the bench console |
| Tests | Unit tests, the simulator's scenarios, the import and example tests, two golden traces ([testing](a2bcpp/docs/testing.md)) |

## Tested on hardware

| Host | Configurations |
|---|---|
| Raspberry Pi (Linux, i2c-dev) | AD2437 main node (`ref`): a chain of four from their EEPROMs; hot-plug at the end; a node's 24 V removed and restored; a chain of six with UTP and RJ45 links and cables pulled at three places. Mixed chain behind the AD2437 (AB0020/AD2428, AB0204/AD2427, two AB0020/AD2437, one on its own supply with `link 2 custom`): EEPROMs read, written, verified and booted from; a write stopped by a pulled cable; codec programs at 100 and 400 kHz; real cable lengths; audio between sub-nodes |
| Windows (ADI USBi) | AB0020 with an AD2437 main node and up to four AD2437 sub-nodes (UTP and RJ45 links); AB0020 with an AD2428 main node and AD2428, AD2427 and AD2437 sub-nodes (an AD2437 on its own supply with `link N custom`); routing on both, a node re-plugged while routed; EEPROMs written, verified and booted from, three ADAU1761 programmed from their EEPROMs |
| RP2040 (AB0310 board) | The fixed app: an AB0312 and four AD2437 modules on RJ45 links, both connections routed with nothing typed. The bench console: four sub-nodes found and audio routed |

## Features not included

Each is one or two lines; where a chip register exists, its call is there and returns
`kNotSupported`.

- **Two-step power-up on an AD2437 link** (ADI's sequence: discover at 5 V, read the EEPROM, close
  the 24 V switch, discover again, check 24 V with VMTR). RJ45 and XLR links into AD242x and
  AD2433 nodes run single-step, as do all the benches' links; `link N rj45` sets the RJ45
  polarity and works. An image whose B port is RJ45 or XLR on an AD2437 node gives a two-step
  link, which isn't probed (logged "two-step (unsupported)"): the chain ends there unless the app
  sets the link. The presets, B port types and power-good and VMTR fields are in the code; the
  sequence isn't ([design](a2bcpp/docs/decisions.md#two-step-links)).
- **Power diagnostics:** telling the faults apart after a power-fault interrupt, the benign open
  line at the end of an AD242x chain from a real cable fault in particular
  ([design](a2bcpp/docs/decisions.md#power-diagnostics)).
- **AD2437 dual parallel buses** (`BusPair`) ([design](a2bcpp/docs/decisions.md#dual-buses-buspair)).
- **The C facade** with ADI's `a2bpnp.h` names ([design](a2bcpp/docs/decisions.md#the-c-facade)).
- **SPI:** GPIO slave-select peripherals, SPI host control, the data tunnel. Peripheral calls with
  an SPI address return `kNotSupported`, and an image's SPI peripheral blocks are skipped.
- **Spread-spectrum clocking** on every node (ADI's `a2b_ConfigSpreadSpectrum`; the main node's
  PLLCTL already comes from its export): `Network::SetSpreadSpectrum()`. Not in the AD2437 TRM
  Rev 0.2.
- **GPIO over distance:** `Network::SetupGpioOverDistance()`, `ClearGpioOverDistance()`,
  `ReadVirtualIoPort()`, without the bugs in ADI's versions; and the simulator's model and console
  commands for them.
- **Custom B port types** (RJ45-CUSTOM, RJ45-CUSTOM-INV, CUSTOM): `CustomBPortLink()` has no
  handler, so the chain ends behind such a node ("Unsupported B port type").
- **SigmaStudio+ sub-node types** matched against the module type in the EEPROM.
- **ADI's `.dat` configuration files** as an import.
- **ADI's cable-length algorithm** as a `RespCycles()` policy, and an automatic retry of the
  other polarity in discovery.
- **A supplier-block reader** for `OnNodeIdentified()`, if apps need the vendor data during the
  review (the bench console reads it with `eeprom info`).
- **A bench console `errors` command** for finding a bad link or cable (clicks on the bench).
  `errors on` turns on bit-error counting on every node (BECCTL) and clears the counts. It also
  stops the stack's once-a-second clear of BECNT (`BoardTimings::becntResetMs` set to 0), which
  would otherwise wipe them. `errors` prints each node's count and its latched errors by name.
  `errors clear` zeroes them, and `errors off` restores the defaults.
- **A USBi on a Linux host:** ADI ships no Linux driver; it may need the USBi's firmware loaded
  (fxload) and a udev rule.
- **A differential routing test** against ADI's `a2bpnp_calc.c` on 10k topologies, with the
  intended-difference rules ([decisions](a2bcpp/docs/decisions.md#bugs-fixed-not-ported)).
  It needs ADI's PnP layer (release 1.3.0), which is not part of this repository, and is best
  run on frozen code.

## Known issues

- **A node dropped after an EEPROM write to the last node:** after an AB0312 image was written
  to the last node with the EEPROMs read, the probe for the next position failed and the chain
  check dropped the node, which then came back from its new image. The cause is not known. Avoid
  it with `ignoreEEPROM 1` while writing.
- **The EEPROM write-cycle wait:** at the default 6 ms, nearly every page write's first try is
  NAKed and retried on the Pi (about one retry a page; harmless, but slower). Raise it with
  `eeprom config cycle MS` if that matters.
- **A rejected node on its own supply** (a link not powered over the A2B pair) stays discovered,
  so its position isn't probed again until `Start()`.
- **No image can say that the next board has its own supply:** the app sets `link N custom` on
  every start ([hardware notes](a2bcpp/docs/hardware_notes.md#open-line-inttype-12-and-13)).
- **`SubNode::TweakRegisters()` is not seen by routing:** routing reads the module info's port
  settings, not the tweaked ones; override `RoutingPolicy::MapGroups()` too when a hook changes
  I2SCFG, I2SGCFG, I2SRATE or PDMCTL.

## What needs testing on hardware

Built and tested on the simulator, not yet on hardware:
- **An AD2433 main node**, and an AD2433 in a routed chain (an AD2433 sub-node in a mixed chain
  has been found without issues).
- **An AD2437 main node with AD242x boards** (the mixed chain turned round).
- **Several buses** on one host.
- **The local mute's two silence assumptions:** that TX crossbar location 31 and an empty RX
  frame buffer location play silence. They need a crossbar node that plays more than one pair, or
  an RX group that doesn't start at its first channel (a wider TDM board with a codec, or a scope
  on an AD2437's DTX pins). `muteMode global` (DATCTL) is the documented mute meanwhile.
- **Mute and sample rates:** `mute on`/`off` by ear or scope, `muteMode global` on an AD242x
  chain, and a 96 kHz stream between two AB0020 with I2SRATE 5 in their images.
- **The bench console's register commands on an AD2428 main node** (`rdm 0x100` must be refused:
  no page 1).
- **The RP2040 minimal example** on its board.
- **EEPROMs:** `eeprom config cycle 0` on the Pi or RP2040 (does a write-cycle NAK raise the main
  node's I2C error interrupt?); `eeprom erase`; a backup written back; the 20 m image on a long
  cable; the boards' power-good GPIO and EEPROM write protect ([provisioning](a2bcpp/docs/provisioning.md)).
- **The SigmaStudio+ import of an AD2428 export** in `test_ss_plus_file`, and an export's host
  operations against a real board ([main- and sub-node programming](a2bcpp/docs/main_and_sub_node_programming.md)).
- **The fixes in this release** that change behaviour on the bench: `eeprom abort` typed after
  the write has cleared the marker (the node left blank, and the console saying so; an abort
  is taken at once on the Pi), as in [testing](a2bcpp/docs/testing.md#bench-checks); a
  cable length over 30 m accepted; the RP2040 console's input with a pasted block of commands.
- **XLR links**, once two-step links are built.

## Documents not written

Interconnects (with two-step links), C API compatibility notes and the migration map (with the
facade), multiple and dual buses (with `BusPair`), troubleshooting, and a changelog.
