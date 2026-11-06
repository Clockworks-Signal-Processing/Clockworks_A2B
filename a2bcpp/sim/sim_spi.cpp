/**
 * @file sim_spi.cpp
 * @brief The simulator's SPI: the host's SPI to each main node (Simulator::SpiTransfer()),
 *        the main node's SPI slave port, and the data tunnel to a sub-node's SPI devices.
 *
 * What it models, from the AD2437 TRM ("SPI Interface", "SPI Over Distance", "SPI Interrupts
 * and Errors"), written apart from the stack's network_spi.cpp from the same tables:
 * - The commands of Table 5-12 the stack uses, each framed by one slave select: local
 *   register read and write (0x01, 0x00), SPI status (0x04, SPISTAT, never refused), SPI
 *   abort (0x0A), the tunnel FIFO read (0x0B: past the data the last read fetched, zeros and
 *   SPIINT.FIFOUNF), atomic write and read (0x0C, 0x0D: the remote transfer runs once all
 *   of the command is in), full duplex and extended full duplex (0x09, 0x0E: the read data
 *   comes back on the next full duplex, zeros on the first; a FIFO read gives the last one's),
 *   bulk and extended bulk (0x06, 0x0F). An extended transaction leaves the remote slave
 *   select asserted until the plain one of its kind; another tunnel command meanwhile aborts
 *   it (SPISTAT.DTABORT). The bus FIFO and the SPI-to-I2C and remote register commands,
 *   which the stack doesn't use, are taken as bad commands.
 * - SPISTAT.SPIBUSY from the command until the tunnel has carried it: the superframes its
 *   bytes take each way at the tunnel's size (DTSLOTS and SLOTFMT: Tables 5-14 and 5-15,
 *   3 or 5 bytes of overhead a superframe in a standard tunnel, 4 or 6 in a jumbo one), two
 *   superframes' latency, and the remote SCK (SYSCLK / (SPICKDIV + 1)), at 48 kHz. A command
 *   while busy, a length that doesn't match the command's, or an unknown command sets
 *   SPIINT.BADCMD and does nothing.
 * - The tunnel: the slots its registers set up must reach the far end and come back (the
 *   AudioTrace's tunnel fields, simulator.cpp); otherwise SPISTAT.DTINVALID and
 *   SPIINT.SPIDTERR, and nothing reaches the device. The far end must be the node the
 *   command names, an SPI master (SPICFG.SPIMODE 1) with that slave select's output on
 *   (SPIPINCFG), or DTABORT. A device on a slave select (Node::AttachSpiDevice()) whose mode
 *   isn't the node's SPICFG CPOL/CPHA sees nothing, and reads 0xFF. While a discovery runs
 *   behind the far end (it is the last node), every tunnel command ends DTABORT, as on the
 *   bench (SpiDiscoveryAborts()).
 * - SPIINT and SPISTAT's error bits are cleared by writing 1s (simulator.cpp, Node::Write()).
 *   Their interrupt (SPIMSK) is not raised.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "sim/simulator.h"
#include "spi_protocol.h"

namespace a2b {
namespace sim {
namespace {

constexpr double kSuperframeUs = 1e6 / 48000.0;  ///< One superframe at 48 kHz.
constexpr double kSysclkMhz = 49.152;            ///< SYSCLK at 48 kHz: 1024 x SYNC.

/** Superframes a tunnel of @p tunnelBytes takes to carry @p bytes (one at least). */
size_t Superframes(size_t bytes, size_t tunnelBytes) {
  const bool jumbo = tunnelBytes > 16;
  const size_t first = (tunnelBytes > (jumbo ? 6u : 5u)) ? tunnelBytes - (jumbo ? 6u : 5u) : 1u;
  const size_t next = (tunnelBytes > (jumbo ? 4u : 3u)) ? tunnelBytes - (jumbo ? 4u : 3u) : 1u;
  if (bytes <= first) {
    return 1;
  }
  return 1 + (bytes - first + next - 1) / next;
}

}  // namespace

