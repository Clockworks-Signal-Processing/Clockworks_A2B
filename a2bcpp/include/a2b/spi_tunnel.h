/**
 * @file spi_tunnel.h
 * @brief SPI over A2B: the data tunnel's settings, its transactions' limits and status.
 *        The calls are Network's; docs/SPI.md has the whole picture.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_A2B_SPI_TUNNEL_H_
#define A2BCPP_A2B_SPI_TUNNEL_H_

#include <cstddef>
#include <cstdint>

#include "a2b/types.h"

namespace a2b {

/** @brief Most data bytes one tunnel transaction carries each way (the TRMs' 256). */
inline constexpr size_t kSpiMaxData = 256;

/** @brief Most registers one SPI local register read takes (the TRMs' burst of 32). */
inline constexpr size_t kSpiMaxRegisters = 32;

/** @brief Fewest and most data tunnel slots each way (AD2437 TRM, "SPI Data Tunnels"). */
inline constexpr uint8_t kSpiMinTunnelSlots = 2;
inline constexpr uint8_t kSpiMaxTunnelSlots = 12;  ///< See #kSpiMinTunnelSlots.

/**
 * @brief Whether a full-duplex or bulk transaction ends the remote transfer or leaves it
 *        open: kMore is the extended form (the remote slave select stays asserted, so
 *        several transactions make one transfer of any length); the next kLast ends it.
 */
enum class SpiChunk : uint8_t {
  kLast,  ///< The standard transaction: the remote transfer ends with it.
  kMore,  ///< Extended: the remote transfer goes on with the next transaction.
};

/**
 * @brief The SPI data tunnel between the main node (its owner) and one sub-node (its
 *        responder, an SPI master to that node's peripherals). Network::OpenSpiTunnel().
 */
struct SpiTunnelConfig {
  /** The sub-node at the far end: an AD2433 or AD2437 in the bus. The tunnel follows the
      module by its chip ID, as a connection does. */
  NodeAddr node = 0;
  /** The node's slave select outputs used: bit n for SPISSELn (0: the ADR1 pin, 1: SIO2,
      2: ADR2). Each transaction names one of them. */
  uint8_t selects = 0x01;
  /** Data slots the tunnel takes on every link between the main node and the node, each way
      (#kSpiMinTunnelSlots to #kSpiMaxTunnelSlots; 16- and 20-bit slots need 3 at least).
      More slots, more bytes a superframe (docs/SPI.md has the table). */
  uint8_t slots = 4;
  /** SPICKDIV on the node: its SCK is SYSCLK / (clockDiv + 1), SYSCLK 1024 x SYNC
      (49.152 MHz at 48 kHz: 4 gives 9.8 MHz, the TRM's example). */
  uint8_t clockDiv = 4;
  /** The node's SPI mode to its peripherals, 0-3: CPOL is bit 1, CPHA bit 0. */
  uint8_t mode = 0;
  /** SPICFG.ENFDCS on the node: a full-duplex transaction starts on the node before all of
      its data has arrived (lower latency; an error may leave part of it done). */
  bool fdClockStretch = false;
};

/** @brief What the main node said about the last SPI transaction. Network::LastSpiStatus(). */
struct SpiStatus {
  uint8_t spiStat = 0;      ///< SPISTAT: its error bits (DTINVALID, DTABORT, DTBADPKT) as read.
  uint8_t spiInt = 0;       ///< SPIINT: the bits the transaction raised (cleared since).
  bool timedOut = false;    ///< SPISTAT.SPIBUSY outlasted BoardTimings::spiTimeoutMs.
  bool hostFailed = false;  ///< The host's SPI transfer failed (Platform::SpiTransfer()).
};

}  // namespace a2b

#endif  // A2BCPP_A2B_SPI_TUNNEL_H_
