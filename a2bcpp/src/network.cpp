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
 * only costs time. TODO(test on hardware): an AD2433 main node.
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
 * probed again until then (a known limit; see the roadmap).
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
 * n = 0 there is none), whose default is the link that node's image gives (its B port type
 * and its chip, LinkProfile::ForBPort()); BoardConfig::defaultLink, for the main node's
 * link. The profile is looked up again before each probe of n, so a change
 * applies to the next probe: the upstream node's link is then set up again (masks,
 * XCVRBINV, the power-configuration check). A link whose node is in the bus keeps its
 * settings until the node is found again. The profile gives XCVRBINV and whether the
 * upstream node's switch powers the next node (LinkProfile::Power::kOnA2bPair). Two-step
 * links are not supported yet: SetLinkProfile() refuses them, and a link whose
 * DownstreamLink() asks for one is not probed (the chain ends there, with a log line); so is
 * a link from a B port the stack can't run yet (Discovery::kUnsupported, "Unsupported B
 * port type", CustomBPortLink()).
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
 * SetCableLength(), else the upstream node's image (its B-side cable, in metres), else
 * BoardRouting::cableDm (4 m by default); an app change re-plans a routed bus. The routing
 * registers are the stack's: the image's values for them are not written (IsOwnedByStack()), as
 * ADI's PnP layer zeroed DNSLOTS until routing ran.
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

#include <cstring>
#include <new>

#include "network_impl.h"

