/**
 * @file network.cpp
 * @brief Network: one bus's main-node start-up and configuration, discovery of its chain
 *        of sub-nodes, and the chain's upkeep (drops, rejections, events), all driven by
 *        Poll().
 *
 * **Start-up.** The same on every start, whether the host pulsed RESETn or the main node
 * is still running from an earlier session (decisions.md, "AD243x main-node start-up";
 * ADI's a2b_dscvryReset and a2b_dscvryWriteMstrI2sgcfgInvSeq in a2bplugin-master/src/
 * discovery.c, as fixed to wait out SUSTAIN):
 * 0. At Start() only, on a Platform with a reset line: RESETn is held low for
 *    BoardTimings::resetPulseMs (the data sheets' tRST is 10 us; ADI's RP2040 app
 *    held it 100 ms), then released, and the stack waits 2 ms before the first access
 *    (tACCESS, 1.5 ms in the AD2437 and AD2431-35 data sheets). The AD242x has no reset
 *    pin; a platform may still drive its supply. The main node is then in POWER-UP, and
 *    the steps below do what they do after a power-on.
 * 1. VENDOR, PRODUCT and VERSION are read and checked against BoardConfig::mainProduct.
 * 2. Soft reset with CONTROL.MSTR = 0. Per the TRMs this unlocks a running main node's
 *    PLL; an AD243x then spends 1024 SYNC periods in SUSTAIN (~21.3 ms at 48 kHz). A soft
 *    reset in POWER-UP has no effect, so the sequence is harmless after RESETn.
 * 3. Wait BoardTimings::rediscoveryWaitMs, never less than 25 ms: INV or MSTR written in
 *    SUSTAIN leaves an AD243x main node unusable until it is power-cycled.
 * 4. I2SGCFG.INV alone (if the image sets it: the TRMs want it before MSTR), then
 *    CONTROL.MSTR.
 * 5. MSTR_RUNNING, from the interrupt checks, by BoardTimings::discoveryStartDelayMs;
 *    at the deadline one last check is made before the start-up fails.
 *
 * ADI's stack wrote SOFTRST | MSTR at once on AD242x main nodes. This stack uses the
 * sequence above on every chip: an AD242x main node doesn't enter SUSTAIN, so the wait
 * only costs time. An AD2428 main node has run on the bench. TODO(test on hardware): an
 * AD2433 main node.
 *
 * **Configuration.** The main-node image (after BoardHooks::BeforeMainNodeInit()) is
 * written in the image's order, consecutive registers in one transfer, without the
 * registers the stack owns (CONTROL, SWCTL, RESPCYCS, DISCVRY, NODEADR, MMRPAGE, CHIP,
 * the interrupt status and pending registers). An INTMSK2 in the image gets DSCDONE added;
 * an image without INTMSK2 leaves it off, and no sub-node is found (ImportSsPlus() always
 * writes INTMSK2). Then the board's host script runs through ScriptRunner. ADI's stack
 * wrote most of the image only after the first sub-node was found; writing it first
 * configures the main node on a bus with no sub-nodes too (a trace difference with no
 * trace_diff.py rule).
 *
 * **Discovery.** Sub-nodes are found one position at a time, each through the B side of
 * the node before it; see discovery.cpp. Its module info comes from the app's
 * ModuleInfoSource, or by default from the node's EEPROM (EepromModuleInfoSource). A node
 * identified is reviewed: the default policy (BoardPolicy's power budget) decides, and
 * the app's NetworkListener::OnNodeIdentified() may overrule it. A node taken in gets its
 * SubNode object, of the class the app's NodeFactory picks (a plain SubNode without one),
 * in a fixed slot here, and is set up (node_setup.cpp: its register settings, its
 * peripheral scripts, its object's operations). The next position is then probed at once.
 * A probe that finds nothing ends the start-up chain (kRunning); the end of the chain is
 * then probed every BoardTimings::probeIntervalMs, so a node plugged in there is found.
 * No probe is made while BoardPolicy::maxSubNodes are in (the system design's limit) or
 * the app pauses discovery.
 *
 * **Rejections.** A rejected node (by the stack's checks, the review, its set-up, or the
 * app's RejectNode()) is switched off, and the chain ends before it. Its chip ID is kept:
 * its position is probed again every BoardTimings::rejectedProbeMs, and the same node
 * found there is switched off again without a new event, while a different module plugged
 * in there is identified and reviewed as usual. The chip ID is forgotten when the chain is
 * cut before it (a drop, RejectNode()) or the bus starts again. A rejected node on its own
 * supply (a link not powered over the A2B pair) stays discovered, so its position is not
 * probed again until then (to be revisited with two-step links; see the roadmap).
 *
 * **Chain checks and drops** (the TRMs' "Bus Drop Detection" and AD2437 "Node Drop
 * Detection"; ADI's a2b_AppDetectBusDrop in a2bpnp/a2bpnp_local.c). Every
 * BoardTimings::chainCheckMs, between probes, the main node's VENDOR and PRODUCT and its
 * CONTROL are read: wrong IDs or MSTR clear mean it was reset, so the bus starts again
 * from RESETn (the TRMs' "power cycle the main node"). Then each sub-node's VENDOR and
 * PRODUCT, in order: the first that fails or differs from what it reported when it was
 * found has dropped, with every node after it. They are taken out (OnRemoved(),
 * kNodeRemoved events), the upstream node's switch is opened, so whatever is left there
 * starts from power-up, and the position is probed again no sooner than 250 ms later
 * (the AD2437 TRM's wait after the switch opens). A node plugged back in is found by that
 * probe (re-plug). Each read is tried up to three times (the TRMs: retry an access two to
 * four times), so a transient I2C error doesn't count as a drop. Interrupts that may mean
 * a drop (SRF missed, power faults, I2C and interrupt messaging errors, a sub-node's
 * INTTYPE read error) and failed transfers make the check come at once. ADI's stack
 * read VENDOR only, of the main node and each sub-node, before each probe and not while
 * probing was paused (trace differences with no trace_diff.py rule).
 *
 * **Transient failures.** Once the bus is up, a failed transfer doesn't stop it: a
 * failed interrupt read, probe or set-up is abandoned (Discovery::Abandon() switches the
 * new node off), and the chain check decides what failed. Only a main node that still
 * doesn't answer after the restart stops the bus (NetworkError::kI2cError).
 *
 * **Bit errors.** Every BoardTimings::becntResetMs (1 s) BECNT is written 0 on every node
 * (the TRMs' advice; ADI's a2b_app_handle_becovf): a broadcast write, or the main node's
 * register while there are no sub-nodes.
 *
 * **Events.** What happens is queued (a fixed queue, Event, 32 entries) and given to the
 * app's NetworkListener at the end of Poll(), so a handler may call any API: the calls
 * that change the bus only record what to do at the next Poll(). Start() and Stop() queue
 * events too; they are given out at the next Poll().
 *
 * **Links.** The link to sub-node n takes its LinkProfile from, in order: the app's
 * SetLinkProfile(n); the upstream node's DownstreamLink() (sub-node n - 1's object; for
 * n = 0 there is none); BoardConfig::defaultLink (which SubNode::DownstreamLink() also
 * gives by default). The profile is looked up again before each probe of n, so a change
 * applies to the next probe: the upstream node's link is then set up again (masks,
 * XCVRBINV, the power-configuration check). A link whose node is in the bus keeps its
 * settings until the node is found again. The profile gives XCVRBINV and whether the
 * upstream node's switch powers the next node (LinkProfile::Power::kOnA2bPair). Two-step
 * links are not supported yet: SetLinkProfile() refuses them, and a link whose
 * DownstreamLink() asks for one is not probed (the chain ends there, with a log line).
 *
 * **Routing** (ADI's a2b_pnp_AddConByChipId and friends, a2b_pnp_ApplyRouting).
 * The app's connections (a source node's RX channel group to a sink node's TX group) are
 * kept in a ConnectionTable by chip ID, so they follow a module to any position and wait
 * while it is out of the bus; they outlast Start(). ApplyRouting() builds a plan for the
 * chain as it is (RoutingPlanner: each node's slots, slot masks, offsets, RXMASK and TXXBAR,
 * by each node's channel map, RoutingPolicy::MapGroups(), from its chip and the port
 * settings the stack wrote: the image's I2SGCFG, I2SCFG, I2SRATE and PDMCTL, the main
 * node's from BoardConfig, a sub-node's from its module info; then the response cycles from
 * the slots, RoutingPolicy::RespCycles()) and returns at once
 * whether one could be built. The plan is written at the next Poll() between probes once
 * the chain is up (NetworkState::kRunning), so it never lands in a probe or a set-up: each
 * sub-node's slot registers and RESPCYCS, the main node's, with SLOTFMT and DATCTL again
 * (DATCTL 0x03, both directions on, if the image has none) and I2SRRATE if the image has
 * it, since these are auto-broadcast and reach only the sub-nodes found when written; then
 * CONTROL.NEWSTRCT; then the NEWSTRCT settle (below); then the crossbars, which take effect
 * at once, as ADI's UpdateSlots ordered them (none on an AD242x, which has no crossbar
 * registers): every node's RXMASK0-7, then every node's TXXBAR0-31, so that no node is
 * unmuted while a node whose slots it plays has its old RXMASK.
 * Mixed chains (test_sim_mixed_routing): each node is planned and written by its
 * own chip and channel map, so AD243x and AD242x nodes mix in any order under any main node;
 * an AD242x sink's TDM order holds across a crossbar node, which passes slots on in the
 * order it receives them.
 * EventType::kRoutingApplied follows, or kRoutingFailed. From the first ApplyRouting() on,
 * the stack keeps the chain routed: a node added or removed (a drop, a rejection, a
 * restart) has the plan built and written again, so a module plugged back in gets its
 * audio back (scenario 5). ADI's stack re-applied after a drop itself, and asked the
 * app to apply after a node joined (A2B_PNP_CONN_IMPORT). Connections changed after
 * ApplyRouting() are routed at the next ApplyRouting() or chain change. A plan that can't
 * be built leaves the last one written.
 * The main node's RESPCYCS from the plan is the base of every later probe's DISCVRY
 * (the TRMs' formula for each position, RoutingPolicy::RespCycles()); until a plan is
 * written, the image's RESPCYCS is. Both take each link's cable length from
 * SetCableLength(), else BoardRouting::cableDm (4 m by default); a change re-plans a routed
 * bus. The routing registers are the stack's: the image's
 * values for them are not written (IsOwnedByStack()), as ADI's PnP layer zeroed
 * DNSLOTS until routing ran.
 *
 * **NEWSTRCT settle.** After each CONTROL.NEWSTRCT (sub-node 0's probe, a routing plan), no
 * transfer at all for at least 1 ms (two ticks of the millisecond clock): the TRMs apply a
 * new structure within 5 superframes (0.1 ms at 48 kHz), and the main node holds off its
 * own INTTYPE reads of the sub-nodes meanwhile, so an INTTYPE read could give 0x80 (ADI's
 * a2b_delayForNewStruct, A2B_DELAY_FOR_NEWSTRUCT_IN_MS 1).
 *
 * **Muting** (ADI's UpdateSlots, networkMute() and CrossbarLocalMute() are the counterparts). A
 * plan is written in four steps (StepRouting()): the app is told (NetworkListener::OnAudioMute());
 * the stack mutes by BoardRouting::mute; the slots, then after the settle the crossbars; and
 * the app is told it is over (OnAudioUnmute()). Local: each node with a crossbar whose
 * TXXBAR the plan changes has all 32 pointed at TX frame buffer location 31, which nothing
 * fills (the TRMs, "I2S TX Crossbar", say a location holds the slot the TX frame buffer
 * received there; they don't say what an empty one plays, nor did ADI's stack rely on more),
 * written before the slots, so the node is silent when NEWSTRCT applies the new ones; the crossbar
 * write of the plan unmutes it. A node without a crossbar (AD242x) has nothing to mute that
 * way and needs nothing for its own registers: its outputs follow the slot registers alone,
 * which change together at NEWSTRCT. But RXMASK takes effect at once too, so until a source
 * node's new RXMASK is written the new slots carry its old frame buffer: a node that plays
 * such a slot is muted locally as well, and if it can't be (an AD242x in a mixed chain), the
 * whole bus is muted for the plan (BuildMuteScope()). Global: DATCTL's DNS and UPS (the
 * TRMs: "globally enables ... synchronous data", main node, auto-broadcast, applied by NEWSTRCT, on
 * every chip) are clear in the slots' write and set again, with a second NEWSTRCT, after the
 * crossbars. MuteNetwork() is the same DATCTL write for the app; the stack's own mute never unmutes
 * what the app muted. The hooks pair: OnAudioMute() is not repeated for a plan written again after
 * a failure. A plan that fails to write leaves the mute in place until it is written (a plan that
 * can no longer be built leaves the nodes muted: silence rather than the wrong channels).
 *
 * An MSTR_RUNNING after start-up means the main node's PLL unlocked and relocked, which
 * resets it (TRMs, "Main Node PLL Locked Interrupt"): the Network starts again, without a
 * RESETn pulse.
 *
 * **The app's register access** (ReadRegisters(), WriteRegisters(); ADI's
 * a2b_pnp_ReadReg and a2b_pnp_WriteReg, for consoles and bring-up). The main node is
 * reached at any time, a sub-node while it is in the bus (or being identified). The
 * accesses go through the Transport, at once, between the stack's own. They can't
 * disturb the Transport: a write of NODEADR or CHIP makes it rewrite them before its next
 * access, and MMRPAGE is not written by the app at all (the register address picks the
 * page: 0x100-0x1FF is page 1, on the chips that have it). Everything else is the app's
 * risk: the stack doesn't know what the app wrote. The registers it owns:
 * - CONTROL, SWCTL, SWCTL2, DISCVRY and RESPCYCS run discovery and the links. A switch
 *   the app opens takes the power from the nodes after it, which the next chain check
 *   then removes as dropped. The main node's MSTR cleared (with or without a soft reset)
 *   makes the bus start again at the next chain check. A soft reset with MSTR kept leaves
 *   its PLL locked but resets its registers and loses the chain (the TRMs): the sub-nodes
 *   are removed as dropped, and no node is found again until the app calls Start().
 * - INTSRC and INTTYPE: a read of INTTYPE clears the interrupt it reports (the TRMs), so
 *   it is lost to the stack (a DSCDONE lost fails that probe). INTPND0-2: a 1 written
 *   clears a pending interrupt. INTMSK0-2: the main node's come from its image at
 *   start-up (its INTMSK2 with DSCDONE added), a sub-node's are written before the first
 *   probe behind it and when that link's profile changes.
 * - BECNT is cleared every second (any write clears it).
 * - The main node's image and each sub-node's register settings are written once, at
 *   start-up and at the node's set-up: a change lasts until the bus starts again or the
 *   node is found again.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "a2b/network.h"

#include <cstring>
#include <new>

#include "a2b/eeprom_source.h"
#include "chip_caps.h"
#include "connection_table.h"
#include "detail/regdefs.h"
#include "discovery.h"
#include "interrupt_poller.h"
#include "node_setup.h"
#include "owned_regs.h"
#include "routing_planner.h"
#include "script_runner.h"
#include "transport.h"

namespace a2b {
namespace {

/** The shortest soft reset to MSTR wait: covers SUSTAIN (ADI's A2B_SW_RESET_DELAY). */
constexpr uint32_t kMinResetWaitMs = 25;
/** RESETn released to the first access: tACCESS is 1.5 ms (AD2437, AD2431-35 data sheets). */
constexpr uint32_t kResetAccessMs = 2;
/** A dropped node's upstream switch opened to the next probe there, at least (the AD2437
    TRM's bus drop flow waits 250 ms after SWCTL = 0; the AD242x TRM's, 100 ms). */
constexpr uint32_t kDropWaitMs = 250;
/** Tries of each read in a chain check (the TRMs: retry an access two to four times). */
constexpr int kCheckTries = 3;

/** Buffer for a peripheral script's blocks and units: the 12-bit size field's range. */
constexpr size_t kPeripheralBufLen = 4096;

/** Events the queue holds between two dispatches. */
constexpr size_t kMaxEvents = 32;

/** No RejectNode() request is waiting. */
constexpr NodeAddr kNoRequest = kMaxSubNodes;

/** Locations in a node's RX or TX frame buffer; data slots on a link each way. */
constexpr size_t kFrameLocations = 32;

/** The registers of node @p index (0 the main node, n + 1 sub-node n) in @p plan. */
const NodeRouting& RegsOf(const RoutingPlan& plan, size_t index) {
  return (index == 0) ? plan.main : plan.sub[index - 1];
}

/** RXMASK0-3 of @p regs: the TDM channels 0-31 its RX frame buffer takes. */
uint32_t RxMaskOf(const NodeRouting& regs) {
  return static_cast<uint32_t>(regs.rxMask[0]) | (static_cast<uint32_t>(regs.rxMask[1]) << 8) |
         (static_cast<uint32_t>(regs.rxMask[2]) << 16) |
         (static_cast<uint32_t>(regs.rxMask[3]) << 24);
}

