// clang-format off
/*=============================================================================
 *
 * Project: a2bstack
 *
 * Copyright (c) 2015 -2023 Analog Devices Inc. All Rights Reserved.
 * See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
 *
 *=============================================================================
 */
// clang-format on

/**
 * @file usbi_platform.h
 * @brief A Platform over the ADI USBi USB-to-I2C adapter: host I2C through the USBi's
 *        vendor requests, time from the host's steady clock, logging to stderr.
 *
 * Ported from ADI's Windows PAL, pal_i2c_usbi.c.
 * C++ form by ClockWorks Signal Processing LLC.
 */

#ifndef A2BCPP_PLATFORMS_USBI_USBI_PLATFORM_H_
#define A2BCPP_PLATFORMS_USBI_USBI_PLATFORM_H_

#include <cstddef>
#include <cstdint>

#include "a2b/platform.h"
#include "usbi_link.h"

namespace a2b {

/// Host I2C through an ADI USBi. No reset or IRQ line: the stack polls the interrupt
/// registers and resets the main node through CONTROL.
class UsbiPlatform : public Platform {
 public:
  /// The USBi's transfer status (its CHECK_WRITE and CHECK_READ requests).
  enum class Status : uint8_t {
    kNone = 0,       ///< Nothing to report (idle, or not started yet).
    kSuccess = 1,    ///< The last transfer completed.
    kWriting = 2,    ///< A write is under way.
    kFailed = 3,     ///< The device did not acknowledge.
    kError = 4,      ///< Bus error during the last transfer.
    kException = 5,  ///< The USBi needs a reset; also any failed status request.
  };

  static constexpr size_t kMaxTransfer = 6000;  ///< Most bytes in one USBi transfer.

  explicit UsbiPlatform(UsbiLink& link);  ///< Over @p link, which must outlive it.

  Result I2cWrite(uint8_t addr, const uint8_t* data, size_t len) override;
  Result I2cRead(uint8_t addr, uint8_t* data, size_t len) override;
  Result I2cWriteRead(uint8_t addr, const uint8_t* writeData, size_t writeLen, uint8_t* readData,
                      size_t readLen) override;
  uint32_t NowMs() override;
  void Log(LogLevel level, const char* text) override;

 private:
  Result Write(uint8_t request, uint8_t addr, const uint8_t* data, size_t len,
               unsigned timeoutMs);             ///< A write request, then its status.
  Status WaitForWrite(uint8_t addr);            ///< Polls CHECK_WRITE until the write is done.
  Status WaitForRead(uint8_t addr);             ///< Polls CHECK_READ until the read is done.
  Status Check(uint8_t request, uint8_t addr);  ///< One status request.
  Result Finish(uint8_t addr, Status status);   ///< The status as a Result.
  static unsigned TimeoutMs(size_t len);        ///< The USB timeout for @p len bytes.

  UsbiLink& m_Link;
  bool m_TransferOk = false;  ///< A transfer has succeeded since the platform was made.
  bool m_HintShown = false;   ///< The "is the USBi connected" hint was logged.
};

}  // namespace a2b

#endif  // A2BCPP_PLATFORMS_USBI_USBI_PLATFORM_H_
