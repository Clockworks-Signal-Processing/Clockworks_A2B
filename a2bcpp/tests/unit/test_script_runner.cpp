/**
 * @file test_script_runner.cpp
 * @brief Unit tests for ScriptRunner against the recording fake platform: operation
 *        order, waits across polls, splitting long peripheral transfers, failures.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string>

#include "fake_platform.h"
#include "script_runner.h"
#include "test_harness.h"

namespace a2b {
namespace {

using State = ScriptRunner::State;

constexpr uint8_t kMainAddr = 0x68;
constexpr uint8_t kBusAddr = 0x69;
constexpr uint8_t kCodecAddr = 0x39;

/** Checks every transfer so far (the log is kept). */
#define CHECK_TRANSFERS(fake, expected)      \
  do {                                       \
    const std::string dump = (fake).Dump();  \
    A2B_CHECK_STREQ(dump.c_str(), expected); \
  } while (0)

struct Bench {
  test::FakePlatform platform;
  Transport transport{platform, kMainAddr, kBusAddr};
  ScriptRunner runner{transport};
  FixedOpList<8> script;
};

A2B_TEST(IdleUntilStarted) {
  Bench bench;
  A2B_CHECK(bench.runner.Poll(0) == State::kIdle);
  bench.runner.Start(bench.script);  // Empty script.
  A2B_CHECK(bench.runner.Poll(0) == State::kDone);
  A2B_CHECK(bench.platform.Dump().empty());
}

A2B_TEST(RunsOperationsInOrder) {
  Bench bench;
  const uint8_t data[2] = {0xAA, 0xBB};
  bench.script.Add(Op::RegWrite(kMainNode, 0x41, 0xE2));
  bench.script.Add(Op::PeripheralWrite(kMainNode, kCodecAddr, 2, 0x4015, data, 2));
  bench.script.Add(Op::PeripheralRead(kMainNode, kCodecAddr, 2, 0x4002, 6));
  bench.script.Add(Op::PeripheralRead(kMainNode, kCodecAddr, 0, 0, 1));
  bench.runner.Start(bench.script);
  A2B_CHECK(bench.runner.Poll(0) == State::kDone);
  CHECK_TRANSFERS(bench.platform,
                  "W 68 41 E2\n"
                  "W 39 40 15 AA BB\n"
                  "X 39 40 02 r6\n"
                  "R 39 r1\n");
}

A2B_TEST(WaitsAcrossPolls) {
  Bench bench;
  bench.script.Add(Op::RegWrite(kMainNode, 0x41, 0x01));
  bench.script.Add(Op::Wait(100));
  bench.script.Add(Op::RegWrite(kMainNode, 0x41, 0x02));
  bench.script.Add(Op::Wait(0));  // No wait at all.
  bench.script.Add(Op::RegWrite(kMainNode, 0x41, 0x03));
  bench.runner.Start(bench.script);
  A2B_CHECK(bench.runner.Poll(1000) == State::kRunning);
  A2B_CHECK_EQ(bench.runner.Position(), 1u);
  CHECK_TRANSFERS(bench.platform, "W 68 41 01\n");
  A2B_CHECK(bench.runner.Poll(1099) == State::kRunning);
  CHECK_TRANSFERS(bench.platform, "W 68 41 01\n");
  A2B_CHECK(bench.runner.Poll(1100) == State::kDone);
  CHECK_TRANSFERS(bench.platform, "W 68 41 01\nW 68 41 02\nW 68 41 03\n");
}

A2B_TEST(WaitAcrossTheClockWrap) {
  Bench bench;
  bench.script.Add(Op::Wait(20));
  bench.script.Add(Op::RegWrite(kMainNode, 0x41, 0x01));
  bench.runner.Start(bench.script);
  A2B_CHECK(bench.runner.Poll(0xFFFFFFF0u) == State::kRunning);
  A2B_CHECK(bench.runner.Poll(0x00000003u) == State::kRunning);
  A2B_CHECK(bench.runner.Poll(0x00000004u) == State::kDone);
}

