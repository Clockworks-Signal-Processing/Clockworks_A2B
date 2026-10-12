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

namespace a2b {

/// The settings for one link. Start from a preset: Utp(), Rj45(), Xlr() or Custom().
struct LinkProfile {
  /** @brief How the next node is discovered. */
  enum class Discovery : uint8_t {
    kSingleStep,  ///< One discovery with the power already on.
    kTwoStep,  ///< Discover at 5 V, read the module info, then switch on 24 V (not supported yet).
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
  bool checkPowerGood =
      false;  ///< Two-step: check power-good before the first step (not supported yet).
  uint8_t powerGoodGpio = 7;  ///< Two-step: the sub-node GPIO that carries power-good.
  bool checkHighVoltage =
      false;  ///< Two-step: check 24 V with the voltage monitor (not supported yet).

  static LinkProfile Utp();     ///< UTP: single-step, power on the pair.
  static LinkProfile Rj45();    ///< RJ45: two-step, B side inverted.
  static LinkProfile Xlr();     ///< XLR: two-step.
  static LinkProfile Custom();  ///< Custom: single-step, power on separate wires (SWBYP).
};

}  // namespace a2b

#endif  // A2BCPP_A2B_LINK_PROFILE_H_
