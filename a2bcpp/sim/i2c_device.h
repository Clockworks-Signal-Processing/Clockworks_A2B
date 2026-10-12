/**
 * @file i2c_device.h
 * @brief A simulated I2C device: an EEPROM, a codec, or anything a test writes.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_SIM_I2C_DEVICE_H_
#define A2BCPP_SIM_I2C_DEVICE_H_

#include <cstddef>
#include <cstdint>

#include "a2b/types.h"

namespace a2b {
namespace sim {

/**
 * @brief A device on the host I2C bus or behind a sub-node.
 *
 * The simulator calls Write() for the write part of a transfer and Read() for the read
 * part; a write-read calls both. Return Result::kI2cError to NAK.
 */
class I2cDevice {
 public:
  virtual ~I2cDevice() = default;

  /** @brief Takes the bytes of a write transfer. */
  virtual Result Write(const uint8_t* data, size_t len, uint32_t nowMs) = 0;

  /** @brief Fills the bytes of a read transfer. */
  virtual Result Read(uint8_t* data, size_t len, uint32_t nowMs) = 0;
};

}  // namespace sim
}  // namespace a2b

#endif  // A2BCPP_SIM_I2C_DEVICE_H_
