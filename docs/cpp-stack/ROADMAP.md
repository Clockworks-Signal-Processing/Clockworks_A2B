# A2B PnP C++ stack: roadmap

Where the stack is, and what is left to do. Why it is built the way it is: [design
decisions](../../a2bcpp/docs/decisions.md). What the hardware taught us:
[hardware notes](../../a2bcpp/docs/hardware_notes.md).

## Where we are

| Area | State |
|---|---|
| Main-node start-up, SS+ import, host scripts | Done. AD2437 and AD2428 main nodes run on hardware; the AD2433 main node is simulator-only. |
| Discovery, chains, hot-plug, drops, re-plug | Done, single-step links (UTP, Custom, and RJ45 run single-step). Up to 16 sub-nodes; default limit 15. |
| Module info from EEPROM, sub-node peripheral scripts, `NodeFactory`, `SubNode` hooks | Done. |
| Events, node review, power-budget policy, `RejectNode()` | Done. |
| Several independent buses | Done on the simulator; no two-bus hardware yet. |
| Audio routing: AD243x crossbar, AD242x TDM, mixed chains, mute hooks and modes, `MuteNetwork`, bandwidth, sample rates 44.1/48/96/192 kHz | Done. Both first customer systems and a mixed chain have run on the bench. |
| Platforms: Linux (i2c-dev), RP2040, ADI USBi (libusb) | Done. The RP2040 fixed app has run on its board; the RP2040 minimal example never has. |
| Bench console, Clockworks board profiles, SigmaStudio XML reader | Done. |
| Raw register access, peripheral read/write | Done. |
| Simulator and tests | Done. Scenarios 8, 16, 17, 19, 20 and 21 and parts of 10 and 18 wait for the features below ([testing.md](../../a2bcpp/docs/testing.md)). |
| Legacy ADI stack | Removed from this snapshot. |

Not built yet: EEPROM provisioning, two-step links, power diagnostics, the C facade, dual buses,
SPI, spread spectrum, GPIO over distance (below).

## Next order

1. **Sub-node peripherals and EEPROM provisioning** (P7)
2. **Two-step links** (P4)
3. **Power diagnostics** (P6)
4. **The C facade** (P9)
5. Later, when hardware, a need or a code freeze arrives: the differential routing test, dual
   buses (P8), SPI and the rest (P11), GPIO over distance.

## 1. Sub-node peripherals and EEPROM provisioning (P7)

