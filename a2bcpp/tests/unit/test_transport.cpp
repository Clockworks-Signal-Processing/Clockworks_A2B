/**
 * @file test_transport.cpp
 * @brief Unit tests for Transport: targets, NODEADR/CHIP/MMRPAGE caching, failures, locking.
 *
 * Transfers are compared as FakePlatform::Dump() text: "W 68 01 02" is a write of
 * 01 02 to address 0x68, "X 69 03 r1" a write-read of 03 then one byte, "R 69 r2" a read.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <string>

#include "detail/regdefs.h"
#include "fake_platform.h"
#include "test_harness.h"
#include "transport.h"

namespace a2b {
namespace {

constexpr uint8_t kMain = 0x68;
constexpr uint8_t kBus = 0x69;
constexpr uint8_t kEeprom = 0x50;

using test::FakePlatform;

/** Checks the transfers since the last call, then clears the log. */
#define CHECK_TRANSFERS(fake, expected)      \
  do {                                       \
    const std::string dump = (fake).Dump();  \
    A2B_CHECK_STREQ(dump.c_str(), expected); \
    (fake).ClearLog();                       \
  } while (0)

A2B_TEST(MainNodeRegistersUseTheMainAddress) {
  FakePlatform fake;
  Transport transport(fake, kMain, kBus);
  A2B_CHECK(transport.WriteReg(kMainNode, 0x12, 0x34) == Result::kOk);
  uint8_t chipId[6] = {};
  A2B_CHECK(transport.ReadRegs(kMainNode, reg::kChipid0, chipId, sizeof(chipId)) == Result::kOk);
  CHECK_TRANSFERS(fake, "W 68 12 34\nX 68 6A r6\n");
}

A2B_TEST(SubNodeIsSelectedOnce) {
  FakePlatform fake;
  Transport transport(fake, kMain, kBus);
  A2B_CHECK(transport.WriteReg(2, 0x40, 0x11) == Result::kOk);
  A2B_CHECK(transport.WriteReg(2, 0x41, 0x22) == Result::kOk);
  A2B_CHECK(transport.WriteReg(kMainNode, 0x12, 0x01) == Result::kOk);
  A2B_CHECK(transport.WriteReg(2, 0x42, 0x33) == Result::kOk);
  A2B_CHECK(transport.WriteReg(3, 0x40, 0x44) == Result::kOk);
  CHECK_TRANSFERS(fake,
                  "W 68 01 02\nW 69 40 11\nW 69 41 22\nW 68 12 01\nW 69 42 33\n"
                  "W 68 01 03\nW 69 40 44\n");
}

A2B_TEST(SelectNodeWritesNodeadrOnlyWhenNeeded) {
  FakePlatform fake;
  Transport transport(fake, kMain, kBus);
  A2B_CHECK(transport.SelectNode(1) == Result::kOk);
  A2B_CHECK(transport.WriteReg(1, 0x09, 0x11) == Result::kOk);
  A2B_CHECK(transport.SelectNode(1) == Result::kOk);  // Already there: nothing sent.
  A2B_CHECK(transport.SelectNode(kMainNode) == Result::kInvalidArgument);
  A2B_CHECK(transport.SelectNode(kMaxSubNodes) == Result::kInvalidArgument);
  CHECK_TRANSFERS(fake, "W 68 01 01\nW 69 09 11\n");
}

A2B_TEST(SubNodeReadReturnsTheValue) {
  FakePlatform fake;
  Transport transport(fake, kMain, kBus);
  fake.SetReg(kBus, reg::kProduct, 0x37);
  uint8_t product = 0;
  A2B_CHECK(transport.ReadReg(1, reg::kProduct, product) == Result::kOk);
  A2B_CHECK_EQ(product, 0x37);
  CHECK_TRANSFERS(fake, "W 68 01 01\nX 69 03 r1\n");
}

A2B_TEST(PeripheralSetsChipThenPeri) {
  FakePlatform fake;
  Transport transport(fake, kMain, kBus);
  const uint8_t data[2] = {0x00, 0xAA};
  uint8_t readBack[2] = {};
  A2B_CHECK(transport.PeripheralWrite(1, kEeprom, data, sizeof(data)) == Result::kOk);
  A2B_CHECK(transport.PeripheralWrite(1, kEeprom, data, sizeof(data)) == Result::kOk);
  A2B_CHECK(transport.PeripheralRead(1, kEeprom, readBack, sizeof(readBack)) == Result::kOk);
  CHECK_TRANSFERS(fake, "W 68 01 01\nW 69 00 50\nW 68 01 21\nW 69 00 AA\nW 69 00 AA\nR 69 r2\n");
}

