/**
 * @file spi_device.h
 * @brief A simulated SPI device on a sub-node's SPI port: a flash, or anything a test writes.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_SIM_SPI_DEVICE_H_
#define A2BCPP_SIM_SPI_DEVICE_H_

#include <cstdint>

namespace a2b {
namespace sim {

/**
 * @brief A device on one slave select of a sub-node's SPI port (Node::AttachSpiDevice()).
 *
 * The node, as the data tunnel's responder (an SPI master), asserts the slave select
 * (Select()), clocks bytes (Exchange(): one byte out on MOSI, one back on MISO) and
 * deasserts it (Deselect()). A device whose mode the node's SPICFG doesn't match sees
 * nothing; its MISO reads 0xFF.
 */
class SpiDevice {
 public:
  virtual ~SpiDevice() = default;

  virtual void Select() = 0;                   ///< The slave select goes low.
  virtual uint8_t Exchange(uint8_t mosi) = 0;  ///< One byte each way.
  virtual void Deselect() = 0;                 ///< The slave select goes high.
  virtual uint8_t Mode() const { return 0; }   ///< SPI mode it takes, 0-3 (CPOL bit 1, CPHA bit 0).
};

}  // namespace sim
}  // namespace a2b

#endif  // A2BCPP_SIM_SPI_DEVICE_H_
