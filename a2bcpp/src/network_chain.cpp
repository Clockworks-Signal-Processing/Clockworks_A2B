/**
 * @file network_chain.cpp
 * @brief Network::Impl: the chain while the bus is up (kDiscovering, kRunning): interrupt
 *        checks, probes and node set-up, chain checks, drops, rejections, each link's
 *        profile, and the sub-nodes' objects.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstring>
#include <new>

#include "network_impl.h"

namespace a2b {
namespace {

/** A dropped node's upstream switch opened to the next probe there, at least (the AD2437
    TRM's bus drop flow waits 250 ms after SWCTL = 0; the AD242x TRM's, 100 ms). */
constexpr uint32_t kDropWaitMs = 250;
/** Tries of each read in a chain check (the TRMs: retry an access two to four times). */
constexpr int kCheckTries = 3;

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
  if (m_Phase == Phase::kIdle && m_MuteDue && TimeReached(nowMs, m_MuteRetry)) {
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
  const NodeAddr position = static_cast<NodeAddr>(m_SubNodeCount);
  if (outcome == Discovery::Outcome::kIdentified && !m_Reviewed) {
    TakeChipsPowerConfig(position);
    const RejectReason decision = Review(position);
    if (decision != RejectReason::kNone) {
      m_Phase = Phase::kIdle;
      EndProbe(m_Discovery.Reject(decision), nowMs);
      return;
    }
    m_Reviewed = true;
    if (m_Discovery.NeedsHighVoltage()) {  // Kept by the review: now the 24 V.
      const Discovery::Outcome begun = m_Discovery.BeginHighVoltageStep();
      if (begun != Discovery::Outcome::kBusy) {
        m_Phase = Phase::kIdle;
        EndProbe(begun, nowMs);
      }
      return;
    }
    if (m_Discovery.FirstStepOnly()) {
      LogNumber(m_Platform, LogLevel::kInfo, "a2b: sub-node ", static_cast<unsigned>(position),
                " isn't CFG-4: kept at 5 V");
    }
  }
  m_Phase = Phase::kIdle;
  if (outcome != Discovery::Outcome::kIdentified) {
    EndProbe(outcome, nowMs);
    return;
  }
  m_Setup.Begin(*MakeNode(position), *m_Discovery.Caps(), Source(),
                m_SubNodes[position].moduleInfo.groups, m_Board.routing.frameRateHz);
  m_Phase = Phase::kSetup;
  StepSetup(nowMs);
}

/**
 * Gives the node at @p position, identified just now, its chip's power configuration in
 * place of its image's (ChipCaps::powerConfig): each supported chip has one, and the image of
 * a board with a plug-in module (kChipExternal) can't know which chip it gets. Everything
 * after uses it: the app's review, the console, and the check of what the node detected
 * (Discovery::CheckPowerConfig()), which would otherwise force CFG-4 on an AD2433. A named
 * or unstated image that disagrees is logged.
 */
void Network::Impl::TakeChipsPowerConfig(NodeAddr position) {
  ModuleInfo& info = m_SubNodes[position].moduleInfo;
  const uint8_t chipConfig = m_Discovery.Caps()->powerConfig;
  if (info.powerConfig == chipConfig) {
    return;
  }
  if (info.targetChip != kChipExternal) {
    LogNumber(m_Platform, LogLevel::kWarning, "a2b: sub-node ", static_cast<unsigned>(position),
              ": image's CFG isn't its chip's; chip's used");
  }
  info.powerConfig = chipConfig;
}

/**
 * Between probes: the chain check when it is due (or asked for), the error watch's
 * settings, then BECNT (summed first while watching).
 * @return False if the check started the bus again.
 */
