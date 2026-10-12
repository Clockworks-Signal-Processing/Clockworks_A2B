# Design decisions

What the C++ stack is meant to do, why it is built the way it is, and the decisions that
shaped it. The hardware lessons are in [hardware_notes.md](hardware_notes.md), the
architecture in [architecture.md](architecture.md), what is left to do in the
[roadmap](../../docs/cpp-stack/ROADMAP.md).

The **prime goal is a stack that is simpler to use** than ADI's PnP stack, while keeping its
`a2b_pnp_*` capabilities. "ADI's stack" below means ADI's A2B stack and PnP layer
(release 1.3.0), which this stack replaces; its function names appear where they help
someone migrating.

## Requirements and scope

### Chips
- AD2427 and AD2428 (AD242x), AD2433 and AD2437 (AD243x). Any may be a sub-node. The main
  node is an AD2428, AD2433 or AD2437: the AD2427 is not main-node capable (AD2426/27/28 data
  sheet Rev. F, Table 1: "Main node capable: No"), and `Network::Start()` refuses it.
- Mixed chains are expected, for example an AD2437 with AD2427 and AD2428 nodes, or an AD2428
  main node with AD2437 sub-nodes.
- The first deployed systems are either all AD2437, or an AD2428 main node with AD2428 and
  AD2427 sub-nodes. That is not a limit: mixed chains of the four chips are a requirement.
- The chip list is these four only: no AD2430/AD2438 and no AD244x.
- Every main node takes up to 16 sub-nodes (current data sheets; a mixed chain is held to the
  most restrictive one, also 16). `BoardPolicy::maxSubNodes` is the system design's limit and
  defaults to 15, because the 16th node has given trouble on hardware; arrays are sized for 16.
- SPI works only when the main node is an AD2433 or AD2437. The stack doesn't police this: the
  application knows its main-node hardware, and an SPI call on a main node without SPI returns
  an error.
- The power configuration (CFG0 or CFG4) matters to the hardware, not the software. The stack
  applies each node's setting as given, from its module info or from the `LinkProfile`, and
  has no rules about mixing them.

### Hot-plug PnP only
The main node is configured from the application. Sub-nodes are always found by periodic
blind discovery. There is no fixed-topology mode.

### Configuration sources
- **Main node: a SigmaStudio+ (SS+) C export.** It covers the main-node settings and the
  programming of the main node's peripherals (for example the AB0020's ADAU1761).
- **Sub-nodes: everything comes from their EEPROM module info.** That includes the register
  values (I2SCFG, I2SGCFG, PDMCTL, CLKOUT, SPICFG), channel groups, power data and peripheral
  scripts.
- Not done: matching SS+ sub-node types against the module type in the EEPROM.
- No protobuf/BDD (bus description data) at run time.

### "Ignore EEPROM" for provisioning
Nodes can be discovered without using their EEPROM content, because on first power-up the
EEPROMs may be blank; the application then supplies each node's module info. Checking that an
EEPROM is present, writing it and building images are all **application code**, built on the
stack's peripheral read and write.

### Peripherals
The stack provides read, write and write-read only. I2C works; SPI is not yet supported, but
the address type reserves room for it (slave-select on one or more node GPIO lines).

### Several buses
One `Network` object per bus, N independent buses per host. The code and the simulator tests
are in place; hardware with two buses has not been tried. Dual parallel AD2437 buses (paired
chips on each module, bus 2's main node clocked from bus 1's) are designed, not built; see the
roadmap.

### Interconnects
- UTP: single-step. RJ45 and XLR: two-step. Custom: single-step, with power on wires other
  than the A2B pair. Chains may mix types.
- **B-side polarity (XCVRBINV)** is a per-link setting on every supported chip. Only the RJ45
  profile defaults to inverted; UTP, XLR and Custom default to off, and the application can
  change any of them. Per the TRMs, a node's XCVRBINV is set before its SWCTL.ENSW, and later
  CONTROL writes (for example NEWSTRCT) must keep it.
- Two-step is an option the host application chooses per link. Its sequence: read the EEPROM,
  then turn on the CFG4 (24 V) power. The XLR code is carried over untested (no XLR hardware).

### When to stop looking for nodes
That is the application's decision. The stack doesn't know where the chain ends: the app sets
the maximum number of sub-nodes (default 15) and can stop hot-plug probing whenever it likes
(`PauseDiscovery()`).

### API
A C++ core. A thin C facade that keeps the `a2bpnp.h` names and types is planned.

## Why C++

