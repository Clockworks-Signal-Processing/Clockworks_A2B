/**
 * @file spi_job.cpp
 * @brief SpiJob: the bench console's long SPI jobs through the data tunnel (spi_job.h).
 *
 * A stream is one remote transfer of any length with the extended full duplex: transactions
 * of up to 256 bytes (SpiChunk::kMore, the last SpiChunk::kLast), each answered with the
 * previous one's read data; the last one's, and a previous one of another length, come from
 * the tunnel FIFO read (AD2437 TRM, "Full Duplex", its notes). A send is the same with the
 * extended bulk write.
 *
 * The flash jobs use the JEDEC SPI NOR commands every serial flash of the W25Q kind takes
 * (the bench's part is a Winbond W25Q128JV): write enable 0x06 and the erase or program
 * command, each by itself (atomic writes), then status register 1 (0x05, an atomic read)
 * until BUSY clears. A sector is 4 KB (erase 0x20); a page 256 bytes (program 0x02, sent
 * by bulk write: a whole page and its command are 260 bytes, so two transactions, the first
 * extended). A verify is a stream read (0x03) compared with the image. Pages all 0xFF aren't
 * programmed: an erased flash holds that already.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "spi_job.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "a2b/spi_tunnel.h"
#include "a2b/strings.h"

namespace {

constexpr size_t kSector = 4096;  ///< A flash sector (erase 0x20).
constexpr size_t kPage = 256;     ///< A flash page (program 0x02).

/** A flash command with a 24-bit address. */
std::vector<uint8_t> Command(uint8_t command, uint32_t address) {
  return {command, static_cast<uint8_t>(address >> 16), static_cast<uint8_t>(address >> 8),
          static_cast<uint8_t>(address)};
}

}  // namespace

/** Starts a job of @p kind. */
void SpiJob::Begin(Kind kind, a2b::NodeAddr node, uint8_t select, uint32_t nowMs) {
  m_Kind = kind;
  m_State = State::kRunning;
  m_Node = node;
  m_Select = select;
  m_Out.clear();
  m_In.clear();
  m_Header = 0;
  m_Done = 0;
  m_Last = 0;
  m_Data.clear();
  m_Unit = 0;
  m_UnitLen = 0;
  m_End = 0;
  m_Step = Step::kEnable;
  m_Units = 0;
  m_Skipped = 0;
  m_Waiting = false;
  m_Fetched = false;
  m_StartMs = nowMs;
  m_EndMs = nowMs;
  m_Error.clear();
}

/**
 * Sends @p header, then reads @p len bytes, in one remote transfer (the extended full
 * duplex): `spi dump`, a flash read (0x03 and its address as the header).
 */
void SpiJob::StartStream(a2b::NodeAddr node, uint8_t select, const std::vector<uint8_t>& header,
                         size_t len, uint32_t nowMs) {
  Begin(Kind::kStream, node, select, nowMs);
  m_Out = header;
  m_Out.resize(header.size() + len, 0x00);
  m_In.assign(m_Out.size(), 0x00);
  m_Header = header.size();
}

/** Sends @p data in one remote transfer (the extended bulk write): `spi send`. */
void SpiJob::StartSend(a2b::NodeAddr node, uint8_t select, const std::vector<uint8_t>& data,
                       uint32_t nowMs) {
  Begin(Kind::kSend, node, select, nowMs);
  m_Out = data;
}

/** Erases the flash's 4 KB sectors that hold @p len bytes from @p address. */
void SpiJob::StartErase(a2b::NodeAddr node, uint8_t select, uint32_t address, size_t len,
                        uint32_t nowMs) {
  Begin(Kind::kErase, node, select, nowMs);
  m_Address = static_cast<uint32_t>(address / kSector * kSector);
  m_End = (address + len - m_Address + kSector - 1) / kSector * kSector;
  m_UnitLen = kSector;
}

