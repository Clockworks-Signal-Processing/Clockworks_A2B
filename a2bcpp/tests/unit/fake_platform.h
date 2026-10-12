/**
 * @file fake_platform.h
 * @brief A Platform for unit tests: records every I2C transfer, answers reads from a
 *        plain register file per I2C address, and can fail chosen transfers.
 *
 * It knows nothing about A2B; the simulator (a2bcpp/sim/) models the chips.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_TESTS_UNIT_FAKE_PLATFORM_H_
#define A2BCPP_TESTS_UNIT_FAKE_PLATFORM_H_

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "a2b/platform.h"

namespace a2b {
namespace test {

class FakePlatform : public Platform {
 public:
  /** One I2C transfer as the platform saw it. */
  struct Transfer {
    char op;  ///< 'W' write, 'R' read, 'X' write-read.
    uint8_t addr;
    std::vector<uint8_t> written;
    size_t readLen;
  };

  Result I2cWrite(uint8_t addr, const uint8_t* data, size_t len) override {
    CheckLocked();
    m_Log.push_back({'W', addr, std::vector<uint8_t>(data, data + len), 0});
    if (FailThis()) {
      return Result::kI2cError;
    }
    for (size_t i = 1; i < len; ++i) {
      m_Regs[addr][(data[0] + i - 1) & 0xFF] = data[i];
    }
    return Result::kOk;
  }

  Result I2cRead(uint8_t addr, uint8_t* data, size_t len) override {
    CheckLocked();
    m_Log.push_back({'R', addr, {}, len});
    if (FailThis()) {
      return Result::kI2cError;
    }
    for (size_t i = 0; i < len; ++i) {
      data[i] = m_Regs[addr][i & 0xFF];
    }
    return Result::kOk;
  }

  Result I2cWriteRead(uint8_t addr, const uint8_t* writeData, size_t writeLen, uint8_t* readData,
                      size_t readLen) override {
    CheckLocked();
    m_Log.push_back({'X', addr, std::vector<uint8_t>(writeData, writeData + writeLen), readLen});
    if (FailThis()) {
      return Result::kI2cError;
    }
    for (size_t i = 0; i < readLen; ++i) {
      readData[i] = m_Regs[addr][(writeData[0] + i) & 0xFF];
    }
    return Result::kOk;
  }

  uint32_t NowMs() override { return m_NowMs; }

  void Lock() override {
    ++m_LockDepth;
    ++m_LockCount;
  }
  void Unlock() override { --m_LockDepth; }

  /** Makes the transfer @p fromNow transfers ahead fail (0 = the next one). */
  void FailTransfer(int fromNow) { m_FailAt = static_cast<int>(m_Log.size()) + fromNow; }

  /** Sets what a read of @p reg at @p addr returns. */
  void SetReg(uint8_t addr, uint8_t reg, uint8_t value) { m_Regs[addr][reg] = value; }

  /** The transfers so far, one per line, e.g. "W 68 01 02" or "X 69 03 r1". */
  std::string Dump() const {
    std::string text;
    char item[8];
    for (const Transfer& transfer : m_Log) {
      std::snprintf(item, sizeof(item), "%c %02X", transfer.op, transfer.addr);
      text += item;
      for (uint8_t byte : transfer.written) {
        std::snprintf(item, sizeof(item), " %02X", byte);
        text += item;
      }
      if (transfer.op != 'W') {
        std::snprintf(item, sizeof(item), " r%u", static_cast<unsigned>(transfer.readLen));
        text += item;
      }
      text += '\n';
    }
    return text;
  }

  /** Forgets the recorded transfers. */
  void ClearLog() {
    m_Log.clear();
    m_FailAt = -1;
  }

  /** Sets what NowMs() returns. */
  void SetNowMs(uint32_t nowMs) { m_NowMs = nowMs; }

  const std::vector<Transfer>& Transfers() const { return m_Log; }
  int LockCount() const { return m_LockCount; }
  int LockDepth() const { return m_LockDepth; }
  int UnlockedTransfers() const { return m_UnlockedTransfers; }

 private:
  bool FailThis() const { return static_cast<int>(m_Log.size()) - 1 == m_FailAt; }
  void CheckLocked() {
    if (m_LockDepth <= 0) {
      ++m_UnlockedTransfers;
    }
  }

  std::vector<Transfer> m_Log;
  uint8_t m_Regs[128][256] = {};
  uint32_t m_NowMs = 0;
  int m_FailAt = -1;
  int m_LockDepth = 0;
  int m_LockCount = 0;
  int m_UnlockedTransfers = 0;
};

}  // namespace test
}  // namespace a2b

#endif  // A2BCPP_TESTS_UNIT_FAKE_PLATFORM_H_