The rewrite meets the "simpler to use" goal, with four caveats.

**Why it helps**
- ADI's "plugins" are hand-made C++ in C: each plugin is a struct of function pointers driven
  by a message router, a job scheduler, per-plugin mailboxes and memory pools. About half of
  the core stack exists only for that model, and the plugins are linked statically anyway.
- The size drops by about three-quarters: ADI's stack and PnP layer are about 30k lines of live
  C plus about 24k lines of generated or duplicated platform files; the C++ core is about 8k
  lines plus about 3k lines of simulator and tests.
- ADI's PnP layer is tangled into the stack: it includes private stack headers, relies on
  `A2B_FEATURE_PNP` patches throughout, writes a continue flag back into a notification
  payload, and uses the BDD as its live register image.
- C++ builds on all three targets (RP2040, Linux, Windows).

**What an application developer uses**: a `Platform` (I2C, time, GPIO), a board from an SS+
export, `Network::Start()` and `Poll(nowMs)`, and a listener for events. Everything else is
optional and has a default. The minimal example stays minimal, with no line-count target that
would push application code into the stack.

**Caveats, and how they are handled**
1. **Subclass node roles, not chip families.** Chip differences are an internal capabilities
   table (`ChipCaps`); users derive from `SubNode`, `Platform` and the routing policy.
2. **Deriving doesn't make upgrades free** (the fragile base class problem). The procedures
   stay non-virtual; hooks are few, never do I/O, and are documented as stable.
3. **Much of what you need is new work, not a port.** ADI's PnP path does not work for the
   AD2427, AD2428 and AD2433 (it hard-codes module product 0x0037, seeds every sub-node as an
   AD2437, and writes crossbar registers the AD242x lacks), and it has no dual-bus code.
4. **Discovery timing is the real risk.** ADI's `discovery.c` encodes timing and ordering that
   are easy to lose: the MSTR_RUNNING check against a 25 ms reset timer, a last-chance
   interrupt read, a 100 ms high-power-switch settle, 50 or 70 ms discovery timeouts, 100 or
   250 ms power-diagnostic settles, and the empty-probe rewrites. The new stack was checked
   against I2C traces captured from ADI's stack (see [timing.md](timing.md) and
   [testing.md](testing.md)).

## Coding standard

The base is the Google C++ Style Guide, with these overrides:
1. **Member variables** take `m_` plus a capital (`m_FrameCount`).
2. **Local variables and parameters are lowerCamelCase** (`frameCount`), which suits long
   hardware register names. Public struct fields (plain data, no `m_`) are lowerCamelCase too
   (`mainProduct`).
3. **Documentation comments are Doxygen style.**
4. **File extensions:** `.h` and `.cpp`.
5. **Function documentation lives on the definition in the `.cpp`**, because you debug in the
   `.cpp`, so the docs should be there. (Google, like LLVM, puts it on the declaration; Doxygen
   merges the two places either way.)
   - In the `.cpp`: a `/** ... */` block (no `@brief`) above every function definition, private
     helpers included. Public functions get the description, `@param` and `@return`. The
     file's header comment carries the class overview.
   - In the `.h`: a short `///<` summary after each declaration, on its last line if the
     declaration wraps, and a one- or two-line `///` brief above each class. Destructors and
     overrides take none: an override's docs are its base's (Doxygen copies them).
   - Exceptions: interfaces, templates and inline functions have no `.cpp` body, so their docs
     stay in the header: `Platform` (`include/a2b/platform.h`), `NetworkListener`,
     `BoardHooks`, `ModuleInfoSource`, `UsbiLink`, `sim::I2cDevice`, `Make<T>`, `types.h` and
     inline accessors. Struct fields, enumerators and constants keep their `///<` comments.

Everything else follows Google: file names are lower_case_with_underscores, types and
functions are UpperCamelCase, constants and enumerators are `kName`, the namespace is `a2b`,
and there are no exceptions or RTTI. `.clang-format`, `.clang-tidy` (naming) and a Doxyfile
enforce the rules.

Repository rules: no customer or product names (boards are "reference board", AB0020, AB0310
and so on); files derived from ADI's keep the `LICENSE_ADI_BSD.txt` header; new code says
"main node" and "sub-node".

## Architecture

### Public classes

Everything a user touches or derives from. The last column names ADI's counterpart.