/** Puts @p device on slave select @p select (0-2) of this node's SPI port. */
void Node::AttachSpiDevice(uint8_t select, SpiDevice& device) {
  if (select < 3) {
    m_SpiDevices[select] = &device;
  }
}

/** The device on slave select @p select, or nullptr. */
SpiDevice* Node::SpiDeviceAt(uint8_t select) const {
  return (select < 3) ? m_SpiDevices[select] : nullptr;
}

/** True while SPISTAT.SPIBUSY is set. */
bool Bus::SpiBusy() const { return m_Sim.NowUs() < m_SpiBusyUntilUs; }

/** Raises SPIINT bits @p spiInt and SPISTAT bits @p spiStat on the main node. */
void Bus::SpiError(uint8_t spiInt, uint8_t spiStat) {
  m_Main->m_Regs[0][reg::kSpiint] |= spiInt;
  m_Main->m_Regs[0][reg::kSpistat] |= spiStat;
}

/** Ends a remote transfer an extended transaction left open: its slave select goes high. */
void Bus::SpiCloseRemote() {
  if (m_SpiOpenDevice != nullptr) {
    m_SpiOpenDevice->Deselect();
  }
  m_SpiOpen = 0;
  m_SpiOpenDevice = nullptr;
}

/**
 * How long a tunnel transaction keeps SPIBUSY set, in microseconds: the superframes its
 * bytes take on the tunnel and the remote SPI's time, plus two superframes' latency. An
 * atomic transaction runs them one after the other (all of it down, then the remote
 * transfer, then its read data up); a full duplex or bulk one overlaps them.
 * @param down Bytes the tunnel carries down (the command's, its data).
 * @param up Bytes it carries up (read data).
 * @param remote Bytes on the remote SPI port.
 * @param overlapped A full duplex or bulk transaction.
 */
uint32_t Bus::SpiTunnelUs(size_t down, size_t up, size_t remote, bool overlapped) const {
  const uint8_t slotFormat = m_Main->AppliedReg(reg::kSlotfmt);
  const auto bitsOf = [](unsigned code) { return (code > 6) ? 32u : 8u + 4u * code; };
  const uint8_t dtslots = m_Main->Reg(reg::kDtslots);
  const size_t dnBytes = (dtslots & 0x0F) * bitsOf(slotFormat & 0x07) / 8;
  const size_t upBytes = (dtslots >> 4) * bitsOf((slotFormat >> 4) & 0x07) / 8;
  const double downUs = static_cast<double>(Superframes(down, dnBytes)) * kSuperframeUs;
  const double upUs = static_cast<double>(Superframes(up, upBytes)) * kSuperframeUs;
  double remoteUs = 0;
  const int node = TraceAudio().tunnelNode;
  if (node >= 0) {
    const double sckMhz = kSysclkMhz / (m_Subs[static_cast<size_t>(node)]->Reg(reg::kSpickdiv) + 1);
    remoteUs = static_cast<double>(remote) * 8.0 / sckMhz;
  }
  double us = overlapped ? std::max(std::max(downUs, upUs), remoteUs) : downUs + remoteUs + upUs;
  us += 2 * kSuperframeUs;
  return static_cast<uint32_t>(us + 0.5);
}

/**
 * One transfer on the main node's SPI slave port (the slave select framing it): the command
 * in @p tx[0], the main node's answer on MISO into @p rx.
 */
