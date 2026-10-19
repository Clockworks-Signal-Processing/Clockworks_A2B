# Testing guide

The C++ stack and its tools are tested in six ways:

| Level | What | Where |
|---|---|---|
| Unit tests | One class at a time, against a fake `Platform` | `a2bcpp/tests/unit/` |
| Simulator scenarios | The stack against simulated main and sub-nodes. The scenario list is below | `a2bcpp/sim/`, `a2bcpp/tests/scenario/` |
| Import tests | `ImportSsPlus()` and the run-time loader (`SsPlusFile`) against the SigmaStudio+ exports in `adi/exports` and `tools/SS+reference` | `a2bcpp/tests/import/` |
| Example apps | `examples/minimal`, the bench console (`examples/bench`) and the fixed app (`examples/fixed`) on the simulator | `a2bcpp/tests/example/` |
| Golden traces | I2C traces captured from ADI's stack on the benches, compared with `trace_diff.py` | `a2bcpp/tests/golden/`, `a2bcpp/tools/` (see `a2bcpp/tools/README.md`) |
| Image tool | The EEPROM image builder and decoder (Python, not under CTest): `python3 tools/a2b_eeprom/tests/test_a2beeprom.py` | `tools/a2b_eeprom/tests/` (see its [README](../../tools/a2b_eeprom/README.md)) |

## Running the tests

The unit and simulator tests build with CMake and run under CTest, on Windows and on
Linux.

| Host | Command (from the repository root) | Build folder |
|---|---|---|
| Windows (MSYS2 UCRT64) | `a2bcpp\tools\run_tests.ps1` (add `-Msys2Root D:\msys64` if MSYS2 is not in `C:\msys64`) | `build\a2bcpp-windows` |
| Linux or WSL | `a2bcpp/tools/run_tests.sh` | `build/a2bcpp-linux` |
| WSL, from Windows | `wsl -d Debian bash a2bcpp/tools/run_tests.sh` | `build/a2bcpp-linux` |

Each script configures the build, builds it and runs `ctest --output-on-failure`. Pass
`clean` to remove the build folder. The Windows script needs `g++`, `cmake`, `ctest` and
`ninja` from MSYS2 UCRT64; with `pkgconf` and `libusb` too it also builds the
[bench console](bench_console.md) and says where. The Linux script needs `g++` and
`cmake`, and uses `ninja` if it is installed; with `libusb-1.0-0-dev` and `pkg-config`
it also builds the USBi platform's libusb link (a compile check). A build folder
configured before libusb was installed doesn't look for it again: `clean` it first.

By hand:

```
cmake -S a2bcpp -B build/a2bcpp-linux -G Ninja
cmake --build build/a2bcpp-linux
ctest --test-dir build/a2bcpp-linux --output-on-failure
```

## Writing a unit test

Each file in `tests/unit/` named `test_<name>.cpp` is its own program, listed in
`tests/CMakeLists.txt` with `a2b_add_unit_test(test_<name>)`. Unit and scenario tests may
read files of the repository through `A2B_REPO_ROOT` (e.g. `adi/module_info/*.dat`). Simulator tests go in
`tests/scenario/` and are listed with `a2b_add_scenario_test(test_<name>)`. A test that
needs a SigmaStudio+ export links one in, as an app does, with
`a2b_add_export_test(test_<name> <folder> <library> <export>)`: the export is compiled as
C without warnings, and the test declares `extern "C" ADI_A2B_BCD sBusDescription;`. Only
one export fits in a program as it is, since each defines `sBusDescription`; a program that
needs several includes each through a small C file that renames it
(`tests/import/export_*.c`, `examples/bench/board_*.c`). The tests use the small harness
in `tests/unit/test_harness.h`, which has no dependencies, so the same tests can later run
on the RP2040:

```cpp
#include "test_harness.h"

A2B_TEST(ElapsedTimeWraps) {
  A2B_CHECK_EQ(a2b::ElapsedMs(5, 0xFFFFFFFBu), 10);
}

A2B_TEST_MAIN()
```

| Macro | Checks |
|---|---|
| `A2B_CHECK(cond)` | `cond` is true |
| `A2B_CHECK_EQ(actual, expected)` | two integer values are equal (printed in hex) |
| `A2B_CHECK_STREQ(actual, expected)` | two C strings are equal (both printed in full) |

A failed check prints its file and line and the test carries on; the program's exit
code tells CTest whether anything failed.

Unit tests may include the internal headers in `a2bcpp/src/`.

### The fake platform

`tests/unit/fake_platform.h` is a `Platform` that records every I2C transfer, answers
reads from a plain register file per I2C address (`SetReg()`), and can make a chosen
transfer fail (`FailTransfer(n)`). It knows nothing about A2B; the simulator does
that. `Dump()` prints the transfers one per line, which makes expected sequences easy
to read:

```
W 68 01 02      write 01 02 to 0x68 (NODEADR = 2)
W 69 40 11      write 11 to register 0x40 of the node NODEADR selects
X 69 03 r1      write 03, then read 1 byte (repeated start)
R 69 r2         read 2 bytes
```

It also counts `Lock()`/`Unlock()` calls and any transfer made without the lock.

## The simulator

`a2bcpp/sim/` (library `a2bcpp_sim`, host only) is a `Platform` whose host I2C bus
reaches simulated A2B buses, so the stack runs against it unchanged. A test builds the
system, then drives time itself:

```cpp
a2b::sim::Simulator sim;                                // a Platform
a2b::sim::Bus& bus = sim.AddBus(0x68, 0x69);            // AD2437 main node by default
a2b::sim::SubNodeSpec spec;                             // AD2437, straight link, power on the pair
spec.link.bSideInverted = true;                         // upstream must set XCVRBINV
a2b::sim::Node& node = bus.Plug(spec);
node.AddEeprom(0x50).Load(a2b::sim::MakeModuleInfoImage(a2b::sim::ModuleInfoHeader()));
// ... run the stack, calling sim.Advance(ms) between polls ...
```

What it models (the details are in `sim/simulator.h`):

