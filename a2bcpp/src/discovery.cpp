/**
 * @file discovery.cpp
 * @brief Discovery: single-step discovery of the next position of a chain, up to the new
 *        node's identification.
 *
 * Follows the single-step path of ADI's master plugin (a2bplugin-master/src/
 * discovery.c: a2b_dscvryPreMasterInit, a2b_dscvryPreSlaveInit, a2b_dscvryNodeDiscovered,
 * a2b_dscvryNodeComplete) and the PnP layer's handling of a new node
 * (a2bpnp_local.c), in the order of the TRMs' discovery flow ("Discovery Flow in Single
 * Wire Pair System"). Sub-node n is found through the B side of the node before it, its
 * upstream node: the main node for sub-node 0, sub-node n - 1 after that.
 *
 * Each node's B side is a Link, which Network fills in: the switch settings from the
 * node's chip and module info, the link settings (XCVRBINV, whether the next node is
 * powered over the A2B pair) from the link's LinkProfile, the interrupt masks from the
 * node's SubNode object.
 *
 * 1. Prepare, once per upstream node and whenever Network changes its link settings (the
 *    main node: once per start-up):
 *    - a sub-node gets its interrupt masks (by default INTMSK0 = PWRERR, INTMSK1 = 0: the
 *      ADI's masks, so power faults are seen during discovery; the main node's come from
 *      its image) and CONTROL with the link's XCVRBINV, which the TRMs want before ENSW,
 *      and, for a link not powered over the A2B pair, SWBYP: the B side passes frames only
 *      once the switch has closed, which it never does onto a node drawing no bias
 *      current, so the TRMs ("Data-Only and Power-Only Bus Operation") set SWBYP instead
 *      of ENSW;
 *    - on a chip with SWSTAT2 (AD243x), the detected power configuration
 *      (SWSTAT2.HPSW_CFG_DET) is checked against the expected one: the board's for the
 *      main node, the module info's for a sub-node. If they differ, SWCTL2.HPSW_CFG and
 *      SWCTL.CFG_DET_OV override it, and the probe waits 100 ms (the TRMs' "WAIT 100 ms
 *      delay"; ADI's A2B_HIGH_PWR_SWITCH_DELAY). CFG_DET_OV is then kept in every SWCTL
 *      write to that node.
 * 2. Probe. The nodes before the upstream node go to switch mode 2 (inrush) with ENSW,
 *    as ADI's PnP layer does around each probe (a2b_pnp_PrepareSwctlforBD:
 *    SWCTL 0x21). INTPND2.DSCDONE is cleared. For sub-node 0 the main node's RESPCYCS and
 *    CONTROL with NEWSTRCT (and its link's XCVRBINV and SWBYP, which every later write of
 *    the main node's CONTROL keeps) are written, and then nothing for the NEWSTRCT settle
 *    (at least 1 ms, as ADI's a2b_delayForNewStruct: the TRMs apply a new structure
 *    within 5 superframes, and the main node holds off its internal INTTYPE reads
 *    meanwhile). The upstream node's SWCTL gets
 *    its mode, with ENSW if the next node is powered over the A2B pair (the switches of
 *    links powered otherwise are left open everywhere else). NODEADR points at
 *    the upstream sub-node (the TRMs: NODEADR.NODE = n - 1 when DISCVRY is written for
 *    node n). Then DISCVRY: the position's response cycles, which Network works out from
 *    the main node's RESPCYCS with the TRMs' formula (RoutingPolicy::RespCycles()). ADI's PnP layer
 * wrote the main node's value less 4 per position (a2bpnp_local.c:410).
 * 3. DSCDONE, from Network's interrupt checks. At the deadline one last check is made
 *    before the probe is given up; another interrupt it reads is handed to Network
 *    (TakeInterrupt()), which reports it. Then the upstream node's switch is opened in its mode
 *    (so the last node of a chain keeps its mode with ENSW clear: SWCTL 0x10),
 *    CONTROL.ENDDSC ends discovery, and the nodes before it go back to their mode.
 * 4. Identify. The upstream node goes to mode 2 while VENDOR, PRODUCT, VERSION and
 *    CAPABILITY are read; then every node up to it goes back to its mode with ENSW
 *    (0x21/0x11, a2b_pnp_RestoreSwctlAftrBD); then CHIPID. A chip without a
 *    ChipCaps row is rejected. The module-info source gives the image (the default source
 *    reads it from the node's EEPROM), which must pass ParseModuleInfo() and, if it names
 *    a transceiver, name the node's (ImageFitsChip(); else RejectReason::kWrongChip). The
 *    probe ends with Outcome::kIdentified; Network makes the node's SubNode object and sets
 *    it up (NodeSetup), and fills in its Link.
 *
 * A rejected node, here, in its set-up or by the app (Reject()), has its power switched
 * off (the upstream node's ENSW cleared). A node on its own supply stays discovered but
 * unconfigured: Network doesn't probe its position again until Start() (a known limit,
 * ROADMAP.md). Network probes a bus-powered rejected node's position
 * again, and gives Begin() the rejected chip ID: if the same node answers, it is switched
 * off again straight after its CHIPID is read (Outcome::kRejectedAgain), without its
 * module info.
 *
 * A probe cut short by a failed transfer is tidied up by Abandon(), as far as the bus
 * lets it: the new node switched off, discovery ended if DSCDONE never came, and the
 * switches before it back in their mode. Network then checks the chain.
 *
 * Not done, each an intended difference from ADI's trace (see a2bcpp/docs/timing.md and
 * the G13 test): the AD2437 24 V check with VMTR (with two-step links), ADI's Pi build's
 * response-cycle search (A2B_FEATURE_OPTIMAL_RESPCYCS; a RespCycles() policy could do it),
 * and ADI's reads of VENDOR on the main node and the upstream node before each probe.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "discovery.h"

#include <cstring>

#include "detail/regdefs.h"

namespace a2b {

/**
 * Makes an idle Discovery.
 * @param transport The bus; must outlive it.
 * @param poller For the last interrupt check before a probe is given up.
 */
