# Quick start

This page brings up one A2B bus with the C++ stack: the main node from a SigmaStudio+
export, then its chain of sub-nodes. The code is the minimal example app,
`a2bcpp/examples/minimal/`, which builds for the Raspberry Pi and the RP2040 and runs
on the simulator in the tests. From Windows, through the ADI USBi, use the
[bench console](bench_console.md) (below, "Windows").

What the stack does: it starts and configures the main
node, runs the board's host script, then finds, identifies and configures the **chain**
of sub-nodes, node after node, and goes on probing at the end of the chain, so a node
plugged in there is found. Each sub-node is set up from the module info in **its own
EEPROM**: its register settings and its peripheral scripts (e.g. a codec's program).
Each link can have its own profile. The chain is **checked for drops**: a node
unplugged is taken out with the nodes after it, and found again when it is plugged back
in. What happens comes to the app as **events**, and the app can accept or reject each
node. A host can run **several buses**, one `Network` each (below). **Routing** connects
nodes' channel groups (AD243x crossbars and AD242x TDM order, in mixed chains too;
[customisation cookbook](cookbook.md), "Routing"). Two-step links are not supported
([roadmap](../../ROADMAP.md)). How it all runs inside is in the
[architecture guide](architecture.md).

## What an app does

An app needs four things, and a listener if it wants to know what happens:

| What | Class | Where |
|---|---|---|
| Host I2C and a millisecond clock | a `Platform` | `platforms/linux`, `platforms/rp2040`, or your own ([porting guide](porting.md)) |
| The main node's settings | a `BoardConfig` | `ImportSsPlus()` from a SigmaStudio+ export, or by hand ([main-node configuration](main_node_config.md)) |
| The bus | a `Network` | `a2b/network.h` |
| Sub-nodes' module info | nothing: the default reads each module's EEPROM | an app `ModuleInfoSource` only to ignore the EEPROMs |
| What happens | a `NetworkListener` (optional) | `a2b/network_listener.h`; below |

Then it calls `Start()` once and `Poll()` often. From `examples/minimal/minimal_app.cpp`:

```cpp
#include "a2b/network.h"
#include "ss_plus_import.h"

extern "C" ADI_A2B_BCD sBusDescription;  // the SigmaStudio+ export, linked in

a2b::BoardConfig board;                  // must outlive the Network
a2b::FixedOpList<64> hostScript;         // one Op per SigmaStudio+ config unit
a2b::Network network(platform, board);   // reads the board at Start()

a2b::ImportSsPlus(sBusDescription, 0, board, hostScript);
network.SetListener(&listener);          // optional: the events, below
network.Start();
for (;;) {
  network.Poll(platform.NowMs());        // all bus traffic happens here
  // ... the app's own work; poll again within a few milliseconds
}
```

`Poll()` never blocks for longer than its I2C transfers: the start-up's waits (the RESETn
pulse, 25 ms or more for SUSTAIN, the PLL lock, discovery, a codec's 100 ms start-up) are
deadlines that later polls check. Poll every millisecond or so. The waits are in the
[timing guide](timing.md).

## Watching the bus

`Network::State()` tells where the bus is:

| State | Meaning |
|---|---|
| `kStopped` | Not started |
| `kStarting` | The host operations due before the main node (e.g. a codec that clocks it), RESETn pulse (if the platform has the line), main-node reset, SUSTAIN wait, PLL lock |
| `kConfiguring` | Writing the main node's settings, running the rest of the host script |
| `kDiscovering` | Bringing up the chain plugged in at start-up, node after node |
| `kRunning` | The chain is up: a probe found nothing more, a node was rejected (`LastRejection()`), `BoardPolicy::maxSubNodes` are in, or discovery is paused |
| `kFailed` | Stopped: `Error()` says why; `Start()` tries again |

`SubNodeCount()` and `SubNode(position)` give the sub-nodes in the bus, positions 0 to
the count - 1. In `kRunning` the stack:

- **probes at the end of the chain** every 500 ms (`BoardTimings::probeIntervalMs`), so
  the count grows when a node is plugged in there; not while `maxSubNodes` are in, nor
  while the app has called `PauseDiscovery()` (until `ResumeDiscovery()`);
- **checks the chain** every 500 ms (`chainCheckMs`): it reads each node's VENDOR and
  PRODUCT, as the TRMs' bus drop detection does. A node that doesn't answer (after three
  tries) has dropped: it is taken out of the bus with every node after it, the switch
  before it is opened, and its position is probed again, so it is found again when it is
  plugged back in (re-plug). A main node that was reset starts the bus again. Interrupts
  that may mean a drop (e.g. SRF missed) have the chain checked at once;