| Class | Role | ADI's counterpart |
|---|---|---|
| `Platform` | Interface for I2C write/write-read, `NowMs()` (wrap-safe), reset/IRQ GPIO, `PowerGood()`, log, optional `Lock()`/`Unlock()` | the three PAL folders; `getPGSignal` |
| `BoardConfig` | Main-node registers and audio groups, the main node's peripheral scripts, I2C addresses for each bus, default `LinkProfile`, timings and policy (max sub-nodes, bandwidth, power budget). Filled by `ImportSsPlus()` or by hand. `BoardHooks::BeforeMainNodeInit()` hook. | `ADI_A2B_MASTER_NCD`, `a2bpnp_MainNodeAppInfo`, `a2b_pnp_ConfigureMainNode` |
| `Network` | One per bus. `Start()`, `Poll(nowMs)`, `Stop()`, the routing API, `RejectNode()`, `PauseDiscovery()`, `SetLinkProfile(position, profile)`, peripheral and raw register access. Owns fixed storage for up to 16 nodes and the routing plan. | `a2b_pnp_Task`, the `a2bpnp.h` APIs, `a2b_AppDetectBusDrop`, `StartBlindDiscovery` |
| `NetworkListener` | App interface: `OnEvent(Network&, Event)`; `OnNodeIdentified(NodeReview)` returns a `RejectReason` (read-only; `kNone` accepts); `OnAudioMute()`/`OnAudioUnmute()`, called from `Poll()` around each routing plan, not queued. | `PNP_CALLBACK`, `INTR_CALLBACK`, `ERR_CALLBACK` |
| `SubNode` | Hooks, all without I/O: `DownstreamLink()`, `InterruptMask()`, `TweakRegisters(RegisterImage&)` (edits values before they are written), `OnConfigured(OpList&)` (may append operations), `OnRemoved()`, `TypeTag()` (no RTTI). | `dscvryNodeComplete`, `PreSlaveInit`, `FinalMasterSetup` |
| `NodeFactory` | Keys are module vendor/product IDs from module info, plus one optional app function (which may use position or CHIPID). `Make<T>` builds the node in a fixed slot, with a `static_assert` on its size. | plugin `open()` polling |
| `ModuleInfoSource` | Interface. The default `EepromModuleInfoSource` scans 0x50-0x54, validates the 0xAA marker and the header CRC-8 at 0x1F, and parses. "Ignore EEPROM" means the app installs its own source. The app may return a raw module-info image, which goes through the same parser. | `a2b_ReadVerify_ModuleInfo` |
| `LinkProfile` | Discovery `Single`/`TwoStep`, power `OnA2BPair`/`SeparateWires`/`None`, `BSideInverted` (XCVRBINV), the PG check and which sub-node GPIO carries PG (default GPIO7), the high-voltage check. Presets `Utp`, `Rj45`, `Xlr`, `Custom`. **Precedence:** `Network::SetLinkProfile(position)`, then the upstream node's `DownstreamLink()`, then the board default. The EEPROM cable-type byte is not used: nothing reads it, and ADI's own images set it inconsistently. | `bTwoStepDisc`, `mainXcvrBInv`/`subXcvrBInv` |
| `PeripheralAddress` | `Network::PeripheralWrite`/`Read`/`WriteRead(Node, Addr, ...)`: main node over the host I2C, sub-node through the CHIP register. `Addr` is I2C 7-bit, with an SPI form reserved. | `a2b_i2cPeriph*`, `a2b_pnp_I2CReadPeri`/`I2CWritePeri` |
| `RoutingPolicy` | Hooks `RespCycles()` and `MapGroups()`, which gives each node's `ChannelMap` (a crossbar or TDM order, and the channels its port carries). The plan is a `RoutingPlan` (a `NodeRouting` per node), built by the internal `RoutingPlanner` (AD243x crossbars, AD242x TDM order). | `AutoCalcSlot`, `crossbarCalc`, `calcMstrRespCycs`, `UpdateSlots` |
| `ConnectionTable` | The connections, by chip ID or position, 32 at most; a sink takes one source; kept through `Start()`; two modules with one chip ID (blank: zeros) are told apart by position. | `a2bpnp_conn_mng.cpp` |

There is no `MainNode` class. Each hook such a class would have already has a `BoardConfig`
counterpart (`BoardHooks::BeforeMainNodeInit()` for `TweakRegisters()`, the host script for
`OnConfigured()`, `defaultLink` or `SetLinkProfile(0)` for `DownstreamLink()`, the image's
INTMSK for `InterruptMask()`), so a second way would make the API harder, not simpler.