void Bus::SpiCommand(const uint8_t* tx, uint8_t* rx, size_t len) {
  std::memset(rx, 0, len);
  const uint8_t spiCfg = m_Main->Reg(reg::kSpicfg);
  if (!m_Main->Caps().hasSpi || (spiCfg & reg::kSpicfgSpimode) != reg::kSpimodeSlave || len == 0) {
    std::memset(rx, 0xFF, len);  // Nothing drives MISO.
    return;
  }
  const uint8_t command = tx[0];
  if (command == spi::kStatusRead) {
    if (len > 1) {
      uint8_t stat = m_Main->Reg(reg::kSpistat);
      stat = static_cast<uint8_t>(SpiBusy()
                                      ? (stat | reg::kSpistatSpibusy | reg::kSpistatDtactive)
                                      : (stat & ~(reg::kSpistatSpibusy | reg::kSpistatDtactive)));
      rx[1] = stat;
    }
    return;
  }
  if (command == spi::kAbort) {
    m_SpiBusyUntilUs = 0;
    m_SpiReadFifo.clear();
    SpiCloseRemote();
    return;
  }
  if (SpiBusy()) {
    ++m_SpiBadCommands;
    SpiError(reg::kSpiintBadcmd, 0);
    return;
  }
  switch (command) {
    case spi::kLocalRead: {
      const size_t count = (len >= 3) ? static_cast<size_t>(tx[2]) + 1 : 0;
      if (len < 3 || len != 3 + count || count > 32) {
        ++m_SpiBadCommands;
        SpiError(reg::kSpiintBadcmd, 0);
        return;
      }
      for (size_t i = 0; i < count; ++i) {
        rx[3 + i] = ReadMain(static_cast<uint16_t>((tx[1] + i) & 0xFF));
      }
      return;
    }
    case spi::kLocalWrite:
      if (len < 3 || len > 34) {
        ++m_SpiBadCommands;
        SpiError(reg::kSpiintBadcmd, 0);
        return;
      }
      for (size_t i = 2; i < len; ++i) {
        WriteMain(static_cast<uint16_t>((tx[1] + i - 2) & 0xFF), tx[i]);
      }
      SpiError(reg::kSpiintSpidone, 0);
      return;
    case spi::kTunnelFifoRead:
      for (size_t i = 1; i < len; ++i) {
        if (i - 1 < m_SpiReadFifo.size()) {
          rx[i] = m_SpiReadFifo[i - 1];
        } else {
          SpiError(reg::kSpiintFifounf, 0);
        }
      }
      return;
    case spi::kAtomicWrite:
    case spi::kAtomicRead:
    case spi::kFullDuplex:
    case spi::kExtFullDuplex:
    case spi::kBulk:
    case spi::kExtBulk:
      SpiTunnelCommand(tx, rx, len);
      return;
    default:
      ++m_SpiBadCommands;
      SpiError(reg::kSpiintBadcmd, 0);
      return;
  }
}