| Area | Behaviour |
|---|---|
| Addressing | Main address; bus address steered by NODEADR to a sub-node, broadcast (every node, the main node too; a NAK with no sub-nodes, which the TRMs don't document), or a peripheral at the sub-node's CHIP address; MMR page 1 per node on AD243x (AD242x ignore MMRPAGE) |
| Main-node start-up | The TRMs' reset rules: a soft reset with MSTR = 0 of a running main node unlocks the PLL; an AD243x then spends SUSTAIN (22 ms), an AD242x doesn't. On an AD243x, MSTR or I2SGCFG.INV written in SUSTAIN wedges the node until `Bus::PowerCycle()`; a reset during SUSTAIN restarts it. A soft reset with MSTR = 1 keeps the PLL locked; one in POWER-UP does nothing. A soft reset keeps CONTROL and BMMCFG (and PLLCTL on the AD2433). MSTR gives MSTR_RUNNING after `Timing::pllLockMs` if SYNC is there (`Bus::SetSync()`); once the PLL is locked, MSTR writes have no effect. A SYNC break unlocks the PLL, and MSTR_RUNNING comes again when SYNC is back |
| Registers | Power-on values from the TRM register maps: CHIP 0x50, RESPCYCS 0x40, NODE.LAST, PINCFG 0x01; I2SRRATE 0x01 (both families); on AD243x RXMASKn 0xFF, TXXBARn = n, VMTR_VMAXn 0xFF. A discovered sub-node has NODE.LAST until the next one is found |
| Discovery | DISCVRY discovers the next plugged sub-node after `Timing::discoveryMs`, only if the link is powered (upstream ENSW, or the node's own supply), the upstream B side passes frames (its switch closed, which a node on its own supply with an isolated A side, `LinkSpec::isolated`, never lets happen; or CONTROL.SWBYP), the upstream XCVRBINV matches the link, the value is in the node's response-cycle window and, for sub-node n > 0, NODEADR pointed at sub-node n - 1 when DISCVRY was written (the TRMs' rule); otherwise DSCACT stays set and nothing comes |
| Interrupts | MSTR_RUNNING; DSCDONE when INTMSK2 enables it (INTPND2 either way, write 1 to clear); a sub-node's interrupt of any type (`Bus::RaiseInterrupt(position, type)`: INTSRC.SLVINT and its position); INTSRC shows the oldest and reading INTTYPE clears it; optional IRQ line |
| Drops | From the TRMs, checked against the bench's cable pulls. A sub-node has power while the upstream node's SWCTL.ENSW is set without CONTROL.SWBYP, which keeps the switch open (or its own supply is on). A discovered sub-node that loses power, is unplugged (`Bus::Unplug()`) or resets (`Bus::ResetSub()`, a PLL unlock: it stays plugged in) drops out with every node after it: undiscovered, registers reset, accesses NAK. The new last node sets NODE.LAST, latches INTPND0.SRFERR and raises INTTYPE 5 if its INTMSK0 enables it (the main node too, for sub-node 0). A node whose downstream supply is removed (`Bus::SetDownstreamSupply()`: its VBUS, e.g. 24 V, while it keeps running from the A2B pair) reports a power fault first, INTTYPE 15 with the default masks, as the Pi bench showed, and nothing behind it is powered until the supply is back. A pulled cable (`Bus::Unplug()`) raises no power fault, as on the bench (no cable pull raised an interrupt); a waiting interrupt of a dropped node becomes 0xFD; a discovery under way finds nothing |
| Power | Main-node PG (`Platform::PowerGood()`); GPIO inputs per node (GPIO7 = PG, high by default) through GPIOIEN/GPIOIN; VMTR readings on AD2433/AD2437 (input 1, VBUS, reads 0xB9 by default: 24 V present); the power configuration an AD243x detects (`ChipSpec::powerConfig`, `lowVoltageInput`: SWSTAT2, CFG-0 by default, 0xA0 on the bench) and its override by SWCTL2 and SWCTL.CFG_DET_OV (`Node::PowerConfig()`) |
| Peripherals | `I2cDevice`s behind sub-nodes and on the host bus; `Eeprom` (24xx, 16-bit addresses, page wrap, write-cycle busy, write protect); `MakeModuleInfoImage()` builds valid module-info images: the header with its CRC-8 at 0x1F, the module-info block, the power configuration and figures (maximum and minimum power, maximum current) and the register settings (`ModuleInfoRegs`) |
| Faults | `FailTransfer(n)`, `FailAddress(addr, count)`, `FailRegisterWrite(addr, reg, count)` (a register's writes, e.g. one of a node's set-up), `Bus::Unplug(position)`, `Bus::ResetSub(position)`, PG and VMTR values, the SUSTAIN trap, no SYNC and SYNC breaks (`Bus::SetSync()`), optional reset line (nothing answers for `Timing::accessAfterResetMs`, tACCESS, after it is released) |
| Audio | `Bus::TraceAudio()` follows the audio through the registers each node has applied, from the TRMs' slot model, written apart from the stack's planner: RXMASK0-7 into the RX frame buffer, DNSLOTS/LDNSLOTS (with DNMASKEN)/DNOFFSET down and UPSLOTS/LUPSLOTS/UPOFFSET up, each node passing the first slots on and adding its own, DNMASK0-3 and UPMASK0-3 into the TX frame buffer, TXXBAR0-31 to the TX channels. It gives each node's TX channels as (node, RX channel) and the slots on each link, and names what it can't follow. Slot registers and RESPCYCS take effect at CONTROL.NEWSTRCT (`Node::AppliedReg()`), the crossbars at once. SLOTFMT, DATCTL and I2SRRATE are auto-broadcast: a main-node write is copied to the sub-nodes discovered by then, and only those (a node found later has the power-on value until the main node's register is written again, the bench's silence); the audio model plays and adds nothing in a direction whose DATCTL.DNS/UPS is clear, or for a sub-node whose slot size and format differ from the main node's (what a real node does there isn't documented). `NewStructCount()` and `AccessesInNewStructSettle()` (transfers within 1 ms after a NEWSTRCT) check the settle. Module images can carry channel groups (`AddChannelGroups()`: the table at 0x100, the block size and block CRC). AD242x nodes: no crossbar, so the RX frame buffer is the port's RX channels in order (TDMMODE on the pins I2SCFG turns on, RXONDTX1, the I2SRATE multiple, PDM microphones) and TX channel n plays location n; a crossbar register written on one is an error. On every node a TX channel past the port's plays nothing |
| Trace | `Trace()` gives every transfer as `I2CT` lines, the format `trace_decode.py` and `trace_diff.py` read |

**Several buses.** `AddBus()` adds a bus per call, each with its main node at its own
pair of I2C addresses on the one simulated host I2C bus (e.g. 0x68/0x69 and 0x6A/0x6B,
as two main nodes' ADR pins would set them). Bus n is the n-th added; the `Platform`'s
GPIO calls (`SetMainNodeReset()`, `IrqPending()`, `PowerGood()`) take its index, as the
stack's `BoardConfig::busIndex`. `Decode()` and `Peripherals()` in
`tests/scenario/sim_trace.h` take a bus's two addresses, so a test can split the trace
by bus.

Time moves only when the test calls `Advance()`. Not modelled yet: two-step discovery's
switch bypass and 24 V check, power faults and SPI (see the [roadmap](../../ROADMAP.md)).

`tests/scenario/sim_steps.h` has test scaffolding that brings the main node up and
discovers sub-nodes through `Transport`, for the simulator's own tests. The stack's
sequences are in `Network` (`test_sim_network`). `tests/scenario/sim_bench.h` is the
bench the event and drop scenarios share: a bus of bench-like AD2437 nodes, module info
per position with power figures, a listener that records the events and reviews, and a
node class that counts `OnRemoved()`. Its helpers `SubOf()`, `ChipOf()`,
`MakeImageFor()` and `MakeBoardFor()` give a sub-node, a transceiver, a module image and a
board for any of the four chips, with the chip's power configuration (CFG-4 on the
AD2437, CFG-0 on the others).

## Test programs

| Program | Covers |
|---|---|
| `test_types` | Node addresses; wrap-safe `ElapsedMs()`/`TimeReached()`; every enum value's text (`a2b/strings.h`) |
| `test_chip_caps` | The four supported chips (AD2427, AD2428, AD2433, AD2437), their families and features, the power-configuration registers and switch mode, register page 1 (AD243x only), main-node capability (not the AD2427); the I2S/TDM port (not the AD2427); unsupported chips |
| `test_transport` | Main-node, sub-node, broadcast and peripheral targets; NODEADR, CHIP (per node) and MMRPAGE (per node) caching; failed transfers; writes to the cached registers; `ForgetNode()`/`ForgetAll()`/`SetAddresses()`; long writes; invalid arguments; locking; two transports on one I2C bus; `SelectNode()` |
| `test_image_source` | `ImageModuleInfoSource`: a position's image, the default, none; reads of the image a node was identified with (bounds), a short image, the fallback source for the nodes without one |
| `test_crc8` | ADI's CRC-8 against the headers of the ADI module-info images in `adi/module_info/`; continuing a CRC |
| `test_sim_main_node` | Reset, wait and MSTR giving MSTR_RUNNING; the SUSTAIN trap (MSTR or INV too early, two resets, recovery only by power cycle or RESETn); no SUSTAIN after power-on or on an AD242x; the TRMs' soft-reset rules (MSTR = 1 keeps the PLL, what the reset keeps, nothing in POWER-UP, MSTR ignored once locked); power-on register values; no SYNC and SYNC breaks; reset and IRQ lines (tACCESS after RESETn); main-node PG |
| `test_sim_discovery` | Single-node DSCDONE and what needs to be right for it (ENSW or own supply, the B side on: switch closed or SWBYP, which an isolated node needs and which keeps the switch open; polarity, response-cycle window, NODEADR on the upstream node); every chip inverting its B side, as main node and as sub-node; masked DSCDONE; ENDDSC; a chain of mixed chips; unsupported chip; soft reset and unplug losing nodes; SWSTAT2 and the power-configuration override |
| `test_register_image` | `RegisterImage` order, values and capacity; the `Op` factories; `OpList` and `FixedOpList` |
| `test_script_runner` | `ScriptRunner` with the fake platform: operation order, waits across polls and the clock wrap, long peripheral writes split by sub-address step, sub-node peripherals, the first failure stopping the script, bad operations, `Stop()` |
| `test_ss_plus_import` | `ImportSsPlus()` on the Pi's export (`adi/exports/rp2040/12_a2b_busconfig.c`): the main-node image matches the G13 golden in values and order; addresses, chip, power, timings and the default link (UTP, B side inverted); the same settings on an AD2428 main node; AD243x pin settings (GPIO0, SYNC disable, interleave); unsupported chips and SPI host control |
| `test_sim_host_script` | Scenario 13, host script with delays: the Windows export's AB0020 codec script imported and run on the simulator (the 100 ms wait, every byte at the right sub-address, a NAK stopping it); a script with waits writing an EEPROM behind a sub-node; the export's codec configured before the transceiver (`hostOpsBeforeMainNode`), and after it when the export marks it so; on `Network`, the codec programmed before the first main-node access, with SYNC only once it is (the AB0020), early and late operations, a failing early one stopping the start before the main node, no rerun after a PLL relock, again at `Start()` |
| `test_module_info` | `ParseModuleInfo()` on ADI's RJ45 and XLR images in `adi/module_info/` (IDs, power configuration and figures, peripheral block count, the register settings in ADI's write order, the group table's length from the block size, and I2CCFG.DATARATE, set in the RJ45 image only); of I2CCFG only DATARATE taken; any module ID and power configuration pass; PINCFG 0 is a setting; refusals (short, no marker, bad CRC, primary function or block type); the image tool's three example images (`tools/a2b_eeprom/examples/*.dat`: settings, groups, peripheral blocks and their CRCs, the module-info block's included). `ParseChannelGroups()`: a table built by the simulator's helper, an empty one, counts over 15 or past the end refused, more than 8 groups each way. The target chip (0x07: a named chip, External, not stated, unprogrammed) and `ImageFitsChip()`; the B port type and cable length (ADI's samples: UTP and XLR, 4 m; the Clockworks examples: 0C54 IDs, External or AD2427, UTP or RJ45, local power on the AB0020 and AB0312 and not on the AB0204) |
| `test_response_cycles` | `ComputeResponseCycles()` (the TRMs' Formula B) against the AD2431-35 TRM's worked example (DN 104 and 121, UP 175.53 and 184.03, main node 148, sub-node 1 143) and the bench's SigmaStudio+ project (0x78; 0x7D with its offset of 254); each position's DISCVRY from a given main-node value (worked out in floating point); the frame rate's bit time; no window; slot widths from SLOTFMT (parity, ECC, floating point) and RESPOFFS from I2SGCFG; the default policy |
| `test_connection_table` | `ConnectionTable`: adding and finding each end's node by chip ID (following a module that moved, waiting for one gone), the same connection twice, a sink taking one source, removing and clearing, the full table, shared and zero chip IDs going by the last position, `SameEnd()` |
| `test_routing_planner` | `RoutingPlanner` on hand-worked chains: the bench's SigmaStudio+ project (its slot registers, masks and crossbars), the G13 golden's routing (ADI's Pi app), the TRMs' three-node example; a slot no node further on needs dropped only when it is last, runs with unused channels in them, groups paired in order with the shorter winning, one source to several sinks taking one slot, no connections; refusals (missing groups), the 32-slot and 32-played-channel limits; AD242x nodes: the second customer system (an AD2428 main node, an AD2428 codec, an AD2427 microphone module), channels out of bus order or with a gap refused, runs carrying every channel and the main node sending from channel 0; channels on the port; `ComputeChannelMap()` on both families' I2SCFG, RXONDTX1, rates, PDM and the 32 cap |
| `test_interrupt_poller` | `InterruptPoller` with the fake platform: the interval, an immediate recheck after an interrupt, `CheckNow()`, sub-node interrupts, the IRQ line, failed reads |
| `test_sim_network` | `Network` on the simulator. Scenario 1 (main node idle: the start-up sequence and its timing, the SUSTAIN wait on a main node left running, the image written in bursts without the stack's registers, the hook, the host script before discovery, probes every 570 ms, the interrupt interval and the IRQ line); scenario 2 (single hot-plug: identity, module info, settings, SPI only on AD243x, the switch modes, then probes behind it; every main-node capable chip as main node; inverted and Custom links; rejections); scenario 9 (no MSTR_RUNNING, the last-chance check, `Start()` again); a PLL relock restarting the bus; I2C failures (a main node that stops answering stops the bus once the restart fails too); `Start()`'s checks (an AD2427 main node refused); the `LinkProfile` presets. `LinkProfile::ForBPort()` for each B port type on each chip (XLR and RJ45 two-step on the AD2437 only, UTP elsewhere; 3-5 and reserved unsupported), the `CustomBPortLink()` placeholder, `SetLinkProfile()` refusing an unsupported link; the board's link into sub-node 0 only, the app's winning behind it |
| `test_sim_g13_image` | The G13 register image: the Pi's export and one sub-node on the simulator, the routing of ADI's Pi app (12 channels down to sub-node 0) as a connection, up to the end of the first probe behind it; every main-node and sub-node register matches the golden's last write, or its power-on value, apart from eight listed intended differences (the response cycles and DISCVRY by the TRMs' formula, the crossbars set to what is routed, the 24 V check, which the stack doesn't make), each checked to be a real difference; the sub-node's SLOTFMT, DATCTL and I2SRRATE are the main node's last writes (auto-broadcast, which the golden's trace of host writes doesn't show); the sub-node's port is declared through a `MapGroups()` override, as the export leaves it off and ADI's Pi app routed to it anyway |
| `test_sim_chain` | Chains. Scenario 3 (chain of 3: identities, settings, masks, switches, DISCVRY per position with NODEADR on the upstream node); a mixed AD243x/AD242x chain (each chip's switch mode, SWSTAT2 on AD243x only); the upstream node's set-up in the TRMs' order, once; the switch modes around a probe and around an identification; the power-configuration override and its 100 ms settle, on a sub-node and on the main node; scenario 7 (max sub-nodes, 16 sub-nodes, the app pausing and resuming, paused before `Start()`); a rejection ending the chain, its position probed again and the same node switched off again at once; hot-plug at the end; the RESETn pulse at `Start()` (tACCESS, not on a PLL relock, a failing line, the board's pulse length) |
| `test_example_minimal` | The minimal example app (`examples/minimal`) on the simulator with the Pi's export: a chain of two bench nodes found and configured from their EEPROMs, and with the EEPROMs ignored, with no power-configuration override; a module without EEPROM plugged in at the end, rejected and switched off; sub-nodes 2 and 3 of four unplugged and plugged back in; the bench's six-node chain for the cable-pull runs (EEPROMs ignored, `SetUtpLink(5)`: sub-node 4's B side straight, the others inverted; the cable pulled and put back after node 4, after node 3 and before node 0; without the UTP link, node 5 never found); a missing SYNC reported; every link the export's, as the app sets them |
| `test_example_fixed` | The fixed app (`examples/fixed`) on the simulator, set up as its RP2040 program is: the AB0310 and AB0312 project's export, an AB0312 at sub-node 0 without an EEPROM and two unknown AD2437 nodes with the generic image, RJ45 links; the AB0312's registers, the connections added and routed with nothing typed (both stereo pairs traced), the heap hook called; sub-node 0 unplugged and plugged back in: found, imaged and routed again; every link the export's, as the app sets them |
| `test_module_script` | `ModuleScript` with the fake platform (images built with the simulator's helpers): writes and waits in order across polls, CRCs left off the writes, CHIP set first; a source with only the start of the image; a unit longer than the buffer; bad units (type, address, CRC, empty) and blocks (count, type, CRC); a failed write; `Stop()` |
| `test_sim_module_info` | Scenario 6 and the EEPROM source: the EEPROM as the default source (the marker, then the start of the image), found anywhere in 0x50-0x54, a blank one skipped; no EEPROM, a blank one, a bad header CRC or block (each rejected and switched off, the chain ending there); an app source ignoring the EEPROM (no peripheral transfers), with nothing to give, and back to the EEPROM. Peripheral scripts: writes and waits from the EEPROM, SPI blocks skipped, the node counted only after them; a bad unit or block CRC and a peripheral that doesn't answer (each `kPeripheralFailed`); ADI's RJ45 image programming its ADAU1701 (a 16 ms wait, seven writes of 4 to 4090 bytes, each in one transfer), from the EEPROM and from an app source; an app source with only the start of the image. The I2C rate: the image read at 100 kHz, then the scripts at 400 kHz when its I2CCFG sets DATARATE (100 kHz throughout when not); of the image's I2CCFG only DATARATE applied, with FRAMERATE added on a 44.1 kHz bus. The boot4 golden: the bench modules' settings written in ADI's values and order, and the same EEPROM areas read. The B port: a UTP image probes on; RJ45 on an AD2437 ends the chain (two-step, logged) unless the app sets the link; RJ45 and XLR on an AD2428 are UTP; RJ45-CUSTOM, RJ45-CUSTOM-INV, CUSTOM and a reserved type end it ("Unsupported B port type"). An image for an AD2427 on an AD2437 rejected (`kWrongChip`), External and not stated taken. An External image's AD243x-only bits (PDMCTL2 0x40, PINCFG 0x81) written clear on an AD2428 (0x00, 0x01) and as they are on an AD2437. An External CFG-4 image gives each chip its own power configuration: CFG-0 on an AD2428 and an AD2433 (never overridden to CFG-4), CFG-4 on an AD2437; an unstated image that disagrees is logged. The image's cable length in `CableLength()` and the DISCVRY, the app's winning, one past the data sheets' 30 m taken as it is |
| `test_sim_node_hooks` | `NodeFactory` (by module IDs, the app's chooser first, the table's limits, no factory) and the `SubNode` hooks: `TweakRegisters()` (changed, added, the stack's registers skipped), `OnConfigured()` (operations and waits before the node counts; a failure rejects, destroying the object without `OnRemoved()`), `InterruptMask()`, `OnRemoved()` at `Start()` and `Stop()`, `NodeObject()` and `TypeTag()`. Link profiles: `DownstreamLink()` for the next link, `SetLinkProfile()` winning over it, the main node's link (XCVRBINV kept on every later CONTROL write), a Custom link in the middle, `ClearLinkProfile()`, the argument checks, a two-step `DownstreamLink()` not probed until the app sets a single-step link, a changed link at the end of the chain applying at the next probe |
| `test_sim_drops` | The running chain. The bench's drop replayed (the 24 V removed at sub-node 2: its power fault, three NAKs from sub-node 3, sub-node 2's switch opened, nothing found while the 24 V is off, found again once it is back); a pulled cable raising no power fault, found by the chain check. Scenario 4 (unplug the last node, a middle node, the first: the nodes taken out in order with `kNodeRemoved` events and `OnRemoved()`, the switch before them opened, the position probed again no sooner than 250 ms later); the chain check's reads and order, also while discovery is paused; re-plug found again; a node that resets (PLL unlock) found again; the SRF-missed interrupt checking at once; a dropped node's waiting interrupt as 0xFD; other sub-node interrupts passed on; a main node that was reset starting the bus again; scenario 12 (a failed interrupt read, check reads retried up to three times, a failed probe and a failed set-up abandoned and tried again, never failing the bus); BECNT cleared every second by broadcast, on the main node alone without sub-nodes, or not at all; `ResumeDiscovery()` during start-up keeping the SUSTAIN wait, and after a drop keeping the 250 ms; `RejectNode()` (the node and those after it taken out, its position probed again silently, a different module taken in, during a probe, on its own supply, forgotten by `Start()`); a rejected node on its own supply not probed again |
| `test_sim_events` | Events and the node review: the start-up events in order, with chip IDs; events at the end of `Poll()` with the node reachable; `Poll()` from a handler returning at once; no listener; the events of `Stop()` and `Start()` and of a PLL relock; the rejection event. Scenario 14 (API calls from events: `RejectNode()` from `kNodeAdded`, `Stop()` and `Start()` from state changes, `Start()` from `kNodeRemoved`, link profiles, pausing and peripheral calls from `kNodeRejected` and `kInterrupt`, `SetListener(nullptr)`, a handler that queues an event for every event it gets). Scenario 7 (the power budget and the current budget, locally powered modules not counted, the budget freed by a drop, the listener overruling the policy, rejecting by chip ID, the default policy without a listener) |
| `test_sim_two_buses` | Scenario 15, two `Network`s on one `Platform`, their main nodes on one host I2C bus (0x68/0x69, 0x6A/0x6B): each finds its own chain, with its bus index in each node's identity; each bus's chain checks and BECNT broadcasts on its own addresses only; a bus not started gets no transfer; each bus's RESETn pulse and IRQ line by its bus index; a drop, a main node reset, a main node that stops answering and `RejectNode()` on one bus leave the other untouched (no transfer to its switches, no event); a listener, a module-info source and a factory shared by both buses telling them apart; a handler on one bus calling the other's peripheral I/O, `PauseDiscovery()` and `RejectNode()`; an AD2437 bus and an AD2428 bus, each with a mixed chain; every transfer under `Lock()` |
| `test_sim_mixed_chips` | Scenario 11: AD2428, AD2433 and AD2437 main nodes, each with a chain of all four chips: every node found, each switch in its chip's mode, SWSTAT2 read and SPI settings written on the AD243x only, no power-configuration override when each module's image matches its chip; a drop in the middle and the re-plug under each main node; inverted links from an AD2428 main node and from an AD2427 sub-node; the bench's isolated AD2437 behind an AD2428 sub-node, found only over a Custom link (SWBYP on the AD2428); the board's sub-node limit under each main node (the default 15, 16 and 5: that many found, then no more probes, only chain checks; below it after a drop, probing again); an AD2427 main node refused; SPI peripheral calls refused on every main node and sub-node (SPI is not supported yet) |
| `test_sim_mixed_routing` | Scenario 11 with routing: under each main node (AD2428, AD2433, AD2437) a chain of an AD2437, an AD2428, an AD2433 and an AD2427 with microphones, routed and traced: the main node's pair down to three nodes, crossbar to TDM and TDM to crossbar node to node, an AD2433's pair up across the AD2428 to the AD2437, the microphones up to the AD2428 after what it plays downstream, every node up to the main node (last sub-node first), no crossbar register written on an AD242x; the TDM order with an AD2437 between two AD2428s under an AD2428 main node (down in bus order whatever lies between, the crossbar node taking any order, up the last sub-node first; refusals that leave the last plan playing; a slot no one further up wants ahead of a wanted one, played by the AD242x main node); the local mute in a mixed chain (an AD2428 playing a slot an AD2437's new RXMASK changes: the whole bus muted, two NEWSTRCT; a change only a crossbar sink sees: that node muted, the AD2428 playing on) and `kGlobal`; the chain changing while routed (a drop and the re-plug under an AD2433 main node, no wrong channel at any new structure, no whole-bus mute; a node whose connection breaks the TDM order refused on its arrival, with the node at fault, the last plan playing on) |
| `test_usbi_platform` | The USBi platform's USB framing against a fake link, byte for byte: write (0xB2), read (0xB4) and write-read (0xB3) with their requests, wValue, wIndex, data and timeouts (four times longer above 16 bytes, not for a write-read's write); the 2 ms, 10 ms and 1 ms waits; status polled while NONE or WRITING (write) or NONE (read), up to 30 more times; FAILED, ERROR, EXCEPTION, an unknown status, a failed request and a short USB transfer as I2C errors; bad lengths; the hint logged once until a transfer works |
| `test_ss_plus_file` | `SsPlusFile`: the Pi's, the Windows, the AB0020 bench project's and the AB0310 and AB0312 project's (SigmaStudio+ 2.3.0, `tools/SS+reference`) exports read at run time give the structures their compiled forms have (byte for byte, pointers followed; the reference exports' sub-nodes too, with no warnings) and the same `ImportSsPlus()` result; the Windows export with its 12 sub-node descriptions enabled (no warnings: the reference header has their SPI fields); unknown fields skipped with a warning and their line; chained and array designators, positional units, data arrays, casts, `NULL_PTR`; `#if`/`#ifdef`/`#ifndef`/`#elif`/`#else`; things skipped (typedefs, functions, other types, `#define`); errors with their lines (unknown constant or variable, pointer type, too many initializers, range, syntax, an unclosed comment, no `sBusDescription`, a missing file); CRLF line ends (a Windows checkout); an unknown constant taken as 0 with a warning where the import doesn't read (a sub-node description, also through `apSlaveConfig`) and refused in the main node |
| `test_example_bench` | The bench console on the simulator: `help`, `board`, refusals (no board, unknown command, bad arguments, a missing file) with nothing sent; the `ref` board bringing up a chain from its EEPROMs, `status`, `stop`; the `AB0020` board's 40-operation host script; the wrong main node; an export loaded from a file and changed to an AD2428 main node, with AD2428 and AD2427 sub-nodes and then an AD2437, EEPROMs ignored (CFG-4 only on the AD2437); `link` per position (a straight link found only once set; an isolated AD2437 behind an AD2428 found only with `link 1 custom`), its forms and `start` again; a new board stopping the bus; the I2C trace file; the `AB0020` board: its codec's operations before the main node, every link UTP though its export sets XCVRBINV; link names `utp`, `rj45`, `custom [inverted]` (no "utp inverted"); the last sub-node's open-line interrupts (types 12, 13) hidden and counted, with a hint the first time (`link N custom` for a node on its own supply), other nodes' and other types shown, `interrupts all`; `rdm`, `rds`, `wrm`, `wrs` (16 values to a line, page 1, a node not in the bus, MMRPAGE refused, bad arguments); `periRead` (with and without a sub-address, sub-node EEPROM and host bus, no answer) and `periWrite`; `pause` and `resume` (a node plugged in while paused found on resume), `reject` (removed, switched off again when found there); `addCon`, `removeCon`, `clearAllCon`, `listCon`, `applyRouting`, `routing`, `groups` and `group` on two AB0020-like sub-nodes, the audio checked; `cable` (set, listed, in `status`, refused out of range, back to the board's, a routed bus planned again); `module all AB0020` and `module 0 AB0020 FILE` (an AB0020 at sub-node 0 without an EEPROM gets I2SGCFG 0xE0, I2SCFG 0x11, GPIO7 high, CLK2CFG 0x81, SPICFG 0x02, groups RX 0 and TX 0, and its built-in ADAU1761 program write for write with the 100 ms wait, again after a re-plug, then routed; an AD2427 at sub-node 1 reads its EEPROM and refuses routing unless its port carries the channels; bad files, boards and arguments); the `AB0310` board with an `AB0312` at sub-node 0 and two unknown AD2437 nodes (no host operations, every link RJ45, the AB0312's registers, the stereo pairs routed both ways); an AD2428 main node with an AB0020 (`module 0 AB0020`) and AD2427 nodes: one with no EEPROM and one with a blank EEPROM taken as the AB0204 (PDMCTL 0x1F written, I2S registers not), one set up from its EEPROM image, an AD2428 with no EEPROM still rejected; the AB0204's DRX1 pair and then its four microphones routed to the main node, traced; the local mute's whole-bus line for an AD2428 playing an AD2437's single-channel group; `board AB0020` taking the AD2437 or AD2428 export for the main node's chip at `start` (both brought up), refusing an AD2433, and the default export when the main node doesn't answer; the chip found named when a board's export is for another; every built-in export declared with the chip its import gives. Links: `link all board` (every link the board's), the AB0312's RJ45 port on an AD2437 ending the chain until it, a link from the node before's image (`link N default`), the image lines (target chip, B port, cable). Board names typed in lower case (`board ab0020`, `module 0 ab0312`) shown in upper case |
| `test_eeprom_provisioning` | Scenario 20, the bench console's `eeprom` commands with the image tool's images (`tools/a2b_eeprom/examples`) on simulated 24FC256 EEPROMs: two blank EEPROMs (no image: `eeprom info` reads them whole) written (131 pages, then the marker), verified, read back to the image's end in 256-byte chunks and described (the Clockworks section included); written again, nothing written; then the nodes found from their EEPROMs, the AB0020's ADAU1761 program write for write and its settings from the image. A write stopped half way (`eeprom abort`): the marker cleared, the node rejected as blank, never a half image; one stopped before its first page says so too; `start` and a second job refused while one runs, `eeprom abort` and `quit` taken at once (`RunsWhileBusy()`). A write-protected EEPROM (the read-back fails), a busy one (no write-cycle wait: its NAKs tried again), `eeprom erase`, an address with no EEPROM (8 tries). Reads of the whole EEPROM, of a length, of a blank one; a whole EEPROM read written back. `module 0 file` (an image without an EEPROM: the codec programmed, the next node's blank EEPROM rejected); the AB0020's External image on an AD2428 module shown as CFG-0, its chip's. The AB0020's image sets I2CCFG.DATARATE: described as 400 kHz and the node's I2C port moved to 400 kHz, and a copy with it clear kept at 100 kHz; the codec programmed write for write either way (the XML's writes, the 1732-byte one split as `AB0020.json` splits it). Refusals (a node not in the bus, bad arguments and settings, an image bigger than the EEPROM, a missing or foreign file, the AB0204's image for an AD2427 on an AD2437 node) with nothing sent; `CheckImage()`, `WalkImage()`, `ParseSupplierInfo()` and `ParseClockworksSection()` on the images and broken copies |
| `test_sigma_xml` | The bench console's SigmaStudio XML reader: the AB0020's sub-node ADAU1761 program (no `i2caddr` or `addr_width`: refused without a default address; with 0x39, 38 writes, 6076 bytes, the largest 1732, the 100 ms wait fifth, sub-addresses high byte first, its `read` skipped), the main node's (another program: its serial port the master); the layout with `i2caddr` and `addr_width`; refusals with their lines (len, address, hex, sizes, an unknown action, an unclosed element, a missing file) |
| `test_clockworks_boards` | The bench console's Clockworks board profiles: the AB0020's settings against the bench SigmaStudio+ export's sub-node (I2SGCFG, I2SCFG with its pins, CLK2CFG with its pin, SPI mode, PINCFG, GPIO7), its built-in codec program against the export's sub-node peripheral, write for write; the image built from it read back by `ParseModuleInfo()` and `ParseChannelGroups()` (settings, power configuration per chip, local power, groups, one peripheral block with the ADAU1761's address, groups and pins, RX 0x21 and TX 0x30); the chips it is made with; the AB0312's settings against the AB0310 and AB0312 project's sub-node, no codec (the export's sub-node peripherals have nothing to program), its image with local power and without a peripheral block; the generic image (GPIO0-6 inputs, PINCFG 0, I2S on DRX0 and DTX0, RX 0 and TX 0 on channels 0-1, CFG-4 for an AD2437; any chip takes it) read back; the AB0204 (PDMCTL 0x1F alone, RX 0 on channels 0-3 and the two pairs, no TX group, CFG-0 without local power, AD2427 only) read back, and the AD2427's no-EEPROM default |
| `test_sim_app_access` | The app's raw register access (`Network::ReadRegisters()`/`WriteRegisters()`): the main node and each sub-node, a 20-register write, page 1 on the AD243x only (refused on AD242x with nothing sent), MMRPAGE refused alone and in a run, NODEADR and CHIP written by the app and rewritten by the stack, the nodes reachable before `Start()`, running and stopped, bad arguments, the joining node from a module-info source; the documented risks: a switch opened (nodes dropped, found again), MSTR cleared (the bus starts again), a soft reset with MSTR kept (chain lost until `Start()`), an INTTYPE read (the interrupt lost to the stack). The calls for features not supported yet (spread spectrum, GPIO over distance) returning `kNotSupported` with nothing sent |
| `test_sim_routing` | Routing on `Network`, each plan checked by following the audio with `TraceAudio()`: two nodes both ways and node to node; a plan asked for before `Start()` written once the chain is up; scenario 5 (a node unplugged: the plan for the shorter chain, its connection waiting; plugged back in: routed again); connections following modules plugged back in the other order; scenario 7 (more than 32 slots refused, a node whose arrival breaks the budget leaving the last plan, no response-cycle window); later probes' DISCVRY from the plan's RESPCYCS; an app `RoutingPolicy`; each link's cable length (the plan's response cycles, a later probe's DISCVRY, a routed bus planned again only when the length in use changes, a length whose response cycles can't be written failing the plan); an AD2428 in the chain refused, then routed without it; a failed write tried again; a SYNC break routed again; SLOTFMT and DATCTL on every node with each plan, including a node found after the first plan (the bench's silence), and a DATCTL from the image that leaves a direction off; the connection calls' checks; two modules with the same chip ID; no transfer in any NEWSTRCT settle; the second customer system (an AD2428 main node, an AD2428 codec and two AD2427 microphone modules, no crossbar written), channels out of bus order refused at once and on a chain change, a port left off refused unless the policy's `MapGroups()` says otherwise, an AD2428 in no connection |
| `test_sim_mute` | Muting, the audio read at each NEWSTRCT instant with `Bus::SetNewStructObserver()`: `MuteNetwork()` and its hold through plans and restarts; the hooks around a plan, an app's codec write before the NEWSTRCT; the local mute (the nodes whose crossbar changes; a module plugged in with its power-on crossbar; a node whose crossbar stays but whose source's RXMASK moves its slot's channel, muted and playing no wrong channel); the whole-bus mute and its two NEWSTRCT; `kNone`; an AD242x chain; a hook that routes again; a failed write; a `MuteNetwork()` write failing with the main node gone (the bus restarts); `Bandwidth()` |
| `test_sim_access` | `Transport` against simulated nodes: registers, bursts, broadcast, page-1 VMTR (AD2433 and AD2437; AD242x have no page 1), PG on GPIO7, EEPROMs behind sub-nodes and on the host bus, the real ADI module-info image, bad CRC, EEPROM paging and busy time, two buses on one I2C bus, transient I2C errors, the trace format; SLOTFMT, DATCTL and I2SRRATE broadcast to the discovered sub-nodes only (AD2437 and AD2428), and the audio model's use of DATCTL and the slot format |

## Simulator scenarios

The scenarios are numbered; tests, comments and other documents refer to them by number.
The ones with no test belong to features that are not built (the
[roadmap](../../ROADMAP.md)'s features not included).

| # | Scenario | Test program |
|---|---|---|
| 1 | Main node idle | `test_sim_network` |
| 2 | Single hot-plug | `test_sim_network` |
| 3 | Chain of 3 | `test_sim_chain` |
| 4 | Unplug last and middle | `test_sim_drops` |
| 5 | Re-plug restores routing | `test_sim_routing` |
| 6 | EEPROM invalid, bad CRC or absent; app-supplied source | `test_sim_module_info` |
| 7 | Max sub-nodes (default 15), power-budget and bandwidth rejection; app pauses probing | `test_sim_chain`, `test_sim_events`, `test_sim_routing` |
| 8 | Power faults and their diagnosis | none (power diagnostics) |
| 9 | No MSTR_RUNNING | `test_sim_network` |
| 10 | Per-link polarity, including XCVRBINV written before ENSW | the polarity: `test_sim_discovery`, `test_sim_node_hooks`; the order check: none |
| 11 | Each main-node-capable chip (AD2428, AD2433, AD2437; the AD2427 is not one) as main node, with mixed sub-nodes; SPI peripheral calls refused | `test_sim_mixed_chips`, `test_sim_mixed_routing` |
| 12 | Transient I2C error | `test_sim_drops` |
| 13 | Host script with delays | `test_sim_host_script` |
| 14 | API calls from every event | `test_sim_events` |
| 15 | Two networks on one I2C bus | `test_sim_two_buses` |
| 16 | Two-step happy path | none (two-step links) |
| 17 | Two-step failures: no PG, node not high-voltage capable (no 24 V turned on), 24 V missing | none (two-step links) |
| 18 | Custom link | single-step: `test_sim_node_hooks`, `test_sim_mixed_chips`; two-step: none |
| 19 | Mixed UTP/RJ45/Custom chain | none (needs two-step RJ45) |
| 20 | Provisioning app: blank EEPROMs, write, verify, chunked transfers | `test_eeprom_provisioning` |
| 21 | Dual bus: follower with no blind probes, `PartnerMissing` and truncation, start order, primary drop and main-node fault | none (dual buses) |

## Golden traces

A golden is a pair of I2C traces captured from ADI's stack on a bench, run twice and compared
with `trace_diff.py` ([tools README](../tools/README.md)). Two are in `tests/golden/`:
`boot4_pi_*` (Pi boot with 4 sub-nodes) and `G13_pi_*` (Pi boot with EEPROM ignored, 4
sub-nodes); each pair compares equal, and both were captured with ADI's stack waiting out
SUSTAIN (`tests/golden/README.md`). The main-node register image of G13 is checked register for
register by `test_sim_g13_image` and `test_ss_plus_import`. Another golden is worth capturing
only if a behaviour diverges from ADI's stack later (steps in the tools README).

There are no goldens for AD2427, AD2428 or AD2433 nodes, or for a main node other than the
AD2437, because ADI's stack can't run them. Those cases are simulator-only (scenario 11) until
first hardware, or until traces are taken with SigmaStudio+ and the USBi.

## Style checks

`a2bcpp/.clang-format` and `a2bcpp/.clang-tidy` hold the code style and naming rules
([decisions.md](decisions.md#coding-standard)). Both hosts have the tools (MSYS2:
`mingw-w64-ucrt-x86_64-clang-tools-extra`, LLVM 22; Debian: `clang-format clang-tidy`,
LLVM 19), and the two versions format the code the same way. From the repository root,
after a build (it writes `compile_commands.json` for clang-tidy):

```
clang-format --dry-run --Werror a2bcpp/include/a2b/*.h a2bcpp/src/*.h a2bcpp/src/*.cpp a2bcpp/src/detail/*.h a2bcpp/sim/*.h a2bcpp/sim/*.cpp a2bcpp/import/ss_plus/*.h a2bcpp/import/ss_plus/*.cpp a2bcpp/tests/unit/*.h a2bcpp/tests/unit/*.cpp a2bcpp/tests/scenario/*.h a2bcpp/tests/scenario/*.cpp a2bcpp/tests/import/*.cpp a2bcpp/tests/example/*.cpp a2bcpp/tools/*.cpp a2bcpp/platforms/linux/*.h a2bcpp/platforms/linux/*.cpp a2bcpp/platforms/rp2040/*.h a2bcpp/platforms/rp2040/*.cpp a2bcpp/platforms/usbi/*.h a2bcpp/platforms/usbi/*.cpp a2bcpp/examples/minimal/*.h a2bcpp/examples/minimal/*.cpp a2bcpp/examples/bench/*.h a2bcpp/examples/bench/*.cpp a2bcpp/examples/clockworks/*.h a2bcpp/examples/clockworks/*.cpp a2bcpp/examples/fixed/*.h a2bcpp/examples/fixed/*.cpp
clang-tidy -p build/a2bcpp-linux --header-filter='a2bcpp/(include|src|sim|tests|import|platforms|examples)/' a2bcpp/src/*.cpp a2bcpp/sim/*.cpp a2bcpp/import/ss_plus/*.cpp a2bcpp/tests/unit/*.cpp a2bcpp/tests/scenario/*.cpp a2bcpp/tests/import/*.cpp a2bcpp/tests/example/*.cpp a2bcpp/platforms/linux/*.cpp a2bcpp/platforms/usbi/*.cpp a2bcpp/examples/minimal/minimal_app.cpp a2bcpp/examples/minimal/main_linux.cpp a2bcpp/examples/bench/bench_console*.cpp a2bcpp/examples/bench/console_text.cpp a2bcpp/examples/bench/eeprom_*.cpp a2bcpp/examples/bench/main_bench.cpp a2bcpp/examples/bench/trace_platform.cpp a2bcpp/examples/clockworks/*.cpp a2bcpp/examples/fixed/fixed_app.cpp
```

On Windows, put `<MSYS2>\ucrt64\bin` first on `PATH` and use `build/a2bcpp-windows`. In
PowerShell the tools expand the file patterns themselves, so the lists name each folder
and extension: there, a bare `*` also matches `.` and `..`, and `*/*` isn't expanded. The
Linux platform and the minimal example build on Linux only, so leave their files out
there, `tests/example/test_example_minimal.cpp` included (and the bench console's and
`libusb_link.cpp` without libusb). The `ss_plus_tables.inc` table is generated
(`a2bcpp/tools/gen_ss_plus_tables.py --check` tells if it is out of date) and isn't
formatted; the C files that rename an export's `sBusDescription`
(`tests/import/export_*.c`, `examples/bench/board_*.c`) aren't either.
The RP2040 files need the Pico SDK build's compile database and clang's Arm target (from
the repository root, after the RP2040 build in the [quick start](quick_start.md)):

```
clang-tidy -p build/a2b-minimal-rp2040 --header-filter='a2bcpp/(include/a2b|platforms/rp2040|examples/minimal)/[a-z_0-9]+\.h$' --extra-arg=--target=arm-none-eabi --extra-arg=-isystem/usr/lib/arm-none-eabi/include --extra-arg=-isystem/usr/include/newlib/c++/13.2.1 --extra-arg=-isystem/usr/include/newlib/c++/13.2.1/arm-none-eabi/thumb/v6-m/nofp a2bcpp/platforms/rp2040/rp2040_platform.cpp a2bcpp/examples/minimal/main_rp2040.cpp
clang-tidy -p build/a2b-fixed-rp2040 --header-filter='a2bcpp/(include/a2b|platforms/rp2040|examples/fixed|examples/clockworks)/[a-z_0-9]+\.h$' --extra-arg=--target=arm-none-eabi --extra-arg=-isystem/usr/lib/arm-none-eabi/include --extra-arg=-isystem/usr/include/newlib/c++/13.2.1 --extra-arg=-isystem/usr/include/newlib/c++/13.2.1/arm-none-eabi/thumb/v6-m/nofp a2bcpp/examples/fixed/main_rp2040.cpp a2bcpp/examples/fixed/fixed_app.cpp a2bcpp/examples/clockworks/clockworks_boards.cpp a2bcpp/examples/clockworks/sigma_xml.cpp
clang-tidy -p build/a2b-bench-rp2040 --header-filter='a2bcpp/(include/a2b|platforms/rp2040|examples/bench|examples/clockworks)/[a-z_0-9]+\.h$' --extra-arg=--target=arm-none-eabi --extra-arg=-isystem/usr/lib/arm-none-eabi/include --extra-arg=-isystem/usr/include/newlib/c++/13.2.1 --extra-arg=-isystem/usr/include/newlib/c++/13.2.1/arm-none-eabi/thumb/v6-m/nofp a2bcpp/examples/bench/main_rp2040.cpp
```

The `-isystem` paths are Debian's `gcc-arm-none-eabi` and `libstdc++-arm-none-eabi-newlib`
packages; use your toolchain's. The header filter is anchored because those builds reach
ADI's headers through the examples' `rp2040/` folders.
Pass `--header-filter` explicitly: without it clang-tidy 19 did not check our headers.
Use `clang-format -i` with the same file list to fix the layout. Text copied verbatim
from ADI (the licence headers in `src/detail/regdefs.h` and `import/ss_plus/`) sits between
`// clang-format off` and `// clang-format on`.

### Doxygen

Doxygen runs on Windows (1.18.0 in `C:\Program Files\doxygen`) and must give no
warnings. On Linux, `cd a2bcpp && doxygen Doxyfile` does the same; Doxygen 1.9.8 also warns
about each link from `a2bcpp/docs` to a file outside `a2bcpp/` (the roadmap, the README, the
image tool's README), which 1.18.0 doesn't, and about a link to an anchor on its own page
(write `page.md#anchor`). Any other warning is a fault.
From the repository root:

| Command | Docs | Output |
|---|---|---|
| `a2bcpp\tools\docs.ps1` | Development: `Doxyfile`, no graphs, quick | `build\a2bcpp-doxygen\html` |
| `a2bcpp\tools\docs.ps1 release` | Published: `Doxyfile.release`, which adds class, include and directory graphs (SVG); needs Graphviz's `dot` on `PATH` | `build\a2bcpp-doxygen-release\html` |

The script finds Doxygen on `PATH` or in its default folder. It clears the output folder
first, and fails if Doxygen prints anything. Function documentation follows the
[coding standard](decisions.md#coding-standard), item 5:
- the header's `///<` summary becomes the brief;
- the block on the definition in the `.cpp` becomes the details.

Autobrief ends a brief at the first ". ", so avoid "e.g." in a summary.

## Bench checks

Checks by hand on hardware, with the bench console, for behaviour the simulator tests cover
but no bench run has shown yet. What still needs one is in the
[roadmap](../../ROADMAP.md#what-needs-testing-on-hardware).

### `eeprom abort` during a write

An abort must be taken at once, and a write stopped after its marker is cleared must leave
the node blank and say so. The simulator's `test_eeprom_provisioning` covers both.

A write is usually too quick to abort by hand. It writes only the pages that differ (the
AB0312 image over the AB0020's: about ten), and the write cycle is at most 100 ms, so the
page writes take about a second. But a write also reads the EEPROM first and reads it all
back before the marker, in transfers of `eeprom config chunk` bytes: at 4 bytes, each read
of an AB0020 image takes seconds.

On a desktop host (Pi or Windows), at an AB0020 sub-node `N` that isn't the last node (the
roadmap's known issue):

1. Copy `tools/a2b_eeprom/examples/AB0312.dat` to the console's folder (on the Pi, `scp` it
   to the home directory).
2. Keep the node's image, and slow the job down:
   ```
   eeprom read N node_backup.dat
   eeprom config chunk 4 cycle 100
   ```
3. Time a whole write, T: `eeprom write N node_backup.dat` (it holds the image already, so
   only the reads run).
4. Write the other image and type `eeprom abort` about two thirds of the way into T, in the
   read-back. No prompt is shown while the job runs; type anyway.
   ```
   eeprom write N AB0312.dat
   eeprom abort
   ```
   Expected:
   ```
   EEPROM job on sub-node N failed: stopped (eeprom abort)
     The EEPROM is partly written, without its marker: the node has no module info until it is written again
   ```
   Then `eeprom info N` finds no image.
   - Only the first line: the abort came in the first read, and nothing was written. That
     passes the "at once" half; repeat with the abort later.
   - No failure: the abort came too late, and the node holds the AB0312 image. Restore it
     (step 5) before any `start`: the AB0312's B port would change the link after the node.
5. Restore the node and the settings:
   ```
   eeprom write N node_backup.dat
   eeprom config chunk 256 cycle 6
   eeprom verify N node_backup.dat
   start
   ```
   The node comes up from its EEPROM, its codec programmed, and routes as before.

Results: on the Pi (AD2437 main node, `ref`, four sub-nodes), an abort typed in the first
read was taken at once and wrote nothing. The abort after the marker is cleared is still to
do.

## Before a push

- `a2bcpp/tools/run_tests.sh` (or `run_tests.ps1`): every CTest program passes.
- `python3 tools/a2b_eeprom/tests/test_a2beeprom.py`, and the GUI tests,
  `tools/a2b_eeprom/tests/test_gui.py` (under `xvfb-run` on a host without a display).
- clang-format and clang-tidy on the changed C++ files (above).
- Doxygen with no new warnings.
- A change to an RP2040 example or `platforms/rp2040`: its Pico SDK project builds with no
  warning and no note ([quick start](quick_start.md)).
- A changed image-tool example project: its `.dat` rebuilt with
  `python3 tools/a2b_eeprom/a2b_eeprom.py build ...` ([its README](../../tools/a2b_eeprom/README.md)).

## RAM

`a2bcpp/tools/ram_report.py` measures the stack's RAM for the RP2040 without running it;
results and method are in `a2bcpp/docs/ram.md`.
