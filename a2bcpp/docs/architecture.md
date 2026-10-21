# Architecture

How the C++ stack is built and how it runs: the classes, the `Poll()` model, the state
machines, when things happen, the events, and several buses on one host. The
[quick start](quick_start.md) shows how to use it; this page shows how it works inside,
for anyone who customises, ports or debugs it. Power diagnostics and dual buses are not
built yet (see the [roadmap](../../ROADMAP.md)). The reasons behind
the design are in [decisions.md](decisions.md); what the hardware taught us is in
[hardware_notes.md](hardware_notes.md).

## The pieces

```
  the app
    |  Start(), Poll(nowMs), Stop(), RejectNode(), PauseDiscovery(), peripheral and
    |  register I/O, ...
    v
  Network (a2b/network.h) ............................. one per bus, fixed storage, no heap
    |-- InterruptPoller   INTSRC/INTTYPE every 20 ms, or when the IRQ line says
    |-- Discovery         one probe: link set-up, DISCVRY, DSCDONE, identification
    |-- NodeSetup         a found node's settings, peripheral scripts (ModuleScript), ops
    |-- ScriptRunner      runs OpLists across polls (host script, nodes' operations)
    |-- ConnectionTable   the app's connections, by chip ID
    |-- RoutingPlanner    a plan for the chain: slots, masks, offsets, crossbars
    |-- EepromModuleInfoSource   the default module-info source
    |-- the sub-nodes' SubNode objects (NodeFactory picks the class), the event queue
    '-- Transport         every register and peripheral access; caches NODEADR, CHIP, MMRPAGE
          |
          v
  Platform (a2b/platform.h) ............................ the host: I2C, clock, GPIO, log, lock
```

| What the app sees (`a2bcpp/include/a2b/`) | What it is |
|---|---|
| `Network` | One bus, from its main node's start-up to its sub-nodes' upkeep |
| `Platform` | The host interface the app implements, or one of `platforms/` ([porting guide](porting.md)) |
| `BoardConfig`, `RegisterImage`, `OpList` | The main node's settings, host script, timings and policy ([main-node configuration](main_node_config.md)) |
| `NetworkListener`, `Event`, `NodeReview` | What the bus tells the app, and the app's say on each node |
| `SubNode`, `NodeFactory` | A sub-node's object, of a class the app may choose ([customisation cookbook](cookbook.md)) |
| `ModuleInfoSource`, `EepromModuleInfoSource`, `ModuleInfo` | Where a sub-node's module info comes from, and what it holds |
| `LinkProfile` | How a link is brought up |
| `Connection`, `ChannelGroups`, `RoutingPlan`, `RoutingPolicy` (`a2b/routing.h`) | Audio routing: what the app connects, each node's groups, the plan written, the response cycles and each node's channel map (cookbook, "Routing") |
| `ToString()` (`a2b/strings.h`), `Crc8()` (`a2b/crc8.h`) | The enums as text for a log or console; ADI's CRC-8, to build or check a module-info image |

The rest is internal (`a2bcpp/src/`): `Network::Impl` (`network_impl.h`), `Transport`, `InterruptPoller`, `Discovery`,
`NodeSetup`, `ModuleScript`, `ScriptRunner`, `ConnectionTable`, `RoutingPlanner`,
`ChipCaps` (what each chip can do; the
stack branches on it, never on product IDs) and `detail/regdefs.h` (the registers).
`Network` keeps them in an `Impl` inside the object (`Network::kImplSize`), so the
public headers include none of them.

## The `Poll()` model

**All bus traffic happens inside `Network::Poll(nowMs)`.** The app calls `Start()` once
and `Poll()` often, every millisecond or so, from one thread. `Start()`, `Stop()` and the
calls that change the bus (`RejectNode()`, `ResumeDiscovery()`, `SetLinkProfile()`, ...)
send nothing: they record what to do, and the next `Poll()` does it. The peripheral
calls (`PeripheralWrite()` and friends) and the app's register access (`ReadRegisters()`,
`WriteRegisters()`) are the exception: they make their transfers at once.