/** A tunnel command (atomic, full duplex, bulk): checked, carried, run on the far end. */
void Bus::SpiTunnelCommand(const uint8_t* tx, uint8_t* rx, size_t len) {
  const uint8_t command = tx[0];
  const bool atomicWrite = (command == spi::kAtomicWrite);
  const bool atomicRead = (command == spi::kAtomicRead);
  const bool duplex = (command == spi::kFullDuplex || command == spi::kExtFullDuplex);
  const bool bulk = (command == spi::kBulk || command == spi::kExtBulk);
  // The command's framing: its length byte must match the bytes sent.
  size_t data = 0;
  size_t readLen = 0;
  if (atomicWrite) {
    data = (len >= 3) ? len - 2 : 0;
  } else if (len >= 3) {
    const size_t count = static_cast<size_t>(tx[2]) + 1;
    if (atomicRead) {
      readLen = count;
      data = len - 3;
    } else {
      data = count;
    }
  }
  const bool framed = atomicWrite  ? (len >= 3 && len <= 258)
                      : atomicRead ? (len >= 3 && len <= 258)
                                   : (len >= 4 && len == 3 + data);
  if (!framed) {
    ++m_SpiBadCommands;
    SpiError(reg::kSpiintBadcmd, 0);
    return;
  }
  const uint8_t nodeSelect = tx[1];
  const uint8_t select = nodeSelect >> 6;
  const size_t target = nodeSelect & 0x0F;
  const AudioTrace trace = TraceAudio();
  const uint8_t dtcfg = m_Main->Reg(reg::kDtcfg);
  if ((dtcfg & (reg::kDtcfgDten | reg::kDtcfgDtfrst)) != (reg::kDtcfgDten | reg::kDtcfgDtfrst) ||
      (m_Main->Reg(reg::kSpicfg) & reg::kSpicfgTnlowner) == 0 || trace.tunnelNode < 0 ||
      !trace.tunnelError.empty() || !trace.tunnelDown || !trace.tunnelUp) {
    SpiCloseRemote();
    SpiError(reg::kSpiintSpidterr, reg::kSpistatDtinvalid);
    m_SpiBusyUntilUs = m_Sim.NowUs() + SpiTunnelUs(data, 0, 0, false);
    return;
  }
  const Node& far = *m_Subs[static_cast<size_t>(trace.tunnelNode)];
  const uint8_t farCfg = far.Reg(reg::kSpicfg);
  const uint8_t enable = static_cast<uint8_t>(reg::kSpipincfgSpimss0en << select);
  if ((nodeSelect & 0x20) != 0 || target != static_cast<size_t>(trace.tunnelNode) || select > 2 ||
      (farCfg & reg::kSpicfgSpimode) != reg::kSpimodeMaster ||
      (far.Reg(reg::kSpipincfg) & enable) == 0) {
    SpiCloseRemote();
    SpiError(reg::kSpiintSpidterr, reg::kSpistatDtabort);
    m_SpiBusyUntilUs = m_Sim.NowUs() + SpiTunnelUs(data, 0, 0, false);
    return;
  }
  // A discovery behind the far end (it is the last node) stops the tunnel: on the bench every
  // transaction ended DTABORT while DISCVRY was out (docs/SPI.md).
  if (m_Discovering && static_cast<size_t>(trace.tunnelNode) + 1 == m_Discovered) {
    ++m_SpiDiscoveryAborts;
    SpiCloseRemote();
    SpiError(reg::kSpiintSpidterr, reg::kSpistatDtabort);
    m_SpiBusyUntilUs = m_Sim.NowUs() + SpiTunnelUs(data, 0, 0, false);
    return;
  }
  // An extended transaction open on the far end takes only more of its own kind.
  const int kind = duplex ? 1 : bulk ? 2 : 0;
  if (m_SpiOpen != 0 && m_SpiOpen != kind) {
    SpiCloseRemote();
    SpiError(reg::kSpiintSpidterr, reg::kSpistatDtabort);
    return;
  }
  SpiDevice* device = far.SpiDeviceAt(select);
  const uint8_t farMode = static_cast<uint8_t>(((farCfg >> reg::kSpicfgCpolPos) & 1) << 1 |
                                               ((farCfg >> reg::kSpicfgCphaPos) & 1));
  const bool hears = device != nullptr && device->Mode() == farMode;
  const auto exchange = [&](uint8_t mosi) { return hears ? device->Exchange(mosi) : 0xFF; };
  const uint8_t* out = tx + (atomicWrite ? 2 : 3);
  std::vector<uint8_t> read;
  if (m_SpiOpen == 0 && hears) {
    device->Select();
  }
  for (size_t i = 0; i < data; ++i) {
    const uint8_t miso = exchange(out[i]);
    if (duplex) {
      read.push_back(miso);
    }
  }
  for (size_t i = 0; i < readLen; ++i) {
    read.push_back(exchange(0x00));
  }
  ++m_SpiRemote;
  const bool keepOpen = (command == spi::kExtFullDuplex || command == spi::kExtBulk);
  if (keepOpen) {
    m_SpiOpen = kind;
    m_SpiOpenDevice = hears ? device : nullptr;
  } else {
    if (hears) {
      device->Deselect();
    }
    m_SpiOpen = 0;
    m_SpiOpenDevice = nullptr;
  }
  if (duplex) {
    // The read data comes back on the next full duplex (zeros on the first after another
    // kind of transaction); a FIFO read gives this one's.
    for (size_t i = 0; i < data; ++i) {
      rx[3 + i] = (m_SpiLastDuplex && i < m_SpiDuplexRead.size()) ? m_SpiDuplexRead[i] : 0;
    }
    m_SpiDuplexRead = read;
    m_SpiReadFifo = read;
  } else if (atomicRead) {
    m_SpiReadFifo = read;
  }
  m_SpiLastDuplex = duplex;
  SpiError(reg::kSpiintSpidone, 0);
  m_SpiBusyUntilUs = m_Sim.NowUs() + SpiTunnelUs(len, duplex || atomicRead ? read.size() : 0,
                                                 data + readLen, duplex || bulk);
}

