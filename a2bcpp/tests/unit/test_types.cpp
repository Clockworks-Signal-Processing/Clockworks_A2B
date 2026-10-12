/**
 * @file test_types.cpp
 * @brief Unit tests for the shared types: node addresses and wrap-safe time.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "a2b/types.h"
#include "test_harness.h"

namespace a2b {
namespace {

A2B_TEST(NodeAddresses) {
  A2B_CHECK(!IsSubNode(kMainNode));
  A2B_CHECK(IsSubNode(0));
  A2B_CHECK(IsSubNode(kMaxSubNodes - 1));
  A2B_CHECK(!IsSubNode(kMaxSubNodes));
}

A2B_TEST(TimeWorksAcrossTheCounterWrap) {
  const uint32_t beforeWrap = 0xFFFFFFFBu;
  const uint32_t afterWrap = 5;
  A2B_CHECK_EQ(ElapsedMs(afterWrap, beforeWrap), 10);
  A2B_CHECK(TimeReached(afterWrap, beforeWrap));
  A2B_CHECK(!TimeReached(beforeWrap, afterWrap));
  A2B_CHECK(TimeReached(afterWrap, afterWrap));
  A2B_CHECK(!TimeReached(afterWrap - 1, afterWrap));
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
