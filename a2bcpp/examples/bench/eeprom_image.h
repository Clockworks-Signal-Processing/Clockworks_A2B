/**
 * @file eeprom_image.h
 * @brief Module-info images as an app handles them around an EEPROM: how long an image
 *        is (from the bytes read so far), whether it is sound before it is written, what
 *        its Supplier Information block holds, and a description in words for the bench
 *        console's `eeprom info`. Application code: the stack itself only reads images.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_EXAMPLES_BENCH_EEPROM_IMAGE_H_
#define A2BCPP_EXAMPLES_BENCH_EEPROM_IMAGE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// The Supplier Information block (0x400 in an image): the vendor ID says how the
/// vendor's own data is laid out (Clockworks': clockworks::ParseClockworksSection()).
inline constexpr uint32_t kSupplierInfoAddr = 0x400;
inline constexpr size_t kSupplierVendorIdLen = 8;          ///< ASCII, at 0x403.
inline constexpr uint32_t kSupplierDataAddr = 0x42B;       ///< The vendor's own data.
inline constexpr size_t kSupplierDataLen = 0x4FF - 0x42B;  ///< 212 bytes, to the CRC at 0x4FF.

/// Where an image's walk got to.
enum class ImageWalk : uint8_t {
  kNeedMore,  ///< More bytes are needed: at least the count given.
  kComplete,  ///< The image's length is known.
  kInvalid,   ///< Not an image: why is given.
};

/// What a Supplier Information block holds.
struct SupplierInfo {
  std::string vendorId;       ///< Its 8-character vendor ID ("CLKWORKS", ADI's "000000AD").
  std::vector<uint8_t> data;  ///< The vendor's data: 212 bytes from 0x42B.
  bool crcOk = false;         ///< Its CRC-8 (at 0x4FF, over 0x400-0x4FE) matches.
};

ImageWalk WalkImage(const uint8_t* data, size_t len, bool final, bool checkCrcs, size_t& end,
                    std::string& why);  ///< The image's length, from the bytes read so far.
bool CheckImage(const std::vector<uint8_t>& image, size_t eepromSize,
                std::string& why);  ///< Sound, as the stack will read it, and fits.
bool ParseSupplierInfo(const std::vector<uint8_t>& image,
                       SupplierInfo& info);  ///< The Supplier Information block, if there.
std::string DescribeImage(const std::vector<uint8_t>& image);  ///< The image in words.
std::string TargetChipName(uint16_t targetChip);  ///< "AD2437", "External" or "not stated".
std::string BPortTypeName(uint8_t bPortType);     ///< "UTP", "RJ45-CUSTOM", "reserved (6)" ...

#endif  // A2BCPP_EXAMPLES_BENCH_EEPROM_IMAGE_H_