/** Slots 0 to @p count - 1 of a link, as a mask. */
uint32_t FirstSlots(size_t count) {
  return (count >= kFrameLocations) ? 0xFFFFFFFFu : ((1u << count) - 1);
}

/** The TDM channel in RX frame buffer location @p location under RX mask @p rxMask (its
    channels in order), or 32 for an empty location. */
unsigned ChannelAt(uint32_t rxMask, size_t location) {
  for (unsigned channel = 0; channel < 32; ++channel) {
    if ((rxMask & (1u << channel)) != 0 && location-- == 0) {
      return channel;
    }
  }
  return 32;
}

/**
 * Logs @p before, the number @p n, then @p after, without the C library's printf (whose
 * stack the RAM report can't see).
 */
void LogNumber(Platform& platform, LogLevel level, const char* before, unsigned n,
               const char* after) {
  char text[64];
  size_t len = 0;
  for (const char* p = before; *p != '\0' && len < sizeof(text) - 4; ++p) {
    text[len++] = *p;
  }
  if (n >= 10) {
    text[len++] = static_cast<char>('0' + (n / 10) % 10);
  }
  text[len++] = static_cast<char>('0' + n % 10);
  for (const char* p = after; *p != '\0' && len < sizeof(text) - 1; ++p) {
    text[len++] = *p;
  }
  text[len] = '\0';
  platform.Log(level, text);
}

/**
 * True for an interrupt type that may mean a node dropped, so the chain is checked at
 * once: SRF missed, the power faults, an I2C error, the interrupt messaging error and a
 * sub-node INTTYPE read error (TRMs, "Line Diagnostics After Discovery" and "Bus Link
 * Communication Errors").
 */
bool MayMeanDrop(uint8_t type) {
  return type == reg::kInttypeSrferr ||
         (type >= reg::kInttypePwrerrFirst && type <= reg::kInttypePwrerrLast) ||
         type == reg::kInttypeI2cerr || type == reg::kInttypePwrerrShortGnd ||
         type == reg::kInttypePwrerrShortVbus || type == reg::kInttypeMsgErr ||
         type == reg::kInttypeSubReadErr;
}

}  // namespace

/// The Network's state and work, in the storage inside the Network object.
class Network::Impl {
 public:
  Impl(Network& owner, Platform& platform, const BoardConfig& board);  ///< Stopped.
  ~Impl();

  Impl(const Impl&) = delete;
  Impl& operator=(const Impl&) = delete;

  Result Start();             ///< See Network::Start().
  void Stop();                ///< See Network::Stop().
  void Poll(uint32_t nowMs);  ///< See Network::Poll().
  /// See Network::SetModuleInfoSource().
  void SetModuleInfoSource(ModuleInfoSource* source) { m_Source = source; }
  /// See Network::SetNodeFactory().
  void SetNodeFactory(const NodeFactory* factory) { m_Factory = factory; }
  /// See Network::SetListener().
  void SetListener(NetworkListener* listener) { m_Listener = listener; }
  Result SetLinkProfile(NodeAddr position, const LinkProfile& profile);  ///< See Network.
  Result ClearLinkProfile(NodeAddr position);                            ///< See Network.
  Result SetCableLength(NodeAddr position, uint16_t decimetres);         ///< See Network.
  Result ClearCableLength(NodeAddr position);                            ///< See Network.
  uint16_t CableLength(NodeAddr position) const;                         ///< See Network.
  void PauseDiscovery() { m_Paused = true; }  ///< See Network::PauseDiscovery().
  void ResumeDiscovery();                     ///< See Network::ResumeDiscovery().
  Result RejectNode(NodeAddr position);       ///< See Network::RejectNode().
  Result PeripheralAccess(NodeAddr node, PeripheralAddress addr, const uint8_t* writeData,
                          size_t writeLen, uint8_t* readData,
                          size_t readLen);  ///< See Network::PeripheralWriteRead().
  Result RegisterAccess(NodeAddr node, uint16_t reg, const uint8_t* writeData, uint8_t* readData,
                        size_t len);  ///< See Network::ReadRegisters() and WriteRegisters().
  Result AddConnection(const Connection& connection);  ///< See Network::AddConnection().
  Result ConnectionAt(NodeAddr source, uint8_t sourceGroup, NodeAddr sink, uint8_t sinkGroup,
                      bool add);  ///< See Network::AddConnection() and RemoveConnection().
  /// See Network::RemoveConnection().
  Result RemoveConnection(const Connection& connection) { return m_Connections.Remove(connection); }
  void ClearConnections() { m_Connections.Clear(); }  ///< See Network::ClearConnections().
  Result SetAudioRoutes(const AudioStream* streams, size_t count,
                        bool append);  ///< See Network::SetAudioRoutes().
  Result ImportConnections(const Connection* connections,
                           size_t count);  ///< See Network::ImportConnections().
  /// See Network::ConnectionCount().
  size_t ConnectionCount() const { return m_Connections.Count(); }
  const Connection* GetConnection(size_t index) const;      ///< See Network::GetConnection().
  NodeAddr ConnectionNode(const ConnectionEnd& end) const;  ///< See Network::ConnectionNode().
  RoutingError ApplyRouting();                              ///< See Network::ApplyRouting().
  const RoutingPlan& Routing() const { return m_Plan; }     ///< See Network::Routing().
  void MuteNetwork(bool mute);                              ///< See Network::MuteNetwork().
  bool NetworkMuted() const { return m_Muted; }             ///< See Network::NetworkMuted().
  BandwidthUsage Bandwidth() const { return m_Bandwidth; }  ///< See Network::Bandwidth().

  NetworkState State() const { return m_State; }              ///< See Network::State().
  NetworkError Error() const { return m_Error; }              ///< See Network::Error().
  size_t SubNodeCount() const { return m_SubNodeCount; }      ///< See Network::SubNodeCount().
  const SubNodeInfo* SubNode(NodeAddr position) const;        ///< See Network::SubNode().
  a2b::SubNode* NodeObject(NodeAddr position);                ///< See Network::NodeObject().
  RejectReason LastRejection() const { return m_Rejection; }  ///< See Network::LastRejection().
  bool DiscoveryPaused() const { return m_Paused; }           ///< See Network::DiscoveryPaused().

 private:
  /** Steps of kStarting and kConfiguring. */
  enum class Step : uint8_t {
    kEarlyScript,  ///< Run the host operations due before the main node (once per Start()).
    kResetLine,    ///< Assert RESETn, if the platform has the line.
    kResetPulse,   ///< Hold RESETn, then release it.
    kResetAccess,  ///< Wait tACCESS after RESETn.
    kReadId,       ///< Check the main node, then soft reset.
    kSustainWait,  ///< Wait out SUSTAIN, then INV and MSTR.
    kWaitRunning,  ///< Wait for MSTR_RUNNING.
    kWriteImage,   ///< Write the main node's settings.
    kHostScript,   ///< Run the board's host script.
  };

  /** What discovery is doing at the end of the chain. */
  enum class Phase : uint8_t {
    kIdle,     ///< Between probes.
    kProbe,    ///< Probing and identifying (Discovery).
    kSetup,    ///< Setting up the node found (NodeSetup).
    kRouting,  ///< Writing a routing plan.
  };

  /** Steps of Phase::kRouting. */
  enum class RouteStep : uint8_t {
    kMute,      ///< OnAudioMute(), then the mute (RoutingMute) before the slots change.
    kSlots,     ///< The slot registers, RESPCYCS and NEWSTRCT.
    kCrossbar,  ///< After the settle: the crossbars, and with a global mute DATCTL again.
    kUnmute,    ///< After that settle: the plan is the one written.
  };

  /** Storage for one sub-node's object. */
  struct alignas(kSubNodeSlotAlign) NodeSlot {
    unsigned char bytes[kSubNodeSlotSize];
  };

  void StepStarting(uint32_t nowMs);     ///< kStarting.
  bool StepReset(uint32_t nowMs);        ///< kStarting: the RESETn pulse.
  void StepConfiguring(uint32_t nowMs);  ///< kConfiguring.
  void StepDiscovering(uint32_t nowMs);  ///< kDiscovering and kRunning.
  bool Upkeep(uint32_t nowMs);           ///< Chain check and BECNT.
  bool CheckChain(uint32_t nowMs);       ///< Finds drops.
  Result ReadForCheck(NodeAddr node, uint16_t reg, uint8_t* data, size_t len);  ///< With retries.
  void Drop(NodeAddr position, uint32_t nowMs);  ///< Takes out a dropped node and after.
  void HandleRejectRequest(uint32_t nowMs);      ///< Carries out RejectNode().
  void HandleInterrupt(const InterruptPoller::Interrupt& irq);  ///< Reports an interrupt.
  bool BeginProbe(uint32_t nowMs);                              ///< Starts the next probe.
  void StepSetup(uint32_t nowMs);                               ///< Phase::kSetup.
  bool BeginRouting(uint32_t nowMs);                            ///< Starts writing a due plan.
  void StepRouting(uint32_t nowMs);                             ///< Phase::kRouting.
  Result WriteSlots(uint32_t nowMs);                            ///< Slot registers, NEWSTRCT.
  Result WriteCrossbars();                                      ///< RXMASK and TXXBAR.
  bool StartMute();                             ///< RouteStep::kMute; false if it did not finish.
  void BuildMuteScope(MuteScope& scope) const;  ///< What the plan being written mutes.
  bool CrossbarChanges(size_t index) const;     ///< A node's TXXBAR differs from the bus's.
  bool LeavesSilence(size_t index) const;       ///< A node plays fewer than 32 locations.
  void MarkStaleSlots(bool* stale) const;       ///< Nodes playing a slot the new RXMASKs change.
  uint32_t AddSlots(size_t index, size_t first, size_t count, uint32_t changed,
                    size_t& len) const;  ///< A node's slots onto a link, marked if changed.
  bool SlotChanges(size_t index, size_t location) const;  ///< A new RXMASK moves a channel.
  void FinishRouting();                     ///< The plan is written: unmute hook, event.
  uint8_t MainDatctl() const;               ///< DATCTL as it is written now.
  Result WriteDataControl(uint32_t nowMs);  ///< DATCTL and NEWSTRCT, then the settle.
  void StepMute(uint32_t nowMs);            ///< MuteNetwork() is carried out.
  bool EndOf(NodeAddr node, uint8_t group, ConnectionEnd& end) const;  ///< A node in the bus.
  static bool Joins(const Connection& connection);                     ///< Two different nodes.
  void RoutingFailed(RoutingError error, NodeAddr node);               ///< Reports a failed plan.
  RoutingError BuildPlan(RoutingPlan& plan, NodeAddr& node);           ///< A plan for the chain.
  PortSettings PortOf(NodeAddr node) const;                            ///< A node's port settings.
  void RespCycleBase(RespCycleInput& input) const;                     ///< The chain's links.
  uint8_t DiscoveryRespCycles(NodeAddr position) const;                ///< DISCVRY for a position.
  void RoutingChanged();                                      ///< The chain changed: plan again.
  ConnectionTable::Chain Chain() const;                       ///< The sub-nodes, for connections.
  RejectReason Review(NodeAddr position);                     ///< Policy and the listener.
  void EndProbe(Discovery::Outcome outcome, uint32_t nowMs);  ///< Acts on a probe's end.
  void BeginStart(bool resetLine);                            ///< Enters kStarting.
  size_t EarlyHostOps() const;                        ///< Host operations due before the main node.
  void Configure();                                   ///< Enters kConfiguring.
  void BeginDiscovery();                              ///< Enters kDiscovering.
  Result WriteMainImage();                            ///< Writes the image, in bursts.
  void Restart(const char* why, bool resetLine);      ///< Back to the main-node reset.
  void Fail(NetworkError error, const char* why);     ///< Stops with an error.
  void CancelProbe();                                 ///< Abandons a probe; no I/O.
  void AbandonProbe();                                ///< Abandons a probe; tidies the bus.
  LinkProfile ResolveLink(NodeAddr position) const;   ///< The link profile to a position.
  void ApplyLink(NodeAddr position);                  ///< Refreshes a link's profile settings.
  a2b::SubNode* MakeNode(NodeAddr position);          ///< Makes a node's object in its slot.
  void FinishNode(NodeAddr position);                 ///< The node's own link, once it is set up.
  void DestroyNode(NodeAddr position, bool removed);  ///< Destroys a node's object.
  void RemoveNodes(NodeAddr from, RemovalReason why);    ///< Takes nodes out of the bus.
  void RememberRejected(NodeAddr position, bool stuck);  ///< Keeps a rejected node's chip ID.
  void SetState(NetworkState state);                     ///< Changes state, with an event.
  void Queue(const Event& event);                        ///< Queues an event.
  void QueueNode(EventType type, NodeAddr position);     ///< Queues a node's event.
  void Dispatch();                                       ///< Gives the events to the listener.
  ModuleInfoSource& Source();                            ///< The app's source, or the EEPROMs.
  bool IsReachable(NodeAddr node) const;  ///< The main node, or a sub-node in the bus or joining.
  const ChipCaps* CapsOf(NodeAddr node) const;  ///< A reachable node's chip, if known.

  Network& m_Owner;
  Platform& m_Platform;
  const BoardConfig& m_Board;
  Transport m_Transport;
  ScriptRunner m_Runner;
  InterruptPoller m_Poller;
  Discovery m_Discovery;
  NodeSetup m_Setup;
  EepromModuleInfoSource m_Eeprom;   ///< The default module-info source.
  Discovery::Settings m_Settings;    ///< What each probe writes: the image's, then the plan's.
  uint8_t m_ImageRespCycles = 0x40;  ///< This start-up's image's RESPCYCS.
  Discovery::Link m_MainLink;        ///< The main node's link, from this start-up's image.
  /** Each node's link: [0] the main node's, [n + 1] sub-node n's. */
  Discovery::Link m_Links[kMaxSubNodes + 1];
  LinkProfile m_LinkProfile[kMaxSubNodes];  ///< The app's profile for the link to each position.
  bool m_HasLinkProfile[kMaxSubNodes] = {};
  uint16_t m_CableDm[kMaxSubNodes] = {};  ///< The app's cable length for each link; 0: none.
  RegisterImage m_Image;                  ///< This start-up's main-node settings.
  const ChipCaps* m_MainCaps = nullptr;   ///< The main node's chip, from the last Start().
  ModuleInfoSource* m_Source = nullptr;
  const NodeFactory* m_Factory = nullptr;
  NetworkListener* m_Listener = nullptr;
  NetworkState m_State = NetworkState::kStopped;
  NetworkError m_Error = NetworkError::kNone;
  Step m_Step = Step::kReadId;
  Phase m_Phase = Phase::kIdle;
  uint32_t m_Deadline = 0;                ///< The next step, or the next probe.
  uint32_t m_CheckDeadline = 0;           ///< The next chain check.
  uint32_t m_BecntDeadline = 0;           ///< The next BECNT write.
  bool m_CheckNow = false;                ///< Check the chain as soon as no probe runs.
  bool m_Paused = false;                  ///< PauseDiscovery() is in force.
  NodeAddr m_RejectRequest = kNoRequest;  ///< The first position RejectNode() asked for.
  RejectReason m_Rejection = RejectReason::kNone;
  bool m_HasRejected = false;        ///< A rejected node's chip ID is kept.
  bool m_RejectedStuck = false;      ///< It is on its own supply: its position isn't probed.
  NodeAddr m_RejectedAt = 0;         ///< Its position.
  uint8_t m_RejectedChipId[6] = {};  ///< Its chip ID.
  size_t m_SubNodeCount = 0;
  SubNodeInfo m_SubNodes[kMaxSubNodes];
  a2b::SubNode* m_Nodes[kMaxSubNodes] = {};    ///< Each position's object, or nullptr.
  NodeSlot m_Slots[kMaxSubNodes];              ///< Where the objects live.
  Event m_Events[kMaxEvents];                  ///< The event queue, a ring.
  uint8_t m_EventHead = 0;                     ///< The oldest event.
  uint8_t m_EventCount = 0;                    ///< Events queued.
  bool m_EventsLost = false;                   ///< The queue overflowed since the last dispatch.
  bool m_InListener = false;                   ///< Inside a listener call.
  uint8_t m_PeripheralBuf[kPeripheralBufLen];  ///< Group tables, peripheral blocks and units.
  ConnectionTable m_Connections;               ///< The app's connections.
  RoutingPlanner m_Planner;                    ///< Builds plans.
  RoutingPlanner::Input m_PlanInput;           ///< The planner's input for the chain.
  RoutingPlan m_Plan;                          ///< The plan written last.
  RoutingPlan m_Pending;                       ///< The plan being built or written.
  RespCycleInput m_RespInput;                  ///< The plan's response-cycle input.
  RoutingPolicy m_DefaultPolicy;               ///< When the board has no policy.
  bool m_RoutingOn = false;                    ///< ApplyRouting() was called: keep it routed.
  bool m_RoutingDue = false;                   ///< Write a plan at the next idle poll.
  uint32_t m_RoutingRetry = 0;                 ///< Not before then (after a failed write).
  RouteStep m_RouteStep = RouteStep::kSlots;
  bool m_Settling = false;     ///< In a routing plan's NEWSTRCT settle.
  uint32_t m_SettleUntil = 0;  ///< Its end.
  BandwidthUsage m_Bandwidth;  ///< Of the plan built last.
  /** Sub-nodes of #m_Plan whose crossbars are still on the bus: fewer after a node leaves. */
  size_t m_XbarKnown = 0;
  bool m_Muted = false;       ///< MuteNetwork(true) is in force.
  bool m_MuteDue = false;     ///< DATCTL is to be written at the next idle poll.
  bool m_RouteMute = false;   ///< The plan being written has DATCTL's slot enables clear.
  bool m_MuteHooked = false;  ///< OnAudioMute() was called, OnAudioUnmute() not yet.
  MuteScope m_MuteScope;      ///< What the plan being written mutes.
};

