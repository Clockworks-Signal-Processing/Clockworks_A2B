/**
 * @file link_profile.h
 * @brief How the link from an upstream node to the next sub-node is brought up: discovery
 *        steps, power and B-side polarity.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_A2B_LINK_PROFILE_H_
#define A2BCPP_A2B_LINK_PROFILE_H_

#include <cstdint>

#include "a2b/types.h"

namespace a2b {

/**
 * @brief What a module's B port is: its image's B-side cable type (ModuleInfo::bPortType,
 *        image byte 0x28). 0-2 are ADI's specification's; 3-5 are Clockworks' additions,
 *        which the stack can't run yet (CustomBPortLink()); 6 and up are reserved.
 */
enum class BPortType : uint8_t {
  kXlr = 0,                 ///< ADI's XLR: two-step on an AD2437, else UTP.
  kRj45 = 1,                ///< ADI's RJ45: two-step, B side inverted, on an AD2437; else UTP.
  kUtp = 2,                 ///< UTP: single-step, power on the pair, B side not inverted.
  kRj45Custom = 3,          ///< An RJ45 that isn't ADI's, B side not inverted.
  kRj45CustomInverted = 4,  ///< An RJ45 that isn't ADI's, B side inverted.
  kCustom = 5,              ///< Something else.
};

/// The settings for one link. Start from a preset: Utp(), Rj45(), Xlr() or Custom().
struct LinkProfile {
  /** @brief How the next node is discovered. */
  enum class Discovery : uint8_t {
    kSingleStep,  ///< One discovery with the power already on.
    kTwoStep,  ///< Discover at 5 V, read the module info, then switch on 24 V (not supported yet).
    /** The stack can't run the link (a B port type with no handler yet, CustomBPortLink()):
        the next node is not probed. */
    kUnsupported,
  };

  /** @brief Where the next node's power comes from. */
  enum class Power : uint8_t {
    kOnA2bPair,  ///< Over the A2B pair: the upstream node closes its switch (SWCTL.ENSW).
    /** On other wires: the switch stays open, and the upstream node's CONTROL.SWBYP turns
        its B side on without it (the TRMs, "Data-Only and Power-Only Bus Operation"). */
    kSeparateWires,
    kNone,  ///< The node has its own supply: as kSeparateWires.
  };

  Discovery discovery = Discovery::kSingleStep;  ///< Discovery steps.
  Power power = Power::kOnA2bPair;               ///< Power source of the next node.
  /** The upstream node inverts its B side (CONTROL.XCVRBINV), as an RJ45 link needs. */
  bool bSideInverted = false;
  /** Two-step: check power-good before the first step (two-step is not supported yet). */
  bool checkPowerGood = false;
  uint8_t powerGoodGpio = 7;  ///< Two-step: the sub-node GPIO that carries power-good.
  /** Two-step: check 24 V with the voltage monitor (two-step is not supported yet). */
  bool checkHighVoltage = false;

  static LinkProfile Utp();     ///< UTP: single-step, power on the pair.
  static LinkProfile Rj45();    ///< RJ45: two-step, B side inverted.
  static LinkProfile Xlr();     ///< XLR: two-step.
  static LinkProfile Custom();  ///< Custom: single-step, power on separate wires (SWBYP).
  /// The link from a module's B port, by its image's B port type and the node's chip.
  static LinkProfile ForBPort(uint8_t bPortType, uint8_t productId);
};

/// The link for a B port type with no preset (BPortType 3-5, reserved values): none yet.
Result CustomBPortLink(uint8_t bPortType, uint8_t productId, LinkProfile& profile);

}  // namespace a2b

#endif  // A2BCPP_A2B_LINK_PROFILE_H_
