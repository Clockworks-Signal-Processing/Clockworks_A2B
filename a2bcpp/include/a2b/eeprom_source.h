/**
 * @file eeprom_source.h
 * @brief EepromModuleInfoSource: module info from the EEPROM on each sub-node's I2C port,
 *        the default source.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_A2B_EEPROM_SOURCE_H_
#define A2BCPP_A2B_EEPROM_SOURCE_H_

#include <cstddef>
#include <cstdint>

#include "a2b/module_info.h"
#include "a2b/types.h"

namespace a2b {

class Network;

/// Reads each sub-node's module info from its EEPROM (24xx, 16-bit addresses).
class EepromModuleInfoSource : public ModuleInfoSource {
 public:
  static constexpr uint8_t kFirstAddr = 0x50;  ///< First I2C address scanned.
  static constexpr uint8_t kLastAddr = 0x54;   ///< Last I2C address scanned.

  explicit EepromModuleInfoSource(Network& network, uint8_t firstAddr = kFirstAddr,
                                  uint8_t lastAddr = kLastAddr);  ///< Reads through @p network.

  size_t GetModuleInfo(const SubNodeIdentity& node, uint8_t* buf,
                       size_t bufLen) override;  ///< Scans for the EEPROM, reads the start.
  Result ReadModuleInfo(const SubNodeIdentity& node, uint32_t offset, uint8_t* buf,
                        size_t len) override;  ///< Reads more of the image found.

  uint8_t EepromAddr(NodeAddr position) const;  ///< Where a node's image was found, or 0.

 private:
  Result Read(NodeAddr position, uint8_t i2cAddr, uint32_t offset, uint8_t* buf,
              size_t len);  ///< One EEPROM read.

  Network& m_Network;
  uint8_t m_FirstAddr;
  uint8_t m_LastAddr;
  uint8_t m_Found[kMaxSubNodes] = {};  ///< Per position: the EEPROM's address, or 0.
};

}  // namespace a2b

#endif  // A2BCPP_A2B_EEPROM_SOURCE_H_
