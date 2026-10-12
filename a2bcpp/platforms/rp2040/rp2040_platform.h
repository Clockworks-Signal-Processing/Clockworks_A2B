/**
 * @file rp2040_platform.h
 * @brief A Platform for the RP2040 with the Pico SDK: host I2C on one of its I2C blocks,
 *        time from the system timer, an optional RESETn line, logging to stdio.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_PLATFORMS_RP2040_RP2040_PLATFORM_H_
#define A2BCPP_PLATFORMS_RP2040_RP2040_PLATFORM_H_

#include <cstddef>
#include <cstdint>

#include "a2b/platform.h"
#include "hardware/i2c.h"

namespace a2b {

/// RP2040 host I2C and an optional RESETn line for bus 0. No IRQ line yet: the stack polls
/// the interrupt registers.
class Rp2040Platform : public Platform {
 public:
  Rp2040Platform(i2c_inst_t* i2c, unsigned sdaPin, unsigned sclPin,
                 unsigned baudHz = 400000);  ///< Not set up yet; see Init().

  void SetResetPin(unsigned pin);  ///< Drives bus 0's RESETn on @p pin; before Init().
  void Init();                     ///< Sets up the I2C block, its pins and RESETn.

  Result I2cWrite(uint8_t addr, const uint8_t* data, size_t len) override;
  Result I2cRead(uint8_t addr, uint8_t* data, size_t len) override;
  Result I2cWriteRead(uint8_t addr, const uint8_t* writeData, size_t writeLen, uint8_t* readData,
                      size_t readLen) override;
  uint32_t NowMs() override;
  Result SetMainNodeReset(uint8_t bus, bool asserted) override;
  void Log(LogLevel level, const char* text) override;

 private:
  i2c_inst_t* const m_I2c;
  const unsigned m_SdaPin;
  const unsigned m_SclPin;
  const unsigned m_BaudHz;
  int m_ResetPin = -1;  ///< GPIO of bus 0's RESETn, or -1.
};

}  // namespace a2b

#endif  // A2BCPP_PLATFORMS_RP2040_RP2040_PLATFORM_H_
