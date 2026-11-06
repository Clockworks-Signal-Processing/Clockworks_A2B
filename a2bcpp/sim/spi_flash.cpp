/**
 * @file spi_flash.cpp
 * @brief SpiFlash: a W25Q128JV-like SPI NOR flash for the SPI tests (spi_flash.h).
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sim/spi_flash.h"

#include <algorithm>

#include "a2b/types.h"
#include "sim/simulator.h"

namespace a2b {
namespace sim {
namespace {

constexpr uint8_t kStatusBusy = 0x01;  ///< Status register 1: BUSY.
constexpr uint8_t kStatusWel = 0x02;   ///< Status register 1: WEL.
constexpr size_t kPageSize = 256;

}  // namespace

/** An erased flash on @p sim's clock. */
SpiFlash::SpiFlash(Simulator& sim) : m_Sim(sim), m_Memory(kSize, 0xFF) {}

/** True while a program or erase runs. */
bool SpiFlash::Busy() const { return m_BusyEver && !TimeReached(m_Sim.NowMs(), m_BusyUntilMs); }

/** The slave select goes low: the next byte is a command. */
void SpiFlash::Select() {
  m_Selected = true;
  m_Index = 0;
  m_Command = 0;
  m_Address = 0;
  m_Page.clear();
}

/**
 * One byte each way. While busy, only the status read answers; everything else is
 * ignored (MISO 0xFF).
 */
uint8_t SpiFlash::Exchange(uint8_t mosi) {
  if (!m_Selected) {
    return 0xFF;
  }
  const size_t index = m_Index++;
  if (index == 0) {
    m_Command = mosi;
    if (Busy() && mosi != 0x05) {
      m_Command = 0xFF;  // Ignored until the flash is free.
    }
    if (m_Command == 0x06) {
      m_Wel = true;
    } else if (m_Command == 0x04) {
      m_Wel = false;
    }
    return 0xFF;
  }
  switch (m_Command) {
    case 0x9F: {
      static const uint8_t kId[] = {0xEF, 0x40, 0x18};
      return (index <= 3) ? kId[index - 1] : 0xFF;
    }
    case 0x90: {
      static const uint8_t kId[] = {0xEF, 0x17};
      return (index >= 4 && index <= 5) ? kId[index - 4] : 0xFF;
    }
    case 0x05:
      return static_cast<uint8_t>((Busy() ? kStatusBusy : 0) | (m_Wel ? kStatusWel : 0));
    case 0x03:
    case 0x0B:
    case 0x02:
    case 0x20:
    case 0xD8: {
      if (index <= 3) {
        m_Address = (m_Address << 8) | mosi;
        return 0xFF;
      }
      const size_t first = (m_Command == 0x0B) ? 5 : 4;  // Fast read: one dummy byte.
      if (index < first) {
        return 0xFF;
      }
      if (m_Command == 0x02) {
        m_Page.push_back(mosi);
        return 0xFF;
      }
      if (m_Command == 0x03 || m_Command == 0x0B) {
        const size_t at = (m_Address + (index - first)) % kSize;
        return m_Memory[at];
      }
      return 0xFF;
    }
    default:
      return 0xFF;
  }
}

/**
 * The slave select goes high: a page program or an erase given with WEL set starts now.
 */
void SpiFlash::Deselect() {
  if (!m_Selected) {
    return;
  }
  m_Selected = false;
  const uint32_t address = m_Address % kSize;
  uint32_t busyMs = 0;
  if (m_Command == 0x02 && m_Wel && m_Index >= 4) {
    const size_t page = address / kPageSize * kPageSize;
    const size_t kept = std::min(m_Page.size(), kPageSize);
    const size_t skip = m_Page.size() - kept;  // Only the last 256 bytes are kept.
    for (size_t i = 0; i < kept; ++i) {
      const size_t at = page + (address % kPageSize + i) % kPageSize;
      m_Memory[at] = static_cast<uint8_t>(m_Memory[at] & m_Page[skip + i]);
    }
    ++m_Programs;
    busyMs = kProgramMs;
  } else if ((m_Command == 0x20 || m_Command == 0xD8) && m_Wel && m_Index >= 4) {
    const size_t size = (m_Command == 0x20) ? 4096 : 65536;
    std::fill_n(m_Memory.begin() + static_cast<std::ptrdiff_t>(address / size * size), size, 0xFF);
    ++m_Erases;
    busyMs = (m_Command == 0x20) ? kSectorEraseMs : 150;
  } else if ((m_Command == 0xC7 || m_Command == 0x60) && m_Wel && m_Index == 1) {
    std::fill(m_Memory.begin(), m_Memory.end(), 0xFF);
    ++m_Erases;
    busyMs = 2000;
  }
  if (busyMs != 0) {
    m_Wel = false;
    m_BusyEver = true;
    m_BusyUntilMs = m_Sim.NowMs() + busyMs;
  }
}

}  // namespace sim
}  // namespace a2b