- **clears the bit error counts** (BECNT) every second, as the TRMs advise.

Once the bus is up, a failed transfer doesn't stop it: the probe or set-up is given up,
the chain checked, and the next probe tries again.

Errors: `kI2cError` (the main node doesn't answer), `kWrongMainNode` (PRODUCT isn't the
BoardConfig's chip), `kMainNodeNotRunning` (no MSTR_RUNNING: is SYNC there?),
`kHostScriptFailed`. The stack also logs through `Platform::Log()`. `Start()` itself
refuses a `BoardConfig` whose main node can't be one (`kInvalidArgument`): the main node
is an AD2428, AD2433 or AD2437; the AD2427 is a sub-node only (its data sheet).

A sub-node is rejected (`LastRejection()`) when its transceiver isn't an AD2427/28/33/37
(`kUnsupportedChip`), when there is no module info for it (`kNoModuleInfo`: no EEPROM at
0x50-0x54, or a blank one), when the image fails its checks (`kBadModuleInfo`: marker,
header CRC, block type), when it would take the bus-powered modules over the board's
power or current budget (`kPowerBudget`: `BoardPolicy::maxBusPowerMw`/`maxBusCurrentMa`,
from each module's maximum power and current in its module info; 0, the default, is no
limit), when the app rejects it (`kByApp`, below), when a peripheral script from its
module info fails (`kPeripheralFailed`: a block or unit CRC, or a peripheral that doesn't
answer), or when an operation its `SubNode` object added fails (`kConfigFailed`). A
rejected node has its power switched off, and the chain ends before it. Its position is
probed again every 2 s (`rejectedProbeMs`): the same node is switched off again at once,
without a new event; a different module plugged in there is taken in as usual.

## Events

What happens is queued and given to the app's `NetworkListener` at the end of `Poll()`,
when the bus is between steps, so a handler may call any `Network` function: take a node
out (`RejectNode()`), reach a node's peripherals, stop or start the bus.

```cpp
class Listener : public a2b::NetworkListener {
 public:
  void OnEvent(a2b::Network& network, const a2b::Event& event) override {
    switch (event.type) {
      case a2b::EventType::kNodeAdded:       // event.node is in the bus: network.SubNodeInfoAt(event.node)
      case a2b::EventType::kNodeRemoved:     // it left: event.removal (dropped, rejected, restart, stopped)
      case a2b::EventType::kNodeRejected:    // it wasn't taken in: event.rejection
      case a2b::EventType::kStateChanged:    // event.state
      case a2b::EventType::kInterrupt:       // event.node raised INTTYPE event.interruptType
      case a2b::EventType::kRoutingApplied:  // a routing plan is written: network.Routing()
      case a2b::EventType::kRoutingFailed:   // event.routingError (event.node at fault)
        break;
    }
  }
  // Optional: decide on each node before it is set up (read-only).
  a2b::RejectReason OnNodeIdentified(const a2b::NodeReview& review) override {
    return review.policy;  // the power budget's decision; kByApp to reject, kNone to take it
  }
};
```

Node events carry the node's chip ID (`event.chipId`), which tells modules apart when
positions change. The [customisation cookbook](cookbook.md) has the details. The
minimal example is its own listener and prints every event.

## Chains

Each sub-node is found through the B side of the node before it, as in the TRMs'
discovery flow. Before a node first powers the next link, the stack gives it its
interrupt masks (power faults), its link polarity (CONTROL.XCVRBINV, from the link's
profile, below), and, on an AD243x, checks
the power configuration it detected (SWSTAT2) against the one its module info gives
(the board's `highPowerSwitchCfg` for the main node). If they differ, the stack
overrides it and waits 100 ms. The response cycles written for sub-node n (DISCVRY) are
the TRMs' value for position n from the main node's RESPCYCS (the image's, or the routing
plan's once one is written), with 4 m cables unless `BoardConfig::routing` says otherwise.

Each link takes its `LinkProfile` (B-side polarity, and whether the upstream node's
switch powers the next node) from, in order:

1. `Network::SetLinkProfile(n, profile)`, for the link to sub-node n (0: the main node's);
2. the upstream node's `SubNode::DownstreamLink()`, for an app class of that module
   ([customisation cookbook](cookbook.md));
3. `BoardConfig::defaultLink`.

The profile is looked up again before each probe, so a change applies to the next probe
of that position. Two-step links (RJ45, XLR) are not built yet: `SetLinkProfile()` refuses
them, and a link a node's `DownstreamLink()` makes two-step is not probed.

## Module info

A sub-node's settings (I2S, PDM, clocks, GPIO, SPI), its power configuration and its
peripheral scripts come from its module info: the image ADI's PnP modules keep in their
EEPROM (layout in `a2bcpp/src/module_info.cpp` and `a2bcpp/src/module_script.cpp`). By
default the stack reads it from the EEPROM at the first of 0x50-0x54 on the node's I2C
port that holds the marker (`EepromModuleInfoSource`), writes the settings, then runs the
peripheral scripts (writes and waits, e.g. an ADAU1701's program) before the node counts
as found.

To ignore the EEPROMs, e.g. to provision blank modules, the app gives the images itself
through a `ModuleInfoSource` (an image file or array: `ImageModuleInfoSource`; writing the
EEPROMs: [provisioning.md](provisioning.md)); no EEPROM is read then:

```cpp
class Source : public a2b::ModuleInfoSource {
 public:
  size_t GetModuleInfo(const a2b::SubNodeIdentity& node, uint8_t* buf, size_t bufLen) override {
    // node has the transceiver's IDs and CHIPID; copy up to bufLen bytes of an image.
    ...
    return a2b::kModuleInfoLen;  // 0: no module info, the node is rejected
  }
  // Optional: the rest of the image (from 0x800), for its peripheral scripts.
  // a2b::Result ReadModuleInfo(const a2b::SubNodeIdentity& node, uint32_t offset,
  //                            uint8_t* buf, size_t len) override;
};
network.SetModuleInfoSource(&source);   // nullptr: back to the EEPROMs
```

With `--ignore-eeprom` the example gives every node the settings the bench modules had
in golden G13 (GPIO0-6 inputs, PINCFG 0). Each node runs in its chip's own power
configuration (CFG-4 on an AD2437, CFG-0 on the others), whatever an image says.

## Several buses

One `Network` per bus, each with its own `BoardConfig` (its main node's I2C addresses,
e.g. 0x68/0x69 and 0x6A/0x6B, and its own `busIndex`), all on one `Platform`; poll them in
turn:

```cpp
a2b::Network bus0(platform, board0);
a2b::Network bus1(platform, board1);
bus0.Start();
bus1.Start();
for (;;) {
  const uint32_t now = platform.NowMs();
  bus0.Poll(now);
  bus1.Poll(now);
}
```

Each bus finds, checks and reports its own chain; nothing one bus does reaches the
other. The [main-node configuration guide](main_node_config.md) and the
[customisation cookbook](cookbook.md) ("Several buses") have the details. Two buses have
run on the simulator only so far. The minimal example runs one bus: a second would add
options and code to an app meant to show the least an app needs, and the benches have
one bus; the simulator's scenario 15 (`tests/scenario/test_sim_two_buses.cpp`) runs two.

## Build and run

### Raspberry Pi

On the Pi itself (Raspberry Pi OS, with `cmake`, `g++`, I2C enabled in `raspi-config`):

```
cmake -S a2bcpp -B build/a2bcpp-pi -DA2B_CPP_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/a2bcpp-pi
build/a2bcpp-pi/examples/minimal/a2b_minimal   # or: a2b_minimal [--ignore-eeprom] [--trace FILE] [--utp N]... [/dev/i2c-N]
```

Or cross-build in Debian WSL with `a2bcpp/tools/toolchain-pi.cmake`
(`crossbuild-essential-arm64`; see [setup_pi.md](setup_pi.md)), then copy
`build/a2bcpp-pi/examples/minimal/a2b_minimal` to the Pi:

```
cmake -S a2bcpp -B build/a2bcpp-pi -DCMAKE_TOOLCHAIN_FILE=$PWD/a2bcpp/tools/toolchain-pi.cmake -DA2B_CPP_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
cmake --build build/a2bcpp-pi
```

This uses make. Add `-G Ninja` if `ninja-build` is
installed. A build folder keeps the generator it was first configured with: after a
failed or different configure, delete `build/a2bcpp-pi` first.

From VS Code, the A2B menu's "Raspberry Pi bench" section builds (`a2bcpp/tools/build_pi.sh`),
deploys, runs and debugs the [bench console](bench_console.md) instead of the minimal
example ([setup_pi.md](setup_pi.md) has the settings).

Stop any other program using the same I2C bus first. With the reference board and one
sub-node, the output is (the module line is the bench module's EEPROM; with
`--ignore-eeprom` it shows the app's image, with 0 peripheral blocks):

```
Module info from the EEPROMs
bus 0: starting the main node
info: a2b: main node running
bus 0: configuring the main node
info: a2b: main node configured
bus 0: bringing up the chain
info: a2b: sub-node 0 found and configured
  sub-node 0: AD2437 version 0x21, chip ID 18ACEE042316
    module 00AD:0037, CFG-4, 1 peripheral block(s)
info: a2b: chain up with 1 sub-node(s)
bus 0: running
  1 sub-node(s)
```

The `info:` lines are the stack's log, printed as they happen; the others are the
example's, printed from the events at the end of the same poll.

A node plugged in later at the end of the chain adds a "found and configured" line and
its own line. A node that is rejected, at start-up or plugged in later, adds a line with
the reason, e.g. `sub-node 4 rejected (reason 2: no module info ...)` for a module with no
EEPROM or a blank one (use `--ignore-eeprom` to take such modules with the app's image);
it is not reported again while it stays plugged in. Unplugging a node adds
`warning: a2b: sub-node 2 dropped` and a `sub-node n removed (dropped ...)` line for it
and each node after it, within about half a second; plugged back in, they are found and
configured again.

`--trace FILE` writes every I2C transfer to FILE as I2CT lines, the format of the traces
captured from ADI's stack, so `a2bcpp/tools/trace_decode.py FILE` decodes it and `trace_diff.py`
compares it with a golden. Each line is written as its transfer ends. The export's main
node sets XCVRBINV, so the example's links are inverted, as on the bench's RJ45 links.
`--utp N` makes the link into sub-node N a UTP link instead (`LinkProfile::Utp()`: B side
not inverted, power on the A2B pair; `Network::SetLinkProfile(N, ...)`); repeat it for
more links. Stop it with Ctrl-C.

### RP2040

A Pico SDK project of its own, with the AB0310 board's wiring (i2c0 on GPIO16/17, the main
node's RESETn on GP2, pulsed at start-up) and its UART console at 230400 baud. It reads
the EEPROMs; set `kIgnoreEeprom` in `main_rp2040.cpp` to give the app's images instead:

```
cmake -S a2bcpp/examples/minimal/rp2040 -B build/a2b-minimal-rp2040 -G Ninja
cmake --build build/a2b-minimal-rp2040
```

The Pico SDK and its Arm toolchain come from the Pico VS Code extension (`~/.pico-sdk`),
with the versions the projects pin: SDK 2.0.0, toolchain 13_2_Rel1 and
picotool 2.0.0. Run the commands from a plain terminal; the extension needn't be open,
but `ninja` must be on `PATH` (the extension keeps one under `~/.pico-sdk/ninja/`; in
Debian, `sudo apt install ninja-build`).
To use another SDK, set `PICO_SDK_PATH` (in the environment, or
`-DPICO_SDK_PATH=...`): the extension is then not used, and `arm-none-eabi-gcc` must be
on `PATH` (or in `PICO_TOOLCHAIN_PATH`). Flash `build/a2b-minimal-rp2040/a2b_minimal.uf2`.

#### The fixed app (AB0310 and AB0312)

`a2bcpp/examples/fixed` is the AB0310 and AB0312 bench with nothing typed, the first
RP2040 program for Clockworks boards. On the same wiring and UART as the minimal example,
it:
- brings up the bus from the AB0310's export
  (`tools/SS+reference/AB0310_AB0312_EVM_with_2437`);
- gives sub-node 0 the AB0312's module info (no EEPROM) and every other sub-node the
  generic image (as the bench console's `--ignore-eeprom`);
- once sub-node 0 is in, routes the main node's stereo pair (RX 0, channels 0-1) to the
  AB0312 and the AB0312's back, and keeps them routed as the chain changes;
- prints the bus's progress as the bench console does, in a shorter form, and the heap in
  use once routing is applied.

The setup is the constants in `examples/fixed/main_rp2040.cpp`; the app itself
(`fixed_app.cpp`) is the same on every host and is tested on the simulator. Like the
bench console, it uses the heap (`clockworks_boards`); the stack itself doesn't. Build it
and program it through a CMSIS-DAP debug probe (the Raspberry Pi Debug Probe), at the
repository root. Run the two build commands in VS Code's terminal, where the Pico
extension puts its `cmake` and `ninja` on `PATH`; the third names the extension's OpenOCD
by its path, so it runs in any PowerShell (`&` runs the quoted path):

```
cmake -S a2bcpp/examples/fixed/rp2040 -B build/a2b-fixed-rp2040 -G Ninja
cmake --build build/a2b-fixed-rp2040
& "$env:USERPROFILE\.pico-sdk\openocd\0.12.0+dev\openocd.exe" -s "$env:USERPROFILE\.pico-sdk\openocd\0.12.0+dev\scripts" -f interface/cmsis-dap.cfg -f target/rp2040.cfg -c "adapter speed 5000; program build/a2b-fixed-rp2040/a2b_fixed.elf verify reset exit"
```

From VS Code: the A2B menu's "RP2040 fixed" section does the same (Build = Debug build,
Debug = F5 through the probe, stopped at `main`, Release = Release build programmed with the
OpenOCD command above), and "RP2040 minimal" the same for the minimal example. The
Debug builds are in `build/a2b-fixed-rp2040` and `build/a2b-minimal-rp2040`, the Release
builds in the same names plus `-release`.

The console is the Pico's uart0 (GP0 TX, GP1 RX) at 230400 baud: the Debug Probe's UART
port, wired to GP0/GP1, is a COM port on the PC. `build/a2b-fixed-rp2040/a2b_fixed.uf2`
can be copied to the Pico in BOOTSEL mode instead. Built with SDK 2.0.0 (Debug): about
160 KB of flash and 29 KB of static RAM (`.data` and `.bss`; the rest of the RAM is the heap
and the stack; [ram.md](ram.md) has the figures).

On the bench, with an AB0310, an AB0312 and four more AD2437 modules on RJ45
links: the five sub-nodes found and configured (the AB0312's image at sub-node 0, the
generic one at the others), both connections routed with nothing typed (main node
RESPCYCS 0x7B), 1460 bytes of heap in use.

#### The bench console

`a2bcpp/examples/bench/rp2040` builds the bench console for the same board, wiring and
UART: the `A2B>` commands typed on the Pico's console, without the ones that need files
(`eeprom`, `i2cTrace`, boards and programs from files). It starts with `board AB0310`
picked; type `start`. Build and program it as the fixed app, with `bench` for `fixed`
(`build/a2b-bench-rp2040/a2b_bench.elf`), or from the A2B menu's "RP2040 bench" section.
The [bench console guide](bench_console.md#on-the-rp2040) has the rest.

What has run on hardware, on each host, is in the
[roadmap](../../ROADMAP.md#tested-on-hardware); the RP2040 minimal example
hasn't yet.

### Windows (USBi)

The minimal example has no Windows build: on Windows the stack runs in the
[bench console](bench_console.md), over the ADI USBi (`UsbiPlatform`, [porting
guide](porting.md)). In PowerShell, from the repository root, with MSYS2 UCRT64 and
libusb installed and the USBi on the WinUSB driver ([setup_windows.md](setup_windows.md)):

```powershell
a2bcpp\tools\run_tests.ps1
build\a2bcpp-windows\examples\bench\a2b_bench.exe --board ref start
```

`--board ref` is the Pi's export, built in; `--board FILE` loads any SigmaStudio+ export's
C file at run time. The console prints the same events as the minimal example, and takes
the commands of the [bench console guide](bench_console.md) (`status`, `link N utp`,
`ignoreEEPROM 1`, `i2cTrace on FILE`, `quit`).

From VS Code, the A2B menu's "Windows bench (USBi)" section builds it without the tests
(`a2bcpp\tools\build_windows.ps1 Debug` or `Release`, into `build\a2b-bench-windows\<type>`),
runs it on the board in `a2b.windowsBoard` and debugs it under gdb.

### On the simulator

`tests/example/test_example_minimal.cpp` runs the same app code against the simulator,
with the same export, and is part of the test suite
([testing guide](testing.md)).

## Next

- [Architecture](architecture.md): the `Poll()` model, the states, when things happen,
  events, several buses.
- [Main-node configuration](main_node_config.md): the export, `BoardConfig`, hooks, the
  start-up sequence, several buses.
- [Customisation cookbook](cookbook.md): module classes, hooks, link profiles,
  module-info sources, events, the node review, interrupts, timings, several buses.
- [Timing](timing.md): every wait and timeout.
- [Porting guide](porting.md): a `Platform` for another host, and the USBi platform.
- [Bench console](bench_console.md): the stack from typed commands, on Windows (USBi),
  Linux and the RP2040.
- [Main- and sub-node programming](main_and_sub_node_programming.md): where each node's
  settings come from, in short.
- [Provisioning](provisioning.md): the module-info image, the image tool and writing EEPROMs.
- [Hardware notes](hardware_notes.md): what the chips, boards and adapters taught us.
- [Design decisions](decisions.md): requirements, rationale, policies, and the designs not
  built yet.
- [Testing](testing.md) and [RAM](ram.md): the tests, the simulator, and the RP2040 figures.
