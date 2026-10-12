/**
 * @file eeprom.h
 * @brief A simulated 24xx-series I2C EEPROM with 16-bit addresses, as on the A2B modules.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_SIM_EEPROM_H_
#define A2BCPP_SIM_EEPROM_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "sim/i2c_device.h"

namespace a2b {
namespace sim {

/// A 24xx-series EEPROM: page writes, write-cycle busy time, write protect (see eeprom.cpp).
class Eeprom : public I2cDevice {
 public:
  static constexpr size_t kDefaultSize = 16384;        ///< 24C128.
  static constexpr size_t kDefaultPageSize = 64;       ///< A2B_PNP_EEPROM_PAGE_SIZE.
  static constexpr uint32_t kDefaultWriteCycleMs = 5;  ///< tWR of common parts.

  explicit Eeprom(size_t size = kDefaultSize, size_t pageSize = kDefaultPageSize,
                  uint32_t writeCycleMs = kDefaultWriteCycleMs);  ///< Erased (0xFF) memory.

  void Load(const std::vector<uint8_t>& image, size_t offset = 0);  ///< Programs bytes directly.
  const std::vector<uint8_t>& Contents() const { return m_Data; }   ///< The whole memory.
  void SetWriteProtect(bool on) { m_WriteProtect = on; }            ///< Write protect on/off.
  int WriteCount() const { return m_WriteCount; }  ///< Data writes accepted so far.

  Result Write(const uint8_t* data, size_t len, uint32_t nowMs) override;  ///< Address, data.
  Result Read(uint8_t* data, size_t len, uint32_t nowMs) override;         ///< Sequential read.

 private:
  bool Busy(uint32_t nowMs) const;  ///< In a write cycle.

  std::vector<uint8_t> m_Data;
  size_t m_PageSize;
  uint32_t m_WriteCycleMs;
  size_t m_Pointer = 0;
  bool m_Busy = false;
  uint32_t m_BusyUntil = 0;
  bool m_WriteProtect = false;
  int m_WriteCount = 0;
};

}  // namespace sim
}  // namespace a2b

#endif  // A2BCPP_SIM_EEPROM_H_
