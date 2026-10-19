/**
 * @file module_info.h
 * @brief Builds module-info images for the simulated EEPROMs.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_SIM_MODULE_INFO_H_
#define A2BCPP_SIM_MODULE_INFO_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "a2b/module_info.h"

namespace a2b {
namespace sim {

inline constexpr uint8_t kModuleInfoMarker = 0xAA;      ///< Byte 0x00 of a valid image.
inline constexpr size_t kModuleInfoHeaderLen = 0x1F;    ///< Bytes the header CRC covers.
inline constexpr size_t kModuleInfoHeaderCrc = 0x1F;    ///< Offset of the header CRC-8.
inline constexpr size_t kModuleInfoPowerConfig = 0x23;  ///< Power configuration (high nibble).
inline constexpr uint8_t kPowerConfigHighPower = 0x04;  ///< Power configuration of a 24 V node.

/** @brief The header fields a test sets. */
struct ModuleInfoHeader {
  uint16_t vendorId = 0x00AD;     ///< Module vendor ID, bytes 0x01-0x02, low byte first.
  uint16_t productId = 0x0037;    ///< Module product ID, bytes 0x03-0x04, low byte first.
  uint8_t siliconVersion = 0x10;  ///< Byte 0x05.
  uint8_t powerConfig = 0;        ///< High nibble of byte 0x23, e.g. #kPowerConfigHighPower.
  bool localPower = false;        ///< Low nibble of byte 0x23 (1 if set).
  uint16_t maxPowerMw = 0;        ///< Bytes 0x24-0x25, low byte first.
  uint16_t minPowerMw = 0;        ///< Bytes 0x26-0x27, low byte first.
  uint16_t maxCurrentMa = 0;      ///< Bytes 0x29-0x2A, low byte first.
  uint16_t targetChip = 0;        ///< Bytes 0x07-0x08, low byte first: 0 (not stated).
  /** Byte 0x28, the B port type: 2 (UTP), so the link to the next node is single-step and
      not inverted. */
  uint8_t bPortType = 2;
  uint16_t bCableM = 0;  ///< Bytes 0x6C-0x6D, low byte first: the B-side cable in metres.
};

/** @brief The sub-node register settings of an image (bytes 0x80-0x8F); power-on values
 *         by default, so a default image asks for no writes. */
struct ModuleInfoRegs {
  uint8_t spicfg = 0;     ///< Byte 0x80.
  uint8_t spickdiv = 0;   ///< Byte 0x81.
  uint8_t spipincfg = 0;  ///< Byte 0x82.
  uint8_t i2scfg = 0;     ///< Byte 0x83.
  uint8_t i2sgcfg = 0;    ///< Byte 0x84.
  uint8_t i2srate = 0;    ///< Byte 0x85.
  uint8_t pdmctl = 0;     ///< Byte 0x86.
  uint8_t pdmctl2 = 0;    ///< Byte 0x87.
  uint8_t clk1cfg = 0;    ///< Byte 0x88.
  uint8_t clk2cfg = 0;    ///< Byte 0x89.
  uint8_t gpiooen = 0;    ///< Byte 0x8A.
  uint8_t gpioien = 0;    ///< Byte 0x8B.
  uint8_t pinten = 0;     ///< Byte 0x8C.
  uint8_t pintinv = 0;    ///< Byte 0x8D.
  uint8_t pincfg = 0x01;  ///< Byte 0x8E.
  uint8_t gpiodat = 0;    ///< Byte 0x8F.
};

/** @brief One configuration unit of a peripheral block (layout: src/module_script.cpp). */
struct ConfigUnit {
  /** @brief The unit's type. */
  enum class Type : uint8_t {
    kA = 0,  ///< A write.
    kB = 1,  ///< A write with a CRC-8.
    kC = 2,  ///< A wait.
  };

  Type type = Type::kC;       ///< What it does.
  uint8_t i2cAddr = 0;        ///< kA, kB: the peripheral's 7-bit address.
  std::vector<uint8_t> data;  ///< kA, kB: the bytes written (sub-address first).
  uint16_t delayMs = 0;       ///< kC: the wait.

  static ConfigUnit Write(uint8_t addr, const std::vector<uint8_t>& bytes,
                          bool withCrc = true);  ///< A type B (or A) unit.
  static ConfigUnit Wait(uint16_t ms);           ///< A type C unit.
};

/** @brief A peripheral block and its units. */
struct PeripheralBlock {
  uint8_t interface = 0;          ///< 0 I2C, 1 SPI.
  std::string name;               ///< Up to 63 characters.
  uint8_t i2cAddr = 0;            ///< The block's address field (not used by the stack).
  std::vector<ConfigUnit> units;  ///< Its configuration units.
};

std::vector<uint8_t> MakeModuleInfoImage(const ModuleInfoHeader& header,
                                         const ModuleInfoRegs& regs = ModuleInfoRegs(),
                                         size_t size = 0x90);  ///< A valid image.

void SealModuleInfoHeader(std::vector<uint8_t>& image);  ///< Recomputes the header CRC.

void AddPeripheralBlocks(std::vector<uint8_t>& image,
                         const std::vector<PeripheralBlock>& blocks);  ///< Peripheral scripts.

void AddChannelGroups(std::vector<uint8_t>& image,
                      const ChannelGroups& groups);  ///< The channel-group table at 0x100.

}  // namespace sim
}  // namespace a2b

#endif  // A2BCPP_SIM_MODULE_INFO_H_