A2B_TEST(ChipIsCachedPerNode) {
  FakePlatform fake;
  Transport transport(fake, kMain, kBus);
  const uint8_t data[2] = {0x00, 0xAA};
  A2B_CHECK(transport.PeripheralWrite(1, kEeprom, data, sizeof(data)) == Result::kOk);
  A2B_CHECK(transport.PeripheralWrite(2, kEeprom, data, sizeof(data)) == Result::kOk);
  fake.ClearLog();

  // Back to node 1's peripheral, and past a register access: only NODEADR changes.
  A2B_CHECK(transport.PeripheralWrite(1, kEeprom, data, sizeof(data)) == Result::kOk);
  A2B_CHECK(transport.WriteReg(1, 0x40, 0x01) == Result::kOk);
  A2B_CHECK(transport.PeripheralWrite(1, kEeprom, data, sizeof(data)) == Result::kOk);
  CHECK_TRANSFERS(fake, "W 68 01 21\nW 69 00 AA\nW 68 01 01\nW 69 40 01\nW 68 01 21\nW 69 00 AA\n");

  // Another peripheral on node 1 rewrites CHIP.
  A2B_CHECK(transport.PeripheralWrite(1, 0x51, data, sizeof(data)) == Result::kOk);
  CHECK_TRANSFERS(fake, "W 68 01 01\nW 69 00 51\nW 68 01 21\nW 69 00 AA\n");
}

A2B_TEST(MainNodePeripheralUsesTheHostBus) {
  FakePlatform fake;
  Transport transport(fake, kMain, kBus);
  const uint8_t subAddress[2] = {0x40, 0x00};
  uint8_t value = 0;
  A2B_CHECK(transport.PeripheralWriteRead(kMainNode, 0x38, subAddress, sizeof(subAddress), &value,
                                          1) == Result::kOk);
  CHECK_TRANSFERS(fake, "X 38 40 00 r1\n");
}

A2B_TEST(PageOneStaysSelectedUntilPageZeroIsNeeded) {
  FakePlatform fake;
  Transport transport(fake, kMain, kBus);
  uint8_t value = 0;
  A2B_CHECK(transport.WriteReg(1, 0x100, 0x01) == Result::kOk);
  A2B_CHECK(transport.ReadReg(1, 0x120, value) == Result::kOk);
  A2B_CHECK(transport.WriteReg(1, 0x40, 0x05) == Result::kOk);
  CHECK_TRANSFERS(fake, "W 68 01 01\nW 69 E0 01\nW 69 00 01\nX 69 20 r1\nW 69 E0 00\nW 69 40 05\n");
}

A2B_TEST(PagesAreCachedPerNode) {
  FakePlatform fake;
  Transport transport(fake, kMain, kBus);
  A2B_CHECK(transport.WriteReg(kMainNode, 0x100, 0x01) == Result::kOk);
  A2B_CHECK(transport.WriteReg(1, 0x100, 0x01) == Result::kOk);
  A2B_CHECK(transport.WriteReg(kMainNode, 0x101, 0x07) == Result::kOk);
  A2B_CHECK(transport.WriteReg(2, 0x40, 0x03) == Result::kOk);
  CHECK_TRANSFERS(fake,
                  "W 68 E0 01\nW 68 00 01\nW 68 01 01\nW 69 E0 01\nW 69 00 01\nW 68 01 07\n"
                  "W 68 01 02\nW 69 40 03\n");
}

A2B_TEST(BroadcastRestoresPageZeroFirst) {
  FakePlatform fake;
  Transport transport(fake, kMain, kBus);
  A2B_CHECK(transport.WriteReg(4, 0x100, 0x01) == Result::kOk);
  fake.ClearLog();
  const uint8_t value = 0x34;
  A2B_CHECK(transport.BroadcastWrite(0x12, &value, 1) == Result::kOk);
  A2B_CHECK(transport.WriteReg(4, 0x40, 0x01) == Result::kOk);
  CHECK_TRANSFERS(fake, "W 69 E0 00\nW 68 01 80\nW 69 12 34\nW 68 01 04\nW 69 40 01\n");
  A2B_CHECK(transport.BroadcastWrite(0x100, &value, 1) == Result::kInvalidArgument);
  CHECK_TRANSFERS(fake, "");
}

A2B_TEST(FailedNodeadrWriteIsRetried) {
  FakePlatform fake;
  Transport transport(fake, kMain, kBus);
  A2B_CHECK(transport.WriteReg(2, 0x40, 0x01) == Result::kOk);
  fake.ClearLog();
  fake.FailTransfer(0);
  A2B_CHECK(transport.WriteReg(3, 0x40, 0x01) == Result::kI2cError);
  A2B_CHECK(transport.WriteReg(2, 0x40, 0x01) == Result::kOk);
  CHECK_TRANSFERS(fake, "W 68 01 03\nW 68 01 02\nW 69 40 01\n");
}