/** Programs @p data into the flash from @p address, page by page (erased first by the user). */
void SpiJob::StartProgram(a2b::NodeAddr node, uint8_t select, uint32_t address,
                          const std::vector<uint8_t>& data, uint32_t nowMs) {
  Begin(Kind::kProgram, node, select, nowMs);
  m_Address = address;
  m_Data = data;
  m_End = data.size();
  m_Unit = 0;
  m_UnitLen = std::min(kPage - address % kPage, m_End);
  while (m_UnitLen != 0 &&
         std::all_of(m_Data.begin() + static_cast<std::ptrdiff_t>(m_Unit),
                     m_Data.begin() + static_cast<std::ptrdiff_t>(m_Unit + m_UnitLen),
                     [](uint8_t b) { return b == 0xFF; })) {
    ++m_Skipped;
    if (!NextUnit()) {
      break;
    }
  }
}

/** Reads the flash from @p address and compares it with @p data. */
void SpiJob::StartVerify(a2b::NodeAddr node, uint8_t select, uint32_t address,
                         const std::vector<uint8_t>& data, uint32_t nowMs) {
  StartStream(node, select, Command(0x03, address), data.size(), nowMs);
  m_Kind = Kind::kVerify;
  m_Address = address;
  m_Data = data;
}

/** Stops the job at once; it fails. */
void SpiJob::Abort() {
  if (m_State == State::kRunning) {
    Fail("stopped (spi abort)");
  }
}

/** Stops with @p why. */
SpiJob::State SpiJob::Fail(const std::string& why) {
  m_State = State::kFailed;
  m_Error = why;
  return m_State;
}

/** Ends the job: a stream's data taken out, a verify compared. */
SpiJob::State SpiJob::Finish(uint32_t nowMs) {
  m_EndMs = nowMs;
  if (m_Kind == Kind::kStream || m_Kind == Kind::kVerify) {
    const std::vector<uint8_t> read(m_In.begin() + static_cast<std::ptrdiff_t>(m_Header),
                                    m_In.end());
    if (m_Kind == Kind::kVerify) {
      for (size_t i = 0; i < read.size(); ++i) {
        if (read[i] != m_Data[i]) {
          char why[96];
          std::snprintf(why, sizeof(why), "differs at 0x%06lX: 0x%02X, not 0x%02X",
                        static_cast<unsigned long>(m_Address + i), read[i], m_Data[i]);
          return Fail(why);
        }
      }
    }
    m_Data = read;
  }
  m_State = State::kDone;
  return m_State;
}

/**
 * Runs the job on by at most one transaction (and the waits a flash job polls through).
 * @return Where the job is.
 */
SpiJob::State SpiJob::Poll(a2b::Network& network, uint32_t nowMs) {
  if (m_State != State::kRunning) {
    return m_State;
  }
  m_NowMs = nowMs;
  bool ended = false;
  switch (m_Kind) {
    case Kind::kStream:
    case Kind::kVerify:
      ended = PollStream(network);
      break;
    case Kind::kSend:
      ended = PollSend(network);
      break;
    case Kind::kErase:
    case Kind::kProgram:
      ended = PollFlash(network, nowMs);
      break;
    case Kind::kNone:
      return Fail("no job");
  }
  if (m_State != State::kRunning) {
    return m_State;
  }
  return ended ? Finish(nowMs) : m_State;
}

/**
 * Result::kBusy from the stack (a node joining behind the tunnel's far end, the tunnel
 * being replanned): the caller leaves its step as it is, to try again at the next poll, for
 * up to #kStackBusyMs.
 * @return True if @p result is Result::kBusy (the job fails if it has waited too long).
 */
bool SpiJob::Retry(a2b::Result result) {
  if (result != a2b::Result::kBusy) {
    m_Waiting = false;
    return false;
  }
  if (!m_Waiting) {
    m_Waiting = true;
    m_WaitSince = m_NowMs;
  } else if (a2b::ElapsedMs(m_NowMs, m_WaitSince) > kStackBusyMs) {
    Fail("the stack stayed busy");
  }
  return true;
}

/**
 * One transaction of a stream: the next full duplex of up to 256 bytes, its predecessor's
 * read data kept; before one of another length, and at the end, a tunnel FIFO read for it.
 * @return True when the stream has all of its data.
 */