Discovery::Discovery(Transport& transport, InterruptPoller& poller)
    : m_Transport(transport), m_Poller(poller) {}

/**
 * Starts a probe for the sub-node at @p position. Nothing is sent until the next Poll().
 * @param settings What every probe of the bus writes.
 * @param position The position to probe: the number of sub-nodes already in the bus.
 * @param respCycles DISCVRY: the response cycles of a node at @p position.
 * @param links The bus's links, #kMaxSubNodes + 1 of them: [0] is the main node's,
 *        [n + 1] sub-node n's. Entries 0 to @p position must be set. Must stay valid
 *        while the probe runs, and until Reject() for a node it identified.
 * @param source Where the node's module info comes from.
 * @param node Filled in when a node is identified; must stay valid while the probe runs.
 * @param rejectedChipId The chip ID of the node rejected at @p position before, or
 *        nullptr: if it answers again, it is switched off at once. Must stay valid while
 *        the probe runs.
 */
void Discovery::Begin(const Settings& settings, NodeAddr position, uint8_t respCycles, Link* links,
                      ModuleInfoSource& source, SubNodeInfo& node, const uint8_t* rejectedChipId) {
  m_Settings = settings;
  m_Position = position;
  m_RespCycles = respCycles;
  m_Links = links;
  m_Source = &source;
  m_Node = &node;
  m_Caps = nullptr;
  m_Rejection = RejectReason::kNone;
  m_RejectedChipId = rejectedChipId;
  m_Sent = false;
  m_Found = false;
  m_Step = Step::kPrepare;
}

/**
 * Advances the probe.
 * @param nowMs Platform::NowMs().
 * @param irq An interrupt Network found at this poll, or nullptr.
 * @return Outcome::kBusy until the probe ends, then how it ended. Once it has ended, and
 *         before Begin(), Outcome::kNothing.
 */
Discovery::Outcome Discovery::Poll(uint32_t nowMs, const InterruptPoller::Interrupt* irq) {
  switch (m_Step) {
    case Step::kIdle:
      return Outcome::kNothing;
    case Step::kPrepare:
      return Prepare(nowMs);
    case Step::kSettle:
      return TimeReached(nowMs, m_Deadline) ? SendProbe(nowMs) : Outcome::kBusy;
    case Step::kProbe:
      return SendProbe(nowMs);
    case Step::kNewStruct:
      return TimeReached(nowMs, m_Deadline) ? SwitchOnAndProbe(nowMs) : Outcome::kBusy;
    case Step::kWaitDscdone: {
      if (irq != nullptr && irq->type == reg::kInttypeDscdone) {
        return Identify();
      }
      if (!TimeReached(nowMs, m_Deadline)) {
        return Outcome::kBusy;
      }
      InterruptPoller::Interrupt last;
      bool found = false;
      if (m_Poller.CheckNow(last, found) != Result::kOk) {
        m_Step = Step::kIdle;
        return Outcome::kFailed;
      }
      if (found && last.type == reg::kInttypeDscdone) {
        return Identify();
      }
      if (found) {  // Someone else's: Network reports it (TakeInterrupt()).
        m_Other = last;
        m_HasOther = true;
      }
      return EndProbe();
    }
  }
  return Outcome::kFailed;
}