// Simulator

/**
 * One transfer on the host's SPI to bus @p bus's main node; it takes its time at the host's
 * clock, and the simulated clock moves on by it.
 * @return Result::kOk; Result::kNotSupported without SPI (SetHasSpi()) or for a bus that isn't
 *         there; Result::kSpiError for a failure FailSpiTransfers() asked for.
 */
Result Simulator::SpiTransfer(uint8_t bus, const uint8_t* tx, uint8_t* rx, size_t len) {
  if (!m_HasSpi || bus >= m_Buses.size()) {
    return Result::kNotSupported;
  }
  SpiEntry entry;
  entry.timeMs = m_NowMs;
  entry.bus = bus;
  entry.sent.assign(tx, tx + len);
  if (m_LockDepth == 0) {
    ++m_UnlockedTransfers;
  }
  AdvanceUs(static_cast<uint32_t>(static_cast<uint64_t>(len) * 8u * 1000000u / m_SpiHz + 10));
  if (m_FailSpi > 0) {
    --m_FailSpi;
    entry.failed = true;
    m_SpiTrace.push_back(entry);
    return Result::kSpiError;
  }
  entry.failed = false;
  std::vector<uint8_t> in(entry.sent);
  m_Buses[bus]->Update();
  m_Buses[bus]->SpiCommand(entry.sent.data(), in.data(), len);
  std::memcpy(rx, in.data(), len);
  entry.received = in;
  m_SpiTrace.push_back(entry);
  return Result::kOk;
}

/** Moves the clock on by @p us microseconds, a whole millisecond at a time. */
void Simulator::AdvanceUs(uint32_t us) {
  m_UsCarry += us;
  if (m_UsCarry >= 1000) {
    const uint32_t ms = m_UsCarry / 1000;
    m_UsCarry %= 1000;
    Advance(ms);
  }
}

/**
 * The SPI transfers as lines `SPIT,<n>,<ms>,<bus>,<status>,<len>,<txHex>,<rxHex>` (the
 * bench console's SPIT format, with the time in milliseconds).
 */
std::string Simulator::SpiTrace() const {
  std::string text;
  char head[64];
  for (size_t i = 0; i < m_SpiTrace.size(); ++i) {
    const SpiEntry& entry = m_SpiTrace[i];
    std::snprintf(head, sizeof(head), "SPIT,%u,%u,%u,%u,%u,", static_cast<unsigned>(i),
                  static_cast<unsigned>(entry.timeMs), static_cast<unsigned>(entry.bus),
                  entry.failed ? 1u : 0u, static_cast<unsigned>(entry.sent.size()));
    text += head;
    char byte[3];
    for (uint8_t b : entry.sent) {
      std::snprintf(byte, sizeof(byte), "%02X", b);
      text += byte;
    }
    text += ',';
    for (uint8_t b : entry.received) {
      std::snprintf(byte, sizeof(byte), "%02X", b);
      text += byte;
    }
    text += '\n';
  }
  return text;
}

}  // namespace sim
}  // namespace a2b
