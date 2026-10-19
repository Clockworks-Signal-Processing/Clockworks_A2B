/**
 * @file test_crc8.cpp
 * @brief Unit tests for ADI's CRC-8, against the module-info headers in adi/module_info/.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "a2b/crc8.h"
#include "test_harness.h"

namespace a2b {
namespace {

// Bytes 0x00-0x1E of adi/module_info/adi_a2b_node_ModuleInfo_1.3.0.0_RJ45.dat; byte 0x1F holds the
// CRC.
constexpr uint8_t kRj45Header[31] = {
    0xAA, 0xAD, 0x00, 0x37, 0x00, 0x10, 0x00, 0x00, 0x00, 0x01, 0x01, 0x00, 0x00, 0x20, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

A2B_TEST(MatchesTheAdiModuleInfoHeaders) {
  A2B_CHECK_EQ(Crc8(kRj45Header, sizeof(kRj45Header)), 0xB8);
  // The XLR image differs only in the cable-type byte at 0x0A.
  uint8_t xlrHeader[sizeof(kRj45Header)];
  for (size_t i = 0; i < sizeof(xlrHeader); ++i) {
    xlrHeader[i] = kRj45Header[i];
  }
  xlrHeader[0x0A] = 0x03;
  A2B_CHECK_EQ(Crc8(xlrHeader, sizeof(xlrHeader)), 0x37);
}

A2B_TEST(ContinuesAcrossCalls) {
  const uint8_t whole = Crc8(kRj45Header, sizeof(kRj45Header));
  const uint8_t first = Crc8(kRj45Header, 10);
  A2B_CHECK_EQ(Crc8(kRj45Header + 10, sizeof(kRj45Header) - 10, first), whole);
}

A2B_TEST(EmptyBufferGivesTheStartingValue) {
  A2B_CHECK_EQ(Crc8(kRj45Header, 0), kCrc8Init);
  A2B_CHECK_EQ(Crc8(kRj45Header, 0, 0x5A), 0x5A);
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