/**
 * Tells whether the probe is in the NEWSTRCT settle after sub-node 0's CONTROL write, when
 * Network makes no bus access at all (no interrupt reads either).
 * @param nowMs Platform::NowMs().
 * @return True until the settle ends.
 */
bool Discovery::Settling(uint32_t nowMs) const {
  return m_Step == Step::kNewStruct && !TimeReached(nowMs, m_Deadline);
}

/**
 * Gives an interrupt the probe's last check read that wasn't its DSCDONE (a sub-node's,
 * say), once, for Network to handle as any other: reading INTTYPE cleared it, so it would
 * be lost otherwise.
 * @param irq Set to the interrupt.
 * @return True if there was one.
 */
bool Discovery::TakeInterrupt(InterruptPoller::Interrupt& irq) {
  if (!m_HasOther) {
    return false;
  }
  m_HasOther = false;
  irq = m_Other;
  return true;
}

/** Abandons the probe; nothing more is sent. */
void Discovery::Cancel() {
  m_Step = Step::kIdle;
  m_Sent = false;
}

/**
 * Tidies up after a probe or a set-up that failed, as far as the bus lets it: the new node
 * switched off (the upstream node's switch opened), CONTROL.ENDDSC if DSCDONE never came,
 * and the switches before it back in their mode. Failures are ignored: Network checks the
 * chain next. Nothing is sent if the probe never reached the switches.
 */
void Discovery::Abandon() {
  m_Step = Step::kIdle;
  if (!m_Sent) {
    return;
  }
  m_Sent = false;
  m_Transport.ForgetNode(m_Position);
  const size_t upstream = static_cast<size_t>(m_Position);
  (void)SwitchOff(upstream);
  if (!m_Found) {
    (void)WriteMain(reg::kControl, MainControl(reg::kControlEnddsc));
  }
  (void)RestoreSwitches(upstream);
}

/**
 * Step 1: the upstream node's interrupt masks and link polarity (sub-nodes only) and its
 * power-configuration check, once per node. Then the probe, at once or after the settle.
 */
Discovery::Outcome Discovery::Prepare(uint32_t nowMs) {
  Link& link = m_Links[m_Position];
  if (link.prepared) {
    return SendProbe(nowMs);
  }
  const NodeAddr upstream = NodeOf(static_cast<size_t>(m_Position));
  Result result = Result::kOk;
  if (upstream != kMainNode) {
    const uint8_t masks[2] = {link.intmsk0, link.intmsk1};
    result = m_Transport.WriteRegs(upstream, reg::kIntmsk0, masks, sizeof(masks));
    if (result == Result::kOk) {
      result = m_Transport.WriteReg(upstream, reg::kControl, link.control);
    }
  }
  bool overridden = false;
  if (result == Result::kOk && link.hasSwitchConfig) {
    result = CheckPowerConfig(static_cast<size_t>(m_Position), overridden);
  }
  if (result != Result::kOk) {
    m_Step = Step::kIdle;
    return Outcome::kFailed;
  }
  link.prepared = true;
  if (overridden) {
    m_Deadline = nowMs + kSwitchSettleMs;
    m_Step = Step::kSettle;
    return Outcome::kBusy;
  }
  return SendProbe(nowMs);
}

/**
 * Reads a node's SWSTAT2 and, if the power configuration it detected is not the link's,
 * overrides it: SWCTL2.HPSW_CFG, then SWCTL with CFG_DET_OV (switch still off).
 * @param index The node's entry in the links.
 * @param overridden Set to true if the configuration was overridden.
 */
