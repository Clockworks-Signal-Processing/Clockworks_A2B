/**
 * @file test_register_image.cpp
 * @brief Unit tests for RegisterImage, Op and OpList.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "a2b/op_list.h"
#include "a2b/register_image.h"
#include "test_harness.h"

namespace a2b {
namespace {

A2B_TEST(ImageKeepsFirstSetOrder) {
  RegisterImage image;
  A2B_CHECK(image.Set(0x41, 0xE2) == Result::kOk);
  A2B_CHECK(image.Set(0x121, 0xFF) == Result::kOk);
  A2B_CHECK(image.Set(0x12, 0x10) == Result::kOk);
  A2B_CHECK(image.Set(0x41, 0x62) == Result::kOk);  // Changes the value, keeps the place.
  A2B_CHECK_EQ(image.Size(), 3u);
  A2B_CHECK_EQ(image.RegAt(0), 0x41);
  A2B_CHECK_EQ(image.ValueAt(0), 0x62);
  A2B_CHECK_EQ(image.RegAt(1), 0x121);
  A2B_CHECK_EQ(image.RegAt(2), 0x12);
}

A2B_TEST(ImageGetAndHas) {
  RegisterImage image;
  uint8_t value = 0x55;
  A2B_CHECK(!image.Get(0x12, value));
  A2B_CHECK_EQ(value, 0x55);  // Unchanged when absent.
  A2B_CHECK(!image.Has(0x12));
  A2B_CHECK(image.Set(0x12, 0x91) == Result::kOk);
  A2B_CHECK(image.Get(0x12, value));
  A2B_CHECK_EQ(value, 0x91);
  A2B_CHECK(image.Has(0x12));
  A2B_CHECK(!image.Has(0x112));  // Page 1 is a different register.
}

A2B_TEST(ImageFullAndClear) {
  RegisterImage image;
  for (size_t i = 0; i < RegisterImage::kCapacity; ++i) {
    A2B_CHECK(image.Set(static_cast<uint16_t>(i), 1) == Result::kOk);
  }
  A2B_CHECK(image.Set(0x1FF, 1) == Result::kNoSpace);
  A2B_CHECK(image.Set(0x05, 2) == Result::kOk);  // A register already there still changes.
  image.Clear();
  A2B_CHECK_EQ(image.Size(), 0u);
  A2B_CHECK(!image.Has(0x05));
}

A2B_TEST(OpFactories) {
  const uint8_t data[3] = {1, 2, 3};
  const Op write = Op::PeripheralWrite(2, 0x39, 2, 0x4000, data, 3, 5);
  A2B_CHECK(write.type == OpType::kPeripheralWrite);
  A2B_CHECK_EQ(write.node, 2);
  A2B_CHECK_EQ(write.i2cAddr, 0x39);
  A2B_CHECK_EQ(write.addrWidth, 2);
  A2B_CHECK_EQ(write.addr, 0x4000u);
  A2B_CHECK(write.data == data);
  A2B_CHECK_EQ(write.len, 3);
  A2B_CHECK_EQ(write.addrIncrement, 5);

  const Op read = Op::PeripheralRead(kMainNode, 0x39, 2, 0x4002, 6);
  A2B_CHECK(read.type == OpType::kPeripheralRead);
  A2B_CHECK_EQ(read.node, kMainNode);
  A2B_CHECK_EQ(read.len, 6);

  const Op wait = Op::Wait(100);
  A2B_CHECK(wait.type == OpType::kWait);
  A2B_CHECK_EQ(wait.addr, 100u);

  const Op reg = Op::RegWrite(kMainNode, 0x41, 0xE2);
  A2B_CHECK(reg.type == OpType::kRegWrite);
  A2B_CHECK_EQ(reg.reg, 0x41);
  A2B_CHECK_EQ(reg.value, 0xE2);
}

A2B_TEST(OpListFillsItsStorage) {
  Op storage[2];
  OpList list(storage, 2);
  A2B_CHECK_EQ(list.Capacity(), 2u);
  A2B_CHECK(list.Add(Op::Wait(1)) == Result::kOk);
  A2B_CHECK(list.Add(Op::Wait(2)) == Result::kOk);
  A2B_CHECK(list.Add(Op::Wait(3)) == Result::kNoSpace);
  A2B_CHECK_EQ(list.Size(), 2u);
  A2B_CHECK_EQ(list[1].addr, 2u);
  list.Clear();
  A2B_CHECK_EQ(list.Size(), 0u);

  FixedOpList<4> fixed;
  A2B_CHECK_EQ(fixed.Capacity(), 4u);
  A2B_CHECK(fixed.Add(Op::Wait(7)) == Result::kOk);
  A2B_CHECK_EQ(fixed[0].addr, 7u);
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