bool Network::Impl::Upkeep(uint32_t nowMs) {
  if ((m_CheckNow || TimeReached(nowMs, m_CheckDeadline)) && !CheckChain(nowMs)) {
    return false;
  }
  ApplyWatch();
  const uint32_t becntMs = m_Board.timings.becntResetMs;
  if (becntMs != 0 && TimeReached(nowMs, m_BecntDeadline)) {
    m_BecntDeadline = nowMs + becntMs;
    SumBitErrors();
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
  FailedAccess failed;
  if (ReadForCheck(kMainNode, reg::kVendor, ids, sizeof(ids), failed) != Result::kOk ||
      ReadForCheck(kMainNode, reg::kControl, &control, 1, failed) != Result::kOk) {
    LogFailure(failed);
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
    const Result result = ReadForCheck(position, reg::kVendor, ids, sizeof(ids), failed);
    if (result != Result::kOk || ids[0] != identity.vendorId || ids[1] != identity.productId) {
      if (result != Result::kOk) {
        LogFailure(failed);
      }
      if (m_Watch) {
        TakeReport(ErrorReport::Cause::kDropped, position, failed);
      }
      Drop(position, nowMs);
      return true;
    }
  }
  return true;
}

/**
 * Reads registers for a chain check, up to three times; the result of the last try.
 * @param failed Set to the read, its tries and their time; FailedAccess::tries is 0 if it
 *        worked.
 */
Result Network::Impl::ReadForCheck(NodeAddr node, uint16_t reg, uint8_t* data, size_t len,
                                   FailedAccess& failed) {
  const uint32_t startMs = m_Platform.NowMs();
  Result result = Result::kI2cError;
  int tries = 0;
  while (tries < kCheckTries && result != Result::kOk) {
    result = m_Transport.ReadRegs(node, reg, data, len);
    ++tries;
  }
  failed = FailedAccess();
  failed.node = node;
  failed.reg = reg;
  failed.tries = static_cast<uint8_t>((result == Result::kOk) ? 0 : tries);
  failed.elapsedMs = m_Platform.NowMs() - startMs;
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
  ClearBypass(position);
  const uint32_t waitMs = (m_Board.timings.probeIntervalMs > kDropWaitMs)
                              ? m_Board.timings.probeIntervalMs
                              : kDropWaitMs;
  m_Deadline = nowMs + waitMs;
  m_ProbeFloor = m_Deadline;
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
  ClearBypass(position);  // A node kept at 5 V loses its frames.
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
 * there, or the link to it is from a B port the stack can't run).
 * @return True if a probe started.
 */
bool Network::Impl::BeginProbe(uint32_t nowMs) {
  const NodeAddr position = static_cast<NodeAddr>(m_SubNodeCount);
  const bool rejectedHere = m_HasRejected && m_RejectedAt == position;
  if (m_Paused || m_SubNodeCount >= m_MaxSubNodes || (rejectedHere && m_RejectedStuck)) {
    SetState(NetworkState::kRunning);
    return false;
  }
  if (!TimeReached(nowMs, m_Deadline)) {
    return false;
  }
  ApplyLink(position);
  // A B port without a handler: not probed; the chain ends here.
  if (m_Links[position].noProbe) {
    m_Deadline = nowMs + m_Board.timings.probeIntervalMs;
    SetState(NetworkState::kRunning);
    return false;
  }
  m_Discovery.Begin(m_Settings, position, DiscoveryRespCycles(position), m_Links, Source(),
                    m_SubNodes[position], rejectedHere ? m_RejectedChipId : nullptr);
  m_Phase = Phase::kProbe;
  m_Reviewed = false;
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
  if (outcome != Discovery::Outcome::kNoPowerGood) {
    m_PowerGoodLow = false;
  }
  switch (outcome) {
    case Discovery::Outcome::kIdentified:
      ++m_SubNodeCount;
      m_HasRejected = false;
      RoutingChanged();
      LogNumber(m_Platform, LogLevel::kInfo, "a2b: sub-node ", number, " found and configured");
      QueueNode(EventType::kNodeAdded, position);
      m_Deadline = nowMs;  // The next position at once.
      if (m_SubNodeCount >= m_MaxSubNodes) {
        SetState(NetworkState::kRunning);
      }
      return;
    case Discovery::Outcome::kNoPowerGood:
      if (!m_PowerGoodLow) {
        m_PowerGoodLow = true;
        LogNumber(m_Platform, LogLevel::kWarning, "a2b: no power-good on the link to sub-node ",
                  number, "; not probed");
      }
      [[fallthrough]];
    case Discovery::Outcome::kNothing:
      m_Deadline = nowMs + m_Board.timings.probeIntervalMs;
      if (m_State == NetworkState::kDiscovering) {
        LogNumber(m_Platform, LogLevel::kInfo, "a2b: chain up with ", number, " sub-node(s)");
        SetState(NetworkState::kRunning);
      }
      return;
    case Discovery::Outcome::kRejected: {
      m_Rejection = m_Discovery.Rejection();
      if (m_Rejection == RejectReason::kNoHighVoltage) {
        m_HasRejected = false;  // Probed again from the first step: the 24 V may come back.
      } else {
        RememberRejected(position, !m_Links[position].closeSwitch);
      }
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
    default: {
      const FailedAccess failed = m_Transport.LastFailure();
      if (m_Watch) {
        TakeReport(ErrorReport::Cause::kProbeFailed, position, failed);
      }
      m_Discovery.Abandon();
      LogNumber(m_Platform, LogLevel::kWarning, "a2b: probe for sub-node ", number,
                " failed; checking the chain");
      LogFailure(failed);
      m_CheckNow = true;
      m_Deadline = nowMs + m_Board.timings.probeIntervalMs;
      return;
    }
  }
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
 * switch powers the next node or is bypassed (SWBYP), two-step and its checks, or
 * unsupported). If they changed, the upstream node's link is set up again at its next probe.
 * A two-step link needs the upstream node's high-power switch and power on the A2B pair;
 * otherwise it runs single-step, without the two-step checks (logged).
 */
void Network::Impl::ApplyLink(NodeAddr position) {
  const LinkProfile profile = ResolveLink(position);
  Discovery::Link& link = m_Links[position];
  const bool closeSwitch = (profile.power == LinkProfile::Power::kOnA2bPair);
  // A link without the DC bias needs SWBYP instead of ENSW, or the B side never passes
  // frames (the AD242x, AD243x and AD2437 TRMs, "Data-Only and Power-Only Bus Operation").
  const uint8_t control = static_cast<uint8_t>((profile.bSideInverted ? reg::kControlXcvrbinv : 0) |
                                               (closeSwitch ? 0 : reg::kControlSwbyp));
  const bool noProbe = (profile.discovery == LinkProfile::Discovery::kUnsupported);
  const bool asksTwoStep = (profile.discovery == LinkProfile::Discovery::kTwoStep);
  const ChipCaps* upstream = CapsOf(static_cast<NodeAddr>(position - 1));  // Main for 0.
  const bool twoStep =
      asksTwoStep && closeSwitch && upstream != nullptr && upstream->hasHighPowerSwitch;
  const bool checkPowerGood = twoStep && profile.checkPowerGood;
  const bool checkHighVoltage = profile.checkHighVoltage && (twoStep || !asksTwoStep);
  if (link.control == control && link.closeSwitch == closeSwitch && link.noProbe == noProbe &&
      link.twoStep == twoStep && link.checkPowerGood == checkPowerGood &&
      link.powerGoodGpio == profile.powerGoodGpio && link.checkHighVoltage == checkHighVoltage) {
    return;
  }
  if (noProbe && !link.noProbe) {
    LogNumber(m_Platform, LogLevel::kWarning, "a2b: link to sub-node ",
              static_cast<unsigned>(position), ": Unsupported B port type; not probed");
  }
  if (asksTwoStep && !twoStep) {
    LogNumber(m_Platform, LogLevel::kInfo, "a2b: link to sub-node ",
              static_cast<unsigned>(position), ": no 24 V switch before it; single-step");
  }
  link.control = control;
  link.closeSwitch = closeSwitch;
  link.noProbe = noProbe;
  link.twoStep = twoStep;
  link.checkPowerGood = checkPowerGood;
  link.powerGoodGpio = profile.powerGoodGpio;
  link.checkHighVoltage = checkHighVoltage;
  link.prepared = false;
}

/**
 * Clears SWBYP on the node before @p position, if a two-step link has it set (a node kept
 * at 5 V, now dropped or rejected): the node there loses its frames. Its CONTROL keeps the
 * link's XCVRBINV, and on the main node its settings. A failed write is left to the chain
 * check.
 */
void Network::Impl::ClearBypass(NodeAddr position) {
  Discovery::Link& link = m_Links[position];
  if (!link.bypass) {
    return;
  }
  link.bypass = false;
  const NodeAddr upstream = static_cast<NodeAddr>(position - 1);  // kMainNode for 0.
  const uint8_t control = (upstream == kMainNode)
                              ? static_cast<uint8_t>(m_Settings.mainControl | link.Control())
                              : link.Control();
  (void)m_Transport.WriteReg(upstream, reg::kControl, control);
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
  link.intmsk0 = LinkIntmsk0(position);
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
    m_Watched[i + 1] = false;  // A node found there again gets the watch's settings again.
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

}  // namespace a2b
