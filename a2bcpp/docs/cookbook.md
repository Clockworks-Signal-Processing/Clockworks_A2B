# Customisation cookbook

How to change what the stack does for your modules, links and buses without changing
the stack.

Everything here is optional: an app with ADI-style modules that keep their module info
in EEPROM needs none of it ([quick start](quick_start.md)). How the stack runs, which
these recipes plug into, is in the [architecture guide](architecture.md).

| To | Use | Section below |
|---|---|---|
| Set up a module your own way | a `SubNode` class and a `NodeFactory` | Module classes, The hooks |
| Change the main node's settings at start-up | `BoardHooks::BeforeMainNodeInit()` | The main node |
| Give a link its own polarity or power | a `LinkProfile` | Link profiles |
| Give module info without EEPROMs, or mix | a `ModuleInfoSource` | Module-info sources |
| Hear what happens | a `NetworkListener` | Events |
| Decide which nodes join | `OnNodeIdentified()`, the power budget, `RejectNode()` | Accepting nodes |
| Stop looking for nodes | `PauseDiscovery()`, `maxSubNodes` | Stopping the search |
| Watch more interrupts | `InterruptMask()`, `kInterrupt` events | Interrupts |
| Read or write a node's registers | `ReadRegisters()`, `WriteRegisters()` | Raw register access |
| Route audio between nodes | channel groups, `AddConnection()`, `ApplyRouting()` | Routing |
| Change periods and limits | `BoardTimings`, `BoardPolicy` | Timings and policy |
| Run several buses | one `Network` each | Several buses |

## Module classes

The stack makes one `a2b::SubNode` object for each sub-node it takes in. To change how a
module is set up, derive a class from `SubNode`, override the hooks you need, and tell a
`NodeFactory` which modules get it:

```cpp
#include "a2b/network.h"
#include "a2b/sub_node.h"

/** A module whose B-side socket is wired inverted, and whose amplifier needs unmuting. */
class AmpModule : public a2b::SubNode {
 public:
  using SubNode::SubNode;
  static constexpr uint32_t kTag = 0xA3;

  a2b::LinkProfile DownstreamLink() const override {
    a2b::LinkProfile link = BoardLink();  // the board's, not the image's B port
    link.bSideInverted = true;            // CONTROL.XCVRBINV on this node
    return link;
  }
  void OnConfigured(a2b::OpList& ops) override {
    ops.Add(a2b::Op::PeripheralWrite(Position(), 0x2C, 1, 0x10, m_Unmute, sizeof(m_Unmute)));
  }
  uint32_t TypeTag() const override { return kTag; }

 private:
  const uint8_t m_Unmute[2] = {0x00, 0x01};  // lives as long as the node
};

a2b::NodeFactory factory;                                  // must outlive the Network
factory.ByModule(0x0C54, 0x0101, a2b::Make<AmpModule>);    // module vendor/product IDs
network.SetNodeFactory(&factory);
```

- **Keys** are the module vendor and product IDs from the module info
  (`ModuleInfo::vendorId`/`productId`), not the chip's. Up to 16 modules.
- **An app function** can choose first, e.g. by position or chip ID:
  `factory.SetChooser(fn, context)`, where `fn(const SubNodeInfo&, void*)` returns a
  maker (`a2b::Make<T>`) or `nullptr` to leave the choice to the table.
- **No heap.** The object lives in a fixed slot inside the `Network`. `Make<T>` checks at
  compile time that `T` derives from `SubNode` and fits `a2b::kSubNodeSlotSize` (64 bytes
  on the RP2040, 128 on 64-bit hosts); keep big data outside the object.
- **No RTTI.** `network.NodeObject(n)` gives the object; check `TypeTag()` before a
  `static_cast` to your class. Tags are yours to pick; a plain `SubNode` returns 0.
- A node nothing matches is a plain `SubNode`, which does what the stack does by default.

## The hooks

None of them reaches the bus: they edit values or add operations, which the stack runs
across polls. All are called from `Network::Poll()` (or `Start()`/`Stop()` for
`OnRemoved()`), on the thread that polls.

| Hook | Called | Default | Use it to |
|---|---|---|---|
| `TweakRegisters(RegisterImage&)` | Before the node's module-info settings are written | none | change, add or drop register values (registers the stack owns are skipped) |
| `OnConfigured(OpList&)` | After those settings and the module's peripheral scripts; the node counts as found only once the operations are done | none | add up to 8 register writes, peripheral transfers or waits (their data must live as long as the node); a failure rejects the node (`kConfigFailed`) |
| `InterruptMask()` | When the node is found, for its INTMSK0/1, written before it first powers the next link | INTMSK0 = PWRERR, INTMSK1 = 0 | watch other interrupts: they come as `kInterrupt` events; enabling SRF missed (`INTMSK0` bit 6) has a drop after this node found at once instead of by the next chain check |
| `DownstreamLink()` | When the node is found, and before each probe of the next position | the link its image's B port type gives (`LinkProfile::ForBPort()`); `BoardLink()` is the board's | give the link to the next node its own profile (below) |
| `OnRemoved()` | The node left the bus: it or a node before it dropped, `RejectNode()` for it or a node before it, `Start()`, `Stop()`, a restart (PLL relock, main node reset) | none | forget app state about the node (it must not reach the bus: the node may be gone); its object is destroyed next |
| `TypeTag()` | By the app | 0 | tell classes apart without RTTI |

A node rejected by the review or during its set-up is destroyed without `OnRemoved()`
(or never made): it never joined.

**The main node** has no class of its own: its hooks are in `BoardConfig` (next
section).