A2B_TEST(FailedPageWriteForcesARewrite) {
  FakePlatform fake;
  Transport transport(fake, kMain, kBus);
  A2B_CHECK(transport.WriteReg(1, 0x40, 0x00) == Result::kOk);
  fake.ClearLog();
  fake.FailTransfer(0);
  A2B_CHECK(transport.WriteReg(1, 0x100, 0x01) == Result::kI2cError);
  A2B_CHECK(transport.WriteReg(1, 0x40, 0x02) == Result::kOk);
  CHECK_TRANSFERS(fake, "W 69 E0 01\nW 69 E0 00\nW 69 40 02\n");
}

A2B_TEST(FailedChipWriteIsRetried) {
  FakePlatform fake;
  Transport transport(fake, kMain, kBus);
  const uint8_t data[2] = {0x00, 0xAA};
  fake.FailTransfer(1);
  A2B_CHECK(transport.PeripheralWrite(1, kEeprom, data, sizeof(data)) == Result::kI2cError);
  A2B_CHECK(transport.PeripheralWrite(1, kEeprom, data, sizeof(data)) == Result::kOk);
  CHECK_TRANSFERS(fake, "W 68 01 01\nW 69 00 50\nW 69 00 50\nW 68 01 21\nW 69 00 AA\n");
}

A2B_TEST(FailedDataTransferKeepsTheCache) {
  FakePlatform fake;
  Transport transport(fake, kMain, kBus);
  fake.FailTransfer(1);
  A2B_CHECK(transport.WriteReg(1, 0x40, 0x01) == Result::kI2cError);
  A2B_CHECK(transport.WriteReg(1, 0x40, 0x01) == Result::kOk);
  CHECK_TRANSFERS(fake, "W 68 01 01\nW 69 40 01\nW 69 40 01\n");
}

A2B_TEST(WritingCachedRegistersForgetsThem) {
  FakePlatform fake;
  Transport transport(fake, kMain, kBus);
  const uint8_t data[2] = {0x00, 0xAA};

  A2B_CHECK(transport.WriteReg(kMainNode, reg::kNodeadr, 0x03) == Result::kOk);
  A2B_CHECK(transport.WriteReg(3, 0x40, 0x01) == Result::kOk);
  CHECK_TRANSFERS(fake, "W 68 01 03\nW 68 01 03\nW 69 40 01\n");

  A2B_CHECK(transport.PeripheralWrite(1, kEeprom, data, sizeof(data)) == Result::kOk);
  A2B_CHECK(transport.WriteReg(1, reg::kChip, 0x51) == Result::kOk);
  fake.ClearLog();
  A2B_CHECK(transport.PeripheralWrite(1, kEeprom, data, sizeof(data)) == Result::kOk);
  CHECK_TRANSFERS(fake, "W 69 00 50\nW 68 01 21\nW 69 00 AA\n");

  A2B_CHECK(transport.WriteReg(1, reg::kMmrpage, 0x01) == Result::kOk);
  fake.ClearLog();
  A2B_CHECK(transport.WriteReg(1, 0x40, 0x01) == Result::kOk);
  CHECK_TRANSFERS(fake, "W 69 E0 00\nW 69 40 01\n");
}

A2B_TEST(ForgetNodeResetsChipAndPage) {
  FakePlatform fake;
  Transport transport(fake, kMain, kBus);
  const uint8_t data[2] = {0x00, 0xAA};
  A2B_CHECK(transport.WriteReg(1, 0x100, 0x01) == Result::kOk);
  A2B_CHECK(transport.PeripheralWrite(1, kEeprom, data, sizeof(data)) == Result::kOk);
  transport.ForgetNode(1);
  fake.ClearLog();
  A2B_CHECK(transport.WriteReg(1, 0x40, 0x01) == Result::kOk);
  A2B_CHECK(transport.PeripheralWrite(1, kEeprom, data, sizeof(data)) == Result::kOk);
  CHECK_TRANSFERS(fake, "W 68 01 01\nW 69 40 01\nW 69 00 50\nW 68 01 21\nW 69 00 AA\n");
}

A2B_TEST(ForgetAllRewritesNodeadr) {
  FakePlatform fake;
  Transport transport(fake, kMain, kBus);
  A2B_CHECK(transport.WriteReg(2, 0x40, 0x01) == Result::kOk);
  transport.ForgetAll();
  fake.ClearLog();
  A2B_CHECK(transport.WriteReg(2, 0x40, 0x01) == Result::kOk);
  CHECK_TRANSFERS(fake, "W 68 01 02\nW 69 40 01\n");
}