Result Discovery::CheckPowerConfig(size_t index, bool& overridden) {
  Link& link = m_Links[index];
  const NodeAddr node = NodeOf(index);
  uint8_t status = 0;
  Result result = m_Transport.ReadReg(node, reg::kSwstat2, status);
  if (result != Result::kOk) {
    return result;
  }
  const uint8_t detected =
      static_cast<uint8_t>((status & reg::kSwstat2HpswCfgDet) >> reg::kSwstat2HpswCfgDetPos);
  const uint8_t expected = static_cast<uint8_t>(link.powerConfig & reg::kSwctl2HpswCfg);
  if (detected == expected) {
    return Result::kOk;
  }
  result = m_Transport.WriteReg(node, reg::kSwctl2, expected);
  if (result == Result::kOk) {
    link.swctl = static_cast<uint8_t>(link.swctl | reg::kSwctlCfgDetOv);
    result = m_Transport.WriteReg(node, reg::kSwctl, link.swctl);
  }
  overridden = (result == Result::kOk);
  return result;
}

/**
 * Step 2: the switches, DSCDONE cleared; for sub-node 0, the main node's settings with
 * NEWSTRCT and the settle. Then (or after the settle) SwitchOnAndProbe().
 */
Discovery::Outcome Discovery::SendProbe(uint32_t nowMs) {
  const size_t upstream = static_cast<size_t>(m_Position);
  m_Sent = true;
  Result result = Result::kOk;
  for (size_t i = 0; result == Result::kOk && i < upstream; ++i) {
    result = SwitchOn(i, true);
  }
  if (result == Result::kOk) {
    result = WriteMain(reg::kIntpnd2, reg::kIntpnd2Dscdone);
  }
  if (result == Result::kOk && m_Position == 0) {
    result = WriteMain(reg::kRespcycs, m_Settings.respCycles);
    if (result == Result::kOk) {
      result = WriteMain(reg::kControl, MainControl(reg::kControlNewstrct));
    }
    if (result == Result::kOk) {
      m_Deadline = nowMs + kNewStructSettleMs;
      m_Step = Step::kNewStruct;
      return Outcome::kBusy;
    }
  }
  if (result != Result::kOk) {
    m_Step = Step::kIdle;
    return Outcome::kFailed;
  }
  return SwitchOnAndProbe(nowMs);
}

/** Step 2, the rest: the upstream node's switch, NODEADR, DISCVRY. */
Discovery::Outcome Discovery::SwitchOnAndProbe(uint32_t nowMs) {
  const size_t upstream = static_cast<size_t>(m_Position);
  const Link& link = m_Links[upstream];  // Its mode, and ENSW if it powers the next node.
  Result result = m_Transport.WriteReg(
      NodeOf(upstream), reg::kSwctl,
      static_cast<uint8_t>(link.swctl | (link.closeSwitch ? reg::kSwctlEnsw : 0)));
  if (result == Result::kOk && m_Position > 0) {
    result = m_Transport.SelectNode(static_cast<NodeAddr>(m_Position - 1));
  }
  if (result == Result::kOk) {
    result = WriteMain(reg::kDiscvry, m_RespCycles);
  }
  if (result != Result::kOk) {
    m_Step = Step::kIdle;
    return Outcome::kFailed;
  }
  m_Deadline = nowMs + m_Settings.timeoutMs;
  m_Step = Step::kWaitDscdone;
  return Outcome::kBusy;
}

/** Step 3, nothing found: the upstream switch opened, CONTROL.ENDDSC, the others restored. */
Discovery::Outcome Discovery::EndProbe() {
  m_Step = Step::kIdle;
  Result result = SwitchOff(static_cast<size_t>(m_Position));
  if (result == Result::kOk) {
    result = WriteMain(reg::kControl, MainControl(reg::kControlEnddsc));
  }
  if (result == Result::kOk) {
    result = RestoreSwitches(static_cast<size_t>(m_Position));
  }
  if (result != Result::kOk) {
    return Outcome::kFailed;
  }
  m_Sent = false;
  return Outcome::kNothing;
}

/**
 * Step 4: reads the node's identity and module info. A node rejected here before (the
 * same chip ID) is switched off again at once.
 */