*Why:* a sub-node whose peripherals (for example the AB0020's ADAU1761) have to be programmed
needs the program in its EEPROM, and a blank EEPROM has to be provisioned first.

*Already built, to build on:*
- `ModuleScript` runs peripheral scripts from a module-info image (units up to 4096 bytes).
- The bench console's SigmaStudio XML reader (`examples/bench/sigma_xml.cpp`) and
  `module N ab0020 FILE` build an AB0020 image with the ADAU1761 program as its peripheral script.
- `clockworks::BoardModuleSource` gives chosen positions their board's image and the rest their
  EEPROMs.
- `Network::PeripheralWrite/Read/WriteRead` reach an EEPROM; `EepromModuleInfoSource` reads one.

*Deliverables:*
- **An image builder** (host tool): a module-info image (the `.dat` layout of `adi/module_info/`,
  ADI's PnP EEPROM format) from the module's settings and its peripheral scripts. For the
  AB0020 the ADAU1761 program comes from a SigmaStudio export; the Windows SS+ export already
  holds the AB0020 codec script for the main node, so the same program can make the sub-node
  image. Check the AB0020 EEPROM's size against the image.
- **A file-based `ModuleInfoSource`:** `ReadModuleInfo()` reads the image from a file, so an
  AB0020 sub-node's ADAU1761 can be programmed from a file before any EEPROM is written. Also
  an array compiled into the program, for a system with no EEPROM. Decide how an app source
  chooses an image per node (it is asked by position and chip ID; the module ID is inside the
  image): the examples key by position.
- **An EEPROM write command** in the bench console (Windows, Linux): write and verify an image at
  a node's EEPROM, paged and with the write-cycle wait (ADI's `updateEEPROM <file> <node>
  <addr>`, `updateEEPROMAll` and `a2b_pnp_UpdatePnPModuleInfo(All)`), as application code on
  `Network`'s peripheral calls.
- **Large peripheral programs:** the image gives each unit as one transfer and `Network` holds a
  4096-byte buffer. The builder should split SS+ units longer than a safe size (the main-node
  script splits at 32 bytes, using SS+'s address increment). The USBi platform reads a
  transfer's status for only about 0.3 s, so a 4 KB write behind a 100 kHz sub-node port can read
  as failed ([porting.md](../../a2bcpp/docs/porting.md)).
- Docs: peripherals and provisioning; remove the "not built yet" notes in
  [main_and_sub_node_programming.md](../../a2bcpp/docs/main_and_sub_node_programming.md) and
  bring that tutorial up to date.

*Exit:*
- Simulator scenario 20: provisioning app, blank EEPROMs, write, verify, chunked transfers.
- Bench: an AB0020 sub-node's ADAU1761 programmed from its EEPROM (sound out), and a bench with
  blank EEPROMs provisioned and then discovered normally.

## 2. Two-step links (P4)

*Why:* RJ45 and XLR links are two-step by design. The stack refuses a two-step profile today, so
the bench console's `link N rj45` only sets the RJ45 polarity and discovers single-step. Once the
modules' EEPROMs are set up, two-step becomes the default for RJ45 and XLR, `link N rj45` selects
`Rj45()` as it is, and the console gets `link N xlr`.

*Design* (`LinkProfile` asks for it per link; carried over from ADI's `discovery.c`):
1. Check power-good (the main node through `Platform::PowerGood()`, a sub-node through the GPIO
   its `LinkProfile` names).
2. Set `SWBYP` so the link runs at 5 V, then discover the node.
3. Take the node's power capability from its `ModuleInfoSource`.
4. Clear the upstream node's ENSW and discover again at high voltage.
5. Check 24 V with the voltage monitor (VMTR).

Fixes compared with ADI's stack:
- CFG4 (24 V) is turned on only when the node's module info gives that power configuration.
  Whether to keep the node is the app's decision, in `OnNodeIdentified()`.
- A failed 24 V check stops processing the node.
- The capability test uses the real chip, not a placeholder product ID.
- Custom stays single-step with `SeparateWires` power.

*Deliverables:* two-step `Discovery`; the `SubNode` hooks `SwitchBypass()` and
`HighPowerSwitch()`; the simulator's check that XCVRBINV is written before ENSW and kept on later
CONTROL writes; whether `Utp()` should run the AD2437 24 V check (ADI's stack ran it on every
AD2437 sub-node, two-step or not); a rejected node on its own supply (Custom) stays discovered
today, so its position isn't probed again until `Start()`.

*XLR* is carried along with a `TODO(test on hardware)`: there is no XLR hardware.

*Exit:* simulator scenarios 10, 16-19; bench: an RJ45 two-step chain from the console. Docs:
interconnects.

## 3. Power diagnostics (P6)

- `PowerDiag`: power-fault diagnosis after a fault interrupt.
- **The open-line fault** (INTTYPE 12, or 13 at a low VIN) an AD242x reports from the end of the
  chain on every probe behind it, with nothing plugged in there: an expected condition, to be
  told apart from a real cable fault (see [hardware notes](../../a2bcpp/docs/hardware_notes.md#open-line-inttype-12-and-13)).
  The bench console only hides it today; the stack passes it on as an event.
- Exit: simulator scenario 8 (power faults and `ClearFaults(0/1)`); a short-circuit trace if a
  fault jig exists.

## 4. The C facade (P9)

- A thin C facade that keeps the `a2bpnp.h` names and types, so an application written for ADI's
  stack moves over with few changes, plus the facade-only additions:
  `a2b_pnp_SetLinkProfile(h, node, profile)`, `a2b_pnp_PairBuses(hPrimary, hSecondary)` and
  `a2b_pnp_SetModuleInfoSource(h, fn, ctx)`, where `fn` returns a module-info image length
  (0 = none). `a2b_pnp_UpdatePnPModuleInfo(All)` becomes an application-side helper with the
  same signature.
- The info getters (`GetDiscTimeInfo`, `ReadOEMInfo`, `GetVersion`, `GetDevicePwrInfo`) behind
  the facade.
- A regression and a scripted console session on each platform; RP2040 RAM at or below ADI's
  (see [ram.md](../../a2bcpp/docs/ram.md)); the minimal example's first run on its RP2040 board.
- Docs: C API compatibility notes and a migration map (old function to new class).

## Later

### Differential routing test
A test of the routing planner against ADI's `a2bpnp_calc.c` on 10k topologies, with the
intended-difference rules ([decisions.md](../../a2bcpp/docs/decisions.md#bugs-fixed-not-ported)).
Deferred until the code is frozen, so that a change made after it can't slip past it. Needs ADI's
PnP layer (release 1.3.0), which is not part of this snapshot.

### Dual buses (P8)
`BusPair` couples two `Network`s for the AD2437's dual parallel buses (paired chips on each
module; the host controls both main nodes; bus 2's main node is clocked from bus 1's). Marked
`TODO(test on dual-bus hardware)`; simulator scenario 21.
- **Follower mode.** Only the primary bus runs blind discovery. When the primary accepts node k,
  the secondary runs one directed discovery step for its partner, using that partner's own
  EEPROM.
- **Start and faults.** The secondary starts after the primary main-node PLL locks. A primary
  main-node fault stops the secondary.
- **Drops.** When the primary loses node k, the secondary loses its partner k and everything
  beyond it.
- **Missing partner.** The chain is daisy-linked, so the secondary truncates at k-1 and a
  `PartnerMissing(k)` event is raised. A policy hook decides, defaulting to "keep the primary
  running".
- **Routing:** the first version has one plan per bus; the app decides which connections use
  which bus.
- Still to confirm from the AD2437 documentation: the secondary main node's clock-source
  settings, and whether a partner must be configured first.

### SPI and the rest (P11)
- SPI: GPIO slave-select peripherals, SPI host control, the data tunnel. Peripheral calls with an
  SPI address return `kNotSupported` today.
- SS+ sub-node types matched against the module type in the EEPROM.
- Optional `.dat` import of ADI's configuration files.
- ADI's cable-length algorithm as `RespCycles()`, and automatic polarity retry.
- **Spread spectrum** on every node (ADI's `a2b_ConfigSpreadSpectrum`: the common-settings
  broadcast and each sub-node's PLLCTL; the main node's PLLCTL already comes from its export).
  `ChipCaps::hasSpreadSpectrum` is kept for it. Not in the AD2437 TRM Rev 0.2.

### GPIO over distance
`SetupGPIOOverDistance`, `ClearGPIOOverDistance` and `ReadVirtualIOPort` on `Network`, without
the bugs in ADI's versions, checked against both TRMs, with the simulator's model and console
commands. Not started: there is no bench set-up for it.

### A USBi on a Linux host
Interesting, not needed now. ADI ships no Linux driver, so it may need the USBi's firmware loaded
(fxload) and a udev rule; to be checked on the hardware.

## Hardware tests outstanding

Code that is complete on the simulator but not yet tried on hardware:
- **AD2433 as a main node**, and an AD2433 in a routed chain (an AD2433 sub-node in a mixed chain
  has been found without issues).
- **XLR links.**
- **Several buses** on one host, and dual buses.
- **An AD2437 main node with the AD242x boards** (the mixed chain turned round).
- **The local mute's two silence assumptions:** that TX crossbar location 31 and an empty RX
  frame buffer location play silence. They need a crossbar node that plays more than one pair, or
  an RX group that doesn't start at its first channel: a wider TDM board with a codec, or a scope
  on an AD2437's DTX pins. `muteMode global` (DATCTL) is the documented mute meanwhile.
- **Mute and sample-rate checks not yet recorded as done:** `mute on`/`off` by ear or scope,
  `muteMode global` on an AD242x chain, and a 96 kHz stream between two AB0020 with I2SRATE 5 in
  their images.
- **The bench console's register commands on an AD2428 main node** (`rdm 0x100` must be refused:
  no page 1).
- **The RP2040 minimal example** on its board.
- **GPIO over distance**, when built.

## Documents still to write

Interconnects (with two-step links), peripherals and provisioning (with EEPROM provisioning), C
API compatibility notes and the migration map (with the facade), multiple and dual buses (with
`BusPair`), troubleshooting, and a changelog.