/**
 * Makes a stopped Network. Nothing is sent until Start() and Poll().
 * @param platform Host I2C, time and GPIO; must outlive the Network.
 * @param board The bus's configuration; must outlive the Network. It is read at each
 *        Start(), so it may be filled in after the Network is made.
 */
Network::Network(Platform& platform, const BoardConfig& board)
    : m_Impl(new (m_Storage) Impl(*this, platform, board)) {
  static_assert(sizeof(Impl) <= kImplSize, "raise Network::kImplSize");
  static_assert(alignof(Impl) <= 8, "Network::m_Storage is aligned to 8");
}

/** Destroys the Network; nothing is sent, and no event is given. */
Network::~Network() { m_Impl->~Impl(); }

/**
 * Starts the bus, or starts it again: the next Poll() pulses RESETn if the Platform has
 * the line, then checks the main node and resets it. No transfer happens here.
 * BoardHooks::BeforeMainNodeInit() is called with this start-up's copy of the main-node
 * image. Sub-nodes found before are forgotten (their objects get SubNode::OnRemoved(),
 * and EventType::kNodeRemoved events with RemovalReason::kRestart are queued), as are
 * rejected nodes. The app's link profiles, source, factory, listener and pause stay as
 * they are.
 * @return Result::kOk; Result::kInvalidArgument if the BoardConfig names no supported main
 *         node chip (the AD2427 is not main-node capable) or its maxSubNodes is not 1-16;
 *         Result::kNotSupported if its default link is two-step (not supported yet).
 */
Result Network::Start() { return m_Impl->Start(); }

/**
 * Stops the bus where it is; nothing more is sent. The main node keeps running. Sub-nodes
 * are forgotten (their objects get SubNode::OnRemoved(), and EventType::kNodeRemoved
 * events with RemovalReason::kStopped are queued, given out at the next Poll()).
 */
void Network::Stop() { m_Impl->Stop(); }

/**
 * Does the work that is due: start-up, configuration, interrupt checks, chain checks and
 * discovery; then gives the queued events to the listener. Call it often (every few
 * milliseconds); waits end at the first poll after their time. Called from a listener's
 * OnEvent(), it returns at once.
 * @param nowMs Platform::NowMs().
 */
void Network::Poll(uint32_t nowMs) { m_Impl->Poll(nowMs); }

/**
 * Sets where sub-nodes' module info comes from. The default reads each node's EEPROM
 * (EepromModuleInfoSource: 0x50-0x54 on the node's I2C port). An app that provisions
 * blank modules ("ignore EEPROM") gives its own source, and no EEPROM is read. It applies
 * from the next node identified.
 * @param source The source, or nullptr for the EEPROMs; must outlive the Network.
 */
void Network::SetModuleInfoSource(ModuleInfoSource* source) { m_Impl->SetModuleInfoSource(source); }

/**
 * Sets what picks each sub-node's class (the customisation cookbook, a2bcpp/docs/
 * cookbook.md). Without one, every sub-node is a plain SubNode. It applies from the next
 * node identified.
 * @param factory The factory, or nullptr; must outlive the Network.
 */
void Network::SetNodeFactory(const NodeFactory* factory) { m_Impl->SetNodeFactory(factory); }

/**
 * Sets the app's listener: it gets the events at the end of each Poll(), and reviews each
 * node identified (NetworkListener::OnNodeIdentified()). Without one, events are
 * discarded and the default policy (BoardPolicy's power budget) decides. Set it before
 * Start() to get every event.
 * @param listener The listener, or nullptr; must outlive the Network.
 */
void Network::SetListener(NetworkListener* listener) { m_Impl->SetListener(listener); }

/**
 * Sets the link to sub-node @p position, from its upstream node's B side (the main node's
 * for position 0). It wins over the upstream node's SubNode::DownstreamLink() and
 * BoardConfig::defaultLink. It applies from the next probe of that position; a link whose
 * node is in the bus keeps its settings until the node is found again. It outlasts
 * Start().
 * @param position The sub-node the link leads to, 0-15.
 * @param profile The link: its XCVRBINV and power source (the two-step fields are not supported
 * yet).
 * @return Result::kOk; Result::kInvalidArgument if @p position is not 0-15;
 *         Result::kNotSupported if @p profile is two-step (not supported yet).
 */
Result Network::SetLinkProfile(NodeAddr position, const LinkProfile& profile) {
  return m_Impl->SetLinkProfile(position, profile);
}

/**
 * Forgets the app's link to sub-node @p position: the upstream node's DownstreamLink(),
 * or BoardConfig::defaultLink, applies again from the next probe of that position.
 * @param position The sub-node the link leads to, 0-15.
 * @return Result::kOk, or Result::kInvalidArgument if @p position is not 0-15.
 */
Result Network::ClearLinkProfile(NodeAddr position) { return m_Impl->ClearLinkProfile(position); }

/**
 * Sets the cable length of the link to sub-node @p position, for the response cycles, in
 * place of BoardRouting::cableDm (until the stack measures cables). It applies to the next
 * probe's DISCVRY and the next routing plan: if the app routes the bus (ApplyRouting()), a
 * plan with it is written at the next idle poll. It outlasts Start().
 * @param position The sub-node the link leads to, 0-15 (0: the main node's link).
 * @param decimetres The cable, 1 to #kMaxCableDm.
 * @return Result::kOk, or Result::kInvalidArgument if @p position or @p decimetres is out
 *         of range.
 */
Result Network::SetCableLength(NodeAddr position, uint16_t decimetres) {
  return m_Impl->SetCableLength(position, decimetres);
}

/**
 * Forgets the app's cable length for the link to sub-node @p position: BoardRouting::cableDm
 * applies again, as for Network::SetCableLength().
 * @param position The sub-node the link leads to, 0-15.
 * @return Result::kOk, or Result::kInvalidArgument if @p position is not 0-15.
 */
Result Network::ClearCableLength(NodeAddr position) { return m_Impl->ClearCableLength(position); }

/**
 * Tells the cable length the response cycles take for the link to sub-node @p position.
 * @param position The sub-node the link leads to, 0-15.
 * @return The app's length (SetCableLength()), else BoardRouting::cableDm, in decimetres; 0
 *         if @p position is not 0-15.
 */
uint16_t Network::CableLength(NodeAddr position) const { return m_Impl->CableLength(position); }

/**
 * Stops probing for new sub-nodes, e.g. a few seconds after start-up once the app knows
 * its chain (ADI's a2b_pnp_PauseNewNodeDisc). A probe under way finishes, and a node it
 * finds is configured. The nodes in the bus are not affected, and the chain is still
 * checked for drops. The setting outlasts Start(): paused before Start(), the bus brings
 * up its main node only.
 */
void Network::PauseDiscovery() { m_Impl->PauseDiscovery(); }

/** Probes again, at the next Poll(), for the next position (ADI's a2b_pnp_ResumeNewNodeDisc). */
void Network::ResumeDiscovery() { m_Impl->ResumeDiscovery(); }

/**
 * Takes sub-node @p position out of the bus, with every node after it, at the next Poll()
 * (ADI's a2b_pnp_RejectNode): the upstream node's switch is opened, so the nodes lose
 * power; their objects get SubNode::OnRemoved(), and EventType::kNodeRemoved events with
 * RemovalReason::kRejected are queued. LastRejection() becomes RejectReason::kByApp. The
 * node's position is probed again every BoardTimings::rejectedProbeMs: the same node is
 * switched off again at once, a different module is identified as usual. A node on its
 * own supply is left discovered (its own switch is opened too, for the nodes after it),
 * and its position is not probed again until Start().
 * @param position A sub-node in the bus.
 * @return Result::kOk, or Result::kInvalidArgument if there is no such sub-node.
 */
Result Network::RejectNode(NodeAddr position) { return m_Impl->RejectNode(position); }

/**
 * Writes to a peripheral of a node, in one transfer, at once: a peripheral on the host
 * I2C bus for the main node, one on a sub-node's I2C port otherwise (through its CHIP
 * register and NODEADR.PERI). Call it from the thread that polls, between polls, from a
 * listener's OnEvent() or from a ModuleInfoSource. It doesn't wait for the bus to be
 * idle: a transfer to a sub-node during its set-up lands between the stack's own.
 * @param node #kMainNode, or a sub-node in the bus (or the one being identified).
 * @param addr The peripheral; PeripheralAddress::I2c(addr) for now.
 * @param data Bytes to write (a sub-address first, if the peripheral has one).
 * @param len Number of bytes, at least 1.
 * @return Result::kOk; Result::kInvalidArgument if @p node is not in the bus or an
 *         argument is out of range; Result::kNotSupported for an SPI peripheral (not supported
 * yet); Result::kI2cError if the transfer failed (e.g. no device answered).
 */
Result Network::PeripheralWrite(NodeAddr node, PeripheralAddress addr, const uint8_t* data,
                                size_t len) {
  return m_Impl->PeripheralAccess(node, addr, data, len, nullptr, 0);
}

/**
 * Reads from a peripheral of a node, in one transfer, at once. As PeripheralWrite().
 * @param node #kMainNode, or a sub-node in the bus (or the one being identified).
 * @param addr The peripheral.
 * @param data Buffer for the bytes.
 * @param len Number of bytes, at least 1.
 * @return As PeripheralWrite().
 */
Result Network::PeripheralRead(NodeAddr node, PeripheralAddress addr, uint8_t* data, size_t len) {
  return m_Impl->PeripheralAccess(node, addr, nullptr, 0, data, len);
}

/**
 * Writes then reads a peripheral of a node, joined by a repeated start (e.g. a
 * sub-address, then its data), at once. As PeripheralWrite().
 * @param node #kMainNode, or a sub-node in the bus (or the one being identified).
 * @param addr The peripheral.
 * @param writeData Bytes to write.
 * @param writeLen Number of bytes to write, at least 1.
 * @param readData Buffer for the bytes read.
 * @param readLen Number of bytes to read, at least 1.
 * @return As PeripheralWrite().
 */
Result Network::PeripheralWriteRead(NodeAddr node, PeripheralAddress addr, const uint8_t* writeData,
                                    size_t writeLen, uint8_t* readData, size_t readLen) {
  if (writeData == nullptr || writeLen == 0 || readData == nullptr || readLen == 0) {
    return Result::kInvalidArgument;
  }
  return m_Impl->PeripheralAccess(node, addr, writeData, writeLen, readData, readLen);
}

/**
 * Reads consecutive registers of a node, in one transfer, at once (ADI's
 * a2b_pnp_ReadReg). As PeripheralWrite(), it may be called between polls, from a
 * listener or from a ModuleInfoSource, and lands between the stack's own transfers. A
 * read of INTTYPE clears the interrupt it reports, which the stack then never sees (see
 * the registers the stack owns, in network.cpp).
 * @param node #kMainNode at any time (at the BoardConfig's address of the last Start()),
 *        or a sub-node in the bus (or the one being identified).
 * @param reg First register. 0x100-0x1FF are page 1, on the AD243x only; the Transport
 *        selects the page.
 * @param data Buffer for the values.
 * @param len Number of registers, at least 1; the run stays within its page.
 * @return Result::kOk; Result::kInvalidArgument if @p node is not in the bus, the
 *         register is not on the node's pages (page 1 needs an AD243x, and a main node
 *         after Start()) or the run leaves its page; Result::kI2cError if the transfer
 *         failed.
 */
Result Network::ReadRegisters(NodeAddr node, uint16_t reg, uint8_t* data, size_t len) {
  if (data == nullptr) {
    return Result::kInvalidArgument;
  }
  return m_Impl->RegisterAccess(node, reg, nullptr, data, len);
}

/**
 * Writes consecutive registers of a node, at once, in transfers of up to 16 registers
 * (ADI's a2b_pnp_WriteReg). As ReadRegisters(). The stack doesn't know what the app
 * writes: registers it owns (CONTROL, SWCTL, SWCTL2, DISCVRY, RESPCYCS, the interrupt
 * registers, BECNT, the configuration it wrote) are the app's risk, listed with their
 * effects in network.cpp. NODEADR and CHIP may be written (the stack rewrites them
 * before its next access); MMRPAGE may not (the register address picks the page).
 * @param node #kMainNode at any time, or a sub-node in the bus (or the one being
 *        identified).
 * @param reg First register; 0x100-0x1FF are page 1, on the AD243x only.
 * @param data Values to write.
 * @param len Number of registers, at least 1; the run stays within its page.
 * @return Result::kOk; Result::kInvalidArgument as for ReadRegisters(), or if the run
 *         covers MMRPAGE; Result::kI2cError if a transfer failed.
 */
Result Network::WriteRegisters(NodeAddr node, uint16_t reg, const uint8_t* data, size_t len) {
  if (data == nullptr) {
    return Result::kInvalidArgument;
  }
  return m_Impl->RegisterAccess(node, reg, data, nullptr, len);
}

/**
 * Adds an audio connection by chip ID (ADI's a2b_pnp_AddConByChipId): the source node's
 * RX group to the sink node's TX group. Either node may be out of the bus; the connection
 * waits for it. A connection that is there already changes nothing; one with the same sink
 * (node and group) is replaced, as a sink plays one source. It is routed at the next
 * ApplyRouting(), or when the chain changes after one. Groups are checked then.
 * @param connection The connection; a sub-node end names its node by chip ID, the main
 *        node's end by ConnectionEnd::mainNode.
 * @return Result::kOk; Result::kInvalidArgument if both ends are the same node;
 *         Result::kNoSpace if #kMaxConnections are kept.
 */
Result Network::AddConnection(const Connection& connection) {
  return m_Impl->AddConnection(connection);
}

/**
 * Adds an audio connection between two nodes in the bus, by position (ADI's
 * a2b_pnp_AddConByNodeId): kept by the nodes' chip IDs, as AddConnection(const
 * Connection&).
 * @param source #kMainNode or a sub-node in the bus: where the audio comes from.
 * @param sourceGroup One of its RX channel groups.
 * @param sink #kMainNode or a sub-node in the bus: where it goes.
 * @param sinkGroup One of its TX channel groups.
 * @return Result::kOk; Result::kInvalidArgument if a node isn't in the bus, hasn't got the
 *         group, or both are the same node; Result::kNoSpace if the table is full.
 */
Result Network::AddConnection(NodeAddr source, uint8_t sourceGroup, NodeAddr sink,
                              uint8_t sinkGroup) {
  return m_Impl->ConnectionAt(source, sourceGroup, sink, sinkGroup, true);
}

/**
 * Removes a connection. It stays routed until the next ApplyRouting() or chain change.
 * @param connection The connection: the same ends (nodes and groups) as when it was added.
 * @return Result::kOk, or Result::kInvalidArgument if there is no such connection.
 */
Result Network::RemoveConnection(const Connection& connection) {
  return m_Impl->RemoveConnection(connection);
}

/**
 * Removes the connection between two nodes in the bus, by position (ADI's
 * a2b_pnp_RemoveConByNodeId).
 * @param source #kMainNode or a sub-node in the bus.
 * @param sourceGroup Its RX group.
 * @param sink #kMainNode or a sub-node in the bus.
 * @param sinkGroup Its TX group.
 * @return Result::kOk, or Result::kInvalidArgument if a node isn't in the bus or there is
 *         no such connection.
 */
Result Network::RemoveConnection(NodeAddr source, uint8_t sourceGroup, NodeAddr sink,
                                 uint8_t sinkGroup) {
  return m_Impl->ConnectionAt(source, sourceGroup, sink, sinkGroup, false);
}

/**
 * Removes every connection (ADI's a2b_pnp_ClearAllCon). The routing stays as written until
 * the next ApplyRouting() or chain change, which then routes nothing.
 */
void Network::ClearConnections() { m_Impl->ClearConnections(); }

/**
 * Sets the connections from audio streams (ADI's a2b_pnp_SetAudioRt): each stream sends one
 * source RX group to one or more sinks' TX groups, the nodes by position. All or nothing:
 * every stream is checked first, and the connections are left as they were if one is wrong.
 * Like AddConnection(), it is routed at the next ApplyRouting() or chain change, and a sink
 * (node and group) plays one source, so a later stream to the same sink replaces an earlier.
 * @param streams The streams.
 * @param count How many.
 * @param append False: the streams replace every connection (a new audio routing); true:
 *        they are added to the connections kept, a sink already in them replaced.
 * @return Result::kOk; Result::kInvalidArgument if a stream has no sink or too many, a node
 *         isn't in the bus, hasn't got the group, or a sink is the stream's source;
 *         Result::kNoSpace if the connections would be more than #kMaxConnections.
 */