Discovery::Outcome Discovery::Identify() {
  m_Step = Step::kIdle;
  m_Found = true;
  const size_t upstream = static_cast<size_t>(m_Position);
  SubNodeIdentity& identity = m_Node->identity;
  identity = SubNodeIdentity();
  identity.position = m_Position;
  identity.bus = m_Settings.bus;
  uint8_t ids[4] = {};
  Result result = SwitchOn(upstream, true);
  if (result == Result::kOk) {
    result = m_Transport.ReadRegs(m_Position, reg::kVendor, ids, sizeof(ids));
  }
  if (result == Result::kOk) {
    result = RestoreSwitches(upstream + 1);
  }
  if (result == Result::kOk) {
    result =
        m_Transport.ReadRegs(m_Position, reg::kChipid0, identity.chipId, sizeof(identity.chipId));
  }
  if (result != Result::kOk) {
    return Outcome::kFailed;
  }
  identity.vendorId = ids[0];
  identity.productId = ids[1];
  identity.version = ids[2];
  identity.capability = ids[3];
  if (m_RejectedChipId != nullptr &&
      std::memcmp(m_RejectedChipId, identity.chipId, sizeof(identity.chipId)) == 0) {
    const Outcome outcome = Reject(RejectReason::kNone);
    return (outcome == Outcome::kRejected) ? Outcome::kRejectedAgain : outcome;
  }

  m_Caps = LookUpChipCaps(identity.vendorId, identity.productId);
  if (m_Caps == nullptr) {
    return Reject(RejectReason::kUnsupportedChip);
  }
  size_t len = m_Source->GetModuleInfo(identity, m_Image, sizeof(m_Image));
  len = (len > sizeof(m_Image)) ? sizeof(m_Image) : len;  // An app's source may claim more.
  if (len == 0) {
    return Reject(RejectReason::kNoModuleInfo);
  }
  if (ParseModuleInfo(m_Image, len, m_Node->moduleInfo) != Result::kOk) {
    return Reject(RejectReason::kBadModuleInfo);
  }
  if (!ImageFitsChip(m_Node->moduleInfo, identity.productId)) {
    return Reject(RejectReason::kWrongChip);
  }
  return Outcome::kIdentified;
}

/**
 * Switches the node of the last probe off (the upstream node's ENSW cleared) and records
 * why. Network calls it when its listener rejects a node it identified, or the node
 * fails its set-up.
 * @param reason Why the node is rejected.
 * @return Outcome::kRejected, or Outcome::kFailed if the switch write failed.
 */
Discovery::Outcome Discovery::Reject(RejectReason reason) {
  m_Step = Step::kIdle;
  m_Rejection = reason;
  m_Transport.ForgetNode(m_Position);
  const size_t upstream = static_cast<size_t>(m_Position);
  if (m_Links[upstream].closeSwitch && SwitchOff(upstream) != Result::kOk) {
    return Outcome::kFailed;
  }
  m_Sent = false;
  return Outcome::kRejected;
}

/**
 * Closes a node's switch: its SWCTL with ENSW, in its own mode or in mode 2 (inrush).
 * A link not powered over the A2B pair is left alone.
 * @param index The node's entry in the links.
 * @param inrush Mode 2 instead of the node's own mode.
 */
Result Discovery::SwitchOn(size_t index, bool inrush) {
  const Link& link = m_Links[index];
  if (!link.closeSwitch) {
    return Result::kOk;
  }
  uint8_t value = static_cast<uint8_t>(link.swctl | reg::kSwctlEnsw);
  if (inrush) {
    value = static_cast<uint8_t>((value & ~reg::kSwctlMode) | reg::kSwctlModeInrush);
  }
  return m_Transport.WriteReg(NodeOf(index), reg::kSwctl, value);
}

/** Opens a node's switch: its SWCTL in its own mode, ENSW clear. */
Result Discovery::SwitchOff(size_t index) {
  return m_Transport.WriteReg(NodeOf(index), reg::kSwctl, m_Links[index].swctl);
}

/** Puts the first @p count nodes' switches back in their own mode, ENSW set. */
Result Discovery::RestoreSwitches(size_t count) {
  Result result = Result::kOk;
  for (size_t i = 0; result == Result::kOk && i < count; ++i) {
    result = SwitchOn(i, false);
  }
  return result;
}

/** The node whose link is links[@p index]: the main node for 0, sub-node index - 1 after. */
NodeAddr Discovery::NodeOf(size_t index) {
  return (index == 0) ? kMainNode : static_cast<NodeAddr>(index - 1);
}

/** Writes one main-node register. */
Result Discovery::WriteMain(uint16_t reg, uint8_t value) {
  return m_Transport.WriteReg(kMainNode, reg, value);
}

/** The main node's CONTROL: its settings, its link's XCVRBINV, and @p command. */
uint8_t Discovery::MainControl(uint8_t command) const {
  return static_cast<uint8_t>(m_Settings.mainControl | m_Links[0].control | command);
}

}  // namespace a2b