bool SpiJob::PollStream(a2b::Network& network) {
  uint8_t buf[a2b::kSpiMaxData];
  if (m_Done == m_Out.size()) {  // The last full duplex's data.
    const a2b::Result result = network.SpiReadTunnelFifo(buf, m_Last);
    if (Retry(result)) {
      return false;
    }
    if (result != a2b::Result::kOk) {
      Fail(std::string("the tunnel FIFO read failed: ") + a2b::ToString(result));
      return false;
    }
    std::memcpy(&m_In[m_Done - m_Last], buf, m_Last);
    return true;
  }
  const size_t chunk = std::min(a2b::kSpiMaxData, m_Out.size() - m_Done);
  if (m_Done > 0 && chunk != m_Last && !m_Fetched) {
    const a2b::Result result = network.SpiReadTunnelFifo(buf, m_Last);
    if (Retry(result)) {
      return false;
    }
    if (result != a2b::Result::kOk) {
      Fail(std::string("the tunnel FIFO read failed: ") + a2b::ToString(result));
      return false;
    }
    std::memcpy(&m_In[m_Done - m_Last], buf, m_Last);
    m_Fetched = true;
  }
  const bool more = m_Done + chunk < m_Out.size();
  const a2b::Result result = network.SpiFullDuplex(
      m_Select, &m_Out[m_Done], buf, chunk, more ? a2b::SpiChunk::kMore : a2b::SpiChunk::kLast);
  if (Retry(result)) {
    return false;
  }
  m_Fetched = false;
  if (result != a2b::Result::kOk) {
    Fail(std::string("a full duplex failed: ") + a2b::ToString(result));
    return false;
  }
  if (m_Done > 0 && chunk == m_Last) {
    std::memcpy(&m_In[m_Done - m_Last], buf, m_Last);
  }
  m_Last = chunk;
  m_Done += chunk;
  return false;
}

/**
 * One transaction of a send: the next bulk write of up to 256 bytes, extended but for the
 * last.
 * @return True when everything is sent.
 */
bool SpiJob::PollSend(a2b::Network& network) {
  const size_t chunk = std::min(a2b::kSpiMaxData, m_Out.size() - m_Done);
  const bool more = m_Done + chunk < m_Out.size();
  const a2b::Result result = network.SpiBulkWrite(
      m_Select, &m_Out[m_Done], chunk, more ? a2b::SpiChunk::kMore : a2b::SpiChunk::kLast);
  if (Retry(result)) {
    return false;
  }
  if (result != a2b::Result::kOk) {
    Fail(std::string("a bulk write failed: ") + a2b::ToString(result));
    return false;
  }
  m_Done += chunk;
  return m_Done == m_Out.size();
}

/**
 * One step of an erase or program: write enable, the command, or one status read (BUSY
 * still set: the next poll reads it again, up to #kBusyTimeoutMs).
 * @return True when every sector or page is done.
 */
