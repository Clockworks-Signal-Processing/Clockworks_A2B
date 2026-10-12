# Main-node configuration

Each A2B bus has one main node, configured by the app. Sub-nodes are configured from
their own EEPROMs and aren't covered here.

A bus's main-node settings live in an `a2b::BoardConfig` (`a2bcpp/include/a2b/board_config.h`).
Fill it from a SigmaStudio+ export with `ImportSsPlus()`, or by hand.

## What a BoardConfig holds

| Field | What it is | From the SigmaStudio+ export |
|---|---|---|
| `busIndex` | This bus's index: the `Platform`'s reset, IRQ and power-good calls take it, and each sub-node's `SubNodeIdentity::bus` reports it. Give each bus its own (below, "Several buses") | no: set by the app |
| `mainAddr`, `busAddr` | The bus's two 7-bit I2C addresses (usually 0x68 and 0x69) | `nMasterI2CAddr`, `nBusI2CAddr` |
| `mainProduct` | The main node's chip: 0x28, 0x33 or 0x37. The AD2427 (0x27) is not main-node capable (its data sheet), so `Network::Start()` refuses it | `sAuthSettings.nProductID` |
| `mainLocalPower`, `highPowerSwitchCfg` | The main node's power settings; `highPowerSwitchCfg` (0 for CFG-0, 4 for CFG-4) is checked against what an AD243x detects, and overrides it (start-up, below). By hand, leave it at `kPowerConfigAsDetected` to keep what the chip detects | `bLocalPower`, `nHighPwrSwitchCfg` |
| `mainNode` | The main node's register values (a `RegisterImage`) | the main-node settings |
| `hostScript`, `hostOpsBeforeMainNode` | Operations on the board's own peripherals, e.g. a codec; the first `hostOpsBeforeMainNode` run before the main node is started, the rest once it is configured (below) | the target's peripherals; each one before or after the transceiver as the export marks it (`bPostDiscCfg`) |
| `defaultLink` | The link to sub-node 0 and to every later sub-node (a `LinkProfile`: discovery steps, power, B-side polarity), unless the app sets one for a position (`Network::SetLinkProfile()`) or the upstream node's class gives its own (`SubNode::DownstreamLink()`; [customisation cookbook](cookbook.md)) | single-step, powered over the A2B pair, with `bSideInverted` only if the export's main-node CONTROL sets XCVRBINV (as for RJ45 links; UTP links are straight) |
| `timings` | `discoveryStartDelayMs` (tPLK) and `rediscoveryWaitMs`; the RESETn pulse, discovery timeout, probe interval, interrupt poll interval, chain checks, BECNT writes and the rejected node's probes ([timing guide](timing.md)) | `nDiscoveryStartDelay`, `nRediscWaitTime`; the rest keep their defaults |
| `policy` | `maxSubNodes` (1-16, default 15): the most sub-nodes the system design has; once that many are in, the stack stops probing until one leaves. The chips take 16; the default is 15 because the 16th node has given trouble on hardware; the power budget, `maxBusPowerMw` and `maxBusCurrentMa` (default 0: no limit; [customisation cookbook](cookbook.md), "Accepting nodes") | no: set by the app (ADI's `maxPWconsumption` and `maxCurrentConsumption`) |
| `hooks` | App hooks (`BoardHooks`), or none | no: set by the app |
| `routing` | The main node's channel groups (RX: TDM channels it sends down from its DRXn pins; TX: those it plays on its DTXn pins), the frame rate and cable length for the response cycles (48 kHz, 4 m; `Network::SetCableLength()` gives a link its own), and the app's `RoutingPolicy`, or none ([customisation cookbook](cookbook.md), "Routing") | no: an export has no channel groups; set them for the board |

**The register image.** `mainNode` holds register addresses and values, in the order they
are written (page 1 registers are 0x100-0x1FF). It is configuration only. The stack
decides when each register is written, and it owns NODEADR, MMRPAGE, DISCVRY, the
interrupt-pending registers, the CONTROL command bits (MSTR, NEWSTRCT, ENDDSC,
SOFTRST), SWCTL.ENSW and the routing registers (the slot registers BCDNSLOTS to UPSLOTS,
UPMASK0-3, UPOFFSET, DNMASK0-3, DNOFFSET, RXMASK0-7, TXXBAR0-31), whose image values are
not written: routing sets them. From the image's CONTROL it keeps SWBYP and (AD243x)
I2SMSINV; XCVRBINV comes from `defaultLink`. From SWCTL it keeps the switch mode.
RESPCYCS is the response-cycle setting until a routing plan gives its own, and the base of
each probe's DISCVRY (0x40, the power-on value, if the image has none). SLOTFMT and
I2SGCFG are written as the image has them, and routing reads them: the slot widths and
the response offset. SLOTFMT, DATCTL and I2SRRATE are auto-broadcast, so a write
reaches only the sub-nodes found by then: each routing plan writes SLOTFMT and DATCTL
again (DATCTL 0x03, both directions on, if the image has none), and I2SRRATE if the
image has it. `Network` writes the image at each start-up; see "Start-up" below.

**The host script.** `hostScript` points to an `a2b::OpList`, a list of operations:
register writes, peripheral writes and reads, and waits. Its first
`hostOpsBeforeMainNode` operations run at `Start()`, before the main node is touched: a
peripheral the main node depends on, such as the AB0020's ADAU1761, which gives the
AD2437 its SYNC and bit clock, so the main node never locks until the codec is
programmed. The rest run once the main node is configured. SigmaStudio+ marks each
peripheral to be configured before or after the transceiver, and `ImportSsPlus()` follows
it. The operations run across polls, so a codec's 100 ms start-up delay doesn't block the
app. The early ones run once per `Start()`, not when the bus starts again after a PLL
relock: programming a clock source again would unlock the PLL again. The list and the
data it points to must outlive the bus.

## From a SigmaStudio+ export

SigmaStudio+ writes the bus configuration as a C file (`adi_a2b_busconfig.c`, which this
repository keeps in `adi/exports/` for the Pi and the AB0020 and in
`tools/SS+reference/<project>/` for the Clockworks boards). It defines
`sBusDescription`.
Link that file into the app, then:

```cpp
#include "ss_plus_import.h"

extern "C" ADI_A2B_BCD sBusDescription;  // from the export

a2b::BoardConfig board;
a2b::FixedOpList<64> hostScript;          // one Op per SigmaStudio+ config unit

a2b::Result result = a2b::ImportSsPlus(sBusDescription, 0, board, hostScript);
// kOk; kInvalidArgument (no such chain, or a malformed unit); kNotSupported (the main
// node isn't an AD2427/28/33/37, the host reaches it over SPI, or a host peripheral is
// on SPI); kNoSpace (hostScript too small). Network::Start() refuses an AD2427 main node.
board.busIndex = 0;
```

The second argument picks the chain (main node) in an export that describes several.
For several buses, import each into its own `BoardConfig` (below, "Several buses").

**Loading an export at run time.** A host app (Windows, Linux) can read the export's C
file instead of linking it in, with `SsPlusFile` (`import/ss_plus/ss_plus_file.h`,
library `a2bcpp_ss_plus_file`; it uses the heap and the C++ library, so not on the
RP2040). The bench console's `board FILE` uses it ([bench console](bench_console.md)):

```cpp
#include "ss_plus_file.h"

a2b::SsPlusFile file;                          // must outlive the bus: the host script points into it
if (file.Load("adi_a2b_busconfig.c") != a2b::Result::kOk) {
  std::printf("%s\n", file.Error().c_str());   // e.g. "line 812: unknown constant X"
}
for (const std::string& warning : file.Warnings()) {
  std::printf("%s\n", warning.c_str());        // fields the header doesn't have, skipped
}
a2b::ImportSsPlus(*file.Bus(), 0, board, hostScript);
```

It reads the C SigmaStudio+ writes (designated and positional initializers, the export's
arrays and pointers, `#if` blocks, ADI's constants) and fills the structures as the
compiler would: `tests/import/test_ss_plus_file.cpp` checks the repository's exports
byte for byte against their compiled forms, and their imports against each other. A field
the header doesn't have is skipped with a warning, so an export from a SigmaStudio+ newer
than the header still loads. So is a constant it doesn't have, taken as 0, where the
import doesn't read (sub-node descriptions, streams); where it reads (the bus, the chains,
the main nodes, the target's peripherals) the file is refused, with the line. The structures' fields and ADI's constants come from the
header: `a2bcpp/tools/gen_ss_plus_tables.py` writes them to
`import/ss_plus/ss_plus_tables.inc`, and the compiler takes each field's offset, size and
type; run it again if the header changes (`--check` tells if the table is out of date).

**What is imported**
- The main node's register settings. The values and write order match ADI's stack:
  a test checks the import of the Pi's export against the G13 golden trace
  (`a2bcpp/tests/import/test_ss_plus_import.cpp`). AD243x main nodes also get the
  crossbar, SPI, data tunnel, PWM and VMTR registers. AD242x main nodes get the LVDS
  transmit controls instead.
- The target's peripherals on the host I2C bus. Each SigmaStudio+ config unit becomes one
  `Op`: write, read (the bytes read are discarded) or delay. Peripherals declared without
  config units, such as the AB0020's EEPROM, are skipped.
- Addresses, the chip, power settings and the two timings.
- When each host peripheral is configured (`bPostDiscCfg`): those configured before the
  transceiver come first in `hostScript`, and `hostOpsBeforeMainNode` counts their
  operations; the others follow. ADI's stack configured every host peripheral
  before the main node (`adi_a2b_ParsePeriCfgTable` doesn't copy the flag).

**What is not imported**
- Sub-node settings: sub-nodes are configured from their EEPROMs.
- Discovery mode, cable type, line diagnostics and fault policies. The new stack has its
  own `LinkProfile` and policy settings for these.

**Differences from ADI's conversion** (`a2b_bdd_helper.c`)
- For GPIO0 used as an input or output, the import takes the export's GPIO0
  interrupt-enable and output value. ADI's code read GPIO1's.
- AD2430, AD2438 and AD241x main nodes are not supported.

**Building.** The import is the optional library `a2bcpp_ss_plus`
(`a2bcpp/import/ss_plus/`, CMake option `A2B_CPP_SS_PLUS`). It includes ADI's
`adi_a2b_busconfig.h`, which needs `adi_a2b_datatypes.h` and a platform's `a2b/`
headers. A compiled export and the import must use the same `adi_a2b_busconfig.h`, or
the import reads the wrong fields. The default (`A2B_SS_PLUS_HEADERS`) is the headers every
export in `tools/SS+reference` is compiled with: `tools/SS+reference/adi`, ADI's header
merged so that SigmaStudio+ 2.3.0 exports compile, with the Linux platform's `a2b/`
headers. A platform build that compiles only its own export may set `A2B_SS_PLUS_HEADERS`
to its own headers, as the minimal RP2040 example does.

## Start-up

`Network::Start()` copies the image, calls `BoardHooks::BeforeMainNodeInit()` with the
copy, and checks the configuration. Then `Network::Poll()` runs the start-up, the same
whether the host pulsed RESETn or the main node is still running from an earlier session
(the Pi reference board has no RESETn line, nor does the Windows host):

0. The host operations due first (`hostOpsBeforeMainNode`), across polls; a failure
   stops the bus with `NetworkError::kHostScriptFailed` before the main node is touched.
   Only at `Start()`, not when the bus starts again after a PLL relock.
   On a `Platform` with a reset line (`SetMainNodeReset()` returns `kOk`): RESETn low for
   `timings.resetPulseMs` (100), then high, then 2 ms before the first access (the data
   sheets' tRST and tACCESS; [timing guide](timing.md)). Only at `Start()`, not when the
   bus starts again after a PLL relock. The main node is then in POWER-UP, where the soft
   reset in step 2 has no effect.
1. Read VENDOR, PRODUCT and VERSION. PRODUCT must be `mainProduct`, or the bus fails with
   `NetworkError::kWrongMainNode`.
2. Soft reset: CONTROL = SOFTRST, with MSTR = 0.
3. Wait `rediscoveryWaitMs`, never less than 25 ms ([timing guide](timing.md)).
4. I2SGCFG = INV alone, if the image sets INV, then CONTROL = MSTR.
5. Wait for MSTR_RUNNING, up to `discoveryStartDelayMs`; one last interrupt check, then
   `NetworkError::kMainNodeNotRunning` (no SYNC, for instance).
6. Write the image in its order, without the registers the stack owns. Consecutive
   registers go in one transfer. If the image has INTMSK2, DSCDONE is added to it. An
   image without INTMSK2 leaves DSCDONE off and no sub-node is ever found, so a
   hand-filled image must set it (`ImportSsPlus()` always does).
7. Run the rest of the host script (the peripherals configured after the transceiver).
8. Before the first probe, on an AD243x, unless `highPowerSwitchCfg` is
   `kPowerConfigAsDetected`: read SWSTAT2 and compare the power configuration the chip
   detected with it. If they differ, write SWCTL2.HPSW_CFG and SWCTL.CFG_DET_OV to
   override it, and wait 100 ms before the switch closes (the TRMs' discovery flows).
9. Discover the chain, node after node (see the [quick start](quick_start.md)).

An MSTR_RUNNING after start-up means the main node's PLL unlocked and relocked, which
resets its registers. The bus then starts again from step 1, without a RESETn pulse.

**What each reset keeps.** Checked against the TRMs in `docs/ADI_chips`;
the simulator does the same:

| Reset | AD2437 | AD2433 (and AD2431/32/35) | AD2427, AD2428 |
|---|---|---|---|
| Power-on or RESETn | nothing | nothing | nothing |
| Soft reset (CONTROL.SOFTRST) | CONTROL, BMMCFG | CONTROL, PLLCTL, BMMCFG | nothing ("all registers return to their respective reset states", CONTROL field table) |
| Main node's PLL unlock (SYNC lost) | CONTROL and BMMCFG ("Soft Reset"; "CONTROL.MSTR" in "Error Management") | CONTROL, PLLCTL, BMMCFG | BMMCFG and CONTROL.MSTR ("Reset and Operating States"; "CONTROL" in "Main Node Bring-Up") |

Also from the TRMs:
- A soft reset with MSTR = 0 unlocks a running main node's PLL. An AD243x then spends
  SUSTAIN (1024 SYNC periods) before it is back in POWER-UP. An AD242x main node never
  enters SUSTAIN.
- A soft reset with MSTR = 1 leaves the PLL locked.
- A soft reset in POWER-UP has no effect.
- Once the PLL is locked, writing MSTR (0 or 1) has no effect.
- INV must be set before MSTR.

The simulator keeps CONTROL and BMMCFG through an AD242x soft reset as well. The
start-up doesn't depend on what a reset keeps: it writes every setting after the reset.

ADI's stack wrote SOFTRST | MSTR at once on AD242x main nodes. The stack uses the
AD243x sequence on every chip, which costs an AD242x main node only the wait.

An AD2428 main node has run on the bench ([bench console](bench_console.md)).
TODO(test on hardware): an AD2433 main node.

## By hand

Fill the image with `RegisterImage::Set(reg, value)` in the order the registers should be
written, and build the host script with the `Op` factory functions:

```cpp
a2b::BoardConfig board;
board.mainProduct = 0x37;
board.mainNode.Set(0x41, 0xE2);   // I2SGCFG: TDM8, 32-bit, alternate sync, early, invert
// ... the other registers ...
board.mainNode.Set(0x1D, 0x01);   // INTMSK2: DSCDONE, which discovery needs

// The AB0020 export's ADAU1761 PLL set-up, then its 100 ms lock time.
static const uint8_t pllSetup[6] = {0x00, 0x01, 0x00, 0x00, 0x20, 0x03};
static a2b::FixedOpList<8> script;
script.Add(a2b::Op::PeripheralWrite(a2b::kMainNode, 0x39, 2, 0x4002, pllSetup, 6));
script.Add(a2b::Op::Wait(100));
board.hostScript = &script;
```

The default link is UTP. For an inverted B side (e.g. the reference board's RJ45 cable
run single-step):

```cpp
board.defaultLink = a2b::LinkProfile::Utp();
board.defaultLink.bSideInverted = true;
```

Two-step links (`LinkProfile::Rj45()`, `Xlr()`) are not built yet: until then
`Network::Start()` refuses them with `Result::kNotSupported`.

**Hooks.** To change the image at each start-up (for example from a setting the app
reads at run time), derive from `a2b::BoardHooks`:

```cpp
class MyHooks : public a2b::BoardHooks {
 public:
  void BeforeMainNodeInit(a2b::RegisterImage& image) override {
    image.Set(0x5A, 0x00);  // CLK2CFG: no clock out on this build
  }
};
static MyHooks hooks;
board.hooks = &hooks;
```

The hook gets this start-up's copy of the image. The BoardConfig itself is not changed.

## Several buses

Each bus has its own `BoardConfig` and its own `Network`, all on one `Platform`
([architecture guide](architecture.md), "Several buses"):

- **I2C addresses.** Up to four main nodes share one host I2C bus. Each one's ADR1 and
  ADR2 pins, read at power-on, give its pair: 0x68/0x69, 0x6A/0x6B, 0x6C/0x6D or
  0x6E/0x6F (TRMs, "I2C Interface"). Set `mainAddr` and `busAddr` to the board's pair; an
  export has them as `nMasterI2CAddr` and `nBusI2CAddr`. Main nodes on separate host I2C
  buses need a `Platform` per I2C bus, or one that picks the bus by address.
- **Bus index.** Give each bus its own `busIndex`: the `Platform`'s GPIO calls take it, so
  bus 1's start-up pulses bus 1's RESETn and not bus 0's, and its interrupt checks follow
  bus 1's IRQ line. Two buses with one index would reset each other's main node. Sub-nodes
  report it too (`SubNodeIdentity::bus`), so an app object shared by the buses can tell
  them apart.
- **One export per bus**, or one export with a chain per bus (`ImportSsPlus()`'s second
  argument), each into its own `BoardConfig` and host script.
- **Host scripts.** Each bus runs its own host script (its early part at `Start()`, the
  rest when its main node is up). If the buses share a peripheral on the host I2C bus,
  only one script should program it.
- **Different chips.** Each bus may have its own main-node chip (e.g. an AD2437 bus and
  an AD2428 bus), with its own chain.

```cpp
a2b::BoardConfig front;
a2b::BoardConfig rear;
a2b::FixedOpList<64> frontScript;
a2b::FixedOpList<64> rearScript;
a2b::ImportSsPlus(sFrontBus, 0, front, frontScript);   // its main node at 0x68/0x69
a2b::ImportSsPlus(sRearBus, 0, rear, rearScript);      // its main node at 0x6A/0x6B
front.busIndex = 0;
rear.busIndex = 1;
a2b::Network frontBus(platform, front);
a2b::Network rearBus(platform, rear);
```

(Two exports can't be linked into one program as they are, since each defines
`sBusDescription`: rename one, or export both chains from one SigmaStudio+ project.)

TODO(test on hardware): two buses have run on the simulator only (scenario 15); golden
G12 needs a bench with two main nodes. AD2437 dual parallel buses, where the second bus
follows the first, are a different thing: `BusPair`, not built yet.

## How the host script runs

`ScriptRunner` (internal) runs one operation after another on each `Poll()` until it
reaches a wait, then returns. The next poll after the wait's end carries on. The first
failure stops the script. Nothing is retried.

A peripheral write longer than 32 data bytes is split into several transfers. Each part
starts at its own sub-address: every `addrIncrement` data bytes (from the export's
`nAddrIncrement`, e.g. 5 for a SigmaDSP program word) advance the sub-address by one. The
ADI's stack sent a host peripheral's write as one transfer however long it was, so the
I2C traces differ in transfer boundaries only.