**Nothing waits.** Every wait is a deadline (`TimeReached()`, wrap-safe on a 32-bit
millisecond clock) that each `Poll()` checks, so a wait ends at the first poll after its
time and the app keeps running in between. `Poll()` takes only as long as the I2C
transfers it makes. Most polls make none or a few. The longest are a node's EEPROM read
(the 144-byte start of its image at the node's 100 kHz, about 15 ms) and one unit of a
peripheral script (ADI's 4090-byte ADAU1701 program, about 0.8 s, as ADI's
blocking call took): `Poll()` runs at most one unit at a time.

**One `Poll()`, in order:**

1. The step the state calls for (below): a start-up step, a configuration step, or in
   `kDiscovering`/`kRunning`:
   1. the interrupt check, if due (every `interruptPollMs`, at once after an interrupt,
      gated by `Platform::IrqPending()`). MSTR_RUNNING restarts the bus; DSCDONE goes to
      the probe; anything else becomes a `kInterrupt` event, and one that may mean a
      drop asks for a chain check;
   2. the app's `RejectNode()`, if it asked;
   3. between probes: the chain check if due or asked for, then BECNT if due, then a
      routing plan if one is due (`kRunning` only), then the next probe if due;
   4. the probe, the set-up or the routing write under way, one step; nothing during a
      NEWSTRCT settle.
2. The events queued since the last round go to the listener (below).

## States

`Network::State()`, and what moves it (each change is a `kStateChanged` event):

```
 kStopped --Start()--> kStarting --MSTR_RUNNING--> kConfiguring --done--> kDiscovering
                         ^  |                          |                       |
                         |  '--------> kFailed <-------'                       | chain up
                         |                                                     v
                         '------- restart (from kDiscovering or kRunning) -- kRunning

 Start(), from any state: kStarting.   Stop(), from any state: kStopped.
```

| From | To | When |
|---|---|---|
| any | `kStarting` | `Start()` |
| any | `kStopped` | `Stop()` |
| `kStarting` | `kConfiguring` | MSTR_RUNNING: the main node's PLL locked |
| `kStarting`, `kConfiguring` | `kFailed` | the main node is not the board's chip or doesn't answer, no MSTR_RUNNING, or the host script failed (`Error()`) |
| `kConfiguring` | `kDiscovering` | the main-node image is written and the rest of the host script has run |
| `kDiscovering` | `kRunning` | the chain is up: a probe found nothing, a node was rejected, the most sub-nodes are in, or discovery is paused |
| `kDiscovering`, `kRunning` | `kStarting` | restart: MSTR_RUNNING again (a PLL relock), or a chain check found the main node reset or silent (then from RESETn) |

`kDiscovering` only covers the chain plugged in at start-up: once a probe finds nothing,
the bus is `kRunning`, and nodes found later at the end of the chain don't change the
state. A failed transfer once the bus is up doesn't fail it: the probe or set-up is
given up and the chain checked. Only a main node that doesn't answer after the restart
fails the bus (`kI2cError`).

### Start-up and configuration

```
 kStarting:   [host operations due before the main node (only at Start())]
                -> [RESETn low, resetPulseMs] -> [RESETn high, 2 ms] -> read IDs, soft reset
              (only at Start(), with the line)                       (MSTR = 0)
                -> wait rediscoveryWaitMs, >= 25 ms (SUSTAIN) -> I2SGCFG.INV, CONTROL.MSTR
                -> MSTR_RUNNING within discoveryStartDelayMs (one last check, then kFailed)
 kConfiguring: main-node image, in bursts -> the rest of the host script (ScriptRunner)
```

The same on every start, whether the host pulsed RESETn or the main node was left
running by an earlier session ([main-node configuration](main_node_config.md),
"Start-up"). A PLL relock restarts at "read IDs" (no RESETn); a main node that was reset
or stopped answering restarts from RESETn.

### Finding the chain

Between probes the `Network` is idle; a probe is one `Discovery`, then one `NodeSetup`:

```
 idle --due--> Prepare -----------> Settle 100 ms --> Probe --> wait DSCDONE (<= 70 ms)
                (once per upstream   (only if the          |        |
                 node: masks,         power config         |        +-- none: ENDDSC,
                 XCVRBINV, SWSTAT2)   was overridden)      |            switches back,
                                                           |            idle (500 ms)
           switches to mode 2 around it, ENSW, NODEADR, DISCVRY
 DSCDONE --> Identify: VENDOR..CAPABILITY, CHIPID, module info (source), its checks
         --> review: default policy, then NetworkListener::OnNodeIdentified()
         --> NodeSetup: TweakRegisters(), settings, peripheral scripts, OnConfigured() ops
         --> in the bus: kNodeAdded, and the next position is probed at once
```

A **two-step link** (RJ45 or XLR from an AD2437; Discovery's file comment has the
details) runs the probe twice around the review:

```
 Prepare: power-good (main: Platform::PowerGood(); a sub-node: its power-good GPIO, 7)
          low --> nothing probed, logged once; tried again in 500 ms
          upstream CONTROL with SWBYP (and XCVRBINV) --> Probe at 5 V --> DSCDONE
 --> Identify, module info --> review
 --> not CFG-4: kept at 5 V (SWBYP stays) --> NodeSetup
 --> CFG-4: switch opened, SWBYP cleared --> Prepare --> Probe (ENSW: 24 V) --> DSCDONE
          none: rejected, no 24 V
 --> same IDs and chip ID? (no: failed probe, chain check)
 --> VBUS (VMTR_VLTG1) >= 4 V, read again for up to 10 ms (no: rejected, no 24 V)
 --> NodeSetup --> in the bus
```

The 24 V check (the last step) also runs on a single-step link that asks for it
(`LinkProfile::checkHighVoltage`). A node rejected for no 24 V isn't remembered by its chip
ID, so its position is probed again from the first step every `rejectedProbeMs`.

A node that fails a check, the review or its set-up is switched off and becomes the
**rejected node**: the chain ends before it, and its position is probed every
`rejectedProbeMs` (2 s). The same chip ID there is switched off again straight after
CHIPID, without an event; another module is identified as usual.

**The error watch** (`Network::SetErrorWatch()`, off by default; the bench console turns it
on) is for finding out why a node drops. It works between probes, like the chain check:
- Each node, as it joins or when the watch is turned on, gets BECCTL counting every error
  type (BECOVF past 16) and the SRF-missed and BECOVF interrupts added to its INTMSK0
  (its link's INTMSK0 too, so the next probe keeps them). Turned off, both go back.
- At each BECNT clear (`becntResetMs`) every node's BECNT is read first and summed
  (`Network::BitErrorCount()`). The sum is an upper bound, not an error count: BECCTL counts
  header-count errors too, and a header-count error mostly raises a CRC error for the same
  line error, so one error can count twice. The TRMs advise leaving ENHDCNT clear for that
  reason; the watch takes it so that nothing goes uncounted, and a sum is meant for telling
  a quiet link from a noisy one, not for counting errors.
- When a probe fails, or a sub-node fails its chain-check reads, the stack reads the error
  registers of the main node and of every node before it (SWSTAT, INTPND0 to BECNT, NODE:
  never INTTYPE), then, before a drop, the failing node's IDs once more. The bits read are
  cleared, the report is kept (`Network::LastErrorReport()`) and `kErrorReport` is queued;
  the drop then goes ahead as before. The upstream node of a node that is really gone shows
  NODE.LAST and the SRF missed; a node that answers that last read was dropped while it
  could still answer.

Whether or not the watch is on, a failed access is logged with its node, register, tries
and time ("a2b: failed access: sub-node 3, 0x02 read, 3 tries, 1521 ms"): a NAK is quick,
while a host adapter that times out (the USBi) takes about 500 ms a try.

### Routing

```
 ApplyRouting() --plan built?--> no: RoutingError returned, nothing written
        |                yes
        v
 due --(kRunning, between probes)--> plan built again for the chain as it is now
        --> each sub-node's slot registers and RESPCYCS, then the main node's with
            SLOTFMT, DATCTL and I2SRRATE again, then CONTROL.NEWSTRCT
        --> NEWSTRCT settle (>= 1 ms, no transfers)
        --> every node's RXMASK0-7, then every node's TXXBAR0-31 (main node first;
            none on an AD242x)
        --> kRoutingApplied; its main-node RESPCYCS is the base of later probes' DISCVRY
 a node added or removed, Start() --> due again (kRoutingFailed if it can't be built;
                                      a failed write: chain check, retried 500 ms later)
```

A plan is only written between probes once the chain is up, so it never lands in the
middle of a probe or a node's set-up; a node found later at the end of the chain is
probed with DISCVRY from the plan's RESPCYCS, then the plan is written again with it.
SLOTFMT, DATCTL and I2SRRATE are auto-broadcast: a write to the main node reaches only
the sub-nodes discovered by then, so each plan writes them again (the image's values;
DATCTL 0x03, both directions on, if the image has none; I2SRRATE only if it has one).

### A position's life

```
          probe: nothing                              review or set-up fails
   empty <-------------- probing ---- DSCDONE ----> identified ----------> rejected
     ^  \_____ every 500 ms __/                         |                (switched off,
     |                                                  | set up         re-probed every 2 s)
     |                                                  v
     +--- dropped (chain check, after 250 ms or more) - in the bus --- kNodeAdded
     +--- RejectNode(), Start(), Stop(), restart ------/   (kNodeRemoved, OnRemoved(),
                                                             with every node after it)
```

When a node leaves, every node after it leaves too, each with its own `kNodeRemoved`
event, in order of position. The switch before a dropped node is opened, and its
position is probed again no sooner than 250 ms later, so a node plugged back in is
found (re-plug).

## When things happen

What runs while the bus is up, with the `BoardTimings` field that sets it. The
[timing guide](timing.md) has every wait, where each value comes from, and the
`trace_diff.py` rule that checks it.

| What | When | `BoardTimings` |
|---|---|---|
| Interrupt check (INTSRC, INTTYPE) | every 20 ms; at once after an interrupt; only while `Platform::IrqPending()` | `interruptPollMs` |
| Probe at the end of the chain | 500 ms after a probe that found nothing; at once after a node is found | `probeIntervalMs` |
| DSCDONE deadline | 70 ms after DISCVRY | `discoveryTimeoutMs` |
| Power-configuration settle | 100 ms after an override, before ENSW | none (the TRMs') |
| Chain check (every node's VENDOR and PRODUCT, the main node's CONTROL) | every 500 ms between probes; at once after a drop-like interrupt or a failed transfer | `chainCheckMs` |
| A dropped node's position probed again | 250 ms or more after its switch opened | `probeIntervalMs`, at least 250 |
| A rejected node's position probed again | every 2 s | `rejectedProbeMs` |
| BECNT cleared (broadcast) | every second, between probes | `becntResetMs` |
| Routing plan written | at the first poll between probes after `ApplyRouting()` or a change of the chain, once `kRunning`; a failed write tried again 500 ms later | `probeIntervalMs` (the retry) |
| NEWSTRCT settle (no transfers) | 1 ms or more after each CONTROL.NEWSTRCT: sub-node 0's probe, a routing plan | none (the TRMs') |
| Events given to the listener | at the end of every `Poll()` | none |

Probes, chain checks and BECNT never overlap: the last two run only between probes. A
check that falls due during a probe or set-up runs right after it.

## Events

Everything the app hears about comes through its `NetworkListener` (cookbook,
"Events"):

- **Queued, then given out.** A `Network` queues an `Event` where something happens
  (in a fixed ring of 32), and gives the queue to `NetworkListener::OnEvent()` at the end
  of `Poll()`, oldest first. The bus is then between steps, so a handler may call any
  `Network` function. Calls that change the bus take effect at the next `Poll()`;
  `Poll()` itself, called from a handler, returns at once.
- **Same round.** Events a handler causes (e.g. `Stop()`'s `kNodeRemoved` events) come in
  the same round, up to 64 per round, so a handler that causes an event per event can't
  hold `Poll()` forever; the rest wait for the next `Poll()`. If the queue overflows, the
  newest events are dropped and the log says so.
- **`Start()` and `Stop()`** queue their events too; they come at the next `Poll()`.
- **The review** (`OnNodeIdentified()`) is a call, not an event: it happens in the middle
  of a probe, before the node is set up, so it is read-only.
- **Without a listener** events are dropped and the default policy decides.

Order within a round follows the bus: e.g. a node whose 24 V downstream supply is
removed gives `kInterrupt` (its power fault), then `kNodeRemoved` for each node after it.
With the error watch on, `kErrorReport` comes just before those `kNodeRemoved` events.

## Several buses

Each bus is its own `Network` with its own `BoardConfig`, on one shared `Platform`:

```cpp
a2b::Network bus0(platform, board0);   // board0: busIndex 0, main node at 0x68/0x69
a2b::Network bus1(platform, board1);   // board1: busIndex 1, main node at 0x6A/0x6B
bus0.Start();
bus1.Start();
for (;;) {
  const uint32_t now = platform.NowMs();
  bus0.Poll(now);
  bus1.Poll(now);
}
```

- **Addresses.** Up to four main nodes share one host I2C bus: their ADR1/ADR2 pins give
  0x68/0x69, 0x6A/0x6B, 0x6C/0x6D or 0x6E/0x6F (the TRMs' "I2C Interface"). Each
  `BoardConfig` names its pair. A main node reaches its own sub-nodes and their
  peripherals through its own bus address, and broadcasts (BECNT) through it too, so one
  bus's traffic never reaches another's nodes.
- **Nothing shared but the `Platform`.** Each `Network` has its own `Transport` (its own
  NODEADR, CHIP and page caches), its own interrupt checks, chain checks, drops,
  restarts, rejected node, events and `RejectNode()`. The stack has no static data.
- **GPIO lines by bus index.** `BoardConfig::busIndex` goes to the `Platform`'s reset, IRQ
  and power-good calls, so each bus pulses its own RESETn and reads its own IRQ line.
  Give each bus its own index.
- **Shared app objects.** One `NetworkListener`, `ModuleInfoSource` or `NodeFactory` may
  serve several buses: `OnEvent()` gets the `Network`, and every node's
  `SubNodeIdentity::bus` is its bus index (in the review, the source's calls, the
  factory's chooser and `SubNode::Info()`).
- **Calls across buses.** A handler on one bus may call another bus's functions (its
  peripheral I/O, `RejectNode()`, `PauseDiscovery()`, ...), but not its `Poll()`: that
  would run one bus's work inside the other's.
- **Threads.** One thread polling every bus in turn (above) needs no locks. With one
  thread per bus, the `Platform` is called from both: implement `Lock()`/`Unlock()` for
  the shared I2C controller (the stack holds the lock for each whole register or
  peripheral access), make its other calls (`NowMs()`, `Log()`, the GPIO calls)
  thread-safe, and guard any app object two buses share.
- **RAM.** Each `Network` is about 15.5 KB on the RP2040, with no heap; one thread polling
  two buses needs 456 bytes of stack plus the I2C driver (`-Og`)
  ([RAM on the RP2040](ram.md)).

Simulator scenario 15 (`tests/scenario/test_sim_two_buses.cpp`) runs two buses on one
host I2C bus; two buses have not run on hardware yet. AD2437 dual parallel buses, where the
second bus follows the first, are not built (`BusPair`;
[decisions](decisions.md#dual-buses-buspair)).

## Memory

A `Network` keeps everything in fixed storage inside the object: sixteen sub-nodes'
identities, module info and channel groups, their `SubNode` objects (`kSubNodeSlotSize`
each), each link's settings and cable length, the event queue, the start-up's copy of
the main-node image, a node's set-up, a 4 KB buffer for peripheral scripts, and the
routing: the connections, the planner's working arrays, the plan written and the one
being built. Nothing is allocated, in the core or in the `Network`, after construction or
ever; the app's objects (`BoardConfig`, host script, listener, source, factory) are the
app's to place. [RAM on the RP2040](ram.md) has the figures.

## Where it is

| File | What |
|---|---|
| `src/network_impl.h`, `src/network.cpp` | `Network` and its `Impl`: the public calls, `Poll()`, events (`network.cpp`'s file comment is the design of the whole) |
| `src/network_startup.cpp` | Start-up, configuration, restarts |
| `src/network_chain.cpp` | The probe loop, chain checks, drops, rejections, links, the sub-nodes' objects |
| `src/network_routing.cpp` | Connections, routing plans, mutes, response cycles |
| `src/strings.cpp` | The enums as text (`a2b/strings.h`) |
| `src/discovery.cpp` | One probe, up to the node's identification |
| `src/node_setup.cpp`, `src/module_script.cpp` | A found node's settings, peripheral scripts and operations |
| `src/interrupt_poller.cpp` | Interrupt checks |
| `src/transport.cpp` | Register and peripheral access, the caches, the lock |
| `src/script_runner.cpp` | `OpList`s across polls |
| `src/eeprom_source.cpp`, `src/image_source.cpp`, `src/module_info.cpp` | Module info from the EEPROM or from images in memory, and its parser |
| `src/chip_caps.cpp` | The chips: features, switch modes, main-node capability |
| `src/connection_table.cpp` | The app's connections, kept by chip ID |
| `src/routing_planner.cpp` | A routing plan: each node's slots, slot masks, offsets and crossbars (AD243x), or TDM order (AD242x) |
| `src/routing.cpp` | The response cycles (the TRMs' Formula B), each node's channel map (`ComputeChannelMap()`) and the default routing policy |
| `sim/` | The simulator, a `Platform` for the tests ([testing guide](testing.md)) |

Each `.cpp` file's header comment has the details, with the TRM sections and ADI
functions it follows.
