/**
 * @file test_types.cpp
 * @brief Unit tests for the shared types: node addresses, wrap-safe time, and the enums as
 *        text (a2b/strings.h).
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstring>

#include "a2b/strings.h"
#include "a2b/types.h"
#include "test_harness.h"

namespace a2b {
namespace {

A2B_TEST(EveryEnumValueHasText) {
  A2B_CHECK(std::strcmp(ToString(Result::kOk), "ok") == 0);
  A2B_CHECK(std::strcmp(ToString(NetworkState::kRunning), "running") == 0);
  for (uint8_t v = 0; v <= static_cast<uint8_t>(Result::kInvalidData); ++v) {
    A2B_CHECK(std::strcmp(ToString(static_cast<Result>(v)), "?") != 0);
  }
  for (uint8_t v = 0; v <= static_cast<uint8_t>(RoutingError::kChannelOrder); ++v) {
    A2B_CHECK(std::strcmp(ToString(static_cast<RoutingError>(v)), "?") != 0);
  }
  for (uint8_t v = 0; v <= static_cast<uint8_t>(EventType::kRoutingFailed); ++v) {
    A2B_CHECK(std::strcmp(ToString(static_cast<EventType>(v)), "?") != 0);
  }
  for (uint8_t v = 0; v <= static_cast<uint8_t>(RejectReason::kWrongChip); ++v) {
    A2B_CHECK(std::strcmp(ToString(static_cast<RejectReason>(v)), "?") != 0);
  }
  A2B_CHECK(std::strcmp(ToString(static_cast<RoutingError>(200)), "?") == 0);
}

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