**Stable hooks.** The hooks above, their signatures and when they run are the stable
interface ([decisions](decisions.md#why-c), caveat 2: the procedures stay non-virtual). A
planned hook (`Partner()` for dual buses) will come with a default, so a class written now
keeps compiling and working.

## The main node

The main node's counterparts of the sub-node hooks are in its `BoardConfig`
([main-node configuration](main_node_config.md)):

| Sub-node hook | Main node |
|---|---|
| `TweakRegisters()` | `BoardHooks::BeforeMainNodeInit(RegisterImage&)`: edits this start-up's copy of `BoardConfig::mainNode` |
| `OnConfigured()` | `BoardConfig::hostScript`: the board's own peripherals, run once the main node is up |
| `DownstreamLink()` | `BoardConfig::defaultLink`, or `SetLinkProfile(0, ...)` |
| `InterruptMask()` | INTMSK0-2 in `BoardConfig::mainNode` (the stack adds DSCDONE to the image's INTMSK2, which the image must have) |

`BeforeMainNodeInit()` runs at every start-up, `Start()` and each restart (a PLL relock,
a main node that was reset), before the first register write, so a setting that depends
on the moment can change there without touching the `BoardConfig`:

```cpp
/** Picks the TDM slot format from a strap the host reads at each start-up. */
class StrapHooks : public a2b::BoardHooks {
 public:
  void BeforeMainNodeInit(a2b::RegisterImage& image) override {
    image.Set(kSlotfmt, ReadStrap() ? 0x44 : 0x22);   // SLOTFMT, register 0x10
  }
};

StrapHooks hooks;            // must outlive the Network
board.hooks = &hooks;
```

Registers the stack owns (NODEADR, DISCVRY, CONTROL's command bits, SWCTL.ENSW, ...) are
skipped when the image is written, so editing them does nothing.

## Link profiles

A `LinkProfile` says how the link from an upstream node's B side to the next sub-node is
brought up. Its B-side polarity (`bSideInverted`, CONTROL.XCVRBINV) and its power source
(`power`: `kOnA2bPair` closes the upstream node's switch, `kSeparateWires` and `kNone` leave it
open and set the upstream node's CONTROL.SWBYP, which turns its B side on without the switch)
apply. Presets: `Utp()` (single-step, power on the pair), `Custom()` (single-step, power on
separate wires), `Rj45()` (two-step, B side inverted) and `Xlr()` (two-step, straight).

**Two-step links.** `Discovery::kTwoStep` finds the node at 5 V first, with the upstream
node's CONTROL.SWBYP set (an RJ45 cable carries 5 V on its data pair), reads its module
info and asks the review (`OnNodeIdentified()`); only then does it close the 24 V switch,
find the node again and check its VBUS with the node's voltage monitor. It needs the
upstream node's high-power switch (an AD2437); from any other node the link runs
single-step, without the two-step checks, and the log says so. A node whose power
configuration isn't CFG-4 stays at 5 V (a safety path: an RJ45 link joins two AD2437
nodes on the Clockworks boards, [boards](boards.md)). The checks, on by default in `Rj45()` and `Xlr()`:
- `checkPowerGood`: the upstream board's power-good before each first step, the main
  node's through `Platform::PowerGood()`, a sub-node's on GPIO `powerGoodGpio` (7). While
  it is low nothing is probed there (logged once).
- `checkHighVoltage`: VBUS at least 4 V once the node is at its final power; else it is
  rejected (`RejectReason::kNoHighVoltage`, also when it isn't found at 24 V). Its chip ID
  isn't kept, so the position is probed from the first step again every 2 s and the node
  joins once the 24 V is back. A single-step link may ask for it too (`Utp()` with
  `checkHighVoltage = true`); a node without a voltage monitor (AD242x) isn't checked.

A node on its own supply that still draws the bias current (its A side DC-coupled) is
found over a `Utp()` link too. One that draws none, such as a galvanically isolated A
side, needs `Custom()`: the upstream switch never finishes closing onto it, so the
upstream node passes it no frames (an AD242x reports INTTYPE 12, open line), and the
TRMs ("Data-Only and Power-Only Bus Operation") set SWBYP instead of ENSW. See the [hardware notes](hardware_notes.md#open-line-inttype-12-and-13).

The link to sub-node n takes its profile from the first of:

1. **The app, per position:** `network.SetLinkProfile(n, profile)` (n = 0 is the main
   node's link). It outlasts `Start()`; `ClearLinkProfile(n)` removes it.
2. **The upstream node:** sub-node n - 1's `DownstreamLink()`. By default it is the link the
   node's image gives: its B-side cable type (0x28), the module's B port, on its chip
   (`LinkProfile::ForBPort()`; [provisioning](provisioning.md)):
   - UTP: single-step, straight;
   - ADI's XLR or RJ45 on an AD2437: two-step; on any other chip, UTP;
   - RJ45-CUSTOM, RJ45-CUSTOM-INV, CUSTOM: no handler yet (`CustomBPortLink()` in
     `src/link_profile.cpp`, "Unsupported B port type"): the chain ends there.

   A node class overrides it for a module whose link its image doesn't describe.
3. **The board:** `BoardConfig::defaultLink`, the main node's link into sub-node 0
   (`ImportSsPlus()` inverts its B side only if the export's main-node CONTROL sets
   XCVRBINV, as an export for RJ45 links does). For a bus whose links are all alike, give
   every position the board's link: `SetLinkProfile(n, board.defaultLink)` for each n, as
   the minimal and fixed examples do.

```cpp
network.SetLinkProfile(1, a2b::LinkProfile::Rj45());  // two-step, B side inverted
a2b::LinkProfile rj45 = a2b::LinkProfile::Rj45();     // an RJ45 link run single-step
rj45.discovery = a2b::LinkProfile::Discovery::kSingleStep;
rj45.checkPowerGood = false;
rj45.checkHighVoltage = false;
network.SetLinkProfile(2, rj45);                      // the link into sub-node 2
network.SetLinkProfile(3, a2b::LinkProfile::Custom());  // sub-node 3 has its own supply
```

The profile is looked up before every probe of that position, so a change applies to
the next probe: the upstream node's link is set up again (masks, polarity, the
power-configuration check). A link whose node is in the bus keeps its settings until the
node is found again. An unsupported profile is refused by `SetLinkProfile()`
(`Result::kNotSupported`); a link a `DownstreamLink()` makes unsupported is not probed, and
the log says so.

## Module-info sources

A sub-node's module info (settings, power configuration, peripheral scripts) comes from
a `ModuleInfoSource`:

- **Default: the EEPROM** (`a2b::EepromModuleInfoSource`). It tries 0x50 to 0x54 on the
  node's I2C port, takes the first EEPROM whose byte 0 is the marker 0xAA, and reads the
  image from it: the start when the node is identified, the peripheral blocks while it is
  set up. An absent or blank EEPROM gives no module info (`kNoModuleInfo`); a corrupt one
  bad module info (`kBadModuleInfo`).
- **Images in memory** (`a2b::ImageModuleInfoSource`, `a2b/image_source.h`): an image per
  position (`SetImage()`) or one for every node (`SetDefault()`), the image tool's `.dat`
  read from a file or an array compiled in, and a fallback source for the nodes without one
  (an `EepromModuleInfoSource`, for instance). It doesn't copy the images; the app keeps
  them while nodes are found and set up.
- **The app's ("ignore EEPROM")**, e.g. for blank modules before provisioning: derive
  from `ModuleInfoSource`, implement `GetModuleInfo()` (the first `a2b::kModuleInfoLen`
  bytes), and optionally `ReadModuleInfo()` (any part of the image, for its peripheral
  scripts; without it the node gets its settings but no scripts). Install it with
  `network.SetModuleInfoSource(&source)`; `nullptr` goes back to the EEPROMs.

A source may reach the node's peripherals with `Network::PeripheralWrite()`,
`PeripheralRead()` and `PeripheralWriteRead()` while the node is identified and set up,
but must not wait. That lets an app combine the two, e.g. the EEPROM where it holds an
image and the app's image for a blank module:

```cpp
class EepromOrApp : public a2b::ModuleInfoSource {
 public:
  explicit EepromOrApp(a2b::Network& network) : m_Eeprom(network) {}

  size_t GetModuleInfo(const a2b::SubNodeIdentity& node, uint8_t* buf, size_t len) override {
    const size_t got = m_Eeprom.GetModuleInfo(node, buf, len);
    m_FromEeprom[node.position] = (got != 0);
    return got != 0 ? got : CopyAppImage(buf, len);  // the app's image for blank modules
  }
  a2b::Result ReadModuleInfo(const a2b::SubNodeIdentity& node, uint32_t offset, uint8_t* buf,
                             size_t len) override {
    return m_FromEeprom[node.position] ? m_Eeprom.ReadModuleInfo(node, offset, buf, len)
                                       : a2b::Result::kNotSupported;
  }

 private:
  a2b::EepromModuleInfoSource m_Eeprom;
  bool m_FromEeprom[a2b::kMaxSubNodes] = {};
};
```

To give a default only where no EEPROM holds an image, and still reject a node whose EEPROM
answers but can't be read, check `m_Eeprom.EepromAddr(node.position)` after a failed
`GetModuleInfo()`: 0 means no EEPROM answered with the marker (none there, or a blank
one). The Clockworks example does this per chip: `clockworks::BoardModuleSource`
(`examples/clockworks/clockworks_boards.cpp`) takes an AD2427 with no EEPROM image to be an
AB0204, four PDM microphones (`clockworks::NoEepromDefault()`), and other chips with no
image are rejected. Which board a chip without an EEPROM is, is the app's to know, so the
stack's own source has no such default.

## Writing EEPROMs (provisioning)

Writing a module's EEPROM is application code on `Network`'s peripheral calls
([decisions](decisions.md)), not the stack's. The bench console's is the worked example
([provisioning.md](provisioning.md)): `EepromJob` (`examples/bench/eeprom_job.h`) reads,
writes, verifies and erases an EEPROM a transfer per poll, the marker written last, and
`CheckImage()` (`examples/bench/eeprom_image.h`) checks an image before it is written.

An app that provisions in the field can copy them. Give the node an image first, so it joins
the bus (`ignoreEEPROM` or `ImageModuleInfoSource`), and start the bus again after the write:
the stack reads a node's module info only when it finds the node.

## Events

A `Network` queues what happens as `a2b::Event`s and gives them to its
`NetworkListener` at the end of each `Poll()`, oldest first
(`a2b/network_listener.h`). Set the listener before `Start()`; without one, events are
dropped.

| `Event::type` | When | Fields |
|---|---|---|
| `kStateChanged` | `State()` changed | `state` |
| `kNodeAdded` | A sub-node was found and configured; it is in the bus | `node`, `chipId` |
| `kNodeRejected` | A node answered discovery but wasn't taken in; the chain ends before it | `node` (its position), `rejection`, `chipId` |
| `kNodeRemoved` | A sub-node left the bus; its object got `OnRemoved()` and is gone | `node`, `removal`, `chipId` |
| `kInterrupt` | A node raised an interrupt the stack doesn't consume (not DSCDONE or MSTR_RUNNING) | `node` (or `kMainNode`), `interruptType` (INTTYPE) |
| `kRoutingApplied` | A routing plan was written (`Routing()` gives it) | none |
| `kRoutingFailed` | A plan for the chain as it changed can't be built, or writing it failed | `routingError`, `node` (the node it is about, or `kNoNode`) |
| `kErrorReport` | The error watch (`SetErrorWatch()`, off by default) took a report: a probe failed, or a node failed a chain check and its `kNodeRemoved` events follow. `LastErrorReport()` has it | `node` (the position probed, or the node dropped) |

`RemovalReason`: `kDropped` (it, or a node before it, stopped answering: unplugged, lost
power, a line fault), `kRejected` (`RejectNode()` for it or a node before it),
`kRestart` (`Start()`, a PLL relock, a main node that was reset) and `kStopped`
(`Stop()`). When a node leaves, every node after it leaves too, each with its own event,
in order of position.

**What a handler may do.** Events come when the bus is between steps, so a handler may
call any `Network` function except `Poll()` (which returns at once from a handler):

```cpp
void OnEvent(a2b::Network& network, const a2b::Event& event) override {
  if (event.type == a2b::EventType::kNodeAdded) {
    const a2b::SubNodeInfo* info = network.SubNodeInfoAt(event.node);   // valid now
    network.PeripheralWrite(event.node, a2b::PeripheralAddress::I2c(0x2C), unmute, 2);
    if (!Wanted(*info)) {
      network.RejectNode(event.node);   // carried out at the next Poll()
    }
  }
}
```

Calls that change the bus (`RejectNode()`, `ResumeDiscovery()`, `Start()`, `Stop()`)
record what to do; the stack does it at the next `Poll()`, or at once for `Start()` and
`Stop()`, which send nothing. Their events come in the same round, and a round gives
out at most 64 events, so a handler that queues an event for every event it gets can't
hold `Poll()` forever. The queue holds 32 events between rounds; if it overflows, the
newest are dropped and the log says so.

**Chip IDs.** Positions change when a node drops and another is plugged in; the chip ID
in node events (CHIPID0-5, unique per transceiver) tells modules apart.

## Accepting nodes: the review

Before a node is set up, the stack reviews it: its own checks first (chip, module info;
a failure rejects it without asking), then the **default policy**, then the app's
`NetworkListener::OnNodeIdentified()`, which gets the policy's decision and may overrule
it:

```cpp
a2b::RejectReason OnNodeIdentified(const a2b::NodeReview& review) override {
  const a2b::ModuleInfo& module = review.node->moduleInfo;
  if (module.vendorId != 0x0C54) {
    return a2b::RejectReason::kByApp;   // not ours (Clockworks' boards: 0x0C54)
  }
  return review.policy;                 // kNone, or kPowerBudget
}
```

It is read-only: it may read the `Network` (e.g. `SubNodeCount()`) but must not change
it. A node it rejects gets a `kNodeRejected` event with the reason it returned.

**The default policy: the power budget.** `BoardPolicy::maxBusPowerMw` and
`maxBusCurrentMa` (0, the default: no limit) bound the sum of the maximum power
(`ModuleInfo::maxPowerMw`, module-info bytes 0x24-0x25) and current (`maxCurrentMa`,
0x29-0x2A) of the bus-powered sub-nodes (module info without local power). A
bus-powered node that would take either sum over its limit gets `kPowerBudget`.
`NodeReview::busPowerMw` and `busCurrentMa` are the sums without the node. The unit is
the module info's: mW and mA in ADI's images, and ADI's API's budget.

**Rejected nodes.** A rejected node is switched off (its upstream node's switch opens)
and the chain ends before it. Its position is probed again every
`BoardTimings::rejectedProbeMs` (2 s): the same node, by chip ID, is switched off again at
once, without its module info and without a new event or review; a different module is
reviewed as usual. The stack forgets the rejected node when the chain is cut before it
(a drop, `RejectNode()`) or the bus starts again, so a node refused for the budget is
reviewed again after a node before it drops and the chain is found again. A rejected node
on its own supply (a Custom link) stays discovered, so its position is not probed again
until `Start()`.

**Taking a node out: `RejectNode(position)`.** At the next `Poll()` the node and every
node after it leave the bus (`kNodeRemoved`, `kRejected`), the switch before it opens,
and it is then treated as a rejected node (above); `LastRejection()` is `kByApp`. A probe
under way is abandoned first.

## Stopping the search

The stack doesn't know where a chain ends: it keeps probing at the end of the chain, so
a node plugged in there is found. When to stop is the app's decision:

- **Pause.** `network.PauseDiscovery()` stops the probes (a probe under way finishes);
  the chain is still checked for drops. `ResumeDiscovery()` probes again at the next
  `Poll()`. E.g. stop a few seconds after start-up, once the chain the product expects is
  in:

  ```cpp
  void OnEvent(a2b::Network& network, const a2b::Event& event) override {
    if (event.type == a2b::EventType::kNodeAdded && network.SubNodeCount() == kExpected) {
      network.PauseDiscovery();       // the product's chain is complete
    }
  }
  ```

  Paused before `Start()`, the bus brings up its main node only. A node that drops while
  discovery is paused is not looked for again until `ResumeDiscovery()`.
- **A limit.** `BoardPolicy::maxSubNodes` (1-16, default 15) is the most sub-nodes the
  system design has: once that many are in, no probe is made (the chain is still
  checked), and probing starts again only when a node leaves. Every main-node chip takes
  16; the default is 15 because the 16th node has given trouble on hardware.

## Interrupts

The stack consumes DSCDONE and MSTR_RUNNING, and checks the chain at once after the
interrupts that may mean a drop. Every other interrupt becomes a `kInterrupt` event with
the node and its INTTYPE. `a2b::InterruptName(type)` names an INTTYPE value. Which
interrupts a sub-node raises is its INTMSK0/1, which its `SubNode::InterruptMask()` gives
(default: power faults only, ADI's masks; the error watch adds SRF missed and BECOVF while
it is on):

```cpp
class WatchedModule : public a2b::SubNode {
 public:
  using SubNode::SubNode;
  InterruptMasks InterruptMask() const override {
    InterruptMasks masks = SubNode::InterruptMask();   // INTMSK0 = power faults
    masks.intmsk0 |= 0x40;                              // + SRF missed: drops found at once
    masks.intmsk1 |= 0x01;                              // + GPIO 0 (INTTYPE 16)
    return masks;
  }
};

void OnEvent(a2b::Network& network, const a2b::Event& event) override {
  if (event.type == a2b::EventType::kInterrupt && event.interruptType == 16) {
    // GPIO 0 of sub-node event.node changed: read the module's peripheral, ...
  }
}
```

The masks are written when the node is found, before it first powers the next link. The
main node's are in its image (above). On a bus whose main node is an AD242x, don't enable
VMTR interrupts on AD243x sub-nodes; read their VMTR registers instead (the AD2431-35
TRM, "Considerations for Systems with Other Transceiver Nodes").

## Raw register access

`Network::ReadRegisters(node, reg, data, len)` and `WriteRegisters(node, reg, data, len)`
reach any register of the main node (`a2b::kMainNode`) or of a sub-node in the bus (ADI's
`a2b_pnp_ReadReg` and `a2b_pnp_WriteReg`), for consoles, bring-up and what the stack
doesn't do yet. Like the peripheral calls they make their transfers at once, from the
thread that polls (between polls, in a handler, or in a module-info source for the node
being identified). `reg` 0x100-0x1FF is page 1, on the AD2433 and AD2437 only (the
AD242x register map ends at 0x9B); the stack selects the page, so MMRPAGE itself is
refused. A run of registers stays within its page; writes go 16 registers to a transfer.

```cpp
uint8_t id[3];
network.ReadRegisters(a2b::kMainNode, 0x02, id, 3);   // VENDOR, PRODUCT, VERSION
const uint8_t gpio = 0x01;
network.WriteRegisters(2, 0x4A, &gpio, 1);            // sub-node 2's GPIODAT
```

The stack doesn't know what the app writes, so a write to a register it owns is the
app's risk:

| Registers | What the stack does with them | A write by the app |
|---|---|---|
| NODEADR, CHIP | Selects nodes and peripherals; cached | Safe: the stack rewrites them before its next access |
| MMRPAGE | Selects the page; cached | Refused (`kInvalidArgument`): the register address picks the page |
| CONTROL, SWCTL, SWCTL2, DISCVRY, RESPCYCS | Discovery and the links | A switch opened takes the power from the nodes after it, which the next chain check removes as dropped. The main node's MSTR cleared (with or without a soft reset) starts the bus again at the next chain check. A soft reset with MSTR kept leaves the PLL locked but resets the main node's registers and loses the chain: the sub-nodes are removed as dropped, and none is found again until the app calls `Start()` |
| INTSRC, INTTYPE, INTPND0-2 | The interrupt checks | A read of INTTYPE clears the interrupt it reports, so the stack never sees it (a DSCDONE lost fails that probe); a 1 written to INTPNDn clears a pending interrupt |
| INTMSK0-2 | The main node's from its image at start-up (its INTMSK2 with DSCDONE added); a sub-node's before the first probe behind it, and when that link's profile changes. The error watch adds SRF missed and BECOVF to INTMSK0 while it is on, and puts back what it found when it is turned off | Lasts until the stack writes them again; DSCDONE cleared in the main node's INTMSK2 makes every probe fail |
| BECNT | Cleared every second (any write clears it); read first while the error watch is on | Clears it too, and the count read before it is lost |
| BECCTL | Only the error watch's: every error type counted with BECOVF past 16 (0x7F) while it is on, and what it found when it is turned off | Lasts until the watch is turned on or off again; with the watch off the stack never writes it |
| BCDNSLOTS-UPSLOTS, RESPCYCS (in a plan), UPMASK0-DNOFFSET, RXMASK0-7, TXXBAR0-31 | The routing plan's (Routing) | Lasts until the next plan is written (`ApplyRouting()`, or the chain changes after one); a slot register takes effect at the next CONTROL.NEWSTRCT, the stack's or the app's |
| The main node's image, a sub-node's settings from its module info | Written at start-up and at the node's set-up | Lasts until the bus starts again or the node is found again |

## Routing

Routing (ADI's `a2b_pnp_AddConByChipId`, `a2b_pnp_ApplyRouting` and friends) connects a
**channel group** of one node to a channel group of another: the source's RX group (TDM
channels it takes from its DRXn pins) is played on the sink's TX group (TDM channels on its
DTXn pins). AD2433 and AD2437 nodes route through their crossbars; AD2428 and
AD2427 nodes, which have none, in TDM order (below). Each node is planned by its
own channel map, so a chain may mix the two in any order under any main node ("Mixed
chains", below).

**Channel groups.** A sub-node's come from its module info: the group table at offset
0x100 of the image (the ADI EEPROM layout: a count of TX groups, then 37 bytes each, an
ID, a 32-byte name and a 4-byte channel bitmap; then the RX groups). The stack reads it
while the node is set up, into `SubNodeInfo::moduleInfo.groups`; a source without
`ReadModuleInfo()` gives none. The main node's are the board's, `BoardConfig::routing.groups`
(an export has none, so `ImportSsPlus()` leaves them empty):

```cpp
board.routing.groups.AddRx(0, 0x3);   // RX group 0: TDM channels 0-1 from DRX0/1
board.routing.groups.AddTx(0, 0x3);   // TX group 0: TDM channels 0-1 on DTX0/1
```

**Connections.** Add them by chip ID, so they follow a module to any position and wait
while it is out of the bus, or between nodes in the bus by position (the stack takes their
chip IDs). They outlast `Start()` and `Stop()`; 32 at most (`a2b::kMaxConnections`).

```cpp
network.AddConnection(a2b::kMainNode, 0, 0, 0);   // main RX 0 -> sub-node 0 TX 0
network.AddConnection(0, 0, a2b::kMainNode, 0);   // sub-node 0 RX 0 -> main TX 0

a2b::Connection connection;                        // by chip ID, plugged in or not
connection.source.mainNode = true;
std::memcpy(connection.sink.chipId, speakerChipId, 6);
connection.sink.group = 1;
network.AddConnection(connection);

network.ApplyRouting();                            // route them, now and as the chain changes
```

A sink (node and TX group) takes one source: a connection to it replaces the one it had,
as in ADI's table. By position, both nodes must be in the bus and have the groups
(`Result::kInvalidArgument` otherwise). Two modules with the same chip ID (a blank one:
all zeros) are told apart by the position the connection was added or last found at.
`RemoveConnection()` and `ClearConnections()` change the table only: what is routed stays
until the next `ApplyRouting()` or chain change.

**Applying.** `ApplyRouting()` builds a plan for the chain at once and returns why it
can't (`RoutingError`): a group a node hasn't got, a channel the node's port doesn't give
(`kNoSuchChannel`), an AD242x's channels out of bus order (`kChannelOrder`), more than 32
slots on a link, more than 32 channels played on a node, no response-cycle window for the
slots. Before the first
`Start()` it only turns routing on. The plan is written at a `Poll()` between probes once
the chain is up (`kRunning`), then `kRoutingApplied` comes; `Routing()` gives the plan
written (each node's slot registers, slot masks, offsets, RXMASK, TXXBAR and RESPCYCS).
From then on the stack keeps the chain routed: when a node joins or leaves, a plan is built
for the new chain and written, so a module plugged back in gets its audio back. A plan
that can't be built then is reported as `kRoutingFailed`, and the last plan written stays.
A write that fails (I2C) is reported too, the chain is checked, and the plan is tried
again after `timings.probeIntervalMs`.

How a connection is carried: the source's RX group channels are paired with the sink's TX
group channels in order, lowest with lowest (a channel without a partner isn't sent);
each node sends its channels as one run of slots down and one up, each node plays the
slots meant for it and passes on only what a node after it needs, and on a crossbar node
every TX channel no connection plays points at frame buffer location 31, which nothing
fills unless all 32 locations are in use. The slot widths are the main node's SLOTFMT
(from its image), which every node gets.

**Each node's port.** A connection's channels must be on its nodes' I2S/TDM (or PDM)
ports: the source's RX channels, the sink's TX channels (`kNoSuchChannel` otherwise; a
module image with groups but its I2S port off was once the bench's "no SYNC"). The
stack counts them from the settings it writes, the main node's image and each sub-node's
module info: TDMMODE's channels (TDM2 to TDM32) on each data pin I2SCFG turns on (the
AD242x's TX0EN/TX1EN and RX0EN/RX1EN, and RXONDTX1; the AD243x's TXPINS and RXPINS
counts), times a sub-node's I2SRATE multiple (2x, 4x), plus each PDM microphone (PDMCTL)
in place of its pin's TDM channels; 32 at most. Channel n is the n-th TDM channel across the
pins, as the TRMs number them. `a2b::ComputeChannelMap()` does this. A node whose port is set
up some other way than the stack knows (a `SubNode` hook writing I2SCFG, say) needs the
policy to say so:

```cpp
class MyPorts : public a2b::RoutingPolicy {
 public:
  a2b::ChannelMap MapGroups(const a2b::PortSettings& port) const override {
    a2b::ChannelMap map = a2b::ComputeChannelMap(port);   // the stack's count
    if (port.node == 2) {
      map.txChannels = 8;   // sub-node 2: TDM8 on DTX0, set up by its hook
    }
    return map;
  }
};
```

**AD242x nodes (no crossbar).** An AD2428 or AD2427 has the same slot registers but no
RXMASK or TXXBAR (the stack writes none to it): its RX frame buffer holds all its RX
channels in order, so a run of slots carries every channel from the lowest it sends to the
highest (the main node, which has no offset register, sends from channel 0), and its TX
channel n plays frame buffer location n. So the slots it plays, in bus order, fill its TX
channels from 0 with no gap, downstream ones before upstream ones, and its connections must
follow that order (`kChannelOrder`, with the node, otherwise). On the bus a node's own
slots follow those it passes on: downstream, the main node's come first and then each
sub-node's in chain order; upstream, the last sub-node's come first. An AD2428 main node
with a codec at sub-node 0 and microphone modules at 1 and 2 takes them in that order:

```cpp
board.routing.groups.AddRx(0, 0x003);   // to the codec
board.routing.groups.AddTx(0, 0x00F);   // sub-node 2's four microphones (they come first)
board.routing.groups.AddTx(1, 0x0F0);   // sub-node 1's
board.routing.groups.AddTx(2, 0x300);   // the codec's pair (sub-node 0 adds its own last)
network.AddConnection(a2b::kMainNode, 0, 0, 0);
network.AddConnection(2, 0, a2b::kMainNode, 0);
network.AddConnection(1, 0, a2b::kMainNode, 1);
network.AddConnection(0, 0, a2b::kMainNode, 2);
```

An AD242x main node plays every upstream slot it receives, so a slot no one wants ahead
of a wanted one (one the planner can't drop, as it isn't last) takes a TX channel too. A
crossbar node anywhere in the chain plays its channels in any order.

**Mixed chains.** AD2433/AD2437 and AD2428/AD2427 nodes mix in one chain in any
order, under an AD2428, AD2433 or AD2437 main node; nothing is set for it. Each node keeps
its own rules: a crossbar node places what it plays and picks its own channels, and an
AD242x node plays in TDM order. A crossbar node doesn't change the TDM order of the nodes
around it: it passes slots on in the order it receives them and adds its own after them,
in channel order. So an AD242x sink downstream of an AD2437 still takes the main node's
slots first, then each sub-node's in chain order, the AD2437's included; an AD2437 sink in
the middle can take the same slots in any order. With an AD2437 at sub-node 1 between
AD2428s at 0 and 2:

```cpp
network.AddConnection(a2b::kMainNode, 0, 2, 0);  // sub-node 2's TX 0: the main node's first
network.AddConnection(0, 0, 2, 1);               // TX 1: sub-node 0's
network.AddConnection(1, 0, 2, 2);               // TX 2: the AD2437's, last
network.AddConnection(0, 0, 1, 0);               // the AD2437 takes them in any order
network.AddConnection(a2b::kMainNode, 0, 1, 1);
```

Swapping sub-node 2's TX 1 and TX 2 is refused (`kChannelOrder`, sub-node 2). The response
cycles (Formula B covers "AD2426/27/28/3x or mixed" systems) and SLOTFMT (the same on both
families) don't depend on the mix. While a plan is written, the local mute can't silence
an AD242x, so a mixed chain sometimes has the whole bus muted for a plan ("Muting", below).

**Response cycles.** A plan's RESPCYCS come from the slots on each link, by the TRMs'
formula (AD2431-35 TRM, "Example Main Node A2B_RESPCYCS Calculation", formula B): the
window between the downstream and upstream limits, the main node in its middle, each
sub-node's from the main node's and its position. The inputs are the main node's slot
widths and TDM mode (I2SGCFG, which picks RESPOFFS), `routing.frameRateHz` (the bit time)
and each link's cable length, the first of: the link's own from
`network.SetCableLength(n, decimetres)` (the link into sub-node n, from 0.1 m; a measured
length, for instance; `ClearCableLength(n)` removes it); the upstream node's image, its
B-side cable length ([provisioning](provisioning.md)); `routing.cableDm` (4 m by default,
which the AD2431-35 TRM calls adequate when the lengths are unknown, since each node adjusts
its response time during discovery; on the bench, short links left at 4 m dropped nodes now
and then, so give lengths close to the real ones: [hardware
notes](hardware_notes.md#routing-and-audio)). `CableLength(n)` gives the one in use. There is no upper
limit: a length whose response cycles can't be written fails the plan
(`kNoResponseCycles`), and one longer than the real cable can stop discovery
([hardware notes](hardware_notes.md#routing-and-audio)). A length that changes a link in the
chain of a routed bus has a new plan written at once. Every probe after a plan starts from the plan's
main-node RESPCYCS: a new node's DISCVRY is the formula's value for its position (before a
plan, from the image's RESPCYCS). To give your own (measured cables, a fixed value),
derive from `a2b::RoutingPolicy`:

```cpp
class FixedCycles : public a2b::RoutingPolicy {
 public:
  a2b::ResponseCycles RespCycles(const a2b::RespCycleInput& input) const override {
    a2b::ResponseCycles cycles = a2b::ComputeResponseCycles(input);   // the formula
    // input.mainRespCycles != 0: a probe; keep that main-node value
    return cycles;
  }
};
FixedCycles cycles;
board.routing.policy = &cycles;   // must outlive the bus
```

SigmaStudio+ uses a response offset of 254 ("accurate"), the TRMs' table 238-248 by TDM
mode, so its RESPCYCS can be a few above the stack's (0x7D against 0x78 for two slots each
way to one AB0020): both are in the window.

**The registers.** The routing registers are the stack's: the main node's image values
for the slot registers (BCDNSLOTS to UPSLOTS), the slot masks and offsets, RXMASK0-7 and
TXXBAR0-31 (written by the plan on AD243x nodes only) are not written, and a module's
settings for them are skipped; the image's
RESPCYCS is the probes' base until a plan is written. With each plan the main node's
SLOTFMT and DATCTL (0x03, both directions on, if the image has no DATCTL), and I2SRRATE if
the image has it, are written again before CONTROL.NEWSTRCT: they are auto-broadcast, so
a write reaches only the sub-nodes discovered by then. After each CONTROL.NEWSTRCT
(sub-node 0's probe, a plan) the stack sends nothing for 1 ms, while the nodes take up
the new structure ([timing guide](timing.md)).

**Muting** (ADI's `a2b_pnp_MuteNetwork` and the `A2B_PNP_AUDIO_MUTE` and
`A2B_PNP_AUDIO_UNMUTE_ROUTING_DONE` callbacks). Writing a plan changes the slot
registers all at once (CONTROL.NEWSTRCT) and the crossbars a moment later, so for that
moment a node can play the wrong channels; a pop in the speakers. The stack therefore mutes
while it writes a plan, and tells the app twice: `NetworkListener::OnAudioMute()` before it
touches the bus, so the app can mute what it feeds and plays (a DSP's output, say), and
`OnAudioUnmute()` once the plan is written. Both get the `Network` (they may use its
peripheral and register calls, as a DSP mute does, but not `Poll()`) and a `MuteScope`:
what the stack mutes itself. They are called from `Poll()` at that moment, not queued like
events, and they pair: a plan whose write fails is tried again without another
`OnAudioMute()`.

```cpp
class MyListener : public a2b::NetworkListener {
 public:
  void OnAudioMute(a2b::Network& network, const a2b::MuteScope&) override {
    network.PeripheralWrite(a2b::kMainNode, a2b::PeripheralAddress::I2c(0x38), kMute, 6);
  }
  void OnAudioUnmute(a2b::Network& network, const a2b::MuteScope&) override {
    network.PeripheralWrite(a2b::kMainNode, a2b::PeripheralAddress::I2c(0x38), kUnmute, 6);
  }
};
```

What the stack mutes itself is `BoardConfig::routing.mute`, a `RoutingMute`:

| Mode | What happens | Reaches |
|---|---|---|
| `kLocal` (default; ADI's) | Each node that would play a wrong channel while the plan is written has all of its TXXBAR pointed at location 31 (the one nothing fills) before the new slots are applied; the plan's crossbar write unmutes it. That is a node whose TX crossbar the plan changes, and a node that plays a slot whose channel the plan's new RXMASK at its source changes: RXMASK takes effect at once, so from NEWSTRCT until it is written the new slots carry the old frame buffer. | AD243x nodes only. If such a node can't be muted that way (an AD242x playing an AD243x's slots in a mixed chain, or a node that plays all 32 locations, which leaves no free one), the whole bus is muted for that plan, as with `kGlobal`. |
| `kGlobal` (ADI's `bGlobalMuteDuringCrossbar`) | The main node's DATCTL slot enables are cleared with the new slots, and set again after the crossbars: two CONTROL.NEWSTRCT instead of one. | Every node, AD242x included. |
| `kNone` | Nothing; the hooks are still called. | |

An AD242x has no crossbar (neither RXMASK nor TXXBAR is in its register map), so there is
no local mute for it, and in an AD242x chain it needs none: its outputs depend only on the
slot registers, which take effect together at NEWSTRCT. In a mixed chain an AD242x that
plays an AD243x's slots can get that node's old RXMASK channels until the new RXMASK is
written; `kLocal` then mutes the whole bus for that plan (the console prints `audio mute:
the whole bus (local: ...)`). A location the old RXMASK left empty is taken to send
silence, so a node that starts sending doesn't need it. The only mute the TRMs give an
AD242x is the data-slot enable, DATCTL.DNS and UPS, which the main node broadcasts and
NEWSTRCT applies, on every chip. Use `kGlobal` if a plan changing an AD242x's slots must
not be heard. `MuteScope`
says which nodes the stack muted (`scope.Includes(node)`, or `scope.network` for the whole
bus; the first plan after a start mutes every crossbar node, as their crossbars are at
power-on values).

**Muting the whole bus yourself.** `network.MuteNetwork(true)` clears DATCTL's slot enables
(and applies them with NEWSTRCT) so every node plays silence, and `MuteNetwork(false)` sets
them again; `NetworkMuted()` tells which. It is written at the next `Poll()` that finds the
bus between steps, so a probe under way (up to `timings.discoveryTimeoutMs`) delays it. It
lasts across plans, node changes and restarts of the bus, and the routing's own unmute leaves
a muted bus muted; the hooks are still called around a plan. What a node's TX pins carry when
its slots are off isn't spelled out in the TRMs (they say the data slots are disabled); check it with
your codec.

**Many connections at once.** `SetAudioRoutes()` (ADI's `a2b_pnp_SetAudioRt`) takes
streams, each a source RX group sent to several sinks' TX groups, by position, and sets
the connections all or nothing: every stream is checked first (nodes in the bus, groups
they have, a sink other than the source, room in the table), and a wrong one leaves the
table as it was. `append` adds to the connections kept; without it the streams replace
them. `ImportConnections()` puts back a list saved earlier (the connections `GetConnection()`
gave, by chip ID) in one call, also all or nothing; modules that aren't in the bus yet wait.

```cpp
a2b::AudioStream stream;                       // the main node's pair to two sub-nodes
stream.source = a2b::kMainNode;
stream.sourceGroup = 0;
stream.sinkCount = 2;
stream.sinks[0] = {0, 0};                      // sub-node 0, TX group 0
stream.sinks[1] = {1, 0};
network.SetAudioRoutes(&stream, 1, /*append=*/false);

std::vector<a2b::Connection> saved;            // saved from the table, restored later
for (size_t i = 0; i < network.ConnectionCount(); ++i) saved.push_back(*network.GetConnection(i));
network.ImportConnections(saved.data(), saved.size());
```

Both change the table only, like `AddConnection()`: `ApplyRouting()` (or a chain change on
a routed bus) writes them.

**How full the bus is.** `network.Bandwidth()` (ADI's `a2b_pnp_GetNwBW`) gives the busiest
sub-node of the plan built last and the share of the superframe's 1024 bits its frames take
(`BandwidthUsage`: the node, the bits, the percentage). A sub-node's bits are the downstream
and upstream terms of the response-cycle formula: 64 bits of control and 8 of turnaround each
way, its slots, the node delays and the cables. The plan built last is the last one
`ApplyRouting()` or a chain change built, written or not, and it is reported even when it
didn't fit. The percentage is ADI's measure, for an app's own warning and error
levels (ADI's `bwWarningLvl` and `bwErrorLvl` rejected nodes by it; this stack rejects
nothing for it); whether the slots fit is the response-cycle window (`kNoResponseCycles`),
which can be empty at less than 100%.

## Timings and policy

`BoardConfig::timings` and `BoardConfig::policy` hold what an app may tune, per bus. The
[timing guide](timing.md) lists every wait, where its value comes from, and which are
hard minimums the stack enforces whatever the setting (the 25 ms SUSTAIN wait, the 100 ms
switch settle, the 250 ms after a drop).

| Field | Default | Change it to |
|---|---|---|
| `timings.probeIntervalMs` | 500 | probe the end of the chain more or less often |
| `timings.chainCheckMs` | 500 | find drops sooner or with fewer reads (an `InterruptMask()` with SRF missed finds them at once) |
| `timings.interruptPollMs` | 20 | check interrupts more or less often, without an IRQ line |
| `timings.becntResetMs` | 1000 | clear the bit error counts less often; 0 for never |
| `timings.rejectedProbeMs` | 2000 | look for a different module at a rejected node's position more or less often |
| `timings.resetPulseMs` | 100 | hold RESETn for less (the data sheets need 10 us) |
| `policy.maxSubNodes` | 15 | stop probing after that many sub-nodes |
| `policy.maxBusPowerMw`, `maxBusCurrentMa` | 0 (no limit) | reject nodes that would overload the bus supply |

Set them before `Start()`.

## Several buses

One `Network` per bus, each with its own `BoardConfig` (its own I2C addresses and
`busIndex`), on one `Platform` ([architecture guide](architecture.md), "Several buses";
[main-node configuration](main_node_config.md), "Several buses"). What the app may share:

- **A listener.** `OnEvent()` gets the `Network`; `OnNodeIdentified()` gets the node,
  whose `identity.bus` is its bus's `busIndex`:

  ```cpp
  class Buses : public a2b::NetworkListener {
   public:
    Buses(a2b::Network& front, a2b::Network& rear) : m_Front(front), m_Rear(rear) {}
    void OnEvent(a2b::Network& network, const a2b::Event& event) override {
      const char* bus = (&network == &m_Front) ? "front" : "rear";
      // ...
    }
    a2b::RejectReason OnNodeIdentified(const a2b::NodeReview& review) override {
      if (review.node->identity.bus == 1 && review.node->moduleInfo.productId != kRearModule) {
        return a2b::RejectReason::kByApp;   // only rear modules on bus 1
      }
      return review.policy;
    }
   private:
    a2b::Network& m_Front;
    a2b::Network& m_Rear;
  };
  ```

- **A module-info source or a factory.** Their calls get the node's `SubNodeIdentity`
  (or `SubNodeInfo`), with `bus`. A source that only reads EEPROMs needs nothing shared:
  each `Network` has its own default `EepromModuleInfoSource`.
- **Calls from one bus's handler to another bus** are fine, except `Poll()`.

With one thread polling all buses, nothing needs a lock. With a thread per bus, the
`Platform` needs `Lock()`/`Unlock()` and thread-safe `NowMs()`, `Log()` and GPIO calls
([porting guide](porting.md)), and a shared listener, source or factory is called from
both threads.

## Not included

The [roadmap](../../ROADMAP.md#features-not-included) lists what the stack
doesn't do, with the calls that are there for it and return `Result::kNotSupported`: power
diagnostics, dual buses, the C facade, SPI, spread spectrum, GPIO over distance and the
others.
