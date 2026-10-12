/**
 * @file chip_caps.cpp
 * @brief The table of supported transceivers.
 *
 * The stack branches on these capabilities, never on product IDs, so a new chip is a new
 * table row.
 *
 * Sources: the family checks in ADI's stack (A2B_IS_AD242X_CHIP and friends in
 * a2bstack/a2bstack/inc/a2b/defs.h, and their uses in discovery.c and pwrdiag.c).
 * Checked against the TRMs in docs/ADI_chips. Every supported chip has
 * CONTROL.XCVRBINV, so B-side polarity is a per-link setting (LinkProfile), not a
 * capability. The AD2431/32/33/35 have the same voltage monitor as the AD2437, and the
 * same power-configuration detection (SWSTAT2.HPSW_CFG_DET, SWCTL2.HPSW_CFG,
 * SWCTL.CFG_DET_OV: CFG-0 or CFG-4). The AD242x TRM has none of those registers.
 * Register page 1 (MMRPAGE, 0x100-0x1FF) is in the AD2437 and AD2431/32/33/35 register
 * maps; the AD242x map ends at 0x9B (MBOX1B3) with no MMRPAGE.
 *
 * The switch mode is the one each family's "Discovery Flow" figure writes while the
 * switch is on: SWCTL = 0x11 on the AD2437 and AD2431/32/33/35, 0x01 on the AD242x (whose
 * mode 1 means "downstream node not using bus power").
 *
 * Main-node capability, from the product comparison tables of the data sheets: the
 * AD2427 is not main-node capable (AD2426/27/28 data sheet Rev. F, Table 1: "Main node
 * capable: No"); the AD2428, AD2433 and AD2437 are. Each main node takes up to 16
 * sub-nodes (all three data sheets; a mixed chain is held to the most restrictive, also
 * 16); BoardPolicy::maxSubNodes sets the bus's own limit.
 *
 * I2S/TDM: the same Table 1 gives the AD2427 none ("I2S/TDM support: No"; its pin list:
 * "for the AD2426 and AD2427, the DRX0 function is not supported", and likewise DRX1), only
 * its four PDM microphone inputs on DRX0/DRX1, clocked from BCLK or PDMCLK/IO7. The AD2428,
 * AD2433 and AD2437 have the port.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "chip_caps.h"

namespace a2b {
namespace {

// TODO(test on hardware): an AD2433 main node; AD2437 and AD2428 ones have run on the bench.
// TODO(datasheet): the AD2437 TRM (rev 0.2) does not describe spread-spectrum clocking;
// ADI's stack programs it (PLLCTL) on AD243x main nodes.
constexpr ChipCaps kChipTable[] = {
    // product, name, family, SPI, xbar, MSINV, spread, VMTR, page 1, switch config,
    // switch mode, main-node capable, I2S/TDM
    {0x27, "AD2427", ChipFamily::kAd242x, false, false, false, true, false, false, false, 0x00,
     false, false},
    {0x28, "AD2428", ChipFamily::kAd242x, false, false, false, true, false, false, false, 0x00,
     true, true},
    {0x33, "AD2433", ChipFamily::kAd243x, true, true, true, true, true, true, true, 0x10, true,
     true},
    {0x37, "AD2437", ChipFamily::kAd243x, true, true, true, true, true, true, true, 0x10, true,
     true},
};

}  // namespace

/**
 * Looks up a transceiver's capabilities.
 * @param vendorId VENDOR register value.
 * @param productId PRODUCT register value.
 * @return The table row, or nullptr if the stack does not support the chip.
 */
const ChipCaps* LookUpChipCaps(uint8_t vendorId, uint8_t productId) {
  if (vendorId != kAdiVendorId) {
    return nullptr;
  }
  for (const ChipCaps& caps : kChipTable) {
    if (caps.productId == productId) {
      return &caps;
    }
  }
  return nullptr;
}

}  // namespace a2b
