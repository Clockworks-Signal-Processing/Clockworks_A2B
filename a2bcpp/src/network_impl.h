/**
 * @file network_impl.h
 * @brief Network::Impl, the Network's state and work, shared by the files that implement it:
 *        network.cpp (the public calls, Poll(), events), network_startup.cpp (the main
 *        node's start-up and configuration), network_chain.cpp (probes, upkeep, drops,
 *        rejections, links and node objects) and network_routing.cpp (connections, routing
 *        plans and muting). Internal.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_NETWORK_IMPL_H_
#define A2BCPP_NETWORK_IMPL_H_

#include <cstddef>
#include <cstdint>

#include "a2b/eeprom_source.h"
#include "a2b/network.h"
#include "chip_caps.h"
#include "connection_table.h"
#include "detail/regdefs.h"
#include "discovery.h"
#include "interrupt_poller.h"
#include "node_setup.h"
#include "routing_planner.h"
#include "script_runner.h"
#include "transport.h"

namespace a2b {

/**
 * Logs @p before, the number @p n (0-99), then @p after, without the C library's printf
 * (whose stack the RAM report can't see).
 */
void LogNumber(Platform& platform, LogLevel level, const char* before, unsigned n,
               const char* after);

/// The Network's state and work, in the storage inside the Network object.
class Network::Impl {
 public:
  /** Buffer for a peripheral script's blocks and units: the 12-bit size field's range. */
  static constexpr size_t kPeripheralBufLen = 4096;

  /** Events the queue holds between two dispatches. */
  static constexpr size_t kMaxEvents = 32;

  /** No RejectNode() request is waiting. */
  static constexpr NodeAddr kNoRequest = kMaxSubNodes;

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
  const SubNodeInfo* SubNodeInfoAt(NodeAddr position) const;  ///< See Network::SubNodeInfoAt().
  a2b::SubNode* NodeObject(NodeAddr position);                ///< See Network::NodeObject().
  RejectReason LastRejection() const { return m_Rejection; }  ///< See Network::LastRejection().
  bool DiscoveryPaused() const { return m_Paused; }           ///< See Network::DiscoveryPaused().
  void SetErrorWatch(bool on);                                ///< See Network::SetErrorWatch().
  bool ErrorWatch() const { return m_Watch; }                 ///< See Network::ErrorWatch().
  /// See Network::LastErrorReport().
  const ErrorReport& LastErrorReport() const { return m_Report; }
  uint32_t BitErrorCount(NodeAddr node) const;                  ///< See Network::BitErrorCount().
  void ClearErrors() { m_ClearErrorsDue = true; }               ///< See Network::ClearErrors().
  Result ReadErrorState(NodeAddr node, NodeErrorState& state);  ///< See Network::ReadErrorState().

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
  Result ReadForCheck(NodeAddr node, uint16_t reg, uint8_t* data, size_t len,
                      FailedAccess& failed);     ///< With retries.
  void Drop(NodeAddr position, uint32_t nowMs);  ///< Takes out a dropped node and after.
  void HandleRejectRequest(uint32_t nowMs);      ///< Carries out RejectNode().
  void HandleInterrupt(const InterruptPoller::Interrupt& irq);  ///< Reports an interrupt.
  void LogFailure(const FailedAccess& failed);                  ///< Names a failed access.
  void ApplyWatch();                                            ///< The error watch's settings.
  Result WatchNode(NodeAddr node);                              ///< One node's BECCTL and INTMSK0.
  uint8_t LinkIntmsk0(NodeAddr position) const;  ///< A sub-node's INTMSK0, watch included.
  void SumBitErrors();                           ///< Adds each node's BECNT to its sum.
  Result ReadErrorRegs(NodeAddr node, NodeErrorState& state);  ///< SWSTAT, INTPND0-BECNT, NODE.
  void TakeReport(ErrorReport::Cause cause, NodeAddr position,
                  const FailedAccess& failed);  ///< The error watch's report.
  bool BeginProbe(uint32_t nowMs);              ///< Starts the next probe.
  void StepSetup(uint32_t nowMs);               ///< Phase::kSetup.
  bool BeginRouting(uint32_t nowMs);            ///< Starts writing a due plan.
  void StepRouting(uint32_t nowMs);             ///< Phase::kRouting.
  Result WriteSlots(uint32_t nowMs);            ///< Slot registers, NEWSTRCT.
  Result WriteCrossbars();                      ///< RXMASK and TXXBAR.
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
  void CableChanged(NodeAddr position, uint16_t before);      ///< Plans again if it matters.
  ConnectionTable::Chain Chain() const;                       ///< The sub-nodes, for connections.
  void TakeChipsPowerConfig(NodeAddr position);               ///< The chip's CFG, not the image's.
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
  void ClearBypass(NodeAddr position);                ///< SWBYP off on the node before it.
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
  /** The node the probe found has been reviewed (a two-step link's, before its 24 V). */
  bool m_Reviewed = false;
  bool m_PowerGoodLow = false;            ///< A two-step link's power-good was low (logged once).
  uint32_t m_Deadline = 0;                ///< The next step, or the next probe.
  uint32_t m_ProbeFloor = 0;              ///< No probe before then, even on ResumeDiscovery().
  size_t m_MaxSubNodes = 1;               ///< BoardPolicy::maxSubNodes, as checked at Start().
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
  bool m_Muted = false;           ///< MuteNetwork(true) is in force.
  bool m_MuteDue = false;         ///< DATCTL is to be written at the next idle poll.
  uint32_t m_MuteRetry = 0;       ///< Not before then (after a failed write).
  bool m_RouteMute = false;       ///< The plan being written has DATCTL's slot enables clear.
  bool m_MuteHooked = false;      ///< OnAudioMute() was called, OnAudioUnmute() not yet.
  MuteScope m_MuteScope;          ///< What the plan being written mutes.
  bool m_Watch = false;           ///< SetErrorWatch(true) is in force.
  bool m_ClearErrorsDue = false;  ///< ClearErrors(): at the next poll between probes.
  /** Each node has the watch's settings: [0] the main node, [n + 1] sub-node n. */
  bool m_Watched[kMaxSubNodes + 1] = {};
  uint8_t m_SavedBecctl[kMaxSubNodes + 1] = {};  ///< Each node's BECCTL before the watch.
  uint8_t m_SavedMainIntmsk0 = 0;                ///< The main node's INTMSK0 before the watch.
  uint32_t m_BecntSum[kMaxSubNodes + 1] = {};    ///< Each node's BECNT, summed while watching.
  ErrorReport m_Report;                          ///< The last report.
};

}  // namespace a2b

#endif  // A2BCPP_NETWORK_IMPL_H_
