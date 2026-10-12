/**
 * @file crc8.h
 * @brief ADI's CRC-8, for module-info images and peripheral packages. Internal.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_CRC8_H_
#define A2BCPP_CRC8_H_

#include <cstddef>
#include <cstdint>

namespace a2b {

/** @brief Starting value of ADI's CRC-8. */
inline constexpr uint8_t kCrc8Init = 0xFF;

uint8_t Crc8(const uint8_t* data, size_t len,
             uint8_t crc = kCrc8Init);  ///< ADI's CRC-8 of a buffer.

}  // namespace a2b

#endif  // A2BCPP_CRC8_H_
