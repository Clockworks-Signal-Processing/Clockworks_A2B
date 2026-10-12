/**
 * @file crc8.cpp
 * @brief ADI's CRC-8.
 *
 * Follows ADI's a2b_crc8/a2b_crc8Cont (a2bstack/src/util.c): polynomial 0x07
 * (x^8 + x^2 + x + 1), most significant bit first, no final XOR, starting value 0xFF.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "crc8.h"

namespace a2b {

/**
 * Computes ADI's CRC-8 over a buffer.
 * @param data Bytes to check.
 * @param len Number of bytes.
 * @param crc Starting value: #kCrc8Init, or the result of an earlier call to continue it.
 * @return The CRC.
 */
uint8_t Crc8(const uint8_t* data, size_t len, uint8_t crc) {
  constexpr uint8_t kPolynomial = 0x07;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = ((crc & 0x80) != 0) ? static_cast<uint8_t>((crc << 1) ^ kPolynomial)
                                : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

}  // namespace a2b
