/**
 * @file op_list.h
 * @brief Scripts of bus operations (register writes, peripheral transfers, waits) that the
 *        stack runs across polls.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_A2B_OP_LIST_H_
#define A2BCPP_A2B_OP_LIST_H_

#include <cstddef>
#include <cstdint>

#include "a2b/types.h"

namespace a2b {

/** @brief What an Op does. */
enum class OpType : uint8_t {
  kRegWrite,         ///< Writes one register of a node.
  kWait,             ///< Waits before the next operation.
  kPeripheralWrite,  ///< Writes a sub-address, then data, to a peripheral.
  kPeripheralRead,   ///< Writes a sub-address, then reads from a peripheral (data discarded).
};

/// One operation of a script. Make one with the factory functions.
struct Op {
  OpType type = OpType::kWait;  ///< What the operation does.
  NodeAddr node = kMainNode;    ///< Node whose register or peripheral is reached.
  uint8_t i2cAddr = 0;          ///< Peripheral ops: 7-bit I2C address of the peripheral.
  uint8_t addrWidth = 0;        ///< Peripheral ops: sub-address bytes, 0-4, sent MSB first.
  /** Peripheral writes: data bytes per sub-address step, for splitting a long write
      (e.g. 5 for a SigmaDSP program word). 0 or 1: one byte per step. */
  uint8_t addrIncrement = 1;
  uint8_t value = 0;              ///< kRegWrite: the value.
  uint16_t reg = 0;               ///< kRegWrite: the register (page 1 is 0x100-0x1FF).
  uint32_t addr = 0;              ///< Peripheral ops: the sub-address. kWait: milliseconds.
  uint16_t len = 0;               ///< Peripheral ops: data bytes to write or read.
  const uint8_t* data = nullptr;  ///< kPeripheralWrite: the data; the script's owner keeps it.

  static Op RegWrite(NodeAddr node, uint16_t reg, uint8_t value);  ///< Register write.
  static Op Wait(uint32_t ms);                                     ///< Wait.
  static Op PeripheralWrite(NodeAddr node, uint8_t i2cAddr, uint8_t addrWidth, uint32_t addr,
                            const uint8_t* data, uint16_t len,
                            uint8_t addrIncrement = 1);  ///< Peripheral write.
  static Op PeripheralRead(NodeAddr node, uint8_t i2cAddr, uint8_t addrWidth, uint32_t addr,
                           uint16_t len);  ///< Peripheral read.
};

/// A script: operations in order, in storage the owner provides.
class OpList {
 public:
  OpList(Op* storage, size_t capacity);  ///< Empty list in @p storage.

  OpList(const OpList&) = delete;
  OpList& operator=(const OpList&) = delete;

  Result Add(const Op& op);     ///< Appends an operation.
  void Clear() { m_Size = 0; }  ///< Empties the list.

  size_t Size() const { return m_Size; }                     ///< Operations in the list.
  size_t Capacity() const { return m_Capacity; }             ///< Most operations the storage holds.
  const Op& operator[](size_t i) const { return m_Ops[i]; }  ///< The i-th operation.

 private:
  Op* const m_Ops;
  const size_t m_Capacity;
  size_t m_Size = 0;
};

/// An OpList with its own storage for @p N operations.
template <size_t N>
class FixedOpList : public OpList {
 public:
  FixedOpList() : OpList(m_Storage, N) {}  ///< Empty list.

 private:
  Op m_Storage[N];
};

}  // namespace a2b

#endif  // A2BCPP_A2B_OP_LIST_H_
