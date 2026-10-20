/**
 * @file network_routing.cpp
 * @brief Network::Impl: the app's connections, routing plans (built, written, kept as the
 *        chain changes), the routing and whole-bus mutes, and the response cycles.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstring>

#include "network_impl.h"

namespace a2b {
namespace {

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

}  // namespace

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
    result =
        m_Transport.WriteReg(kMainNode, reg::kControl,
                             static_cast<uint8_t>(m_Settings.mainControl | m_Links[0].Control() |
                                                  reg::kControlNewstrct));
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

/**
 * Carries out MuteNetwork(): DATCTL, NEWSTRCT and the settle. A failed write is tried again
 * #BoardTimings::probeIntervalMs later; the polls in between check the chain, so a main node
 * that has gone restarts the bus rather than being written to on every poll.
 */
void Network::Impl::StepMute(uint32_t nowMs) {
  m_MuteDue = false;
  if (WriteDataControl(nowMs) != Result::kOk) {
    m_MuteDue = true;
    m_MuteRetry = nowMs + m_Board.timings.probeIntervalMs;
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
    result =
        m_Transport.WriteReg(kMainNode, reg::kControl,
                             static_cast<uint8_t>(m_Settings.mainControl | m_Links[0].Control() |
                                                  reg::kControlNewstrct));
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
