/**
 * @file eeprom.cpp
 * @brief A simulated 24xx-series I2C EEPROM with 16-bit addresses, as on the A2B modules.
 *
 * - A write starts with a two-byte address, high byte first. A write of just the address
 *   sets the read pointer; a longer one writes the bytes and wraps within the page, like
 *   the real parts.
 * - After a data write the part is busy for the write-cycle time and NAKs everything,
 *   so a writer must poll for the acknowledge.
 * - Reads are sequential from the pointer and wrap at the end of memory.
 * - With write protect on, data writes are acknowledged but change nothing.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sim/eeprom.h"

namespace a2b {
namespace sim {

/**
 * Makes an erased EEPROM (every byte 0xFF).
 * @param size Bytes of memory.
 * @param pageSize Write page size; a power of two.
 * @param writeCycleMs Busy time after each data write.
 */
Eeprom::Eeprom(size_t size, size_t pageSize, uint32_t writeCycleMs)
    : m_Data(size, 0xFF), m_PageSize(pageSize), m_WriteCycleMs(writeCycleMs) {}

/**
 * Loads bytes directly, as a programmer would, with no write cycle. Bytes past the end of
 * memory are dropped.
 * @param image Bytes to load.
 * @param offset Where the first byte goes.
 */
void Eeprom::Load(const std::vector<uint8_t>& image, size_t offset) {
  for (size_t i = 0; i < image.size() && offset + i < m_Data.size(); ++i) {
    m_Data[offset + i] = image[i];
  }
}

/**
 * Takes a write transfer: a two-byte address, then any data bytes, which wrap within the
 * page. Data starts a write cycle.
 * @return Result::kI2cError (NAK) during a write cycle, else Result::kOk.
 */
Result Eeprom::Write(const uint8_t* data, size_t len, uint32_t nowMs) {
  if (Busy(nowMs)) {
    return Result::kI2cError;
  }
  if (len < 2) {
    return Result::kOk;  // Half an address: acknowledged, nothing changes.
  }
  m_Pointer = ((static_cast<size_t>(data[0]) << 8) | data[1]) % m_Data.size();
  if (len == 2 || m_WriteProtect) {
    return Result::kOk;
  }
  const size_t pageStart = m_Pointer & ~(m_PageSize - 1);
  size_t inPage = m_Pointer - pageStart;
  for (size_t i = 2; i < len; ++i) {
    m_Data[pageStart + inPage] = data[i];
    inPage = (inPage + 1) % m_PageSize;
  }
  m_Pointer = pageStart + inPage;
  m_Busy = true;
  m_BusyUntil = nowMs + m_WriteCycleMs;
  ++m_WriteCount;
  return Result::kOk;
}

/**
 * Reads sequentially from the address pointer, wrapping at the end of memory.
 * @return Result::kI2cError (NAK) during a write cycle, else Result::kOk.
 */
Result Eeprom::Read(uint8_t* data, size_t len, uint32_t nowMs) {
  if (Busy(nowMs)) {
    return Result::kI2cError;
  }
  for (size_t i = 0; i < len; ++i) {
    data[i] = m_Data[m_Pointer];
    m_Pointer = (m_Pointer + 1) % m_Data.size();
  }
  return Result::kOk;
}

/** True while a write cycle is running. */
bool Eeprom::Busy(uint32_t nowMs) const {
  // Busy for the whole write-cycle time: a poll exactly tWR later still gets a NAK.
  return m_Busy && !TimeReached(nowMs, m_BusyUntil + 1);
}

}  // namespace sim
}  // namespace a2b