Result Network::SetAudioRoutes(const AudioStream* streams, size_t count, bool append) {
  return m_Impl->SetAudioRoutes(streams, count, append);
}

/**
 * Replaces the connections with a list saved earlier (ADI's a2b_pnp_ImportConnections, never
 * defined in ADI's stack): the connections GetConnection() gave, by chip ID, to be
 * restored at start-up before the modules are in the bus. All or nothing. Like
 * AddConnection(), it is routed at the next ApplyRouting() or chain change; no group is
 * checked until then, so an end may name a module that isn't in the bus yet.
 * @param connections The list; a sink (node and group) twice keeps the later.
 * @param count How many, at most #kMaxConnections.
 * @return Result::kOk; Result::kInvalidArgument if a connection joins a node to itself;
 *         Result::kNoSpace if there are more than #kMaxConnections.
 */
Result Network::ImportConnections(const Connection* connections, size_t count) {
  return m_Impl->ImportConnections(connections, count);
}

/**
 * Counts the connections kept (ADI's a2b_pnp_GetConnectionList), routed or waiting for
 * their nodes.
 * @return The count.
 */
size_t Network::ConnectionCount() const { return m_Impl->ConnectionCount(); }

/**
 * Gives a connection.
 * @param index 0 to ConnectionCount() - 1, the oldest first.
 * @return The connection, or nullptr if there is no such index. Valid until the
 *         connections change.
 */
const Connection* Network::GetConnection(size_t index) const {
  return m_Impl->GetConnection(index);
}

/**
 * Finds the node a connection's end names in the bus: the main node, or the sub-node with
 * its chip ID.
 * @param end The end.
 * @return #kMainNode, the sub-node's position, or #kNoNode if it isn't in the bus (or
 *         several sub-nodes share its chip ID).
 */
NodeAddr Network::ConnectionNode(const ConnectionEnd& end) const {
  return m_Impl->ConnectionNode(end);
}

/**
 * Routes the connections (ADI's a2b_pnp_ApplyRouting): builds a routing plan for the chain
 * as it is, at once, and writes it at the next Poll() once the chain is up and between
 * probes (EventType::kRoutingApplied, or kRoutingFailed). From then on the stack keeps the
 * chain routed: when a node joins or leaves, a new plan is built and written. Connections
 * whose nodes aren't in the bus wait. Called before the first Start(), it only turns
 * routing on: the plan is built once the chain is up. See the customisation cookbook,
 * "Routing".
 * @return RoutingError::kNone if a plan was built (it will be written), or before the
 *         first Start(); otherwise why not, and nothing is written (the chain is planned
 *         again when it changes).
 */
RoutingError Network::ApplyRouting() { return m_Impl->ApplyRouting(); }

/**
 * Gives the routing plan written last: each node's routing registers and each link's
 * slots.
 * @return The plan; RoutingPlan::valid is false until one is written after Start().
 */
const RoutingPlan& Network::Routing() const { return m_Impl->Routing(); }

/**
 * Mutes or unmutes the whole bus (ADI's a2b_pnp_MuteNetwork): the main node's DATCTL
 * downstream and upstream slot enables are cleared (or set again) and CONTROL.NEWSTRCT
 * applies it, at the next Poll() when the main node is configured and the bus is between the
 * stack's own steps; a probe or a node's set-up under way delays it (at most about
 * BoardTimings::discoveryTimeoutMs). The nodes then play silence; the TRMs' DATCTL works the
 * same on every chip. The mute
 * lasts until MuteNetwork(false), across routing plans and bus restarts, and a plan being
 * written does not unmute it. The NetworkListener's OnAudioMute() is not called: the app
 * asked for this one.
 * @param mute True to mute, false to unmute.
 */
void Network::MuteNetwork(bool mute) { m_Impl->MuteNetwork(mute); }

/**
 * Tells whether MuteNetwork(true) is in force.
 * @return True if the app muted the bus (the routing's own mute is not counted).
 */
bool Network::NetworkMuted() const { return m_Impl->NetworkMuted(); }

/**
 * Gives how full the bus is with the routing plan built last (ADI's a2b_pnp_GetNwBW): the
 * busiest sub-node and the share of the superframe its frames take. The plan built last is
 * the one ApplyRouting() or the chain's last change built, written or not, and it is
 * reported even if it didn't fit. See ComputeBandwidth().
 * @return The sub-node, its bits and percentage; all zero before any plan is built.
 */
BandwidthUsage Network::Bandwidth() const { return m_Impl->Bandwidth(); }

/**
 * Tells what the bus is doing.
 * @return The state.
 */
NetworkState Network::State() const { return m_Impl->State(); }

/**
 * Tells why the bus stopped.
 * @return NetworkError::kNone unless State() is NetworkState::kFailed.
 */
NetworkError Network::Error() const { return m_Impl->Error(); }

/**
 * Counts the sub-nodes in the bus.
 * @return The sub-nodes found and configured, at positions 0 to the count - 1.
 */
size_t Network::SubNodeCount() const { return m_Impl->SubNodeCount(); }

/**
 * Describes a sub-node in the bus.
 * @param position Its position.
 * @return Its identity and module info, or nullptr if there is no such sub-node. Valid
 *         until the next Start(), Stop() or Poll().
 */
const SubNodeInfo* Network::SubNode(NodeAddr position) const { return m_Impl->SubNode(position); }

/**
 * Gives a sub-node's object, of the class the NodeFactory picked: check its TypeTag()
 * before a static_cast to the app's class.
 * @param position Its position.
 * @return The object, or nullptr if there is no such sub-node. Valid until the next
 *         Start(), Stop() or Poll().
 */
a2b::SubNode* Network::NodeObject(NodeAddr position) { return m_Impl->NodeObject(position); }

/**
 * Tells why the last sub-node that answered discovery was not taken into the bus, or was
 * taken out by RejectNode().
 * @return RejectReason::kNone if none was rejected since Start().
 */
RejectReason Network::LastRejection() const { return m_Impl->LastRejection(); }

/**
 * Tells whether the app has paused discovery.
 * @return True between PauseDiscovery() and ResumeDiscovery().
 */
bool Network::DiscoveryPaused() const { return m_Impl->DiscoveryPaused(); }

/**
 * Makes the state of a stopped Network.
 * @param owner The Network, which the default module-info source reads through and the
 *        listener is given.
 * @param platform Host services.
 * @param board The bus's configuration.
 */
Network::Impl::Impl(Network& owner, Platform& platform, const BoardConfig& board)
    : m_Owner(owner),
      m_Platform(platform),
      m_Board(board),
      m_Transport(platform, board.mainAddr, board.busAddr),
      m_Runner(m_Transport),
      m_Poller(platform, m_Transport, board.busIndex, board.timings.interruptPollMs),
      m_Discovery(m_Transport, m_Poller),
      m_Setup(platform, m_Transport, m_Runner, m_PeripheralBuf, sizeof(m_PeripheralBuf)),
      m_Eeprom(owner) {}

/** Destroys the nodes' objects, without OnRemoved(): the bus isn't stopped. */
Network::Impl::~Impl() {
  for (NodeAddr position = 0; position < kMaxSubNodes; ++position) {
    DestroyNode(position, false);
  }
}

/** Checks the BoardConfig, takes this start-up's image and settings; see Network::Start(). */
Result Network::Impl::Start() {
  const ChipCaps* caps = LookUpChipCaps(kAdiVendorId, m_Board.mainProduct);
  if (caps == nullptr || !caps->mainCapable || m_Board.policy.maxSubNodes < 1 ||
      m_Board.policy.maxSubNodes > kMaxSubNodes) {
    return Result::kInvalidArgument;
  }
  const LinkProfile& link = m_Board.defaultLink;
  if (link.discovery != LinkProfile::Discovery::kSingleStep) {
    return Result::kNotSupported;  // Two-step discovery is not supported yet.
  }
  CancelProbe();
  m_Runner.Stop();
  RemoveNodes(0, RemovalReason::kRestart);
  m_MainCaps = caps;
  m_Transport.SetAddresses(m_Board.mainAddr, m_Board.busAddr);
  m_Poller.Configure(m_Board.busIndex, m_Board.timings.interruptPollMs);
  m_Image = m_Board.mainNode;
  if (m_Board.hooks != nullptr) {
    m_Board.hooks->BeforeMainNodeInit(m_Image);
  }

  // What each probe writes: the image's CONTROL settings (its link's XCVRBINV comes from
  // the link's profile at each probe), and its response cycles (0x40, the power-on value,
  // if it has none).
  uint8_t control = 0;
  uint8_t respCycles = 0x40;
  m_Image.Get(reg::kControl, control);
  m_Image.Get(reg::kRespcycs, respCycles);
  const uint8_t kept =
      static_cast<uint8_t>(reg::kControlSwbyp | (caps->hasI2sMsInv ? reg::kControlI2smsinv : 0));
  m_Settings.mainControl = static_cast<uint8_t>(reg::kControlMstr | (control & kept));
  m_ImageRespCycles = respCycles;
  m_Settings.timeoutMs = m_Board.timings.discoveryTimeoutMs;
  m_Settings.bus = m_Board.busIndex;

  // The main node's switch: the image's mode (the chip's if the image has no SWCTL), and
  // the board's power configuration, checked unless the board keeps the detected one.
  uint8_t swctl = caps->switchMode;
  m_Image.Get(reg::kSwctl, swctl);
  m_MainLink = Discovery::Link();
  m_MainLink.swctl = static_cast<uint8_t>(swctl & ~reg::kSwctlEnsw);
  m_MainLink.hasSwitchConfig =
      caps->hasSwitchConfig && m_Board.highPowerSwitchCfg != kPowerConfigAsDetected;
  m_MainLink.powerConfig = m_Board.highPowerSwitchCfg;

  m_Rejection = RejectReason::kNone;
  BeginStart(true);
  const size_t early = EarlyHostOps();
  if (early > 0) {  // A codec that clocks the main node is programmed first.
    m_Runner.Start(*m_Board.hostScript, 0, early);
    m_Step = Step::kEarlyScript;
  }
  return Result::kOk;
}

/** Stops where it is; see Network::Stop(). */
void Network::Impl::Stop() {
  CancelProbe();
  m_Runner.Stop();
  RemoveNodes(0, RemovalReason::kStopped);
  m_RejectRequest = kNoRequest;
  m_Error = NetworkError::kNone;
  SetState(NetworkState::kStopped);
}

/** Sets the app's link to a position; see Network::SetLinkProfile(). */
Result Network::Impl::SetLinkProfile(NodeAddr position, const LinkProfile& profile) {
  if (!IsSubNode(position)) {
    return Result::kInvalidArgument;
  }
  if (profile.discovery != LinkProfile::Discovery::kSingleStep) {
    return Result::kNotSupported;  // Two-step discovery is not supported yet.
  }
  m_LinkProfile[position] = profile;
  m_HasLinkProfile[position] = true;
  return Result::kOk;
}

/** Forgets the app's link to a position; see Network::ClearLinkProfile(). */
Result Network::Impl::ClearLinkProfile(NodeAddr position) {
  if (!IsSubNode(position)) {
    return Result::kInvalidArgument;
  }
  m_HasLinkProfile[position] = false;
  return Result::kOk;
}

/** Sets the app's cable length for a link; see Network::SetCableLength(). */
Result Network::Impl::SetCableLength(NodeAddr position, uint16_t decimetres) {
  if (!IsSubNode(position) || decimetres == 0 || decimetres > kMaxCableDm) {
    return Result::kInvalidArgument;
  }
  m_CableDm[position] = decimetres;
  RoutingChanged();
  return Result::kOk;
}

/** Forgets the app's cable length for a link; see Network::ClearCableLength(). */
Result Network::Impl::ClearCableLength(NodeAddr position) {
  if (!IsSubNode(position)) {
    return Result::kInvalidArgument;
  }
  m_CableDm[position] = 0;
  RoutingChanged();
  return Result::kOk;
}

/** The cable length in use for a link; see Network::CableLength(). */
uint16_t Network::Impl::CableLength(NodeAddr position) const {
  if (!IsSubNode(position)) {
    return 0;
  }
  return (m_CableDm[position] != 0) ? m_CableDm[position] : m_Board.routing.cableDm;
}

/** Records the app's rejection for the next poll; see Network::RejectNode(). */
Result Network::Impl::RejectNode(NodeAddr position) {
  if (!IsSubNode(position) || static_cast<size_t>(position) >= m_SubNodeCount) {
    return Result::kInvalidArgument;
  }
  if (position < m_RejectRequest) {
    m_RejectRequest = position;
  }
  return Result::kOk;
}

/**
 * One peripheral transfer: a write (@p readData null), a read (@p writeData null) or a
 * write-read; see Network::PeripheralWriteRead().
 */
Result Network::Impl::PeripheralAccess(NodeAddr node, PeripheralAddress addr,
                                       const uint8_t* writeData, size_t writeLen, uint8_t* readData,
                                       size_t readLen) {
  if (addr.kind != PeripheralAddress::Kind::kI2c) {
    return Result::kNotSupported;  // SPI peripherals are not supported yet.
  }
  if (!IsReachable(node)) {
    return Result::kInvalidArgument;
  }
  if (readData == nullptr) {
    return m_Transport.PeripheralWrite(node, addr.i2cAddr, writeData, writeLen);
  }
  if (writeData == nullptr) {
    return m_Transport.PeripheralRead(node, addr.i2cAddr, readData, readLen);
  }
  return m_Transport.PeripheralWriteRead(node, addr.i2cAddr, writeData, writeLen, readData,
                                         readLen);
}

/**
 * One register access by the app: a write (@p readData null) or a read (@p writeData
 * null) of @p len consecutive registers; see Network::ReadRegisters() and
 * Network::WriteRegisters().
 */
Result Network::Impl::RegisterAccess(NodeAddr node, uint16_t reg, const uint8_t* writeData,
                                     uint8_t* readData, size_t len) {
  if (!IsReachable(node) || len == 0) {
    return Result::kInvalidArgument;
  }
  const uint8_t page = static_cast<uint8_t>(reg >> 8);
  const ChipCaps* caps = CapsOf(node);
  if (page > 1 || (page == 1 && (caps == nullptr || !caps->hasPage1))) {
    return Result::kInvalidArgument;
  }
  if (readData != nullptr) {
    return m_Transport.ReadRegs(node, reg, readData, len);
  }
  // MMRPAGE is the Transport's: the register address picks the page.
  const size_t low = reg & 0xFF;
  if (low <= reg::kMmrpage && low + len > reg::kMmrpage) {
    return Result::kInvalidArgument;
  }
  return m_Transport.WriteRegs(node, reg, writeData, len);
}

/** Probes again at the next poll; see Network::ResumeDiscovery(). */
void Network::Impl::ResumeDiscovery() {
  m_Paused = false;
  m_Deadline = m_Platform.NowMs();
}

/** Does the work that is due, then gives out the events; see Network::Poll(). */
void Network::Impl::Poll(uint32_t nowMs) {
  if (m_InListener) {
    return;  // Called from a listener.
  }
  switch (m_State) {
    case NetworkState::kStarting:
      StepStarting(nowMs);
      break;
    case NetworkState::kConfiguring:
      StepConfiguring(nowMs);
      break;
    case NetworkState::kDiscovering:
    case NetworkState::kRunning:
      StepDiscovering(nowMs);
      break;
    case NetworkState::kStopped:
    case NetworkState::kFailed:
      break;
  }
  Dispatch();
}

/** Sub-node @p position, or nullptr; see Network::SubNode(). */
const SubNodeInfo* Network::Impl::SubNode(NodeAddr position) const {
  return (IsSubNode(position) && static_cast<size_t>(position) < m_SubNodeCount)
             ? &m_SubNodes[position]
             : nullptr;
}

/** Sub-node @p position's object, or nullptr; see Network::NodeObject(). */
a2b::SubNode* Network::Impl::NodeObject(NodeAddr position) {
  return (IsSubNode(position) && static_cast<size_t>(position) < m_SubNodeCount) ? m_Nodes[position]
                                                                                 : nullptr;
}

/**
 * kStarting: the RESETn pulse, then check the main node, soft reset, wait out SUSTAIN,
 * INV and MSTR, MSTR_RUNNING.
 */
