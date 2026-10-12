/**
 * @file eeprom_source.cpp
 * @brief EepromModuleInfoSource: finds a sub-node's EEPROM and reads its module-info image.
 *
 * Follows the EEPROM search of ADI's a2b_ReadVerify_ModuleInfo (a2bpnp_calc.c): the
 * addresses 0x50-0x54 (A2B_PNP_EEPROM_ADRESS_START/END) are tried in turn, reading one
 * byte at address 0 (a two-byte address, 0x0000, then a read after a repeated start). The
 * first EEPROM that answers with the marker 0xAA holds the image. An address that NAKs
 * (no EEPROM there) or holds another byte (e.g. a blank 0xFF) is skipped. The start of the
 * image (a2b::kModuleInfoLen bytes) is then read in one transfer; the stack's parser checks
 * the header CRC and the rest (ParseModuleInfo()). So a node whose EEPROM is absent or
 * blank has no module info (RejectReason::kNoModuleInfo), and one whose image is corrupt
 * has bad module info (RejectReason::kBadModuleInfo).
 *
 * ADI's stack read the image in pieces (the marker, the IDs, the software version,
 * the primary function address, the block header, the block); this source reads the
 * marker, then the start in one go. The addresses read and the data are the same; the
 * transfer boundaries differ (a trace difference with no trace_diff.py rule).
 *
 * Reads go through Network::PeripheralWriteRead(): the sub-node's CHIP register is set to
 * the EEPROM's address, then NODEADR.PERI (TRMs, "Remote Peripheral Access"). A sub-node's
 * I2C port runs at 100 kHz unless its I2CCFG says otherwise (I2CCFG.DATARATE, reset 0),
 * so the 144 bytes take about 15 ms.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "a2b/eeprom_source.h"

#include "a2b/network.h"

namespace a2b {
namespace {

constexpr uint8_t kMarker = 0xAA;  ///< Byte 0 of a module-info image.

}  // namespace

/**
 * Makes the source.
 * @param network The bus it reads through; must outlive the source.
 * @param firstAddr First 7-bit I2C address to try.
 * @param lastAddr Last address to try.
 */
EepromModuleInfoSource::EepromModuleInfoSource(Network& network, uint8_t firstAddr,
                                               uint8_t lastAddr)
    : m_Network(network), m_FirstAddr(firstAddr), m_LastAddr(lastAddr) {}

/**
 * Finds the node's EEPROM and reads the start of its image.
 * @param node The node, discovered and identified.
 * @param buf Buffer for the image.
 * @param bufLen Its size; up to #kModuleInfoLen bytes are read.
 * @return Bytes read; 0 if no EEPROM answered with the marker (EepromAddr() is then 0), or
 *         reading the start failed (EepromAddr() gives the EEPROM).
 */
size_t EepromModuleInfoSource::GetModuleInfo(const SubNodeIdentity& node, uint8_t* buf,
                                             size_t bufLen) {
  if (!IsSubNode(node.position) || buf == nullptr || bufLen == 0) {
    return 0;
  }
  m_Found[node.position] = 0;
  const size_t len = (bufLen < kModuleInfoLen) ? bufLen : kModuleInfoLen;
  for (unsigned addr = m_FirstAddr; addr <= m_LastAddr; ++addr) {
    const uint8_t i2cAddr = static_cast<uint8_t>(addr);
    uint8_t marker = 0;
    if (Read(node.position, i2cAddr, 0, &marker, 1) != Result::kOk || marker != kMarker) {
      continue;
    }
    m_Found[node.position] = i2cAddr;
    return (Read(node.position, i2cAddr, 0, buf, len) == Result::kOk) ? len : 0;
  }
  return 0;
}

/**
 * Reads more of the image from the EEPROM GetModuleInfo() found, in one transfer.
 * @param node The node.
 * @param offset EEPROM address (16 bits).
 * @param buf Buffer for the bytes.
 * @param len Bytes to read.
 * @return Result::kOk; Result::kInvalidArgument if no EEPROM was found for the node or
 *         @p offset is beyond 16 bits; Result::kI2cError if the read failed.
 */
Result EepromModuleInfoSource::ReadModuleInfo(const SubNodeIdentity& node, uint32_t offset,
                                              uint8_t* buf, size_t len) {
  if (!IsSubNode(node.position) || m_Found[node.position] == 0 || offset + len > 0x10000) {
    return Result::kInvalidArgument;
  }
  return Read(node.position, m_Found[node.position], offset, buf, len);
}

/**
 * Tells where a node's image was found: the EEPROM that answered with the marker at the
 * node's last identification, even if reading the rest of the start then failed. So an
 * app's source can tell a node with no image (absent or blank EEPROM: 0) from one whose
 * EEPROM it couldn't read.
 * @param position The node.
 * @return The EEPROM's 7-bit I2C address, or 0 if none answered with the marker.
 */
uint8_t EepromModuleInfoSource::EepromAddr(NodeAddr position) const {
  return IsSubNode(position) ? m_Found[position] : 0;
}

/** Writes the two-byte EEPROM address, then reads @p len bytes after a repeated start. */
Result EepromModuleInfoSource::Read(NodeAddr position, uint8_t i2cAddr, uint32_t offset,
                                    uint8_t* buf, size_t len) {
  const uint8_t addr[2] = {static_cast<uint8_t>(offset >> 8), static_cast<uint8_t>(offset)};
  return m_Network.PeripheralWriteRead(position, PeripheralAddress::I2c(i2cAddr), addr,
                                       sizeof(addr), buf, len);
}

}  // namespace a2b
