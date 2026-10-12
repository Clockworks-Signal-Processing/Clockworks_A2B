/**
 * @file test_usbi_platform.cpp
 * @brief UsbiPlatform's USB framing, byte for byte, against a fake USB link: the requests,
 *        values, data stages and timeouts of ADI's PAL (pal_i2c_usbi.c), its waits,
 *        and its status handling.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include "test_harness.h"
#include "usbi_platform.h"

namespace {

/// A UsbiLink that records every call as a line and answers status requests from a
/// queue of status bytes (success once the queue is empty).
class FakeLink : public a2b::UsbiLink {
 public:
  int ControlTransfer(uint8_t requestType, uint8_t request, uint16_t value, uint16_t index,
                      uint8_t* data, uint16_t length, unsigned timeoutMs) override {
    char line[64];
    std::snprintf(line, sizeof(line), "%s %02X %02X %04X %04X %u t%u",
                  (requestType & 0x80) ? "IN" : "OUT", requestType, request, value, index,
                  static_cast<unsigned>(length), timeoutMs);
    std::string text = line;
    const bool status = (request == 0xB5 || request == 0xB6);
    if (status) {
      int answer = 1;
      if (!m_Statuses.empty()) {
        answer = m_Statuses.front();
        m_Statuses.pop_front();
      }
      if (answer < 0) {
        m_Log.push_back(text + " -> fail");
        return answer;
      }
      data[0] = static_cast<uint8_t>(answer);
      m_Log.push_back(text + " -> " + Hex(data, 1));
      return 1;
    }
    if (requestType & 0x80) {
      for (uint16_t i = 0; i < length; ++i) {
        data[i] = static_cast<uint8_t>(0xA0 + i);
      }
      m_Log.push_back(text + " -> " + Hex(data, length));
    } else {
      m_Log.push_back(text + " [" + Hex(data, length) + "]");
    }
    return (m_ShortBy > 0) ? length - m_ShortBy : length;
  }

  void SleepMs(unsigned ms) override { m_Log.push_back("SLEEP " + std::to_string(ms)); }

  /// The next status requests answer these, in order (a negative value fails the request).
  void QueueStatus(std::initializer_list<int> statuses) {
    m_Statuses.insert(m_Statuses.end(), statuses.begin(), statuses.end());
  }

  void ShortBy(int bytes) { m_ShortBy = bytes; }  ///< Data transfers move fewer bytes.
  std::vector<std::string>& Log() { return m_Log; }

  /// The log as one string, a line per call, for comparison with the expected one.
  std::string Dump() const {
    std::string all;
    for (const std::string& line : m_Log) {
      all += line + "\n";
    }
    return all;
  }

 private:
  static std::string Hex(const uint8_t* data, size_t len) {
    std::string text;
    char byte[4];
    for (size_t i = 0; i < len; ++i) {
      std::snprintf(byte, sizeof(byte), i ? " %02X" : "%02X", data[i]);
      text += byte;
    }
    return text;
  }

  std::vector<std::string> m_Log;
  std::deque<int> m_Statuses;
  int m_ShortBy = 0;
};

/// A UsbiPlatform that keeps its log lines instead of printing them.
class QuietPlatform : public a2b::UsbiPlatform {
 public:
  using UsbiPlatform::UsbiPlatform;
  void Log(a2b::LogLevel level, const char* text) override {
    (void)level;
    lines.push_back(text);
  }
  std::vector<std::string> lines;  ///< What it logged.
};

}  // namespace

A2B_TEST(WriteIsVendorRequestB2ThenWriteStatus) {
  FakeLink link;
  QuietPlatform platform(link);
  const uint8_t data[] = {0x01, 0x02};
  A2B_CHECK(platform.I2cWrite(0x68, data, sizeof(data)) == a2b::Result::kOk);
  const std::string dump = link.Dump();
  A2B_CHECK_STREQ(dump.c_str(),
                  "OUT 40 B2 0068 0000 2 t500 [01 02]\n"
                  "SLEEP 2\n"
                  "IN C0 B5 0068 0000 1 t500 -> 01\n");
}

A2B_TEST(ReadIsVendorRequestB4ThenReadStatus) {
  FakeLink link;
  QuietPlatform platform(link);
  uint8_t data[3] = {};
  A2B_CHECK(platform.I2cRead(0x69, data, sizeof(data)) == a2b::Result::kOk);
  A2B_CHECK_EQ(data[0], 0xA0);
  A2B_CHECK_EQ(data[2], 0xA2);
  const std::string dump = link.Dump();
  A2B_CHECK_STREQ(dump.c_str(),
                  "IN C0 B4 0069 0000 3 t500 -> A0 A1 A2\n"
                  "SLEEP 2\n"
                  "IN C0 B6 0069 0000 1 t500 -> 01\n");
}

A2B_TEST(WriteReadIsB3ThenOneMillisecondThenRead) {
  FakeLink link;
  QuietPlatform platform(link);
  const uint8_t reg = 0x17;
  uint8_t value = 0;
  A2B_CHECK(platform.I2cWriteRead(0x68, &reg, 1, &value, 1) == a2b::Result::kOk);
  A2B_CHECK_EQ(value, 0xA0);
  const std::string dump = link.Dump();
  A2B_CHECK_STREQ(dump.c_str(),
                  "OUT 40 B3 0068 0000 1 t500 [17]\n"
                  "SLEEP 2\n"
                  "IN C0 B5 0068 0000 1 t500 -> 01\n"
                  "SLEEP 1\n"
                  "IN C0 B4 0068 0000 1 t500 -> A0\n"
                  "SLEEP 2\n"
                  "IN C0 B6 0068 0000 1 t500 -> 01\n");
}

A2B_TEST(LongTransfersGetFourTimesTheTimeout) {
  FakeLink link;
  QuietPlatform platform(link);
  uint8_t data[17] = {};
  A2B_CHECK(platform.I2cWrite(0x50, data, 16) == a2b::Result::kOk);
  A2B_CHECK(platform.I2cWrite(0x50, data, 17) == a2b::Result::kOk);
  A2B_CHECK(platform.I2cRead(0x50, data, 17) == a2b::Result::kOk);
  // The write half of a write-read keeps 500 ms, as in ADI's PAL; its read doesn't.
  A2B_CHECK(platform.I2cWriteRead(0x50, data, 17, data, 17) == a2b::Result::kOk);
  const std::vector<std::string>& log = link.Log();
  A2B_CHECK(log[0].find("OUT 40 B2 0050 0000 16 t500 ") == 0);
  A2B_CHECK(log[3].find("OUT 40 B2 0050 0000 17 t2000 ") == 0);
  A2B_CHECK(log[6].find("IN C0 B4 0050 0000 17 t2000 ") == 0);
  A2B_CHECK(log[9].find("OUT 40 B3 0050 0000 17 t500 ") == 0);
  A2B_CHECK(log[13].find("IN C0 B4 0050 0000 17 t2000 ") == 0);
}

A2B_TEST(WriteStatusPolledWhileNoneOrWriting) {
  FakeLink link;
  QuietPlatform platform(link);
  link.QueueStatus({0, 2, 2, 1});  // NONE, WRITING, WRITING, SUCCESS
  const uint8_t data = 0x55;
  A2B_CHECK(platform.I2cWrite(0x68, &data, 1) == a2b::Result::kOk);
  const std::string dump = link.Dump();
  A2B_CHECK_STREQ(dump.c_str(),
                  "OUT 40 B2 0068 0000 1 t500 [55]\n"
                  "SLEEP 2\n"
                  "IN C0 B5 0068 0000 1 t500 -> 00\n"
                  "SLEEP 10\n"
                  "IN C0 B5 0068 0000 1 t500 -> 02\n"
                  "SLEEP 10\n"
                  "IN C0 B5 0068 0000 1 t500 -> 02\n"
                  "SLEEP 10\n"
                  "IN C0 B5 0068 0000 1 t500 -> 01\n");
}

A2B_TEST(ReadStatusPolledWhileNoneOnly) {
  FakeLink link;
  QuietPlatform platform(link);
  link.QueueStatus({0, 0, 1});
  uint8_t data = 0;
  A2B_CHECK(platform.I2cRead(0x68, &data, 1) == a2b::Result::kOk);
  A2B_CHECK_EQ(link.Log().size(), 7);  // read, sleep 2, check, (sleep 10, check) x 2
  A2B_CHECK_STREQ(link.Log()[3].c_str(), "SLEEP 10");

  // WRITING ends a read's wait: it is not a success.
  FakeLink link2;
  QuietPlatform platform2(link2);
  link2.QueueStatus({2});
  A2B_CHECK(platform2.I2cRead(0x68, &data, 1) == a2b::Result::kI2cError);
  A2B_CHECK_EQ(link2.Log().size(), 3);
}

A2B_TEST(StatusGivesUpAfterThirtyMoreChecks) {
  FakeLink link;
  QuietPlatform platform(link);
  for (int i = 0; i < 40; ++i) {
    link.QueueStatus({2});
  }
  const uint8_t data = 0;
  A2B_CHECK(platform.I2cWrite(0x68, &data, 1) == a2b::Result::kI2cError);
  // The write, the 2 ms wait and the first check, then 30 waits and checks.
  A2B_CHECK_EQ(link.Log().size(), 3 + 2 * 30);
  A2B_CHECK_STREQ(link.Log().back().c_str(), "IN C0 B5 0068 0000 1 t500 -> 02");
}

A2B_TEST(FailedStatusesAreI2cErrors) {
  const int statuses[] = {3, 4, 5, 6, -1};  // FAILED (NAK), ERROR, EXCEPTION, unknown, USB error
  for (int status : statuses) {
    FakeLink link;
    QuietPlatform platform(link);
    link.QueueStatus({status});
    const uint8_t data = 0;
    A2B_CHECK(platform.I2cWrite(0x68, &data, 1) == a2b::Result::kI2cError);
    A2B_CHECK_EQ(link.Log().size(), 3);  // no more checks after a final status
  }
}

A2B_TEST(ShortUsbTransferFailsWithoutStatusCheck) {
  FakeLink link;
  QuietPlatform platform(link);
  link.ShortBy(1);
  const uint8_t data[2] = {};
  A2B_CHECK(platform.I2cWrite(0x68, data, 2) == a2b::Result::kI2cError);
  uint8_t read[2] = {};
  A2B_CHECK(platform.I2cRead(0x68, read, 2) == a2b::Result::kI2cError);
  A2B_CHECK(platform.I2cWriteRead(0x68, data, 2, read, 2) == a2b::Result::kI2cError);
  A2B_CHECK_EQ(link.Log().size(), 3);  // one request each, nothing else
}

A2B_TEST(BadArgumentsSendNothing) {
  FakeLink link;
  QuietPlatform platform(link);
  static uint8_t big[a2b::UsbiPlatform::kMaxTransfer + 1] = {};
  A2B_CHECK(platform.I2cWrite(0x68, big, 0) == a2b::Result::kInvalidArgument);
  A2B_CHECK(platform.I2cWrite(0x68, nullptr, 1) == a2b::Result::kInvalidArgument);
  A2B_CHECK(platform.I2cWrite(0x68, big, sizeof(big)) == a2b::Result::kInvalidArgument);
  A2B_CHECK(platform.I2cRead(0x68, big, 0) == a2b::Result::kInvalidArgument);
  A2B_CHECK(platform.I2cRead(0x68, big, sizeof(big)) == a2b::Result::kInvalidArgument);
  A2B_CHECK(platform.I2cWriteRead(0x68, big, 1, big, 0) == a2b::Result::kInvalidArgument);
  A2B_CHECK(platform.I2cWriteRead(0x68, big, 0, big, 1) == a2b::Result::kInvalidArgument);
  A2B_CHECK_EQ(link.Log().size(), 0);
  A2B_CHECK(platform.I2cWrite(0x68, big, a2b::UsbiPlatform::kMaxTransfer) == a2b::Result::kOk);
}

A2B_TEST(HintLoggedOnceUntilATransferWorks) {
  FakeLink link;
  QuietPlatform platform(link);
  const uint8_t data = 0;
  link.QueueStatus({3, 3});
  A2B_CHECK(platform.I2cWrite(0x68, &data, 1) == a2b::Result::kI2cError);
  A2B_CHECK(platform.I2cWrite(0x68, &data, 1) == a2b::Result::kI2cError);
  A2B_CHECK_EQ(platform.lines.size(), 1);
  A2B_CHECK(platform.lines[0].find("0x68 failed (no acknowledge from the device)") !=
            std::string::npos);

  // Once a transfer has worked, failures are the stack's to report.
  FakeLink link2;
  QuietPlatform platform2(link2);
  link2.QueueStatus({1, 3});
  A2B_CHECK(platform2.I2cWrite(0x68, &data, 1) == a2b::Result::kOk);
  A2B_CHECK(platform2.I2cWrite(0x69, &data, 1) == a2b::Result::kI2cError);
  A2B_CHECK_EQ(platform2.lines.size(), 0);
}

A2B_TEST(ClockMoves) {
  FakeLink link;
  QuietPlatform platform(link);
  const uint32_t first = platform.NowMs();
  A2B_CHECK(a2b::ElapsedMs(platform.NowMs(), first) < 1000);
}

A2B_TEST_MAIN()