void Network::Impl::StepStarting(uint32_t nowMs) {
  if (m_Step == Step::kEarlyScript) {
    switch (m_Runner.Poll(nowMs)) {
      case ScriptRunner::State::kRunning:
        return;
      case ScriptRunner::State::kDone:
        m_Step = Step::kResetLine;
        return;
      default:
        Fail(NetworkError::kHostScriptFailed, "a2b: host script (before the main node) failed");
        return;
    }
  }
  if ((m_Step == Step::kResetLine || m_Step == Step::kResetPulse || m_Step == Step::kResetAccess) &&
      !StepReset(nowMs)) {
    return;
  }
  switch (m_Step) {
    case Step::kReadId: {
      uint8_t ids[3] = {};
      if (m_Transport.ReadRegs(kMainNode, reg::kVendor, ids, sizeof(ids)) != Result::kOk) {
        Fail(NetworkError::kI2cError, "a2b: main node does not answer");
        return;
      }
      if (ids[0] != kAdiVendorId || ids[1] != m_Board.mainProduct) {
        Fail(NetworkError::kWrongMainNode, "a2b: main node is not the chip BoardConfig names");
        return;
      }
      if (m_Transport.WriteReg(kMainNode, reg::kControl, reg::kControlSoftrst) != Result::kOk) {
        Fail(NetworkError::kI2cError, "a2b: main node soft reset failed");
        return;
      }
      m_Transport.ForgetAll();
      const uint32_t waitMs = (m_Board.timings.rediscoveryWaitMs > kMinResetWaitMs)
                                  ? m_Board.timings.rediscoveryWaitMs
                                  : kMinResetWaitMs;
      m_Deadline = nowMs + waitMs;
      m_Step = Step::kSustainWait;
      return;
    }
    case Step::kSustainWait: {
      if (!TimeReached(nowMs, m_Deadline)) {
        return;
      }
      uint8_t i2sgcfg = 0;
      Result result = Result::kOk;
      if (m_Image.Get(reg::kI2sgcfg, i2sgcfg) && (i2sgcfg & reg::kI2sgcfgInv) != 0) {
        result = m_Transport.WriteReg(kMainNode, reg::kI2sgcfg, reg::kI2sgcfgInv);
      }
      if (result == Result::kOk) {
        result = m_Transport.WriteReg(kMainNode, reg::kControl, reg::kControlMstr);
      }
      if (result != Result::kOk) {
        Fail(NetworkError::kI2cError, "a2b: writing CONTROL.MSTR failed");
        return;
      }
      m_Deadline = nowMs + m_Board.timings.discoveryStartDelayMs;
      m_Step = Step::kWaitRunning;
      return;
    }
    case Step::kWaitRunning: {
      InterruptPoller::Interrupt irq;
      bool found = false;
      Result result = m_Poller.Poll(nowMs, irq, found);
      if (result == Result::kOk && !found && TimeReached(nowMs, m_Deadline)) {
        result = m_Poller.CheckNow(irq, found);  // The last look before giving up.
        if (result == Result::kOk && !found) {
          Fail(NetworkError::kMainNodeNotRunning, "a2b: no MSTR_RUNNING (is SYNC there?)");
          return;
        }
      }
      if (result != Result::kOk) {
        Fail(NetworkError::kI2cError, "a2b: interrupt read failed");
        return;
      }
      if (found && irq.type == reg::kInttypeMstrRunning) {
        m_Platform.Log(LogLevel::kInfo, "a2b: main node running");
        Configure();
      }
      return;
    }
    default:
      return;
  }
}

/**
 * kStarting, before the ID check: RESETn low for BoardTimings::resetPulseMs, then high,
 * then tACCESS. Without the line (Result::kNotSupported) it goes straight to the ID check;
 * a line that fails is logged and the start-up carries on without it.
 * @return True when the ID check is due, in this poll.
 */
bool Network::Impl::StepReset(uint32_t nowMs) {
  const uint8_t bus = m_Board.busIndex;
  switch (m_Step) {
    case Step::kResetLine: {
      const Result result = m_Platform.SetMainNodeReset(bus, true);
      if (result == Result::kOk) {
        m_Deadline = nowMs + m_Board.timings.resetPulseMs;
        m_Step = Step::kResetPulse;
        return false;
      }
      if (result != Result::kNotSupported) {
        m_Platform.Log(LogLevel::kWarning, "a2b: RESETn failed; starting without it");
      }
      m_Step = Step::kReadId;
      return true;
    }
    case Step::kResetPulse:
      if (!TimeReached(nowMs, m_Deadline)) {
        return false;
      }
      if (m_Platform.SetMainNodeReset(bus, false) != Result::kOk) {
        m_Platform.Log(LogLevel::kWarning, "a2b: releasing RESETn failed");
      }
      m_Transport.ForgetAll();
      m_Deadline = nowMs + kResetAccessMs;
      m_Step = Step::kResetAccess;
      return false;
    default:  // Step::kResetAccess.
      if (!TimeReached(nowMs, m_Deadline)) {
        return false;
      }
      m_Step = Step::kReadId;
      return true;
  }
}

/** kConfiguring: the main-node image, then the host script. */
void Network::Impl::StepConfiguring(uint32_t nowMs) {
  if (m_Step == Step::kWriteImage) {
    if (WriteMainImage() != Result::kOk) {
      Fail(NetworkError::kI2cError, "a2b: writing the main-node settings failed");
      return;
    }
    const size_t early = EarlyHostOps();
    if (m_Board.hostScript == nullptr || early >= m_Board.hostScript->Size()) {
      BeginDiscovery();
      return;
    }
    m_Runner.Start(*m_Board.hostScript, early, m_Board.hostScript->Size());
    m_Step = Step::kHostScript;
  }
  switch (m_Runner.Poll(nowMs)) {
    case ScriptRunner::State::kRunning:
      return;
    case ScriptRunner::State::kDone:
      BeginDiscovery();
      return;
    default:
      Fail(NetworkError::kHostScriptFailed, "a2b: host script failed");
      return;
  }
}

/**
 * kDiscovering and kRunning: interrupt checks, the app's rejections, the chain's upkeep
 * between probes, and probes for the next position. A failed interrupt read has the
 * chain checked, which tells a transient failure from a main node gone.
 */
void Network::Impl::StepDiscovering(uint32_t nowMs) {
  if (m_Settling && TimeReached(nowMs, m_SettleUntil)) {
    m_Settling = false;
  }
  if (m_Settling || m_Discovery.Settling(nowMs)) {
    return;  // The NEWSTRCT settle: no transfer at all.
  }
  InterruptPoller::Interrupt irq;
  bool found = false;
  if (m_Poller.Poll(nowMs, irq, found) != Result::kOk) {
    m_CheckNow = true;
    found = false;
  }
  if (found && irq.type == reg::kInttypeMstrRunning) {
    Restart("a2b: main node PLL relocked; starting again", false);
    return;
  }
  if (found && irq.type != reg::kInttypeDscdone) {
    HandleInterrupt(irq);
  }
  if (m_RejectRequest != kNoRequest) {
    HandleRejectRequest(nowMs);
  }
  if (m_Phase == Phase::kRouting) {
    StepRouting(nowMs);
    return;
  }
  if (m_Phase == Phase::kIdle && m_MuteDue) {
    StepMute(nowMs);
    return;
  }
  if (m_Phase == Phase::kIdle && (!Upkeep(nowMs) || BeginRouting(nowMs) || !BeginProbe(nowMs))) {
    return;
  }
  if (m_Phase == Phase::kSetup) {
    StepSetup(nowMs);
    return;
  }
  const bool dscdone = found && irq.type == reg::kInttypeDscdone;
  const Discovery::Outcome outcome = m_Discovery.Poll(nowMs, dscdone ? &irq : nullptr);
  InterruptPoller::Interrupt other;
  if (m_Discovery.TakeInterrupt(other)) {  // Read by the probe's last check.
    if (other.type == reg::kInttypeMstrRunning) {
      Restart("a2b: main node PLL relocked; starting again", false);
      return;
    }
    HandleInterrupt(other);
  }
  if (outcome == Discovery::Outcome::kBusy) {
    return;
  }
  m_Phase = Phase::kIdle;
  if (outcome != Discovery::Outcome::kIdentified) {
    EndProbe(outcome, nowMs);
    return;
  }
  const NodeAddr position = static_cast<NodeAddr>(m_SubNodeCount);
  const RejectReason decision = Review(position);
  if (decision != RejectReason::kNone) {
    EndProbe(m_Discovery.Reject(decision), nowMs);
    return;
  }
  m_Setup.Begin(*MakeNode(position), *m_Discovery.Caps(), Source(),
                m_SubNodes[position].moduleInfo.groups, m_Board.routing.frameRateHz);
  m_Phase = Phase::kSetup;
  StepSetup(nowMs);
}

/**
 * Between probes: the chain check when it is due (or asked for), then BECNT.
 * @return False if the check started the bus again.
 */
bool Network::Impl::Upkeep(uint32_t nowMs) {
  if ((m_CheckNow || TimeReached(nowMs, m_CheckDeadline)) && !CheckChain(nowMs)) {
    return false;
  }
  const uint32_t becntMs = m_Board.timings.becntResetMs;
  if (becntMs != 0 && TimeReached(nowMs, m_BecntDeadline)) {
    m_BecntDeadline = nowMs + becntMs;
    const uint8_t zero = 0;
    const Result result = (m_SubNodeCount == 0) ? m_Transport.WriteReg(kMainNode, reg::kBecnt, zero)
                                                : m_Transport.BroadcastWrite(reg::kBecnt, &zero, 1);
    if (result != Result::kOk) {
      m_CheckNow = true;  // At the next poll.
    }
  }
  return true;
}

/**
 * Checks the chain: the main node's IDs and CONTROL.MSTR, then each sub-node's IDs. A
 * main node that fails starts the bus again from RESETn; the first sub-node that fails is
 * dropped, with the nodes after it.
 * @return False if the bus starts again.
 */
bool Network::Impl::CheckChain(uint32_t nowMs) {
  m_CheckNow = false;
  m_CheckDeadline = nowMs + m_Board.timings.chainCheckMs;
  uint8_t ids[2] = {};
  uint8_t control = 0;
  if (ReadForCheck(kMainNode, reg::kVendor, ids, sizeof(ids)) != Result::kOk ||
      ReadForCheck(kMainNode, reg::kControl, &control, 1) != Result::kOk) {
    Restart("a2b: main node does not answer; starting again", true);
    return false;
  }
  if (ids[0] != kAdiVendorId || ids[1] != m_Board.mainProduct ||
      (control & reg::kControlMstr) == 0) {
    Restart("a2b: main node was reset; starting again", true);
    return false;
  }
  for (size_t i = 0; i < m_SubNodeCount; ++i) {
    const NodeAddr position = static_cast<NodeAddr>(i);
    const SubNodeIdentity& identity = m_SubNodes[i].identity;
    if (ReadForCheck(position, reg::kVendor, ids, sizeof(ids)) != Result::kOk ||
        ids[0] != identity.vendorId || ids[1] != identity.productId) {
      Drop(position, nowMs);
      return true;
    }
  }
  return true;
}

/** Reads registers for a chain check, up to three times; the result of the last try. */
Result Network::Impl::ReadForCheck(NodeAddr node, uint16_t reg, uint8_t* data, size_t len) {
  Result result = Result::kI2cError;
  for (int i = 0; i < kCheckTries && result != Result::kOk; ++i) {
    result = m_Transport.ReadRegs(node, reg, data, len);
  }
  return result;
}

/**
 * Takes sub-node @p position and the nodes after it out of the bus: they dropped. The
 * upstream node's switch is opened, and the position is probed again after the switch
 * has settled.
 */
void Network::Impl::Drop(NodeAddr position, uint32_t nowMs) {
  LogNumber(m_Platform, LogLevel::kWarning, "a2b: sub-node ", static_cast<unsigned>(position),
            " dropped");
  RemoveNodes(position, RemovalReason::kDropped);
  const NodeAddr upstream = static_cast<NodeAddr>(position - 1);  // kMainNode for 0.
  (void)m_Transport.WriteReg(upstream, reg::kSwctl, m_Links[position].swctl);
  const uint32_t waitMs = (m_Board.timings.probeIntervalMs > kDropWaitMs)
                              ? m_Board.timings.probeIntervalMs
                              : kDropWaitMs;
  m_Deadline = nowMs + waitMs;
}

/**
 * Carries out RejectNode(): a probe under way is abandoned, the node and those after it
 * are taken out, the upstream node's switch is opened (and the node's own, if it has its
 * own supply), and its chip ID is kept.
 */
void Network::Impl::HandleRejectRequest(uint32_t nowMs) {
  const NodeAddr position = m_RejectRequest;
  m_RejectRequest = kNoRequest;
  if (static_cast<size_t>(position) >= m_SubNodeCount) {
    return;  // Gone already.
  }
  if (m_Phase == Phase::kRouting) {
    m_Phase = Phase::kIdle;  // Planned again for the shorter chain.
  } else if (m_Phase != Phase::kIdle) {
    AbandonProbe();
  }
  LogNumber(m_Platform, LogLevel::kInfo, "a2b: sub-node ", static_cast<unsigned>(position),
            " rejected by the app");
  std::memcpy(m_RejectedChipId, m_SubNodes[position].identity.chipId, sizeof(m_RejectedChipId));
  const Discovery::Link& link = m_Links[position];
  const uint8_t ownSwitch = m_Links[position + 1].swctl;
  RemoveNodes(position, RemovalReason::kRejected);
  const NodeAddr upstream = static_cast<NodeAddr>(position - 1);  // kMainNode for 0.
  (void)m_Transport.WriteReg(upstream, reg::kSwctl, link.swctl);
  if (!link.closeSwitch) {  // Still powered: cut the nodes after it off.
    (void)m_Transport.WriteReg(position, reg::kSwctl, ownSwitch);
  }
  m_HasRejected = true;
  m_RejectedAt = position;
  m_RejectedStuck = !link.closeSwitch;
  m_Rejection = RejectReason::kByApp;
  m_Deadline = nowMs + m_Board.timings.rejectedProbeMs;
  SetState(NetworkState::kRunning);
}

/**
 * Queues an interrupt the stack doesn't consume for the app, and has the chain checked if
 * it may mean a drop.
 */
void Network::Impl::HandleInterrupt(const InterruptPoller::Interrupt& irq) {
  Event event;
  event.type = EventType::kInterrupt;
  event.node = ((irq.source & reg::kIntsrcSlvint) != 0 && (irq.source & reg::kIntsrcMstint) == 0)
                   ? static_cast<NodeAddr>(irq.source & reg::kIntsrcInode)
                   : kMainNode;
  event.interruptType = irq.type;
  Queue(event);
  if (MayMeanDrop(irq.type)) {
    m_CheckNow = true;
  }
}

/**
 * Starts the probe of the next position when it is due, unless no probe is to be made
 * there (the most sub-nodes are in, discovery is paused, a rejected node on its own supply sits
 * there, or the link to it is two-step).
 * @return True if a probe started.
 */
bool Network::Impl::BeginProbe(uint32_t nowMs) {
  const NodeAddr position = static_cast<NodeAddr>(m_SubNodeCount);
  const bool rejectedHere = m_HasRejected && m_RejectedAt == position;
  if (m_Paused || m_SubNodeCount >= m_Board.policy.maxSubNodes ||
      (rejectedHere && m_RejectedStuck)) {
    SetState(NetworkState::kRunning);
    return false;
  }
  if (!TimeReached(nowMs, m_Deadline)) {
    return false;
  }
  ApplyLink(position);
  // Two-step links are not supported yet: not probed; the chain ends here.
  if (m_Links[position].twoStep) {
    m_Deadline = nowMs + m_Board.timings.probeIntervalMs;
    SetState(NetworkState::kRunning);
    return false;
  }
  m_Discovery.Begin(m_Settings, position, DiscoveryRespCycles(position), m_Links, Source(),
                    m_SubNodes[position], rejectedHere ? m_RejectedChipId : nullptr);
  m_Phase = Phase::kProbe;
  return true;
}

/** Phase::kSetup: the identified node's set-up; then it is in, or it is rejected. */
void Network::Impl::StepSetup(uint32_t nowMs) {
  const NodeAddr position = static_cast<NodeAddr>(m_SubNodeCount);
  const NodeSetup::Outcome outcome = m_Setup.Poll(nowMs);
  if (outcome == NodeSetup::Outcome::kBusy) {
    return;
  }
  m_Phase = Phase::kIdle;
  if (m_Setup.Script().SkippedBlocks() > 0) {
    LogNumber(m_Platform, LogLevel::kWarning, "a2b: sub-node ", static_cast<unsigned>(position),
              ": SPI peripheral scripts skipped");
  }
  switch (outcome) {
    case NodeSetup::Outcome::kDone:
      FinishNode(position);
      EndProbe(Discovery::Outcome::kIdentified, nowMs);
      return;
    case NodeSetup::Outcome::kRejected:
      DestroyNode(position, false);
      EndProbe(m_Discovery.Reject(m_Setup.Rejection()), nowMs);
      return;
    default:
      DestroyNode(position, false);
      EndProbe(Discovery::Outcome::kFailed, nowMs);
      return;
  }
}

/**
 * Reviews the node identified at @p position: the default policy (BoardPolicy's power
 * budget over the bus-powered nodes), then the app's listener, which may overrule it.
 * @return RejectReason::kNone to take it in, or why not.
 */
