/**
 * @file spi_flash.h
 * @brief A simulated SPI NOR flash with the W25Q128JV's commands and JEDEC ID: the bench's
 *        SPI test part (docs/SPI.md).
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_SIM_SPI_FLASH_H_
#define A2BCPP_SIM_SPI_FLASH_H_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "sim/spi_device.h"

namespace a2b {
namespace sim {

class Simulator;

/**
 * @brief A W25Q128JV-like flash: 16 MB erased to 0xFF, 256-byte pages, 4 KB sectors.
 *
 * Commands (Winbond W25Q128JV data sheet): 0x9F JEDEC ID (EF 40 18), 0x90 manufacturer and
 * device ID (EF 17), 0x05 status register 1 (BUSY, WEL), 0x06 write enable, 0x04 write
 * disable, 0x03 read, 0x0B fast read (one dummy byte), 0x02 page program (wraps in its
 * page; bits only go from 1 to 0), 0x20 4 KB sector erase, 0xD8 64 KB block erase,
 * 0xC7/0x60 chip erase. A program or erase needs WEL, starts when the slave select goes
 * high, and keeps BUSY for its time on the simulator's clock; meanwhile only 0x05 is taken.
 */
class SpiFlash : public SpiDevice {
 public:
  static constexpr size_t kSize = 16u * 1024u * 1024u;  ///< 128 Mbit.
  static constexpr uint32_t kProgramMs = 1;             ///< A page program's busy time.
  static constexpr uint32_t kSectorEraseMs = 45;        ///< A sector erase's (typical).

  explicit SpiFlash(Simulator& sim);  ///< Erased.

  void Select() override;
  uint8_t Exchange(uint8_t mosi) override;
  void Deselect() override;

  const std::vector<uint8_t>& Memory() const { return m_Memory; }  ///< Its contents.
  std::vector<uint8_t>& Memory() { return m_Memory; }              ///< Its contents, to preset.
  size_t Programs() const { return m_Programs; }                   ///< Page programs done.
  size_t Erases() const { return m_Erases; }                       ///< Erases done.
  bool Busy() const;                                               ///< A program or erase runs.

 private:
  Simulator& m_Sim;
  std::vector<uint8_t> m_Memory;
  bool m_Selected = false;
  uint8_t m_Command = 0;
  size_t m_Index = 0;  ///< Bytes since the slave select went low.
  uint32_t m_Address = 0;
  bool m_Wel = false;          ///< Write enable latch.
  uint32_t m_BusyUntilMs = 0;  ///< BUSY until then.
  bool m_BusyEver = false;
  std::vector<uint8_t> m_Page;  ///< A page program's bytes, applied at Deselect().
  size_t m_Programs = 0;
  size_t m_Erases = 0;
};

}  // namespace sim
}  // namespace a2b

#endif  // A2BCPP_SIM_SPI_FLASH_H_