namespace a2b {

/** See network_impl.h. */
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
 * Makes a stopped Network. Nothing is sent until Start() and Poll().
 * @param platform Host I2C, time and GPIO; must outlive the Network.
 * @param board The bus's configuration; must outlive the Network. It may be filled in after
 *        the Network is made, before Start(); the stack reads it at Start() and as the bus
 *        runs (timings, policy, routing), so change it only while the bus is stopped.
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
 *         Result::kNotSupported if @p profile is two-step (not supported yet) or
 *         Discovery::kUnsupported.
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
 * place of the upstream node's image's B-side cable length and BoardRouting::cableDm: the
 * app's choice, a measured length for instance (the stack doesn't measure cables). It applies to
 * the next probe's DISCVRY and the next routing plan: if the app routes the bus (ApplyRouting())
 * and the link is in the chain, a plan with it is written at the next idle poll, but only if the
 * length in use changes. It outlasts Start(). There is no upper limit here: a length too long
 * for the slots fails the plan (RoutingError::kNoResponseCycles). Declare the real length: one
 * longer than the cable can stop discovery past that link (hardware_notes.md).
 * @param position The sub-node the link leads to, 0-15 (0: the main node's link).
 * @param decimetres The cable, from 1.
 * @return Result::kOk, or Result::kInvalidArgument if @p position or @p decimetres is out
 *         of range.
 */
Result Network::SetCableLength(NodeAddr position, uint16_t decimetres) {
  return m_Impl->SetCableLength(position, decimetres);
}

/**
 * Forgets the app's cable length for the link to sub-node @p position: the upstream node's
 * image's, else BoardRouting::cableDm, applies again, as for Network::SetCableLength().
 * @param position The sub-node the link leads to, 0-15.
 * @return Result::kOk, or Result::kInvalidArgument if @p position is not 0-15.
 */
Result Network::ClearCableLength(NodeAddr position) { return m_Impl->ClearCableLength(position); }

/**
 * Tells the cable length the response cycles take for the link to sub-node @p position.
 * @param position The sub-node the link leads to, 0-15.
 * @return The app's length (SetCableLength()); else the upstream node's B-side cable from its
 *         image (ModuleInfo::bCableDm), if it gives one; else
 *         BoardRouting::cableDm. In decimetres; 0 if @p position is not 0-15.
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
 * @param addr The peripheral: PeripheralAddress::I2c(addr) (an SPI address is refused).
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
 * Spread-spectrum clocking on every node (ADI's a2b_ConfigSpreadSpectrum: the common
 * settings broadcast and each sub-node's PLLCTL; the main node's PLLCTL comes from its
 * export). Not supported yet; see ChipCaps::hasSpreadSpectrum.
 * @param enable On or off.
 * @return Result::kNotSupported.
 */
Result Network::SetSpreadSpectrum(bool enable) {
  (void)enable;
  return Result::kNotSupported;
}

/**
 * GPIO over distance: ties GPIO pin @p pin of @p node to the bus ports @p busPorts (the
 * node's GPIODEN, GPIODnMSK, GPIODINV; ADI's a2b_SetupGPIOOverDistance). Not supported yet.
 * @param node The node, #kMainNode or a sub-node in the bus.
 * @param pin The GPIO pin, 0-7.
 * @param busPorts The bus ports (bits 0-7) it drives or follows.
 * @param output The pin follows the bus ports (an output); false: it drives them.
 * @param inverted The pin's level is inverted.
 * @return Result::kNotSupported.
 */
Result Network::SetupGpioOverDistance(NodeAddr node, uint8_t pin, uint8_t busPorts, bool output,
                                      bool inverted) {
  (void)node;
  (void)pin;
  (void)busPorts;
  (void)output;
  (void)inverted;
  return Result::kNotSupported;
}

/**
 * Frees GPIO pin @p pin of @p node from GPIO over distance (ADI's
 * a2b_ClearGPIOOverDistance). Not supported yet.
 * @param node The node.
 * @param pin The GPIO pin, 0-7.
 * @return Result::kNotSupported.
 */
Result Network::ClearGpioOverDistance(NodeAddr node, uint8_t pin) {
  (void)node;
  (void)pin;
  return Result::kNotSupported;
}

/**
 * Reads the bus ports' levels as the main node sees them (GPIODDAT; ADI's
 * a2b_ReadVirtualIOPort). Not supported yet.
 * @param ports Set to the levels, bit n for bus port n.
 * @return Result::kNotSupported (@p ports is set to 0).
 */
Result Network::ReadVirtualIoPort(uint8_t& ports) {
  ports = 0;
  return Result::kNotSupported;
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
const SubNodeInfo* Network::SubNodeInfoAt(NodeAddr position) const {
  return m_Impl->SubNodeInfoAt(position);
}

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
  m_MaxSubNodes = m_Board.policy.maxSubNodes;  // Checked above; the board may change later.
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
  if (!IsSubNode(position) || decimetres == 0) {
    return Result::kInvalidArgument;
  }
  const uint16_t before = CableLength(position);
  m_CableDm[position] = decimetres;
  CableChanged(position, before);
  return Result::kOk;
}

/** Forgets the app's cable length for a link; see Network::ClearCableLength(). */
Result Network::Impl::ClearCableLength(NodeAddr position) {
  if (!IsSubNode(position)) {
    return Result::kInvalidArgument;
  }
  const uint16_t before = CableLength(position);
  m_CableDm[position] = 0;
  CableChanged(position, before);
  return Result::kOk;
}

/**
 * A plan is due only if the length in use for the link to @p position changed and that link
 * is in the chain: a new plan mutes the audio while it is written. A link past the chain
 * takes its length at its next probe.
 */
void Network::Impl::CableChanged(NodeAddr position, uint16_t before) {
  if (CableLength(position) != before && static_cast<size_t>(position) < m_SubNodeCount) {
    RoutingChanged();
  }
}

/** The cable length in use for a link; see Network::CableLength(). */
uint16_t Network::Impl::CableLength(NodeAddr position) const {
  if (!IsSubNode(position)) {
    return 0;
  }
  if (m_CableDm[position] != 0) {
    return m_CableDm[position];
  }
  if (position > 0 && m_Nodes[position - 1] != nullptr) {
    const uint16_t imageDm = m_SubNodes[position - 1].moduleInfo.bCableDm;  // Its B side.
    if (imageDm != 0) {
      return imageDm;
    }
  }
  return m_Board.routing.cableDm;
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

/**
 * Probes again at the next poll, but never inside a wait the bus needs: m_Deadline also times
 * the start-up steps (an INV/MSTR write inside SUSTAIN can leave an AD243x main node unusable
 * until power-cycled), and a dropped node's position waits for its switch to settle
 * (m_ProbeFloor). See Network::ResumeDiscovery().
 */
void Network::Impl::ResumeDiscovery() {
  if (!m_Paused) {
    return;
  }
  m_Paused = false;
  if (m_State != NetworkState::kDiscovering && m_State != NetworkState::kRunning) {
    return;
  }
  const uint32_t nowMs = m_Platform.NowMs();
  m_Deadline = TimeReached(nowMs, m_ProbeFloor) ? nowMs : m_ProbeFloor;
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

/** Sub-node @p position, or nullptr; see Network::SubNodeInfoAt(). */
const SubNodeInfo* Network::Impl::SubNodeInfoAt(NodeAddr position) const {
  return (IsSubNode(position) && static_cast<size_t>(position) < m_SubNodeCount)
             ? &m_SubNodes[position]
             : nullptr;
}

/** Sub-node @p position's object, or nullptr; see Network::NodeObject(). */
a2b::SubNode* Network::Impl::NodeObject(NodeAddr position) {
  return (IsSubNode(position) && static_cast<size_t>(position) < m_SubNodeCount) ? m_Nodes[position]
                                                                                 : nullptr;
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

}  // namespace a2b
