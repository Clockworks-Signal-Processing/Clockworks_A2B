/**
 * @file transport.h
 * @brief Register and peripheral access to one bus, with NODEADR, CHIP and MMRPAGE cached.
 *        Internal; the caching rules are in transport.cpp.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_TRANSPORT_H_
#define A2BCPP_TRANSPORT_H_

#include <cstddef>
#include <cstdint>

#include "a2b/platform.h"
#include "a2b/types.h"

namespace a2b {

/// Reaches every node of one A2B bus through the host I2C bus. One thread per Transport.
class Transport {
 public:
  static constexpr size_t kMaxRegBurst = 16;  ///< Registers per write transfer; longer is split.

  Transport(Platform& platform, uint8_t mainAddr, uint8_t busAddr);  ///< Nothing cached yet.

  Transport(const Transport&) = delete;
  Transport& operator=(const Transport&) = delete;

  Result WriteRegs(NodeAddr node, uint16_t reg, const uint8_t* data,
                   size_t len);  ///< Writes consecutive registers of one node.
  /// Writes one register of one node.
  Result WriteReg(NodeAddr node, uint16_t reg, uint8_t value) {
    return WriteRegs(node, reg, &value, 1);
  }
  Result ReadRegs(NodeAddr node, uint16_t reg, uint8_t* data,
                  size_t len);  ///< Reads consecutive registers of one node.
  /// Reads one register of one node.
  Result ReadReg(NodeAddr node, uint16_t reg, uint8_t& value) {
    return ReadRegs(node, reg, &value, 1);
  }
  Result BroadcastWrite(uint16_t reg, const uint8_t* data,
                        size_t len);  ///< Writes registers of every sub-node at once.

  Result PeripheralWrite(NodeAddr node, uint8_t i2cAddr, const uint8_t* data,
                         size_t len);  ///< Writes to a peripheral in one transfer.
  Result PeripheralRead(NodeAddr node, uint8_t i2cAddr, uint8_t* data,
                        size_t len);  ///< Reads from a peripheral in one transfer.
  Result PeripheralWriteRead(NodeAddr node, uint8_t i2cAddr, const uint8_t* writeData,
                             size_t writeLen, uint8_t* readData,
                             size_t readLen);  ///< Write, repeated start, read.

  Result SelectNode(NodeAddr node);  ///< Points NODEADR at a sub-node's registers.
  void ForgetNode(NodeAddr node);    ///< Forgets a sub-node's cached CHIP and page.
  void ForgetAll();                  ///< Forgets everything cached (main-node reset).
  void SetAddresses(uint8_t mainAddr, uint8_t busAddr);  ///< New addresses; forgets all.

  uint8_t MainAddr() const { return m_MainAddr; }  ///< The main node's 7-bit I2C address.
  uint8_t BusAddr() const { return m_BusAddr; }    ///< The bus's 7-bit I2C address.

 private:
  static constexpr uint8_t kUnknown = 0xFF;  ///< Cached value unknown; no value written is this.

  Result SelectNodeadr(uint8_t value);                      ///< Writes NODEADR if needed.
  Result SelectPage(NodeAddr node, uint8_t page);           ///< Writes a node's MMRPAGE if needed.
  Result SelectPeripheral(NodeAddr node, uint8_t i2cAddr);  ///< Points NODEADR at a peripheral.
  Result WriteChunk(uint8_t i2cAddr, uint8_t regLow, const uint8_t* data,
                    size_t len);  ///< One register-write transfer.
  void NoteRegisterWrites(NodeAddr node, uint16_t reg, size_t len);  ///< Updates the caches.
  /// Index of a node in m_MmrPage.
  static size_t PageIndex(NodeAddr node) { return static_cast<size_t>(node + 1); }

  Platform& m_Platform;
  uint8_t m_MainAddr;
  uint8_t m_BusAddr;
  uint8_t m_Nodeadr;                    ///< Last NODEADR value written, or kUnknown.
  uint8_t m_Chip[kMaxSubNodes];         ///< CHIP per sub-node, or kUnknown.
  uint8_t m_MmrPage[kMaxSubNodes + 1];  ///< MMRPAGE per node ([0] = main), or kUnknown.
};

}  // namespace a2b

#endif  // A2BCPP_TRANSPORT_H_
