/**
 * @file link_profile.cpp
 * @brief The LinkProfile presets.
 *
 * A link's profile comes from the app. The EEPROM cable-type byte is not used: nothing in
 * ADI's stack reads it, and ADI's own images set it inconsistently (decisions.md).
 *
 * Only single-step links are supported: Network::Start() refuses a two-step default link.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "a2b/link_profile.h"

namespace a2b {

/**
 * UTP: single-step discovery, power on the A2B pair, B side not inverted.
 * @return The profile.
 */
LinkProfile LinkProfile::Utp() { return LinkProfile(); }

/**
 * RJ45: two-step discovery with the power-good and 24 V checks, power on the pair, B side
 * inverted (the only preset that inverts it).
 * @return The profile.
 */
LinkProfile LinkProfile::Rj45() {
  LinkProfile profile;
  profile.discovery = Discovery::kTwoStep;
  profile.bSideInverted = true;
  profile.checkPowerGood = true;
  profile.checkHighVoltage = true;
  return profile;
}

/**
 * XLR: two-step discovery with the power-good and 24 V checks, power on the pair, B side
 * not inverted.
 * TODO(test on hardware): XLR hardware is limited; the XLR code is carried over untested.
 * @return The profile.
 */
LinkProfile LinkProfile::Xlr() {
  LinkProfile profile;
  profile.discovery = Discovery::kTwoStep;
  profile.checkPowerGood = true;
  profile.checkHighVoltage = true;
  return profile;
}

/**
 * Custom: single-step discovery with the power on wires other than the A2B pair, so the
 * upstream node's switch stays open.
 * @return The profile.
 */
LinkProfile LinkProfile::Custom() {
  LinkProfile profile;
  profile.power = Power::kSeparateWires;
  return profile;
}

}  // namespace a2b
