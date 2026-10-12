/**
 * @file op_list.cpp
 * @brief Op factory functions and OpList.
 *
 * An OpList is a script: the stack builds one for work that takes several polls (a
 * node's configuration, a board's peripheral set-up), and ScriptRunner runs it without
 * blocking. The list doesn't own its storage or the data its peripheral writes point
 * to, so a script can live in a Network's fixed storage or be built once from a
 * SigmaStudio+ export.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "a2b/op_list.h"

namespace a2b {

/**
 * Makes a register write.
 * @param node #kMainNode or a sub-node position.
 * @param reg Register address; page 1 registers are 0x100-0x1FF.
 * @param value Value to write.
 * @return The operation.
 */
Op Op::RegWrite(NodeAddr node, uint16_t reg, uint8_t value) {
  Op op;
  op.type = OpType::kRegWrite;
  op.node = node;
  op.reg = reg;
  op.value = value;
  return op;
}

/**
 * Makes a wait.
 * @param ms Milliseconds to wait before the next operation.
 * @return The operation.
 */
Op Op::Wait(uint32_t ms) {
  Op op;
  op.type = OpType::kWait;
  op.addr = ms;
  return op;
}

/**
 * Makes a peripheral write: the sub-address, then the data, in one I2C transfer, or
 * several if the data is longer than ScriptRunner's buffer.
 * @param node #kMainNode (a peripheral on the host I2C bus) or a sub-node position.
 * @param i2cAddr 7-bit I2C address of the peripheral.
 * @param addrWidth Sub-address bytes, 0-4, sent MSB first.
 * @param addr Sub-address of the first data byte.
 * @param data Data to write; must stay valid while the script runs.
 * @param len Data bytes, at least 1.
 * @param addrIncrement Data bytes per sub-address step when a long write is split
 *        (0 or 1: one byte per step).
 * @return The operation.
 */
Op Op::PeripheralWrite(NodeAddr node, uint8_t i2cAddr, uint8_t addrWidth, uint32_t addr,
                       const uint8_t* data, uint16_t len, uint8_t addrIncrement) {
  Op op;
  op.type = OpType::kPeripheralWrite;
  op.node = node;
  op.i2cAddr = i2cAddr;
  op.addrWidth = addrWidth;
  op.addr = addr;
  op.data = data;
  op.len = len;
  op.addrIncrement = addrIncrement;
  return op;
}

/**
 * Makes a peripheral read: the sub-address is written, then @p len bytes are read, with a
 * repeated start between. The bytes read are discarded; SigmaStudio+ scripts use reads
 * only for their side effects.
 * @param node #kMainNode or a sub-node position.
 * @param i2cAddr 7-bit I2C address of the peripheral.
 * @param addrWidth Sub-address bytes, 0-4 (0: a plain read).
 * @param addr Sub-address.
 * @param len Bytes to read, at least 1.
 * @return The operation.
 */
Op Op::PeripheralRead(NodeAddr node, uint8_t i2cAddr, uint8_t addrWidth, uint32_t addr,
                      uint16_t len) {
  Op op;
  op.type = OpType::kPeripheralRead;
  op.node = node;
  op.i2cAddr = i2cAddr;
  op.addrWidth = addrWidth;
  op.addr = addr;
  op.len = len;
  return op;
}

/**
 * Makes an empty list.
 * @param storage Room for @p capacity operations; must outlive the list.
 * @param capacity Most operations the list may hold.
 */
OpList::OpList(Op* storage, size_t capacity) : m_Ops(storage), m_Capacity(capacity) {}

/**
 * Appends an operation.
 * @param op The operation.
 * @return Result::kOk, or Result::kNoSpace if the list is full.
 */
Result OpList::Add(const Op& op) {
  if (m_Size == m_Capacity) {
    return Result::kNoSpace;
  }
  m_Ops[m_Size++] = op;
  return Result::kOk;
}

}  // namespace a2b
