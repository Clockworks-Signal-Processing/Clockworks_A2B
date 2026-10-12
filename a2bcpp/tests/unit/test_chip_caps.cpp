/**
 * @file test_chip_caps.cpp
 * @brief Unit tests for the ChipCaps table.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstring>
#include <initializer_list>

#include "chip_caps.h"
#include "test_harness.h"

namespace a2b {
namespace {

A2B_TEST(FindsTheFourSupportedChips) {
  const uint8_t products[] = {0x27, 0x28, 0x33, 0x37};
  const char* names[] = {"AD2427", "AD2428", "AD2433", "AD2437"};
  for (size_t i = 0; i < sizeof(products); ++i) {
    const ChipCaps* caps = LookUpChipCaps(kAdiVendorId, products[i]);
    A2B_CHECK(caps != nullptr);
    if (caps != nullptr) {
      A2B_CHECK_EQ(caps->productId, products[i]);
      A2B_CHECK_STREQ(caps->name, names[i]);
    }
  }
}

A2B_TEST(FamiliesAndFeatures) {
  const ChipCaps* ad2428 = LookUpChipCaps(kAdiVendorId, 0x28);
  const ChipCaps* ad2433 = LookUpChipCaps(kAdiVendorId, 0x33);
  const ChipCaps* ad2437 = LookUpChipCaps(kAdiVendorId, 0x37);
  A2B_CHECK(ad2428 != nullptr && ad2433 != nullptr && ad2437 != nullptr);
  if (ad2428 == nullptr || ad2433 == nullptr || ad2437 == nullptr) {
    return;
  }
  A2B_CHECK(ad2428->family == ChipFamily::kAd242x);
  A2B_CHECK(!ad2428->hasSpi && !ad2428->hasCrossbar && !ad2428->hasI2sMsInv);
  A2B_CHECK(ad2433->family == ChipFamily::kAd243x);
  A2B_CHECK(ad2433->hasSpi && ad2433->hasCrossbar && ad2433->hasVoltageMonitor);
  A2B_CHECK(ad2437->hasSpi && ad2437->hasVoltageMonitor);
}

A2B_TEST(I2sPort) {
  // The AD2426/27/28 data sheet's Table 1: the AD2427 has no I2S/TDM, only PDM inputs.
  const ChipCaps* ad2427 = LookUpChipCaps(kAdiVendorId, 0x27);
  A2B_CHECK(ad2427 != nullptr && !ad2427->hasI2s);
  for (uint8_t product : {0x28, 0x33, 0x37}) {
    const ChipCaps* caps = LookUpChipCaps(kAdiVendorId, product);
    A2B_CHECK(caps != nullptr && caps->hasI2s);
  }
}

A2B_TEST(RegisterPageOne) {
  // MMRPAGE and page 1 in the AD243x TRMs; the AD242x register map ends at 0x9B.
  for (uint8_t product : {0x27, 0x28}) {
    const ChipCaps* caps = LookUpChipCaps(kAdiVendorId, product);
    A2B_CHECK(caps != nullptr && !caps->hasPage1);
  }
  for (uint8_t product : {0x33, 0x37}) {
    const ChipCaps* caps = LookUpChipCaps(kAdiVendorId, product);
    A2B_CHECK(caps != nullptr && caps->hasPage1);
  }
}

A2B_TEST(SwitchConfigAndMode) {
  // SWSTAT2/SWCTL2 on the AD243x only; the TRMs' discovery flows write SWCTL 0x11 on
  // AD243x and 0x01 on AD242x.
  for (uint8_t product : {0x27, 0x28}) {
    const ChipCaps* caps = LookUpChipCaps(kAdiVendorId, product);
    A2B_CHECK(caps != nullptr && !caps->hasSwitchConfig && caps->switchMode == 0x00);
  }
  for (uint8_t product : {0x33, 0x37}) {
    const ChipCaps* caps = LookUpChipCaps(kAdiVendorId, product);
    A2B_CHECK(caps != nullptr && caps->hasSwitchConfig && caps->switchMode == 0x10);
  }
}

A2B_TEST(MainNodeCapability) {
  // The data sheets: the AD2427 is not main-node capable; the others are.
  const uint8_t products[] = {0x27, 0x28, 0x33, 0x37};
  const bool mainCapable[] = {false, true, true, true};
  for (size_t i = 0; i < sizeof(products); ++i) {
    const ChipCaps* caps = LookUpChipCaps(kAdiVendorId, products[i]);
    A2B_CHECK(caps != nullptr && caps->mainCapable == mainCapable[i]);
  }
}

A2B_TEST(RejectsUnsupportedChips) {
  A2B_CHECK(LookUpChipCaps(kAdiVendorId, 0x10) == nullptr);  // AD2410
  A2B_CHECK(LookUpChipCaps(kAdiVendorId, 0x25) == nullptr);  // AD2425
  A2B_CHECK(LookUpChipCaps(kAdiVendorId, 0x38) == nullptr);  // AD2438 (not supported)
  A2B_CHECK(LookUpChipCaps(0x00, 0x37) == nullptr);
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
