/**
 * @file link_profile.cpp
 * @brief The LinkProfile presets, and the link a module's B port gives.
 *
 * A link's profile comes from the app (Network::SetLinkProfile()), else the upstream node's
 * SubNode::DownstreamLink(), whose default is ForBPort() of that node's image: its B-side
 * cable type (ModuleInfo::bPortType) and its chip. ADI's stack never read the cable type,
 * and ADI's own images number it differently from the specification (their "RJ45" image
 * stores 2, UTP in the specification); the specification's numbering is used
 * (provisioning.md).
 *
 * A two-step link needs the upstream node's high-power switch (ChipCaps::hasHighPowerSwitch,
 * the AD2437): Network runs it single-step from any other node. A link from a B port the
 * stack has no handler for (Discovery::kUnsupported) is not probed.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "a2b/link_profile.h"

#include "chip_caps.h"

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
 * TODO(test on hardware): no XLR link has been run.
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

/**
 * The link from a module's B port to the next sub-node.
 *
 * ADI's XLR and RJ45 links are two-step, which needs the AD2437's 24 V switch: on any other
 * chip they are UTP (an AB0312's RJ45 port, for instance, is UTP with an AD2428 module).
 * Types 3-5 and the reserved values go to CustomBPortLink().
 * @param bPortType The node's image's B port type (ModuleInfo::bPortType, a BPortType).
 * @param productId The node's PRODUCT register.
 * @return The profile: Xlr(), Rj45() or Utp(); for another type, CustomBPortLink()'s, or
 *         one with Discovery::kUnsupported (the next node is not probed).
 */
LinkProfile LinkProfile::ForBPort(uint8_t bPortType, uint8_t productId) {
  const ChipCaps* caps = LookUpChipCaps(kAdiVendorId, productId);
  const bool twoStepChip = (caps != nullptr && caps->hasHighPowerSwitch);
  if (bPortType == static_cast<uint8_t>(BPortType::kXlr)) {
    return twoStepChip ? Xlr() : Utp();
  }
  if (bPortType == static_cast<uint8_t>(BPortType::kRj45)) {
    return twoStepChip ? Rj45() : Utp();
  }
  if (bPortType == static_cast<uint8_t>(BPortType::kUtp)) {
    return Utp();
  }
  LinkProfile profile;
  if (CustomBPortLink(bPortType, productId, profile) != Result::kOk) {
    profile = LinkProfile();
    profile.discovery = Discovery::kUnsupported;
  }
  return profile;
}

/**
 * The link for a B port type the stack has no preset for: RJ45-CUSTOM, RJ45-CUSTOM-INV,
 * CUSTOM and the reserved values. A placeholder: each type gets its handler here.
 * @param bPortType The B port type.
 * @param productId The node's PRODUCT register.
 * @param profile The link, when the type has a handler.
 * @return Result::kNotSupported, "Unsupported B port type": no type has a handler.
 */
Result CustomBPortLink(uint8_t bPortType, uint8_t productId, LinkProfile& profile) {
  (void)bPortType;
  (void)productId;
  (void)profile;
  return Result::kNotSupported;  // Unsupported B port type.
}

}  // namespace a2b