bool SpiJob::PollFlash(a2b::Network& network, uint32_t nowMs) {
  if (m_UnitLen == 0) {
    return true;  // Nothing to program.
  }
  const a2b::PeripheralAddress flash = a2b::PeripheralAddress::Spi(m_Select);
  const uint32_t at = static_cast<uint32_t>(m_Address + m_Unit);
  a2b::Result result = a2b::Result::kOk;
  switch (m_Step) {
    case Step::kEnable: {
      const uint8_t enable = 0x06;
      result = network.PeripheralWrite(m_Node, flash, &enable, 1);
      if (Retry(result)) {
        return false;
      }
      m_Step = Step::kWrite;
      break;
    }
    case Step::kWrite:
      if (m_Kind == Kind::kErase) {
        const std::vector<uint8_t> erase = Command(0x20, at);
        result = network.PeripheralWrite(m_Node, flash, erase.data(), erase.size());
        if (Retry(result)) {
          return false;
        }
      } else {
        std::vector<uint8_t> program = Command(0x02, at);
        program.insert(program.end(), m_Data.begin() + static_cast<std::ptrdiff_t>(m_Unit),
                       m_Data.begin() + static_cast<std::ptrdiff_t>(m_Unit + m_UnitLen));
        const size_t first = std::min(a2b::kSpiMaxData, program.size());
        const bool more = first < program.size();
        result = network.SpiBulkWrite(m_Select, program.data(), first,
                                      more ? a2b::SpiChunk::kMore : a2b::SpiChunk::kLast);
        if (Retry(result)) {
          return false;
        }
        if (result == a2b::Result::kOk && more) {
          result = network.SpiBulkWrite(m_Select, program.data() + first, program.size() - first,
                                        a2b::SpiChunk::kLast);
        }
      }
      m_Step = Step::kBusy;
      m_BusySince = nowMs;
      break;
    case Step::kBusy: {
      const uint8_t read = 0x05;
      uint8_t status = 0xFF;
      result = network.PeripheralWriteRead(m_Node, flash, &read, 1, &status, 1);
      if (Retry(result)) {
        return false;
      }
      if (result == a2b::Result::kOk && (status & 0x01) != 0) {
        if (a2b::ElapsedMs(nowMs, m_BusySince) > kBusyTimeoutMs) {
          Fail("the flash stayed busy");
        }
        return false;
      }
      if (result == a2b::Result::kOk && (status & 0x02) != 0) {
        Fail("the flash didn't take the command (write enable still set)");
        return false;
      }
      ++m_Units;
      m_Step = Step::kEnable;
      if (!NextUnit()) {
        return true;
      }
      break;
    }
  }
  if (result != a2b::Result::kOk) {
    char why[96];
    std::snprintf(why, sizeof(why), "at 0x%06lX: %s", static_cast<unsigned long>(at),
                  a2b::ToString(result));
    Fail(why);
  }
  return false;
}

/** Moves to the next sector or page (skipping pages all 0xFF). @return False at the end. */
bool SpiJob::NextUnit() {
  for (;;) {
    m_Unit += m_UnitLen;
    if (m_Unit >= m_End) {
      m_UnitLen = 0;
      return false;
    }
    m_UnitLen = (m_Kind == Kind::kErase) ? kSector : std::min(kPage, m_End - m_Unit);
    if (m_Kind != Kind::kProgram ||
        !std::all_of(m_Data.begin() + static_cast<std::ptrdiff_t>(m_Unit),
                     m_Data.begin() + static_cast<std::ptrdiff_t>(m_Unit + m_UnitLen),
                     [](uint8_t b) { return b == 0xFF; })) {
      return true;
    }
    ++m_Skipped;
  }
}

/** What the job did, its time and its rate: "65536 bytes in 410 ms, 1278 kbit/s". */
std::string SpiJob::Summary() const {
  const uint32_t ms = a2b::ElapsedMs(m_EndMs, m_StartMs);
  size_t bytes = 0;
  char text[160];
  switch (m_Kind) {
    case Kind::kStream:
    case Kind::kVerify:
      bytes = m_Out.size() - m_Header;
      break;
    case Kind::kSend:
      bytes = m_Out.size();
      break;
    case Kind::kErase:
      std::snprintf(text, sizeof(text), "%u sector(s) of 4 KB erased in %u ms",
                    static_cast<unsigned>(m_Units), static_cast<unsigned>(ms));
      return text;
    case Kind::kProgram:
      std::snprintf(text, sizeof(text), "%u page(s) programmed, %u left out (all 0xFF), in %u ms",
                    static_cast<unsigned>(m_Units), static_cast<unsigned>(m_Skipped),
                    static_cast<unsigned>(ms));
      return text;
    case Kind::kNone:
      return "nothing";
  }
  std::snprintf(text, sizeof(text), "%u bytes in %u ms, %u kbit/s", static_cast<unsigned>(bytes),
                static_cast<unsigned>(ms), static_cast<unsigned>((ms == 0) ? 0 : bytes * 8 / ms));
  return text;
}
