/**
 * @file spi_protocol.h
 * @brief The AD243x SPI slave port's command bytes and the node/slave-select byte (AD2437
 *        TRM, "SPI Programming Concepts", Table 5-12, and "Slave Select/Node Byte", Figure
 *        5-43; the AD2431-35 TRM's are the same). Internal; the simulator decodes the same.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_SPI_PROTOCOL_H_
#define A2BCPP_SPI_PROTOCOL_H_

#include <cstdint>

namespace a2b {
namespace spi {

inline constexpr uint8_t kLocalWrite = 0x00;      ///< Local register write: address, data.
inline constexpr uint8_t kLocalRead = 0x01;       ///< Local register read: address, N - 1.
inline constexpr uint8_t kStatusRead = 0x04;      ///< SPISTAT, one byte, never an interrupt.
inline constexpr uint8_t kBusFifoRead = 0x05;     ///< The bus data FIFO (remote reads).
inline constexpr uint8_t kBulk = 0x06;            ///< Bulk write: node/SS, LEN - 1, data.
inline constexpr uint8_t kFullDuplex = 0x09;      ///< Full duplex: node/SS, LEN - 1, data.
inline constexpr uint8_t kAbort = 0x0A;           ///< Ends the transaction, flushes the FIFOs.
inline constexpr uint8_t kTunnelFifoRead = 0x0B;  ///< The tunnel's read data FIFO.
inline constexpr uint8_t kAtomicWrite = 0x0C;     ///< Atomic write: node/SS, data.
inline constexpr uint8_t kAtomicRead = 0x0D;      ///< Atomic read: node/SS, LEN - 1, data out.
inline constexpr uint8_t kExtFullDuplex = 0x0E;   ///< Extended full duplex: as kFullDuplex.
inline constexpr uint8_t kExtBulk = 0x0F;         ///< Extended bulk: as kBulk.

/** Bytes before the data of a tunnel command with a length: command, node/SS, LEN - 1. */
inline constexpr unsigned kHeaderLen = 3;

/**
 * The node/slave-select byte of a tunnel command: SSEL (bits 7:6), M/S (bit 5, the main
 * node; never used here: the tunnel's far end is a sub-node), NODEID (bits 3:0).
 */
inline constexpr uint8_t NodeSelect(uint8_t select, uint8_t node) {
  return static_cast<uint8_t>(((select & 0x03) << 6) | (node & 0x0F));
}

}  // namespace spi
}  // namespace a2b

#endif  // A2BCPP_SPI_PROTOCOL_H_
