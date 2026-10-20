/**
 * @file discovery.h
 * @brief Finds the next sub-node behind the last one, identifies it and reads its module
 *        info. Internal; Network drives it, and sets the node up with NodeSetup.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_DISCOVERY_H_
#define A2BCPP_DISCOVERY_H_

#include <cstddef>
#include <cstdint>

#include "a2b/module_info.h"
#include "a2b/network.h"
#include "a2b/platform.h"
#include "a2b/types.h"
#include "chip_caps.h"
#include "detail/regdefs.h"
#include "interrupt_poller.h"
#include "transport.h"

namespace a2b {

/// Discovery of one position of a chain, single-step or two-step, up to the node's
/// identification. One thread per object.
class Discovery {
 public:
  /** @brief How a probe ended. */
  enum class Outcome : uint8_t {
    kBusy,        ///< Still working; poll again.
    kIdentified,  ///< A node answered and its module info parsed; Network sets it up.
    kNothing,     ///< Nothing answered before the deadline.
    kRejected,    ///< A node answered but was not taken in; see Rejection().
    /** The node rejected at this position before answered again (same chip ID): it is
        switched off again, without its module info being read. */
    kRejectedAgain,
    kFailed,  ///< A transfer failed; Abandon() tidies up.
    /** A two-step link's power-good was low: nothing was probed (nothing to tidy up). */
    kNoPowerGood,
  };

  /** @brief What every probe of a bus writes; Network works it out at start-up. */
  struct Settings {
    /** Main CONTROL without the command bits and without XCVRBINV (MSTR, SWBYP, ...). */
    uint8_t mainControl = 0;
    uint8_t respCycles = 0x40;  ///< Main RESPCYCS, written before sub-node 0's probe.
    uint32_t timeoutMs = 70;    ///< DISCVRY to the DSCDONE deadline.
    uint8_t bus = 0;            ///< BoardConfig::busIndex, for SubNodeIdentity::bus.
  };

  /** @brief One node's B side: the switch that powers the next node, and its settings.
   *         Network keeps one per node: [0] is the main node, [n + 1] sub-node n, and
   *         fills in the link settings from the link's LinkProfile. */
  struct Link {
    uint8_t swctl = 0;        ///< SWCTL with ENSW clear: switch mode and CFG_DET_OV.
    uint8_t control = 0;      ///< CONTROL.XCVRBINV and SWBYP for the link (main's too).
    bool closeSwitch = true;  ///< ENSW powers the next node over the A2B pair.
    /** A B port the stack can't run (LinkProfile::Discovery::kUnsupported): Network does
        not probe it. */
    bool noProbe = false;
    /** Two-step: the next node is found at 5 V with SWBYP, then again with the 24 V switch
        closed (the upstream node has the high-power switch). */
    bool twoStep = false;
    bool checkPowerGood = false;    ///< Two-step: the upstream board's power-good first.
    uint8_t powerGoodGpio = 7;      ///< Two-step: the upstream sub-node's power-good GPIO.
    bool checkHighVoltage = false;  ///< The new node's VBUS checked with its VMTR.
    /** Two-step: SWBYP is in force on the upstream node (the first step, or a node kept at
        5 V because its power configuration isn't CFG-4). Every CONTROL write keeps it. */
    bool bypass = false;
    bool hasSwitchConfig = false;  ///< The chip has SWSTAT2 and SWCTL2.
    uint8_t powerConfig = 0;       ///< Expected power configuration, 0 (CFG-0) or 4 (CFG-4).
    uint8_t intmsk0 = 0;           ///< Sub-nodes: INTMSK0, from SubNode::InterruptMask().
    uint8_t intmsk1 = 0;           ///< Sub-nodes: INTMSK1.
    bool prepared = false;         ///< Its masks, polarity and switch check are done.

    /** CONTROL's link bits as they are on the bus: #control, with SWBYP while #bypass. */
    uint8_t Control() const {
      return static_cast<uint8_t>(control | (bypass ? reg::kControlSwbyp : 0));
    }
  };

  /** @brief The TRMs' wait after the power configuration is overridden. */
  static constexpr uint32_t kSwitchSettleMs = 100;
  /** @brief Clock ticks after CONTROL.NEWSTRCT with no bus access: at least 1 ms (ADI's
      A2B_DELAY_FOR_NEWSTRUCT_IN_MS; the TRMs apply the new structure within 5 superframes,
      0.1 ms at 48 kHz) on a millisecond clock. */
  static constexpr uint32_t kNewStructSettleMs = 2;
  /** @brief VBUS (VMTR input 1, 125 mV a step) below this is no 24 V: 4 V, ADI's
      A2B_24V_DISCONECT_THRESHOLD and the AD2437 TRM's "VMTR_VLTG1 < 4V" (Figure 7-3). */
  static constexpr uint8_t kHighVoltageMin = 0x20;
  /** @brief How long a low VBUS reading is read again before the node is rejected. */
  static constexpr uint32_t kHighVoltageWindowMs = 10;

  Discovery(Transport& transport, InterruptPoller& poller, Platform& platform);  ///< Idle.

  Discovery(const Discovery&) = delete;
  Discovery& operator=(const Discovery&) = delete;

  void Begin(const Settings& settings, NodeAddr position, uint8_t respCycles, Link* links,
             ModuleInfoSource& source, SubNodeInfo& node,
             const uint8_t* rejectedChipId = nullptr);  ///< Starts a probe; Poll() sends it.
  Outcome Poll(uint32_t nowMs, const InterruptPoller::Interrupt* irq);  ///< Advances the probe.
  Outcome Reject(RejectReason reason);  ///< Switches the identified node off.
  bool NeedsHighVoltage() const;        ///< A two-step link's first step found a CFG-4 node.
  Outcome BeginHighVoltageStep();       ///< The second step: switch opened, SWBYP off.
  bool FirstStepOnly() const;           ///< The node found stays at 5 V (no second step).
  void Abandon();                       ///< After a failure: tidies up what it can.
  void Cancel();                        ///< Abandons the probe; nothing more is sent.
  bool Settling(uint32_t nowMs) const;  ///< In the NEWSTRCT settle: no bus access.
  bool TakeInterrupt(InterruptPoller::Interrupt& irq);  ///< Another interrupt it read.

  RejectReason Rejection() const { return m_Rejection; }  ///< Why the node was rejected.
  const ChipCaps* Caps() const { return m_Caps; }         ///< The identified node's chip.

 private:
  /** @brief Where the probe is. */
  enum class Step : uint8_t {
    kIdle,          ///< No probe.
    kPrepare,       ///< Set up the upstream node's link, once per node.
    kSettle,        ///< Wait after a power-configuration override.
    kProbe,         ///< Send the probe.
    kNewStruct,     ///< Wait after NEWSTRCT (sub-node 0's probe), then switch on.
    kWaitDscdone,   ///< Wait for DSCDONE.
    kCheckVoltage,  ///< Read the new node's VBUS again until it is up or the window ends.
  };

  Outcome Prepare(uint32_t nowMs);             ///< Masks, polarity, switch check.
  Result PowerGood(bool& good);                ///< The upstream board's power-good.
  Outcome SendProbe(uint32_t nowMs);           ///< Up to NEWSTRCT, for sub-node 0.
  Outcome SwitchOnAndProbe(uint32_t nowMs);    ///< Switches on and writes DISCVRY.
  Outcome EndProbe();                          ///< Nothing found: ends discovery.
  Outcome Identify(uint32_t nowMs);            ///< Reads the node and its module info.
  Outcome Reidentify(uint32_t nowMs);          ///< Second step: the same node again.
  Outcome CheckVoltage(uint32_t nowMs);        ///< The new node's VBUS, with its VMTR.
  Outcome FinishVoltage(bool good);            ///< VMTR off; identified or rejected.
  Outcome Identified(uint32_t nowMs);          ///< The node is in, or its VBUS is checked.
  Result ClearBypass();                        ///< SWBYP off on the upstream node.
  Result SwitchOn(size_t index, bool inrush);  ///< SWCTL of links[index], switch on.
  Result SwitchOff(size_t index);              ///< SWCTL of links[index], switch off.
  Result RestoreSwitches(size_t count);        ///< links[0..count-1] back to their mode.
  Result CheckPowerConfig(size_t index, bool& overridden);  ///< SWSTAT2 against the link.
  static NodeAddr NodeOf(size_t index);                     ///< The node of links[index].
  Result WriteMain(uint16_t reg, uint8_t value);            ///< One main-node register write.
  uint8_t MainControl(uint8_t command) const;               ///< Main CONTROL with a command.

  Transport& m_Transport;
  InterruptPoller& m_Poller;
  Platform& m_Platform;
  Settings m_Settings;
  NodeAddr m_Position = 0;
  uint8_t m_RespCycles = 0;  ///< DISCVRY for the position.
  Link* m_Links = nullptr;
  ModuleInfoSource* m_Source = nullptr;
  SubNodeInfo* m_Node = nullptr;
  const ChipCaps* m_Caps = nullptr;  ///< The new node's chip.
  Step m_Step = Step::kIdle;
  uint32_t m_Deadline = 0;
  RejectReason m_Rejection = RejectReason::kNone;
  const uint8_t* m_RejectedChipId = nullptr;  ///< Chip ID rejected here before, or nullptr.
  bool m_Sent = false;                        ///< The probe reached the switches.
  bool m_Found = false;                       ///< DSCDONE came.
  bool m_SecondStep = false;                  ///< A two-step link's 24 V step.
  bool m_HasOther = false;                    ///< m_Other waits for Network.
  InterruptPoller::Interrupt m_Other;         ///< Not DSCDONE, read by the last check.
  uint8_t m_Image[kModuleInfoLen] = {};       ///< Its module-info image.
};

}  // namespace a2b

#endif  // A2BCPP_DISCOVERY_H_