RejectReason Network::Impl::Review(NodeAddr position) {
  NodeReview review;
  review.node = &m_SubNodes[position];
  for (size_t i = 0; i < m_SubNodeCount; ++i) {
    const ModuleInfo& module = m_SubNodes[i].moduleInfo;
    if (!module.localPower) {
      review.busPowerMw += module.maxPowerMw;
      review.busCurrentMa += module.maxCurrentMa;
    }
  }
  const ModuleInfo& module = review.node->moduleInfo;
  const BoardPolicy& policy = m_Board.policy;
  if (!module.localPower &&
      ((policy.maxBusPowerMw != 0 &&
        review.busPowerMw + module.maxPowerMw > policy.maxBusPowerMw) ||
       (policy.maxBusCurrentMa != 0 &&
        review.busCurrentMa + module.maxCurrentMa > policy.maxBusCurrentMa))) {
    review.policy = RejectReason::kPowerBudget;
  }
  if (m_Listener == nullptr) {
    return review.policy;
  }
  m_InListener = true;
  const RejectReason decision = m_Listener->OnNodeIdentified(review);
  m_InListener = false;
  return decision;
}

/**
 * Acts on the end of a probe: counts a node found (Outcome::kIdentified, once it is set
 * up), keeps a rejected node's chip ID, has the chain checked after a failure, and sets
 * the next probe's time.
 */
void Network::Impl::EndProbe(Discovery::Outcome outcome, uint32_t nowMs) {
  const NodeAddr position = static_cast<NodeAddr>(m_SubNodeCount);
  const unsigned number = static_cast<unsigned>(position);
  switch (outcome) {
    case Discovery::Outcome::kIdentified:
      ++m_SubNodeCount;
      m_HasRejected = false;
      RoutingChanged();
      LogNumber(m_Platform, LogLevel::kInfo, "a2b: sub-node ", number, " found and configured");
      QueueNode(EventType::kNodeAdded, position);
      m_Deadline = nowMs;  // The next position at once.
      if (m_SubNodeCount >= m_Board.policy.maxSubNodes) {
        SetState(NetworkState::kRunning);
      }
      return;
    case Discovery::Outcome::kNothing:
      m_Deadline = nowMs + m_Board.timings.probeIntervalMs;
      if (m_State == NetworkState::kDiscovering) {
        LogNumber(m_Platform, LogLevel::kInfo, "a2b: chain up with ", number, " sub-node(s)");
        SetState(NetworkState::kRunning);
      }
      return;
    case Discovery::Outcome::kRejected: {
      m_Rejection = m_Discovery.Rejection();
      RememberRejected(position, !m_Links[position].closeSwitch);
      LogNumber(m_Platform, LogLevel::kWarning, "a2b: sub-node ", number, " rejected");
      Event event;
      event.type = EventType::kNodeRejected;
      event.node = position;
      event.rejection = m_Rejection;
      std::memcpy(event.chipId, m_SubNodes[position].identity.chipId, sizeof(event.chipId));
      Queue(event);
      m_Deadline = nowMs + m_Board.timings.rejectedProbeMs;
      SetState(NetworkState::kRunning);
      return;
    }
    case Discovery::Outcome::kRejectedAgain:
      m_Deadline = nowMs + m_Board.timings.rejectedProbeMs;
      SetState(NetworkState::kRunning);
      return;
    default:
      m_Discovery.Abandon();
      LogNumber(m_Platform, LogLevel::kWarning, "a2b: probe for sub-node ", number,
                " failed; checking the chain");
      m_CheckNow = true;
      m_Deadline = nowMs + m_Board.timings.probeIntervalMs;
      return;
  }
}

/**
 * Enters kStarting: the next poll pulses RESETn (@p resetLine) or reads the main node's
 * IDs. Every sub-node is forgotten, and any rejected one; the routing plan too (it is
 * written again once the chain is up, if the app routes the bus), and probes start from
 * the image's RESPCYCS again.
 */
void Network::Impl::BeginStart(bool resetLine) {
  RemoveNodes(0, RemovalReason::kRestart);
  m_Error = NetworkError::kNone;
  m_Step = resetLine ? Step::kResetLine : Step::kReadId;
  m_HasRejected = false;
  m_RejectRequest = kNoRequest;
  m_CheckNow = false;
  m_Links[0] = m_MainLink;
  m_Settings.respCycles = m_ImageRespCycles;  // The main node's registers are reset.
  RoutingPlanner::Clear(m_Plan);
  m_XbarKnown = 0;
  m_Bandwidth = BandwidthUsage();
  m_RoutingDue = m_RoutingOn;  // Routed again once the chain is up.
  m_MuteDue = false;           // The image is written with DATCTL muted if the app muted.
  m_RouteMute = false;
  m_Settling = false;
  SetState(NetworkState::kStarting);
}

/**
 * The host operations due before the main node is started: BoardConfig's
 * hostOpsBeforeMainNode, at most the script's length.
 * @return Their count; 0 without a host script.
 */
size_t Network::Impl::EarlyHostOps() const {
  if (m_Board.hostScript == nullptr) {
    return 0;
  }
  const size_t size = m_Board.hostScript->Size();
  return (m_Board.hostOpsBeforeMainNode < size) ? m_Board.hostOpsBeforeMainNode : size;
}

/** Enters kConfiguring: the next poll writes the image. */
void Network::Impl::Configure() {
  SetState(NetworkState::kConfiguring);
  m_Step = Step::kWriteImage;
}

/** Enters kDiscovering: the next poll sends the first probe; checks and BECNT follow. */
void Network::Impl::BeginDiscovery() {
  m_Platform.Log(LogLevel::kInfo, "a2b: main node configured");
  SetState(NetworkState::kDiscovering);
  m_Phase = Phase::kIdle;
  const uint32_t nowMs = m_Platform.NowMs();
  m_Deadline = nowMs;
  m_CheckDeadline = nowMs + m_Board.timings.chainCheckMs;
  m_BecntDeadline = nowMs + m_Board.timings.becntResetMs;
  m_Poller.CheckAtNextPoll();
}

/**
 * Writes the image in its order, without the registers the stack owns, with DSCDONE added
 * to its INTMSK2 (an image without INTMSK2 gets none). A run of consecutive registers on
 * one page goes in one Transport call.
 */
Result Network::Impl::WriteMainImage() {
  uint8_t run[Transport::kMaxRegBurst];
  size_t runLen = 0;
  uint16_t runStart = 0;
  for (size_t i = 0; i <= m_Image.Size(); ++i) {
    const bool end = (i == m_Image.Size());
    const uint16_t reg = end ? 0 : m_Image.RegAt(i);
    const bool skip = !end && IsOwnedByStack(reg);
    const bool extends = !end && !skip && runLen > 0 && runLen < sizeof(run) &&
                         reg == runStart + runLen && (reg >> 8) == (runStart >> 8);
    if (runLen > 0 && !extends) {
      const Result result = m_Transport.WriteRegs(kMainNode, runStart, run, runLen);
      if (result != Result::kOk) {
        return result;
      }
      runLen = 0;
    }
    if (end || skip) {
      continue;
    }
    uint8_t value = m_Image.ValueAt(i);
    if (reg == reg::kIntmsk2) {
      value = static_cast<uint8_t>(value | reg::kIntmsk2Dscdone);
    }
    if (reg == reg::kDatctl && m_Muted) {
      value = static_cast<uint8_t>(value & ~(reg::kDatctlDns | reg::kDatctlUps));
    }
    if (runLen == 0) {
      runStart = reg;
    }
    run[runLen++] = value;
  }
  return Result::kOk;
}

/**
 * Starts again from the main-node reset, e.g. after the PLL relocked, or from RESETn
 * (@p resetLine) after the main node failed its check. The sub-nodes are taken out
 * (RemovalReason::kRestart).
 */
void Network::Impl::Restart(const char* why, bool resetLine) {
  m_Platform.Log(LogLevel::kWarning, why);
  CancelProbe();
  m_Runner.Stop();
  BeginStart(resetLine);
}

/**
 * Stops with an error; Start() tries again. The sub-nodes in the bus stay listed until
 * then; a node being set up is dropped.
 */
void Network::Impl::Fail(NetworkError error, const char* why) {
  m_Platform.Log(LogLevel::kError, why);
  CancelProbe();
  m_Runner.Stop();
  m_Error = error;
  SetState(NetworkState::kFailed);
}

/** Abandons a probe or a set-up under way, without I/O; a joining node's object is destroyed. */
void Network::Impl::CancelProbe() {
  m_Discovery.Cancel();
  if (m_Phase == Phase::kRouting) {
    m_Phase = Phase::kIdle;
    RoutingChanged();
  }
  if (m_Phase == Phase::kSetup) {
    m_Setup.Cancel();
    m_Runner.Stop();
    DestroyNode(static_cast<NodeAddr>(m_SubNodeCount), false);
  }
  m_Phase = Phase::kIdle;
}

/** Abandons a probe or a set-up under way, and switches the joining node off. */
void Network::Impl::AbandonProbe() {
  m_Discovery.Abandon();
  CancelProbe();
}

/**
 * The profile of the link to sub-node @p position: the app's (SetLinkProfile()), else the
 * upstream node's DownstreamLink(), else BoardConfig::defaultLink.
 */
LinkProfile Network::Impl::ResolveLink(NodeAddr position) const {
  if (m_HasLinkProfile[position]) {
    return m_LinkProfile[position];
  }
  if (position > 0 && m_Nodes[position - 1] != nullptr) {
    return m_Nodes[position - 1]->DownstreamLink();
  }
  return m_Board.defaultLink;
}

/**
 * Brings the profile settings of the link to @p position up to date (XCVRBINV, whether the
 * switch powers the next node or is bypassed (SWBYP), two-step). If they changed, the upstream
 * node's link is set up again at its next probe.
 */
void Network::Impl::ApplyLink(NodeAddr position) {
  const LinkProfile profile = ResolveLink(position);
  Discovery::Link& link = m_Links[position];
  const bool closeSwitch = (profile.power == LinkProfile::Power::kOnA2bPair);
  // A link without the DC bias needs SWBYP instead of ENSW, or the B side never passes
  // frames (the AD242x, AD243x and AD2437 TRMs, "Data-Only and Power-Only Bus Operation").
  const uint8_t control = static_cast<uint8_t>((profile.bSideInverted ? reg::kControlXcvrbinv : 0) |
                                               (closeSwitch ? 0 : reg::kControlSwbyp));
  const bool twoStep = (profile.discovery != LinkProfile::Discovery::kSingleStep);
  if (link.control == control && link.closeSwitch == closeSwitch && link.twoStep == twoStep) {
    return;
  }
  if (twoStep && !link.twoStep) {
    LogNumber(m_Platform, LogLevel::kWarning, "a2b: link to sub-node ",
              static_cast<unsigned>(position), " is two-step (unsupported); not probed");
  }
  link.control = control;
  link.closeSwitch = closeSwitch;
  link.twoStep = twoStep;
  link.prepared = false;
}

/**
 * Makes the object of the node at @p position, identified just now, in its slot: of the
 * class the app's factory picks, or a plain SubNode.
 * @return The object.
 */
a2b::SubNode* Network::Impl::MakeNode(NodeAddr position) {
  DestroyNode(position, false);
  a2b::SubNode::Context context;
  context.info = &m_SubNodes[position];
  context.boardLink = &m_Board.defaultLink;
  const MakeNodeFn make = (m_Factory != nullptr) ? m_Factory->Find(m_SubNodes[position]) : nullptr;
  void* slot = m_Slots[position].bytes;
  a2b::SubNode* node = (make != nullptr) ? make(slot, context) : nullptr;
  if (node == nullptr) {
    node = Make<a2b::SubNode>(slot, context);
  }
  m_Nodes[position] = node;
  return node;
}

/**
 * Fills in the B-side link of the node at @p position, now set up: its chip's switch mode
 * and power-configuration check, its module info's power configuration, its object's
 * interrupt masks. The link's profile settings follow before its first probe.
 */
void Network::Impl::FinishNode(NodeAddr position) {
  const ChipCaps& caps = *m_Discovery.Caps();
  const a2b::SubNode::InterruptMasks masks = m_Nodes[position]->InterruptMask();
  Discovery::Link& link = m_Links[position + 1];
  link = Discovery::Link();
  link.swctl = caps.switchMode;
  link.hasSwitchConfig = caps.hasSwitchConfig;
  link.powerConfig = m_SubNodes[position].moduleInfo.powerConfig;
  link.intmsk0 = masks.intmsk0;
  link.intmsk1 = masks.intmsk1;
}

/**
 * Destroys the object of the node at @p position, if it has one.
 * @param removed The node was in the bus: SubNode::OnRemoved() first.
 */
void Network::Impl::DestroyNode(NodeAddr position, bool removed) {
  a2b::SubNode* node = m_Nodes[position];
  if (node == nullptr) {
    return;
  }
  if (removed) {
    node->OnRemoved();
  }
  node->~SubNode();
  m_Nodes[position] = nullptr;
}

/**
 * Takes the sub-nodes from @p from to the end of the chain out of the bus, in order: each
 * gets an EventType::kNodeRemoved event and its object's OnRemoved(). A rejected node
 * after them is forgotten.
 */
void Network::Impl::RemoveNodes(NodeAddr from, RemovalReason why) {
  for (size_t i = static_cast<size_t>(from); i < m_SubNodeCount; ++i) {
    const NodeAddr position = static_cast<NodeAddr>(i);
    Event event;
    event.type = EventType::kNodeRemoved;
    event.node = position;
    event.removal = why;
    std::memcpy(event.chipId, m_SubNodes[i].identity.chipId, sizeof(event.chipId));
    Queue(event);
    DestroyNode(position, true);
    m_Transport.ForgetNode(position);
  }
  if (static_cast<size_t>(from) < m_SubNodeCount) {
    m_SubNodeCount = static_cast<size_t>(from);
    m_HasRejected = false;
    if (m_XbarKnown > m_SubNodeCount) {
      m_XbarKnown = m_SubNodeCount;  // A module plugged in there starts with its own crossbar.
    }
    RoutingChanged();
  }
}

/**
 * Keeps the chip ID of the node rejected at @p position, so that it is switched off again
 * at once if it answers there again.
 * @param stuck It stays discovered (its own supply): its position isn't probed again.
 */
void Network::Impl::RememberRejected(NodeAddr position, bool stuck) {
  std::memcpy(m_RejectedChipId, m_SubNodes[position].identity.chipId, sizeof(m_RejectedChipId));
  m_HasRejected = true;
  m_RejectedAt = position;
  m_RejectedStuck = stuck;
}

/** Changes the state, and queues an EventType::kStateChanged event if it changed. */
void Network::Impl::SetState(NetworkState state) {
  if (state == m_State) {
    return;
  }
  m_State = state;
  Event event;
  event.type = EventType::kStateChanged;
  event.state = state;
  Queue(event);
}

/** Queues @p event; a full queue drops it, with one log line until the next dispatch. */
void Network::Impl::Queue(const Event& event) {
  if (m_EventCount >= kMaxEvents) {
    if (!m_EventsLost) {
      m_Platform.Log(LogLevel::kWarning, "a2b: event queue full; events lost");
    }
    m_EventsLost = true;
    return;
  }
  m_Events[(m_EventHead + m_EventCount) % kMaxEvents] = event;
  ++m_EventCount;
}

/** Queues a node's event (kNodeAdded) with its position and chip ID. */
void Network::Impl::QueueNode(EventType type, NodeAddr position) {
  Event event;
  event.type = type;
  event.node = position;
  std::memcpy(event.chipId, m_SubNodes[position].identity.chipId, sizeof(event.chipId));
  Queue(event);
}

/**
 * Gives the queued events to the listener, oldest first; without one they are dropped.
 * Events the handlers queue come in the same round, up to twice the queue's size, so a
 * handler that queues an event for every event it gets can't hold Poll() for ever; the
 * rest wait for the next Poll().
 */
void Network::Impl::Dispatch() {
  m_InListener = true;
  for (size_t n = 0; n < 2 * kMaxEvents && m_EventCount > 0; ++n) {
    const Event event = m_Events[m_EventHead];
    m_EventHead = static_cast<uint8_t>((m_EventHead + 1) % kMaxEvents);
    --m_EventCount;
    if (m_Listener != nullptr) {
      m_Listener->OnEvent(m_Owner, event);
    }
  }
  m_EventsLost = false;
  m_InListener = false;
}

/** The app's module-info source, or the default: the nodes' EEPROMs. */
ModuleInfoSource& Network::Impl::Source() { return (m_Source != nullptr) ? *m_Source : m_Eeprom; }

/**
 * True for the nodes the app may reach: the main node, a sub-node in the bus, or the one
 * being identified or set up (a module-info source reads it).
 */
bool Network::Impl::IsReachable(NodeAddr node) const {
  const bool joining = ((m_Phase == Phase::kProbe || m_Phase == Phase::kSetup) &&
                        static_cast<size_t>(node) == m_SubNodeCount);
  return node == kMainNode ||
         (IsSubNode(node) && (static_cast<size_t>(node) < m_SubNodeCount || joining));
}

/**
 * The chip of a reachable node: the main node's from the last Start(), a sub-node's from
 * its identity, or the joining node's once it is identified.
 * @return Its row of the chip table, or nullptr before the first Start() or before the
 *         joining node is identified.
 */
