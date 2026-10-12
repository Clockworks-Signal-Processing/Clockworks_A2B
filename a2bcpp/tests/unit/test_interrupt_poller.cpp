/**
 * @file test_interrupt_poller.cpp
 * @brief Unit tests for InterruptPoller against the fake platform: the interval, draining
 *        after an interrupt, the IRQ line and failed reads.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string>

#include "detail/regdefs.h"
#include "fake_platform.h"
#include "interrupt_poller.h"
#include "test_harness.h"

namespace a2b {
namespace {

constexpr uint8_t kMainAddr = 0x68;
constexpr uint8_t kBusAddr = 0x69;

/** The fake platform with an IRQ line. */
class IrqPlatform : public test::FakePlatform {
 public:
  bool IrqPending(uint8_t bus) override {
    lastBus = bus;
    return irq;
  }
  bool irq = false;
  int lastBus = -1;
};

A2B_TEST(ChecksAtTheIntervalAndDrains) {
  test::FakePlatform platform;
  Transport transport(platform, kMainAddr, kBusAddr);
  InterruptPoller poller(platform, transport, 0, 20);
  InterruptPoller::Interrupt irq;
  bool found = true;
  A2B_CHECK(poller.Poll(1000, irq, found) == Result::kOk);  // The first poll checks.
  A2B_CHECK(!found);
  std::string dump = platform.Dump();
  A2B_CHECK_STREQ(dump.c_str(), "X 68 16 r1\n");
  platform.ClearLog();
  A2B_CHECK(poller.Poll(1019, irq, found) == Result::kOk);
  A2B_CHECK_EQ(platform.Transfers().size(), 0u);
  platform.SetReg(kMainAddr, reg::kIntsrc, reg::kIntsrcMstint);
  platform.SetReg(kMainAddr, reg::kInttype, reg::kInttypeDscdone);
  A2B_CHECK(poller.Poll(1020, irq, found) == Result::kOk);
  A2B_CHECK(found);
  A2B_CHECK_EQ(irq.source, reg::kIntsrcMstint);
  A2B_CHECK_EQ(irq.type, reg::kInttypeDscdone);
  dump = platform.Dump();
  A2B_CHECK_STREQ(dump.c_str(), "X 68 16 r1\nX 68 17 r1\n");
  // After an interrupt the next poll checks at once.
  platform.ClearLog();
  platform.SetReg(kMainAddr, reg::kIntsrc, 0);
  A2B_CHECK(poller.Poll(1021, irq, found) == Result::kOk);
  A2B_CHECK(!found);
  A2B_CHECK_EQ(platform.Transfers().size(), 1u);
  // CheckAtNextPoll() does the same; CheckNow() ignores the interval.
  poller.CheckAtNextPoll();
  A2B_CHECK(poller.Poll(1022, irq, found) == Result::kOk);
  A2B_CHECK_EQ(platform.Transfers().size(), 2u);
  A2B_CHECK(poller.CheckNow(irq, found) == Result::kOk);
  A2B_CHECK_EQ(platform.Transfers().size(), 3u);
  A2B_CHECK_EQ(platform.UnlockedTransfers(), 0);
}

A2B_TEST(SubNodeInterruptsCount) {
  test::FakePlatform platform;
  Transport transport(platform, kMainAddr, kBusAddr);
  InterruptPoller poller(platform, transport, 0, 20);
  platform.SetReg(kMainAddr, reg::kIntsrc, reg::kIntsrcSlvint | 2);
  platform.SetReg(kMainAddr, reg::kInttype, 8);
  InterruptPoller::Interrupt irq;
  bool found = false;
  A2B_CHECK(poller.Poll(0, irq, found) == Result::kOk);
  A2B_CHECK(found);
  A2B_CHECK_EQ(irq.source, reg::kIntsrcSlvint | 2);
}

A2B_TEST(IrqLineGatesTheReads) {
  IrqPlatform platform;
  Transport transport(platform, kMainAddr, kBusAddr);
  InterruptPoller poller(platform, transport, 3, 20);
  InterruptPoller::Interrupt irq;
  bool found = true;
  A2B_CHECK(poller.Poll(0, irq, found) == Result::kOk);
  A2B_CHECK(!found);
  A2B_CHECK_EQ(platform.lastBus, 3);
  A2B_CHECK_EQ(platform.Transfers().size(), 0u);
  platform.irq = true;
  A2B_CHECK(poller.CheckNow(irq, found) == Result::kOk);
  A2B_CHECK_EQ(platform.Transfers().size(), 1u);
}

A2B_TEST(FailedReadsAreReported) {
  test::FakePlatform platform;
  Transport transport(platform, kMainAddr, kBusAddr);
  InterruptPoller poller(platform, transport, 0, 20);
  InterruptPoller::Interrupt irq;
  bool found = true;
  platform.FailTransfer(0);
  A2B_CHECK(poller.Poll(0, irq, found) == Result::kI2cError);
  A2B_CHECK(!found);
  platform.SetReg(kMainAddr, reg::kIntsrc, reg::kIntsrcMstint);
  platform.FailTransfer(1);  // INTTYPE.
  A2B_CHECK(poller.CheckNow(irq, found) == Result::kI2cError);
  A2B_CHECK(!found);
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
