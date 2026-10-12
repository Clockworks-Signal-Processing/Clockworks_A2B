/**
 * @file linux_platform.h
 * @brief A Platform for Linux hosts such as the Raspberry Pi: host I2C through i2c-dev,
 *        time from the monotonic clock, logging to stderr.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_PLATFORMS_LINUX_LINUX_PLATFORM_H_
#define A2BCPP_PLATFORMS_LINUX_LINUX_PLATFORM_H_

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "a2b/platform.h"

namespace a2b {

/// Linux i2c-dev host I2C. No reset or IRQ line yet: the stack polls the interrupt registers.
class LinuxPlatform : public Platform {
 public:
  LinuxPlatform() = default;  ///< Not open.
  ~LinuxPlatform() override;

  LinuxPlatform(const LinuxPlatform&) = delete;
  LinuxPlatform& operator=(const LinuxPlatform&) = delete;

  Result Open(const char* device);  ///< Opens an I2C bus, e.g. "/dev/i2c-1".
  void Close();                     ///< Closes it.
  void SetTrace(std::FILE* out);    ///< Writes each transfer to @p out (I2CT lines), or stops.

  Result I2cWrite(uint8_t addr, const uint8_t* data, size_t len) override;
  Result I2cRead(uint8_t addr, uint8_t* data, size_t len) override;
  Result I2cWriteRead(uint8_t addr, const uint8_t* writeData, size_t writeLen, uint8_t* readData,
                      size_t readLen) override;
  uint32_t NowMs() override;
  void Log(LogLevel level, const char* text) override;

 private:
  Result Run(uint8_t addr, const uint8_t* writeData, size_t writeLen, uint8_t* readData,
             size_t readLen);  ///< One transfer, traced.

  int m_Fd = -1;                 ///< The i2c-dev file, or -1.
  std::FILE* m_Trace = nullptr;  ///< Where transfers are traced, or none.
  uint32_t m_TraceSeq = 0;       ///< Next trace sequence number.
  uint64_t m_TraceStartUs = 0;   ///< Time of SetTrace().
};

}  // namespace a2b

#endif  // A2BCPP_PLATFORMS_LINUX_LINUX_PLATFORM_H_