const ChipCaps* Network::Impl::CapsOf(NodeAddr node) const {
  if (node == kMainNode) {
    return m_MainCaps;
  }
  if (static_cast<size_t>(node) < m_SubNodeCount) {
    const SubNodeIdentity& identity = m_SubNodes[node].identity;
    return LookUpChipCaps(identity.vendorId, identity.productId);
  }
  return m_Discovery.Caps();
}

/**
 * Adds a connection by chip ID; see Network::AddConnection(). Its ends' positions are
 * remembered if they are in the bus now.
 */
Result Network::Impl::AddConnection(const Connection& connection) {
  if (!Joins(connection)) {
    return Result::kInvalidArgument;
  }
  const ConnectionTable::Chain chain = Chain();
  return m_Connections.Add(connection, ConnectionTable::Find(connection.source, kNoNode, chain),
                           ConnectionTable::Find(connection.sink, kNoNode, chain));
}

/** True if @p connection joins two different nodes. */
bool Network::Impl::Joins(const Connection& connection) {
  return !ConnectionTable::SameEnd(connection.source, connection.sink) &&
         !(connection.source.mainNode && connection.sink.mainNode) &&
         !(!connection.source.mainNode && !connection.sink.mainNode &&
           std::memcmp(connection.source.chipId, connection.sink.chipId,
                       sizeof(connection.source.chipId)) == 0);
}

/**
 * Makes the end of a connection at a node in the bus.
 * @param node #kMainNode or a sub-node in the bus.
 * @param group The channel group's ID.
 * @param end Filled in: the main node, or the sub-node's chip ID.
 * @return False if the node isn't in the bus.
 */
bool Network::Impl::EndOf(NodeAddr node, uint8_t group, ConnectionEnd& end) const {
  if (node != kMainNode && (!IsSubNode(node) || static_cast<size_t>(node) >= m_SubNodeCount)) {
    return false;
  }
  end = ConnectionEnd();
  end.mainNode = (node == kMainNode);
  end.group = group;
  if (node != kMainNode) {
    std::memcpy(end.chipId, m_SubNodes[node].identity.chipId, sizeof(end.chipId));
  }
  return true;
}

/**
 * Adds (@p add) or removes the connection between two nodes in the bus, by position; see
 * Network::AddConnection() and Network::RemoveConnection(). An add checks the groups.
 */
Result Network::Impl::ConnectionAt(NodeAddr source, uint8_t sourceGroup, NodeAddr sink,
                                   uint8_t sinkGroup, bool add) {
  Connection connection;
  if (!EndOf(source, sourceGroup, connection.source) || !EndOf(sink, sinkGroup, connection.sink) ||
      source == sink) {
    return Result::kInvalidArgument;
  }
  if (!add) {
    return m_Connections.Remove(connection);
  }
  const ChannelGroups& sourceGroups =
      (source == kMainNode) ? m_Board.routing.groups : m_SubNodes[source].moduleInfo.groups;
  const ChannelGroups& sinkGroups =
      (sink == kMainNode) ? m_Board.routing.groups : m_SubNodes[sink].moduleInfo.groups;
  if (sourceGroups.FindRx(sourceGroup) == nullptr || sinkGroups.FindTx(sinkGroup) == nullptr) {
    return Result::kInvalidArgument;
  }
  return m_Connections.Add(connection, source, sink);
}

/**
 * Sets the connections from streams, all or nothing; see Network::SetAudioRoutes(). First
 * every stream is checked, and the rows the new connections need are counted (a sink
 * already in the table, with @p append, or earlier in the call, takes none); then the table is
 * written, which can't fail.
 */
Result Network::Impl::SetAudioRoutes(const AudioStream* streams, size_t count, bool append) {
  if (streams == nullptr && count > 0) {
    return Result::kInvalidArgument;
  }
  ConnectionEnd added[kMaxConnections];  // Sink ends that take a row of their own.
  size_t addedCount = 0;
  const size_t kept = append ? m_Connections.Count() : 0;
  for (size_t s = 0; s < count; ++s) {
    const AudioStream& stream = streams[s];
    if (stream.sinkCount == 0 || stream.sinkCount > AudioStream::kMaxSinks) {
      return Result::kInvalidArgument;
    }
    Connection connection;
    if (!EndOf(stream.source, stream.sourceGroup, connection.source)) {
      return Result::kInvalidArgument;
    }
    const ChannelGroups& sourceGroups = (stream.source == kMainNode)
                                            ? m_Board.routing.groups
                                            : m_SubNodes[stream.source].moduleInfo.groups;
    if (sourceGroups.FindRx(stream.sourceGroup) == nullptr) {
      return Result::kInvalidArgument;
    }
    for (size_t k = 0; k < stream.sinkCount; ++k) {
      const AudioStream::Sink& sink = stream.sinks[k];
      if (!EndOf(sink.node, sink.group, connection.sink) || sink.node == stream.source) {
        return Result::kInvalidArgument;
      }
      const ChannelGroups& sinkGroups = (sink.node == kMainNode)
                                            ? m_Board.routing.groups
                                            : m_SubNodes[sink.node].moduleInfo.groups;
      if (sinkGroups.FindTx(sink.group) == nullptr) {
        return Result::kInvalidArgument;
      }
      bool known = false;
      for (size_t i = 0; i < addedCount && !known; ++i) {
        known = ConnectionTable::SameEnd(added[i], connection.sink);
      }
      for (size_t i = 0; i < kept && !known; ++i) {
        known = ConnectionTable::SameEnd(m_Connections.At(i).sink, connection.sink);
      }
      if (!known) {
        if (kept + addedCount >= kMaxConnections) {
          return Result::kNoSpace;
        }
        added[addedCount++] = connection.sink;
      }
    }
  }
  if (!append) {
    m_Connections.Clear();
  }
  for (size_t s = 0; s < count; ++s) {
    for (size_t k = 0; k < streams[s].sinkCount; ++k) {
      (void)ConnectionAt(streams[s].source, streams[s].sourceGroup, streams[s].sinks[k].node,
                         streams[s].sinks[k].group, true);
    }
  }
  return Result::kOk;
}

/**
 * Replaces the connections with a list; see Network::ImportConnections(). Checked before the
 * table is touched, so it can't be left half done.
 */
Result Network::Impl::ImportConnections(const Connection* connections, size_t count) {
  if (connections == nullptr && count > 0) {
    return Result::kInvalidArgument;
  }
  if (count > kMaxConnections) {
    return Result::kNoSpace;
  }
  for (size_t i = 0; i < count; ++i) {
    if (!Joins(connections[i])) {
      return Result::kInvalidArgument;
    }
  }
  m_Connections.Clear();
  for (size_t i = 0; i < count; ++i) {
    (void)AddConnection(connections[i]);
  }
  return Result::kOk;
}

/** Connection @p index, or nullptr; see Network::GetConnection(). */
const Connection* Network::Impl::GetConnection(size_t index) const {
  return (index < m_Connections.Count()) ? &m_Connections.At(index) : nullptr;
}

/** Where an end's node is; see Network::ConnectionNode(). */
NodeAddr Network::Impl::ConnectionNode(const ConnectionEnd& end) const {
  return ConnectionTable::Find(end, kNoNode, Chain());
}

/**
 * Builds a plan for the chain now, to report whether one can be built, and has one written
 * at the next idle poll; see Network::ApplyRouting().
 */
RoutingError Network::Impl::ApplyRouting() {
  m_RoutingOn = true;
  if (m_Phase == Phase::kRouting) {
    m_Phase = Phase::kIdle;  // The plan being written is replaced; written again whole.
  }
  if (m_MainCaps == nullptr) {
    m_RoutingDue = true;  // Not started yet: the main node's chip isn't known.
    return RoutingError::kNone;
  }
  NodeAddr node = kNoNode;
  const RoutingError error = BuildPlan(m_Pending, node);
  m_RoutingDue = (error == RoutingError::kNone);
  m_RoutingRetry = m_Platform.NowMs();
  return error;
}

/**
 * Starts writing a plan if one is due and the chain is up (between probes, kRunning): it is
 * built for the chain as it is now. A plan that can't be built is reported.
 * @return True if Phase::kRouting started.
 */
bool Network::Impl::BeginRouting(uint32_t nowMs) {
  if (!m_RoutingDue || m_State != NetworkState::kRunning || !TimeReached(nowMs, m_RoutingRetry)) {
    return false;
  }
  m_RoutingDue = false;
  NodeAddr node = kNoNode;
  const RoutingError error = BuildPlan(m_Pending, node);
  if (error != RoutingError::kNone) {
    RoutingFailed(error, node);
    return false;
  }
  m_Phase = Phase::kRouting;
  m_RouteStep = RouteStep::kMute;
  StepRouting(nowMs);
  return true;
}

/**
 * Phase::kRouting, in four steps (ADI's UpdateSlots is the counterpart):
 * - kMute: OnAudioMute(), then the mute BoardRouting::mute asks for, before the slots change;
 * - kSlots: the slot registers and NEWSTRCT (with the whole bus muted, DATCTL's enables
 *   clear), and the settle;
 * - kCrossbar: the crossbars, which unmute a node muted locally; with a whole-bus mute DATCTL
 *   again, NEWSTRCT and the settle;
 * - kUnmute: the plan is the one written, its main-node RESPCYCS the base of later probes,
 *   and OnAudioUnmute().
 */
void Network::Impl::StepRouting(uint32_t nowMs) {
  if (m_RouteStep == RouteStep::kMute) {
    if (!StartMute()) {
      return;
    }
    m_RouteStep = RouteStep::kSlots;
  }
  if (m_RouteStep == RouteStep::kSlots) {
    if (WriteSlots(nowMs) != Result::kOk) {
      RoutingFailed(RoutingError::kI2cError, kNoNode);
      return;
    }
    m_RouteStep = RouteStep::kCrossbar;
    return;  // The settle; the crossbars at the first poll after it.
  }
  if (m_RouteStep == RouteStep::kCrossbar) {
    if (WriteCrossbars() != Result::kOk) {
      RoutingFailed(RoutingError::kI2cError, kNoNode);
      return;
    }
    if (m_RouteMute && !m_Muted) {
      m_RouteMute = false;  // MainDatctl() has the enables again.
      if (WriteDataControl(nowMs) != Result::kOk) {
        RoutingFailed(RoutingError::kI2cError, kNoNode);
        return;
      }
      m_RouteStep = RouteStep::kUnmute;
      return;  // The settle; the plan is complete at the first poll after it.
    }
  }
  FinishRouting();
}

/**
 * RouteStep::kMute: works out what the plan mutes, tells the app (OnAudioMute(), unless it
 * was told already for a plan that failed to write), then mutes. A node muted locally has all
 * of its TXXBAR pointed at location 31, which nothing fills; they take effect at once, ahead
 * of the new slots. A whole-bus mute is DATCTL, written with the slots.
 * @return True if the slots can be written now; false if the plan failed (reported), or the
 *         app's hook started the routing again (the plan is built again).
 */
bool Network::Impl::StartMute() {
  BuildMuteScope(m_MuteScope);
  m_RouteMute = m_MuteScope.network && !m_Muted;
  if (m_Listener != nullptr && !m_MuteHooked) {
    m_MuteHooked = true;
    m_InListener = true;
    m_Listener->OnAudioMute(m_Owner, m_MuteScope);
    m_InListener = false;
    if (m_Phase != Phase::kRouting) {
      return false;  // The hook called ApplyRouting().
    }
  }
  uint8_t silence[sizeof(NodeRouting::txXbar)];
  std::memset(silence, RoutingPlanner::kUnusedLocation, sizeof(silence));
  for (size_t i = 0; i <= m_Pending.subNodeCount && !m_MuteScope.network; ++i) {
    if (m_MuteScope.nodes[i] && m_Transport.WriteRegs(RoutingPlanner::NodeOf(i), reg::kTxxbar0,
                                                      silence, sizeof(silence)) != Result::kOk) {
      RoutingFailed(RoutingError::kI2cError, kNoNode);
      return false;
    }
  }
  return true;
}

/**
 * Works out what the plan being written mutes: nothing with RoutingMute::kNone; the whole bus
 * with kGlobal, or when the app muted it (MuteNetwork()). With kLocal, each node that would
 * otherwise play the wrong audio between the plan's NEWSTRCT and its crossbar write, since
 * both RXMASK and TXXBAR take effect at once ("without setting the A2B_CONTROL.NEWSTRCT bit",
 * AD2437 TRM, "I2S/TDM Flexible Mapping"): a node with a crossbar whose TXXBAR the plan
 * changes, and a node that plays a slot whose channel the plan's new RXMASK at its source
 * changes (MarkStaleSlots()). Each is muted locally if it can be: it has a
 * crossbar, and leaves location 31 empty. If one can't (in a mixed chain, an AD242x playing
 * an AD243x's slots; or a node playing all 32 locations), the whole bus is muted for the
 * plan, as with kGlobal: silence rather than the wrong channels.
 * @param scope Filled in.
 */
void Network::Impl::BuildMuteScope(MuteScope& scope) const {
  scope = MuteScope();
  scope.network = m_Muted || m_Board.routing.mute == RoutingMute::kGlobal;
  if (scope.network || m_Board.routing.mute != RoutingMute::kLocal) {
    return;
  }
  bool stale[kMaxSubNodes + 1] = {};
  MarkStaleSlots(stale);
  for (size_t i = 0; i <= m_Pending.subNodeCount; ++i) {
    const ChipCaps* caps = CapsOf(RoutingPlanner::NodeOf(i));
    const bool crossbar = caps != nullptr && caps->hasCrossbar;
    if (!stale[i] && !(crossbar && CrossbarChanges(i))) {
      continue;
    }
    if (!crossbar || !LeavesSilence(i)) {
      scope = MuteScope();
      scope.network = true;  // This node can't be muted alone.
      return;
    }
    scope.nodes[i] = true;
  }
}

/**
 * Tells whether the plan being written changes a node's TX crossbar. The crossbar on the bus
 * is the plan written last, or a node's power-on one (TXXBARn = n) for a node that plan
 * doesn't cover.
 * @param index 0 for the main node, n + 1 for sub-node n.
 */
bool Network::Impl::CrossbarChanges(size_t index) const {
  const NodeRouting& now = RegsOf(m_Pending, index);
  const bool known = m_Plan.valid && index <= m_XbarKnown;
  const NodeRouting& before = RegsOf(m_Plan, index);
  bool changed = false;
  for (size_t channel = 0; channel < sizeof(now.txXbar); ++channel) {
    const uint8_t was = known ? before.txXbar[channel] : static_cast<uint8_t>(channel);
    changed = changed || was != now.txXbar[channel];
  }
  return changed;
}

/**
 * Tells whether a node leaves TX frame buffer location 31 empty under the plan being written,
 * so that pointing its crossbar there plays silence: it plays fewer than 32 slots.
 * @param index 0 for the main node, n + 1 for sub-node n.
 */
bool Network::Impl::LeavesSilence(size_t index) const {
  const NodeRouting& now = RegsOf(m_Pending, index);
  size_t played = 0;
  for (uint32_t mask = now.dnMask | now.upMask; mask != 0; mask &= mask - 1) {
    ++played;
  }
  played = (index == 0) ? now.upSlots : played;
  return played < kFrameLocations;
}

/**
 * Marks the nodes that, under the plan being written, play a slot whose channel changes when
 * its source's RXMASK is written (SlotChanges()): from the plan's NEWSTRCT until then, they
 * would play the channel the old RXMASK put in that location. Follows the plan's slots down
 * and up the chain, each link's as a mask of the slots that carry such a channel: the slots
 * each node plays (DNMASK, UPMASK; the main node, each slot it receives that its TXXBAR
 * points at, or every one without a crossbar), passes on (DNSLOTS, UPSLOTS) and adds
 * (LDNSLOTS from DNOFFSET, LUPSLOTS from UPOFFSET; the main node's from location 0).
 * @param stale Set true at [0] for the main node, [n + 1] for sub-node n; the rest are left.
 */
void Network::Impl::MarkStaleSlots(bool* stale) const {
  const size_t nodes = m_Pending.subNodeCount + 1;
  size_t len = 0;
  uint32_t changed = AddSlots(0, 0, m_Pending.main.dnSlots, 0, len);
  for (size_t k = 1; k < nodes; ++k) {
    const NodeRouting& regs = m_Pending.sub[k - 1];
    stale[k] = stale[k] || (changed & regs.dnMask) != 0;
    len = (regs.dnSlots < len) ? regs.dnSlots : len;
    changed =
        AddSlots(k, regs.dnOffset, regs.ldnSlots & static_cast<uint8_t>(~reg::kLdnslotsDnmasken),
                 changed & FirstSlots(len), len);
  }
  len = 0;
  changed = 0;
  for (size_t k = nodes - 1; k >= 1; --k) {
    const NodeRouting& regs = m_Pending.sub[k - 1];
    stale[k] = stale[k] || (changed & regs.upMask) != 0;
    len = (regs.upSlots < len) ? regs.upSlots : len;
    changed = AddSlots(k, regs.upOffset, regs.lupSlots, changed & FirstSlots(len), len);
  }
  const NodeRouting& main = m_Pending.main;
  len = (main.upSlots < len) ? main.upSlots : len;
  const ChipCaps* caps = CapsOf(kMainNode);
  uint32_t played = 0xFFFFFFFFu;  // Without a crossbar, every slot received.
  if (caps != nullptr && caps->hasCrossbar) {
    played = 0;
    for (uint8_t at : main.txXbar) {
      played |= (at < kFrameLocations) ? (1u << at) : 0u;
    }
  }
  stale[0] = stale[0] || (changed & FirstSlots(len) & played) != 0;
}