A2B_TEST(SplitsLongWritesBySubAddressStep) {
  Bench bench;
  // 13 SigmaDSP program words of 5 bytes: 65 bytes, sent in 30-byte parts (6 words).
  uint8_t data[65];
  for (size_t i = 0; i < sizeof(data); ++i) {
    data[i] = static_cast<uint8_t>(i);
  }
  bench.script.Add(Op::PeripheralWrite(kMainNode, kCodecAddr, 2, 0x0800, data, 65, 5));
  bench.runner.Start(bench.script);
  A2B_CHECK(bench.runner.Poll(0) == State::kDone);
  const auto& log = bench.platform.Transfers();
  A2B_CHECK_EQ(log.size(), 3u);
  const uint16_t addrs[3] = {0x0800, 0x0806, 0x080C};
  const size_t lens[3] = {30, 30, 5};
  size_t offset = 0;
  for (size_t i = 0; i < 3 && i < log.size(); ++i) {
    A2B_CHECK_EQ(log[i].written.size(), 2 + lens[i]);
    A2B_CHECK_EQ((log[i].written[0] << 8) | log[i].written[1], addrs[i]);
    A2B_CHECK_EQ(log[i].written[2], data[offset]);
    offset += lens[i];
  }
}

A2B_TEST(SplitsLongWritesByteByByte) {
  Bench bench;
  uint8_t data[40] = {};
  bench.script.Add(Op::PeripheralWrite(kMainNode, kCodecAddr, 1, 0x10, data, 40));
  bench.runner.Start(bench.script);
  A2B_CHECK(bench.runner.Poll(0) == State::kDone);
  const auto& log = bench.platform.Transfers();
  A2B_CHECK_EQ(log.size(), 2u);
  A2B_CHECK_EQ(log[0].written.size(), 1 + ScriptRunner::kMaxChunk);
  A2B_CHECK_EQ(log[1].written[0], 0x10 + ScriptRunner::kMaxChunk);
  A2B_CHECK_EQ(log[1].written.size(), 1u + 8u);
}

A2B_TEST(ReachesSubNodePeripherals) {
  Bench bench;
  const uint8_t data[1] = {0x5A};
  bench.script.Add(Op::PeripheralWrite(3, 0x50, 2, 0x0010, data, 1));
  bench.runner.Start(bench.script);
  A2B_CHECK(bench.runner.Poll(0) == State::kDone);
  // NODEADR = 3, CHIP = 0x50, NODEADR = PERI | 3, then the transfer on the bus address.
  CHECK_TRANSFERS(bench.platform,
                  "W 68 01 03\n"
                  "W 69 00 50\n"
                  "W 68 01 23\n"
                  "W 69 00 10 5A\n");
}

A2B_TEST(StopsAtTheFirstFailure) {
  Bench bench;
  bench.script.Add(Op::RegWrite(kMainNode, 0x41, 0x01));
  bench.script.Add(Op::RegWrite(kMainNode, 0x41, 0x02));
  bench.script.Add(Op::RegWrite(kMainNode, 0x41, 0x03));
  bench.platform.FailTransfer(1);
  bench.runner.Start(bench.script);
  A2B_CHECK(bench.runner.Poll(0) == State::kFailed);
  A2B_CHECK_EQ(bench.runner.Position(), 1u);
  A2B_CHECK(bench.runner.Error() == Result::kI2cError);
  A2B_CHECK(bench.runner.Poll(10) == State::kFailed);  // Stays failed; nothing more sent.
  A2B_CHECK_EQ(bench.platform.Transfers().size(), 2u);
}

A2B_TEST(RejectsBadOperations) {
  Bench bench;
  Op bad = Op::PeripheralWrite(kMainNode, kCodecAddr, 5, 0, nullptr, 1);
  bench.script.Add(bad);
  bench.runner.Start(bench.script);
  A2B_CHECK(bench.runner.Poll(0) == State::kFailed);
  A2B_CHECK(bench.runner.Error() == Result::kInvalidArgument);

  Bench big;
  const uint8_t data[1] = {0};
  big.script.Add(Op::PeripheralWrite(kMainNode, kCodecAddr, 2, 0, data, 1, 40));  // Step > chunk.
  big.runner.Start(big.script);
  A2B_CHECK(big.runner.Poll(0) == State::kFailed);
  A2B_CHECK(big.platform.Transfers().empty());
}

A2B_TEST(StopAbandonsTheScript) {
  Bench bench;
  bench.script.Add(Op::Wait(10));
  bench.script.Add(Op::RegWrite(kMainNode, 0x41, 0x01));
  bench.runner.Start(bench.script);
  A2B_CHECK(bench.runner.Poll(0) == State::kRunning);
  bench.runner.Stop();
  A2B_CHECK(bench.runner.Poll(100) == State::kIdle);
  A2B_CHECK(bench.platform.Transfers().empty());
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
