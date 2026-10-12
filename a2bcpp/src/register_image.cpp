/**
 * @file register_image.cpp
 * @brief RegisterImage: a node's register settings, in write order.
 *
 * The image keeps each register once, at the place it was first set, so the order a
 * configuration source gives (e.g. ADI's stack's write order, which the
 * SigmaStudio+ import follows) survives later changes to a value.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "a2b/register_image.h"

namespace a2b {

/**
 * Sets a register's value. A register already in the image keeps its place.
 * @param reg Register address; page 1 registers are 0x100-0x1FF.
 * @param value The value.
 * @return Result::kOk, or Result::kNoSpace if the image is full.
 */
Result RegisterImage::Set(uint16_t reg, uint8_t value) {
  const int index = Find(reg);
  if (index >= 0) {
    m_Value[index] = value;
    return Result::kOk;
  }
  if (m_Size == kCapacity) {
    return Result::kNoSpace;
  }
  m_Reg[m_Size] = reg;
  m_Value[m_Size] = value;
  ++m_Size;
  return Result::kOk;
}

/**
 * Reads a register's value.
 * @param reg Register address.
 * @param value Set to the value if the image has the register; unchanged otherwise.
 * @return True if the image sets the register.
 */
bool RegisterImage::Get(uint16_t reg, uint8_t& value) const {
  const int index = Find(reg);
  if (index < 0) {
    return false;
  }
  value = m_Value[index];
  return true;
}

/**
 * Tells whether the image sets a register.
 * @param reg Register address.
 * @return True if it does.
 */
bool RegisterImage::Has(uint16_t reg) const { return Find(reg) >= 0; }

/** Empties the image. */
void RegisterImage::Clear() { m_Size = 0; }

/** Index of @p reg in the image, or -1. A linear search: images are small and set once. */
int RegisterImage::Find(uint16_t reg) const {
  for (size_t i = 0; i < m_Size; ++i) {
    if (m_Reg[i] == reg) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

}  // namespace a2b