/**
 * Appends node @p index's slots to a link being followed by MarkStaleSlots(): @p count slots
 * from RX frame buffer location @p first, each marked if SlotChanges().
 * @param changed The link's slots so far that carry a changed channel.
 * @param len The link's slots so far; the new ones are added (32 at most).
 * @return @p changed with the new slots' marks.
 */
uint32_t Network::Impl::AddSlots(size_t index, size_t first, size_t count, uint32_t changed,
                                 size_t& len) const {
  for (size_t j = 0; j < count && len < kFrameLocations; ++j, ++len) {
    if (SlotChanges(index, first + j)) {
      changed |= 1u << len;
    }
  }
  return changed;
}

/**
 * Tells whether RX frame buffer location @p location of a node holds a TDM channel under the
 * RXMASK on the bus (the plan written last, or the power-on one, every channel, for a node
 * that plan doesn't cover) other than the one it holds under the plan being written. A
 * location the old RXMASK leaves empty is taken to send silence, not a wrong channel, as the
 * local mute takes TX location 31 to play silence (neither is in the TRMs). A node without a
 * crossbar has no RXMASK, so its frame buffer doesn't change.
 * @param index 0 for the main node, n + 1 for sub-node n.
 * @param location The location.
 */
bool Network::Impl::SlotChanges(size_t index, size_t location) const {
  const ChipCaps* caps = CapsOf(RoutingPlanner::NodeOf(index));
  if (caps == nullptr || !caps->hasCrossbar) {
    return false;
  }
  const bool known = m_Plan.valid && index <= m_XbarKnown;
  const unsigned before =
      ChannelAt(known ? RxMaskOf(RegsOf(m_Plan, index)) : 0xFFFFFFFFu, location);
  return before < 32 && before != ChannelAt(RxMaskOf(RegsOf(m_Pending, index)), location);
}

/**
 * The plan is written: it becomes the plan written last, the app is told it can unmute
 * (OnAudioUnmute(), if it was told to mute), and EventType::kRoutingApplied is queued.
 */
void Network::Impl::FinishRouting() {
  m_Phase = Phase::kIdle;
  m_RouteMute = false;
  m_Plan = m_Pending;
  m_Plan.valid = true;
  m_XbarKnown = m_Plan.subNodeCount;
  m_Settings.respCycles = m_Plan.main.respCycles;
  m_Connections.Remember(Chain());
  LogNumber(m_Platform, LogLevel::kInfo, "a2b: routing applied, ",
            static_cast<unsigned>(m_Plan.routed), " connection(s)");
  if (m_MuteHooked) {
    m_MuteHooked = false;
    if (m_Listener != nullptr) {
      m_InListener = true;
      m_Listener->OnAudioUnmute(m_Owner, m_MuteScope);
      m_InListener = false;
    }
  }
  Event event;
  event.type = EventType::kRoutingApplied;
  Queue(event);
}

/**
 * DATCTL as it is written now: the image's (both slot enables if it has none), without the
 * enables while the app has the bus muted, or while a plan is written with a whole-bus mute.
 */
uint8_t Network::Impl::MainDatctl() const {
  uint8_t datctl = static_cast<uint8_t>(reg::kDatctlDns | reg::kDatctlUps);
  m_Image.Get(reg::kDatctl, datctl);
  if (m_Muted || (m_Phase == Phase::kRouting && m_RouteMute)) {
    datctl = static_cast<uint8_t>(datctl & ~(reg::kDatctlDns | reg::kDatctlUps));
  }
  return datctl;
}

/**
 * Writes DATCTL to the main node (auto-broadcast to the sub-nodes found by now) and applies
 * it with CONTROL.NEWSTRCT, and starts the settle.
 */
Result Network::Impl::WriteDataControl(uint32_t nowMs) {
  Result result = m_Transport.WriteReg(kMainNode, reg::kDatctl, MainDatctl());
  if (result == Result::kOk) {
    result = m_Transport.WriteReg(
        kMainNode, reg::kControl,
        static_cast<uint8_t>(m_Settings.mainControl | m_Links[0].control | reg::kControlNewstrct));
  }
  if (result == Result::kOk) {
    m_Settling = true;
    m_SettleUntil = nowMs + Discovery::kNewStructSettleMs;
  }
  return result;
}

/** Records the app's mute or unmute; see Network::MuteNetwork(). */
void Network::Impl::MuteNetwork(bool mute) {
  if (m_Muted != mute) {
    m_Muted = mute;
    m_MuteDue = true;
  }
}

/** Carries out MuteNetwork(): DATCTL, NEWSTRCT and the settle; tried again after a failure. */
void Network::Impl::StepMute(uint32_t nowMs) {
  m_MuteDue = false;
  if (WriteDataControl(nowMs) != Result::kOk) {
    m_MuteDue = true;
    m_CheckNow = true;
  }
}

/**
 * Writes each sub-node's slot registers (BCDNSLOTS to RESPCYCS; UPMASK0 to DNOFFSET), the
 * main node's (DNSLOTS, UPSLOTS, RESPCYCS), then CONTROL.NEWSTRCT, and starts the settle.
 */
Result Network::Impl::WriteSlots(uint32_t nowMs) {
  for (size_t i = 0; i < m_Pending.subNodeCount; ++i) {
    const NodeRouting& regs = m_Pending.sub[i];
    const NodeAddr node = static_cast<NodeAddr>(i);
    const uint8_t slots[] = {0,  // BCDNSLOTS: not used with LDNSLOTS.DNMASKEN.
                             regs.ldnSlots,
                             regs.lupSlots,
                             regs.dnSlots,
                             regs.upSlots,
                             regs.respCycles};
    const uint8_t masks[] = {static_cast<uint8_t>(regs.upMask),
                             static_cast<uint8_t>(regs.upMask >> 8),
                             static_cast<uint8_t>(regs.upMask >> 16),
                             static_cast<uint8_t>(regs.upMask >> 24),
                             regs.upOffset,
                             static_cast<uint8_t>(regs.dnMask),
                             static_cast<uint8_t>(regs.dnMask >> 8),
                             static_cast<uint8_t>(regs.dnMask >> 16),
                             static_cast<uint8_t>(regs.dnMask >> 24),
                             regs.dnOffset};
    Result result = m_Transport.WriteRegs(node, reg::kBcdnslots, slots, sizeof(slots));
    if (result == Result::kOk) {
      result = m_Transport.WriteRegs(node, reg::kUpmask0, masks, sizeof(masks));
    }
    if (result != Result::kOk) {
      return result;
    }
  }
  const NodeRouting& main = m_Pending.main;
  const uint8_t slots[] = {main.dnSlots, main.upSlots, main.respCycles};
  Result result = m_Transport.WriteRegs(kMainNode, reg::kDnslots, slots, sizeof(slots));
  // SLOTFMT, DATCTL and I2SRRATE are auto-broadcast: a main-node write reaches only the
  // sub-nodes discovered by then (AD2437 TRM, their register descriptions), so the image's
  // values, written at start-up, never reached them. Written again with each plan, as
  // ADI's stack did after each discovery (G13 golden).
  uint8_t format[] = {0x00, MainDatctl()};
  m_Image.Get(reg::kSlotfmt, format[0]);
  if (result == Result::kOk) {
    result = m_Transport.WriteRegs(kMainNode, reg::kSlotfmt, format, sizeof(format));
  }
  uint8_t reducedRate = 0;
  if (result == Result::kOk && m_Image.Get(reg::kI2srrate, reducedRate)) {
    result = m_Transport.WriteReg(kMainNode, reg::kI2srrate, reducedRate);
  }
  if (result == Result::kOk) {
    result = m_Transport.WriteReg(
        kMainNode, reg::kControl,
        static_cast<uint8_t>(m_Settings.mainControl | m_Links[0].control | reg::kControlNewstrct));
  }
  if (result == Result::kOk) {
    m_Settling = true;
    m_SettleUntil = nowMs + Discovery::kNewStructSettleMs;
  }
  return result;
}

/**
 * Writes every node's RXMASK0-7, then every node's TXXBAR0-31, the main node first; none on
 * a chip without a crossbar (AD242x, whose register map ends at 0x9B). The RX masks go first
 * so that no node muted locally is unmuted (its TXXBAR) while a node whose slots it plays
 * still has its old RXMASK.
 */
Result Network::Impl::WriteCrossbars() {
  for (int pass = 0; pass < 2; ++pass) {
    for (size_t i = 0; i <= m_Pending.subNodeCount; ++i) {
      const NodeAddr node = RoutingPlanner::NodeOf(i);
      const ChipCaps* caps = CapsOf(node);
      if (caps == nullptr || !caps->hasCrossbar) {
        continue;
      }
      const NodeRouting& regs = RegsOf(m_Pending, i);
      const Result result =
          (pass == 0)
              ? m_Transport.WriteRegs(node, reg::kRxmask0, regs.rxMask, sizeof(regs.rxMask))
              : m_Transport.WriteRegs(node, reg::kTxxbar0, regs.txXbar, sizeof(regs.txXbar));
      if (result != Result::kOk) {
        return result;
      }
    }
  }
  return Result::kOk;
}

/**
 * Reports a plan that couldn't be built or written: a log line and
 * EventType::kRoutingFailed. A failed write has the chain checked and the plan tried again
 * after BoardTimings::probeIntervalMs.
 * @param error Why.
 * @param node The node at fault, or kNoNode.
 */
void Network::Impl::RoutingFailed(RoutingError error, NodeAddr node) {
  m_Phase = Phase::kIdle;
  if (error == RoutingError::kI2cError) {
    m_CheckNow = true;
    m_RoutingDue = true;
    m_RoutingRetry = m_Platform.NowMs() + m_Board.timings.probeIntervalMs;
  }
  LogNumber(m_Platform, LogLevel::kWarning, "a2b: routing failed (error ",
            static_cast<unsigned>(error), ")");
  Event event;
  event.type = EventType::kRoutingFailed;
  event.node = node;
  event.routingError = error;
  Queue(event);
}

/**
 * Builds a plan for the chain as it is: each node's channel map and the connections whose
 * nodes are in the bus, the planner's registers, then the response cycles (the board's
 * policy, or the default), which must fit.
 * @param plan Filled with the plan (not valid: not written yet).
 * @param node Set to the node at fault on failure.
 * @return RoutingError::kNone, or why there is no plan.
 */
RoutingError Network::Impl::BuildPlan(RoutingPlan& plan, NodeAddr& node) {
  const ConnectionTable::Chain chain = Chain();
  const RoutingPolicy& policy =
      (m_Board.routing.policy != nullptr) ? *m_Board.routing.policy : m_DefaultPolicy;
  RoutingPlanner::Input& input = m_PlanInput;
  input.subNodeCount = m_SubNodeCount;
  for (size_t i = 0; i <= m_SubNodeCount; ++i) {
    const NodeAddr at = RoutingPlanner::NodeOf(i);
    const ChipCaps* caps = CapsOf(at);
    input.map[i] = (caps != nullptr) ? policy.MapGroups(PortOf(at)) : ChannelMap();
    input.map[i].crossbar = input.map[i].crossbar && caps != nullptr && caps->hasCrossbar;
    input.groups[i] = (i == 0) ? &m_Board.routing.groups : &m_SubNodes[i - 1].moduleInfo.groups;
  }
  input.routeCount = 0;
  for (size_t i = 0; i < m_Connections.Count(); ++i) {
    const NodeAddr source = m_Connections.SourceNode(i, chain);
    const NodeAddr sink = m_Connections.SinkNode(i, chain);
    if (source == kNoNode || sink == kNoNode) {
      continue;  // Waits for its node.
    }
    RoutingPlanner::Route& route = input.routes[input.routeCount++];
    route.source = static_cast<uint8_t>(source + 1);  // kMainNode is -1: index 0.
    route.sourceGroup = m_Connections.At(i).source.group;
    route.sink = static_cast<uint8_t>(sink + 1);
    route.sinkGroup = m_Connections.At(i).sink.group;
  }
  RoutingError error = m_Planner.Build(input, plan, node);
  if (error != RoutingError::kNone) {
    return error;
  }
  RespCycleInput& respInput = m_RespInput;  // A member: kept off the stack.
  RespCycleBase(respInput);
  for (size_t i = 0; i < m_SubNodeCount; ++i) {
    respInput.dnSlots[i] = plan.linkDnSlots[i];
    respInput.upSlots[i] = plan.linkUpSlots[i];
  }
  plan.bandwidth = ComputeBandwidth(respInput);
  m_Bandwidth = plan.bandwidth;
  uint8_t i2sgcfg = 0;
  m_Image.Get(reg::kI2sgcfg, i2sgcfg);
  respInput.respOffset = RoutingPlanner::RespOffset(i2sgcfg, plan.main.upSlots > 0);
  plan.respCycles = (m_SubNodeCount > 0) ? policy.RespCycles(respInput) : ResponseCycles();
  if (m_SubNodeCount == 0) {
    plan.respCycles.fits = true;
    plan.respCycles.main = m_Settings.respCycles;
  }
  if (!plan.respCycles.fits) {
    node = kMainNode;
    return RoutingError::kNoResponseCycles;
  }
  plan.main.respCycles = plan.respCycles.main;
  for (size_t i = 0; i < m_SubNodeCount; ++i) {
    plan.sub[i].respCycles = plan.respCycles.sub[i];
  }
  return RoutingError::kNone;
}

/**
 * The port settings the stack wrote to a node, for its channel map: the main node's from
 * its image, a sub-node's from its module info's register settings (a setting not there is
 * 0, its power-on value).
 * @param node The main node or a sub-node in the chain.
 */
PortSettings Network::Impl::PortOf(NodeAddr node) const {
  PortSettings port;
  port.node = node;
  const ChipCaps* caps = CapsOf(node);
  port.productId = (caps != nullptr) ? caps->productId : 0;
  if (node == kMainNode) {
    m_Image.Get(reg::kI2sgcfg, port.i2sgcfg);
    m_Image.Get(reg::kI2scfg, port.i2scfg);
    m_Image.Get(reg::kPdmctl, port.pdmctl);
    return port;  // A main node's I2SRATE does nothing.
  }
  const ModuleInfo& info = m_SubNodes[node].moduleInfo;
  for (size_t i = 0; i < info.regCount; ++i) {
    uint8_t* field = (info.reg[i] == reg::kI2sgcfg)   ? &port.i2sgcfg
                     : (info.reg[i] == reg::kI2scfg)  ? &port.i2scfg
                     : (info.reg[i] == reg::kI2srate) ? &port.i2srate
                     : (info.reg[i] == reg::kPdmctl)  ? &port.pdmctl
                                                      : nullptr;
    if (field != nullptr) {
      *field = info.value[i];
    }
  }
  return port;
}

/**
 * The response-cycle input common to the plan and the probes: the main node's slot widths
 * (its image's SLOTFMT), the frame rate (BoardRouting) and each link's cable
 * (CableLength()). No slots.
 * @param input Filled in.
 */
void Network::Impl::RespCycleBase(RespCycleInput& input) const {
  input = RespCycleInput();
  uint8_t slotFormat = 0;
  m_Image.Get(reg::kSlotfmt, slotFormat);
  input.dnSlotBits = RoutingPlanner::SlotBits(slotFormat, false);
  input.upSlotBits = RoutingPlanner::SlotBits(slotFormat, true);
  input.frameRateHz = m_Board.routing.frameRateHz;
  for (size_t i = 0; i < static_cast<size_t>(kMaxSubNodes); ++i) {
    input.cableDm[i] = CableLength(static_cast<NodeAddr>(i));
  }
  input.subNodeCount = m_SubNodeCount;
}

/**
 * The DISCVRY value of a probe of @p position: the response cycles of a node there, from
 * the main node's current RESPCYCS (the image's, or the last plan's), by the routing policy.
 */
uint8_t Network::Impl::DiscoveryRespCycles(NodeAddr position) const {
  RespCycleInput input;
  RespCycleBase(input);
  input.subNodeCount = static_cast<size_t>(position) + 1;
  input.mainRespCycles = m_Settings.respCycles;
  const RoutingPolicy& policy =
      (m_Board.routing.policy != nullptr) ? *m_Board.routing.policy : m_DefaultPolicy;
  return policy.RespCycles(input).sub[position];
}

/** The chain changed: if the app routes the bus, a plan is due at the next idle poll. */
void Network::Impl::RoutingChanged() {
  if (m_RoutingOn) {
    m_RoutingDue = true;
  }
}

/** The bus's sub-nodes, as the connection table matches them. */
ConnectionTable::Chain Network::Impl::Chain() const {
  ConnectionTable::Chain chain;
  chain.nodes = m_SubNodes;
  chain.count = m_SubNodeCount;
  return chain;
}

}  // namespace a2b