### Internal classes
Not in public headers: `Transport` (caches NODEADR, CHIP and the MMRPAGE register page; every
write to them goes through it), `ChipCaps` (a capabilities table, no per-chip ops classes),
`Discovery` (a state machine), `ScriptRunner` (runs `OpList`s across polls), `InterruptPoller`,
`NodeSetup`, `ModuleScript` and `RoutingPlanner`.

### Design rules
- **No blocking I/O outside `Poll()`.** `Configure()` and `PrepareDownstream()` build an
  `OpList` (register writes, checked reads, waits, peripheral writes) that `ScriptRunner`
  executes across polls. Hooks only edit values or append operations.
- **Memory.** No heap in the core after `Start()`: node slots, the event queue, `OpList` and
  the plan are all fixed storage inside `Network`. Examples and development tools may use the
  heap (the Pico SDK's malloc panics when out of memory); the stack library stays heap-free.
  The RAM per `Network` and the stack high-water mark are measured against the RP2040 budget
  ([ram.md](ram.md)). ADI's `gApp_Info` alone is 107,744 B, and its `.bss` plus `.data` about
  163 KB of the RP2040's 264 KB.
- **Threads.** One thread makes all calls on a given `Network`. `Platform::Lock()` covers a
  shared I2C controller.
- **Events** are queued and dispatched at the end of `Poll()`, so handlers may call any API.
  `Poll()` from a handler returns at once, and a round gives out at most 64 events, so a
  handler that queues an event per event can't hold `Poll()`.
- **The stack branches on chip capabilities** (`ChipCaps`), never on product IDs.

### Customisation example

```cpp
/** @brief Module whose B side is an RJ45 link: two-step discovery, port-B polarity swapped. */
class Rj45Module : public a2b::SubNode {
 public:
  using SubNode::SubNode;
  a2b::LinkProfile DownstreamLink() const override { return a2b::LinkProfile::Rj45(); }
};
factory.ByModule(0x00AD, 0x0037, a2b::Make<Rj45Module>);   // module vendor/product IDs
```

More in the [cookbook](cookbook.md).

## Behaviour kept from ADI's stack

Each item is ported, and has a rule in the trace comparison ([testing.md](testing.md)).

- SWCTL written 0x21/0x11 on every node around each blind probe, in each chip's own switch mode
  (0x01 on the AD242x).
- BECNT reset every second: a broadcast, as ADI's stack wrote it, or the main node's register
  while there are no sub-nodes.
- The high-power-switch mode-4 open-detect timer is compiled out of ADI's PnP builds
  (`#ifndef A2B_FEATURE_PNP`), so there is nothing to port.
- **AD243x main-node start-up, the same on the first discovery as on a rediscovery:**
  1. Soft reset with CONTROL.MSTR = 0.
  2. Wait at least 25 ms (`nRediscWaitTime`, never less than `A2B_SW_RESET_DELAY`).
  3. Write I2SGCFG.INV (if used), then CONTROL.MSTR.

  The wait covers SUSTAIN; see [hardware_notes.md](hardware_notes.md#sustain-after-a-soft-reset).
  Hosts without a RESETn pulse at start-up find the main node still running from the last
  session, so this sequence runs even when `Platform::SetMainNodeReset()` is supported, and
  [timing.md](timing.md) lists the wait as a hard minimum.
- GPIO over distance (not yet on `Network`; see the roadmap).
- The power budget, **now enforced** in the default `OnNodeIdentified` policy (ADI's stack
  stores it and never uses it).

## Bugs fixed, not ported

ADI's stack has these defects; the C++ stack does the intended thing instead. Each has an
"intended difference" rule in the trace comparison and the routing tests.

- Main-node config wiped on rediscovery.
- `DeInit` use-after-free.
- `ReadVirtualIOPort` precedence.
- `ClearGPIOOverDistance` writes the wrong registers.
- `RejectNode` underflow.
- SPI APIs return raw stack codes.
- Notifier leak in `discovery.c`.
- Main and sub-node chip identity mixed.
- `ImportConnections` is declared but never defined. Here it replaces the connection table with
  a saved list, all or nothing.
- EEPROM `configType` crash.
- Hard-coded 48 kHz and 4 m per hop in the routing calculation. Here `routing.frameRateHz` and
  `cableDm` are settings, and `RoutingPolicy` can override the response cycles.
- RESPCYCS arrays sized 16 but overrun at 16 sub-nodes. Here 16 entries, positions 0-15.
- The routing calculation (`routing_planner.cpp`): ADI's keeps every RX channel (RXMASK 0xFF),
  derives DNOFFSET/UPOFFSET from stream indexes, carries frame offsets and crossbar indexes from
  one node to the next, takes a group's ID as its index, and has no 32-slot check.
- Ignore-EEPROM path: `NEW_NODE_CONNECTED` fires after a rejection, and there is a null
  dereference.
- Unchecked device-name copies.
- `getPGSignal` called without a null check.
- The EEPROM check requires module ID 0x00AD/0x0037 and has no CRC check. Here any module ID
  passes and the header CRC-8 is checked.
- Two fixes from ADI's 19.11.1: pwrdiag ENSW masking and the `spi.c` length check.

## Dropped

- self-discovery
- comm channel
- custom authentication
- Modified, Optimized and Advanced discovery modes
- AD244x content protection, and AD244x parts
- AD2430/AD2438 parts, and their VMTR path (the AD2437 VMTR use is kept)
- the AD241x cross-talk fix
- the demo audio routing
- the EEPROM cable-type byte

## Policy decisions

- **`maxSubNodes` defaults to 15**, with arrays sized for 16 (see Chips).
- **No `MainNode` class** (above).
- **Sample rates:** 44.1, 48, 96 and 192 kHz. 44.1 and 48 kHz are the superframe (SYNC) rate,
  `routing.frameRateHz`; the planner and the response cycles take either, and a sub-node's I2C
  clock follows through I2CCFG.FRAMERATE (the stack sets it at 44.1 kHz). 96 and 192 kHz are
  not bus rates: a *sub-node's* I2S/TDM port runs at 2x or 4x the superframe rate through
  I2SRATE (0x43, REDUCE = 0), so a 96 kHz stereo stream is four slots per superframe on that
  node; the main node's port stays at the SYNC rate. A sub-node's 96/192 kHz comes from its
  module image; a module with no EEPROM is 48 kHz. There is no per-position hook or console
  setting.
- **No reduced data rate and no bus monitoring (sniffing).** A module image with SHARE, REDUCE
  or any other rate field fails as invalid data (the node is rejected with `kBadModuleInfo`),
  so nothing runs on a rate the routing doesn't plan for.
- **Cable length** defaults to 4 m on every link (AD2431-35 TRM: adequate when the lengths are
  unknown, as each node adjusts its response time during discovery, -4 to +15 bit times), with a
  per-link override (`Network::SetCableLength(n, dm)`, 0.1-30 m; 30 m is the AD2437's longest
  link, 15 m the AD242x's). A measured cable length is not yet used.
- **Every probe's DISCVRY** is the response-cycle formula's value for its position, not ADI's
  main-node RESPCYCS less 4 per node. SigmaStudio+ takes RESPOFFS 254 ("accurate"); the stack's
  value is slightly lower, and both lie in the valid window.
- **The power budget** is in mW and mA, as ADI's API and ADI's XLR image suggest
  (`BoardPolicy::maxBusPowerMw`, `maxBusCurrentMa`, 0 = no limit).
- **A rejected node** is switched off and the chain ends before it, but its position is probed
  again every `BoardTimings::rejectedProbeMs` (2 s): a different module is taken in as usual.
- **Muting:** the local mute (the default) silences rather than playing wrong channels; see
  [cookbook.md](cookbook.md#routing) and [hardware_notes.md](hardware_notes.md#muting).
- **The AB0204 default** (an AD2427 with no EEPROM image is taken to be a four-microphone
  AB0204) lives in the Clockworks example (`examples/bench/clockworks_boards`), not the stack.
- **The fixed and minimal examples stay at one bus**, to show the least an app needs; the quick
  start, main-node guide and cookbook show the few lines a second bus takes.
- **Trace replay was dropped.** Replaying captured traces through the simulator was not built;
  the bench runs checked on hardware what the replays would have. A golden is captured again
  only if a behaviour diverges later. `trace_diff.py` still compares two traces of the same
  scenario.
- **Peripheral transfers:** each module-script unit is one transfer, because the image doesn't
  give the peripheral's sub-address step; `Network` holds a 4096-byte buffer (ADI's stack had
  6000 B static).
- **Export header merging:** the SS+ import and loader use one set of ADI headers on desktop
  builds, `tools/SS+reference/adi`. No ADI header compiles an SS+ 2.3.0 export, so that
  `adi_a2b_busconfig.h` is ADI's 2023 header with the fields of its older one and of SS+ 2.3.0
  added, and `adi_a2b_datatypes.h` is single-core. See [bench_console.md](bench_console.md).
