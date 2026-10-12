/**
 * @file register_image.h
 * @brief A node's register settings: register addresses and values, in write order.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_A2B_REGISTER_IMAGE_H_
#define A2BCPP_A2B_REGISTER_IMAGE_H_

#include <cstddef>
#include <cstdint>

#include "a2b/types.h"

namespace a2b {

/// Register values for one node, kept in the order they were first set. Fixed storage.
class RegisterImage {
 public:
  /** @brief Most registers an image holds; a full AD243x main-node image has about 125. */
  static constexpr size_t kCapacity = 160;

  RegisterImage() = default;  ///< Empty image.

  Result Set(uint16_t reg, uint8_t value);       ///< Sets a register's value.
  bool Get(uint16_t reg, uint8_t& value) const;  ///< Reads a register's value.
  bool Has(uint16_t reg) const;                  ///< True if the image sets the register.
  void Clear();                                  ///< Empties the image.

  size_t Size() const { return m_Size; }                  ///< Registers set.
  uint16_t RegAt(size_t i) const { return m_Reg[i]; }     ///< Address of the i-th register set.
  uint8_t ValueAt(size_t i) const { return m_Value[i]; }  ///< Value of the i-th register set.

 private:
  int Find(uint16_t reg) const;  ///< Index of a register, or -1.

  uint16_t m_Reg[kCapacity] = {};  ///< Register addresses (page 1 is 0x100-0x1FF).
  uint8_t m_Value[kCapacity] = {};
  size_t m_Size = 0;
};

}  // namespace a2b

#endif  // A2BCPP_A2B_REGISTER_IMAGE_H_