A2B_TEST(SetAddressesMovesTheBusAndForgetsAll) {
  FakePlatform fake;
  Transport transport(fake, kMain, kBus);
  A2B_CHECK(transport.WriteReg(2, 0x40, 0x01) == Result::kOk);
  transport.SetAddresses(0x6A, 0x6B);
  A2B_CHECK_EQ(transport.MainAddr(), 0x6A);
  A2B_CHECK_EQ(transport.BusAddr(), 0x6B);
  fake.ClearLog();
  A2B_CHECK(transport.WriteReg(2, 0x40, 0x01) == Result::kOk);
  CHECK_TRANSFERS(fake, "W 6A 01 02\nW 6B 40 01\n");
}

A2B_TEST(LongWritesAreSplit) {
  FakePlatform fake;
  Transport transport(fake, kMain, kBus);
  uint8_t data[20];
  for (uint8_t i = 0; i < sizeof(data); ++i) {
    data[i] = i;
  }
  A2B_CHECK(transport.WriteRegs(kMainNode, 0x10, data, sizeof(data)) == Result::kOk);
  A2B_CHECK_EQ(fake.Transfers().size(), 2);
  if (fake.Transfers().size() == 2) {
    A2B_CHECK_EQ(fake.Transfers()[0].written.size(), 1 + Transport::kMaxRegBurst);
    A2B_CHECK_EQ(fake.Transfers()[0].written[0], 0x10);
    A2B_CHECK_EQ(fake.Transfers()[1].written.size(), 1 + sizeof(data) - Transport::kMaxRegBurst);
    A2B_CHECK_EQ(fake.Transfers()[1].written[0], 0x10 + Transport::kMaxRegBurst);
    A2B_CHECK_EQ(fake.Transfers()[1].written[1], Transport::kMaxRegBurst);
  }
}

A2B_TEST(InvalidArgumentsDoNothing) {
  FakePlatform fake;
  Transport transport(fake, kMain, kBus);
  uint8_t data[2] = {};
  A2B_CHECK(transport.WriteRegs(kMaxSubNodes, 0x40, data, 1) == Result::kInvalidArgument);
  A2B_CHECK(transport.WriteRegs(-2, 0x40, data, 1) == Result::kInvalidArgument);
  A2B_CHECK(transport.WriteRegs(0, 0x200, data, 1) == Result::kInvalidArgument);
  A2B_CHECK(transport.WriteRegs(0, 0xFF, data, 2) == Result::kInvalidArgument);
  A2B_CHECK(transport.WriteRegs(0, 0x40, data, 0) == Result::kInvalidArgument);
  A2B_CHECK(transport.WriteRegs(0, 0x40, nullptr, 1) == Result::kInvalidArgument);
  A2B_CHECK(transport.ReadRegs(0, 0x1FF, data, 2) == Result::kInvalidArgument);
  A2B_CHECK(transport.PeripheralWrite(0, 0x80, data, 1) == Result::kInvalidArgument);
  A2B_CHECK(transport.PeripheralRead(0, kEeprom, data, 0) == Result::kInvalidArgument);
  A2B_CHECK(transport.PeripheralWriteRead(0, kEeprom, data, 1, nullptr, 1) ==
            Result::kInvalidArgument);
  A2B_CHECK_EQ(fake.Transfers().size(), 0);
  A2B_CHECK_EQ(fake.LockCount(), 0);
}

A2B_TEST(EveryTransferHoldsTheLock) {
  FakePlatform fake;
  Transport transport(fake, kMain, kBus);
  const uint8_t data[2] = {0x00, 0xAA};
  uint8_t value = 0;
  A2B_CHECK(transport.WriteReg(1, 0x100, 0x01) == Result::kOk);
  A2B_CHECK(transport.ReadReg(2, 0x40, value) == Result::kOk);
  A2B_CHECK(transport.BroadcastWrite(0x12, data, 1) == Result::kOk);
  A2B_CHECK(transport.PeripheralWrite(3, kEeprom, data, sizeof(data)) == Result::kOk);
  A2B_CHECK_EQ(fake.LockCount(), 4);
  A2B_CHECK_EQ(fake.LockDepth(), 0);
  A2B_CHECK_EQ(fake.UnlockedTransfers(), 0);
}

A2B_TEST(TwoTransportsShareOneI2cBus) {
  FakePlatform fake;
  Transport busA(fake, kMain, kBus);
  Transport busB(fake, 0x6A, 0x6B);
  A2B_CHECK(busA.WriteReg(1, 0x40, 0x01) == Result::kOk);
  A2B_CHECK(busB.WriteReg(1, 0x40, 0x02) == Result::kOk);
  A2B_CHECK(busA.WriteReg(1, 0x40, 0x03) == Result::kOk);
  A2B_CHECK(busB.WriteReg(2, 0x40, 0x04) == Result::kOk);
  CHECK_TRANSFERS(fake,
                  "W 68 01 01\nW 69 40 01\nW 6A 01 01\nW 6B 40 02\nW 69 40 03\n"
                  "W 6A 01 02\nW 6B 40 04\n");
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
