/**
 * @file trace_platform.cpp
 * @brief TracePlatform: the bench console's I2C trace, the same on every host.
 *
 * Each transfer becomes one line, written and flushed as it ends:
 * `I2CT,<seq>,<t_us>,0,<addr>,<W|R|WR>,<status>,<wLen>,<wHex>,<rLen>,<rHex>`, the trace
 * format of ADI's stack's traces (a2bcpp/tools/README.md) and LinuxPlatform's, which
 * a2bcpp/tools/trace_decode.py and trace_diff.py read. The time is when the transfer
 * started, in microseconds from Start(), from the host's steady clock (not the wrapped
 * platform's millisecond clock, too coarse for a trace); status 1 is a failed transfer.
 * Lines end in "\n" on Windows too, so traces from every host compare byte for byte.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "trace_platform.h"

#include <chrono>

namespace {

/** Microseconds from the host's steady clock. */
uint64_t NowUs() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

/** Writes @p len bytes as hex. */
void PutHex(std::FILE* out, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    std::fprintf(out, "%02X", data[i]);
  }
}

}  // namespace

/**
 * Makes the wrapper; it doesn't trace until Start().
 * @param host The host's platform, which must outlive the wrapper.
 */
TracePlatform::TracePlatform(a2b::Platform& host) : m_Host(host) {}

/** Closes the trace file, if one is open. */
TracePlatform::~TracePlatform() { Stop(); }

/**
 * Starts a trace in a new file (an open one is closed first).
 * @param path The file; it is overwritten.
 * @return False if it can't be written.
 */
bool TracePlatform::Start(const char* path) {
  Stop();
  m_File = std::fopen(path, "wb");  // "\n" line ends on every host, as on the Pi.
  m_Seq = 0;
  m_StartUs = NowUs();
  return m_File != nullptr;
}

/** Stops tracing and closes the file. */
void TracePlatform::Stop() {
  if (m_File != nullptr) {
    std::fclose(m_File);
    m_File = nullptr;
  }
}

/**
 * Writes through the host, then traces the transfer.
 * @param addr 7-bit I2C address.
 * @param data Bytes to write.
 * @param len Number of bytes.
 * @return The host's result.
 */
a2b::Result TracePlatform::I2cWrite(uint8_t addr, const uint8_t* data, size_t len) {
  const uint64_t startUs = NowUs();
  const a2b::Result result = m_Host.I2cWrite(addr, data, len);
  Write(startUs, addr, "W", result, data, len, nullptr, 0);
  return result;
}

/**
 * Reads through the host, then traces the transfer.
 * @param addr 7-bit I2C address.
 * @param data Buffer for the bytes.
 * @param len Number of bytes.
 * @return The host's result.
 */
a2b::Result TracePlatform::I2cRead(uint8_t addr, uint8_t* data, size_t len) {
  const uint64_t startUs = NowUs();
  const a2b::Result result = m_Host.I2cRead(addr, data, len);
  Write(startUs, addr, "R", result, nullptr, 0, data, len);
  return result;
}

/**
 * Writes then reads through the host, then traces the transfer.
 * @param addr 7-bit I2C address.
 * @param writeData Bytes to write.
 * @param writeLen Number of bytes to write.
 * @param readData Buffer for the bytes read.
 * @param readLen Number of bytes to read.
 * @return The host's result.
 */
a2b::Result TracePlatform::I2cWriteRead(uint8_t addr, const uint8_t* writeData, size_t writeLen,
                                        uint8_t* readData, size_t readLen) {
  const uint64_t startUs = NowUs();
  const a2b::Result result = m_Host.I2cWriteRead(addr, writeData, writeLen, readData, readLen);
  Write(startUs, addr, "WR", result, writeData, writeLen, readData, readLen);
  return result;
}

/**
 * The host's clock.
 * @return Milliseconds.
 */
uint32_t TracePlatform::NowMs() { return m_Host.NowMs(); }

/**
 * The host's reset line.
 * @param bus Bus index.
 * @param asserted Hold the main node in reset.
 * @return The host's result.
 */
a2b::Result TracePlatform::SetMainNodeReset(uint8_t bus, bool asserted) {
  return m_Host.SetMainNodeReset(bus, asserted);
}

/**
 * The host's IRQ line.
 * @param bus Bus index.
 * @return The host's answer.
 */
bool TracePlatform::IrqPending(uint8_t bus) { return m_Host.IrqPending(bus); }

/**
 * The host's power-good input.
 * @param bus Bus index.
 * @return The host's answer.
 */
bool TracePlatform::PowerGood(uint8_t bus) { return m_Host.PowerGood(bus); }

/**
 * The host's log.
 * @param level Severity.
 * @param text The message.
 */
void TracePlatform::Log(a2b::LogLevel level, const char* text) { m_Host.Log(level, text); }

/** The host's lock. */
void TracePlatform::Lock() { m_Host.Lock(); }

/** The host's unlock. */
void TracePlatform::Unlock() { m_Host.Unlock(); }

/** Writes one I2CT line for a transfer that started at @p startUs, if tracing. */
void TracePlatform::Write(uint64_t startUs, uint8_t addr, const char* op, a2b::Result result,
                          const uint8_t* writeData, size_t writeLen, const uint8_t* readData,
                          size_t readLen) {
  if (m_File == nullptr) {
    return;
  }
  const bool ok = (result == a2b::Result::kOk);
  std::fprintf(m_File, "I2CT,%u,%llu,0,%02X,%s,%u,%u,", static_cast<unsigned>(m_Seq++),
               static_cast<unsigned long long>(startUs - m_StartUs), addr, op, ok ? 0u : 1u,
               static_cast<unsigned>(writeLen));
  PutHex(m_File, writeData, (writeData != nullptr) ? writeLen : 0);
  std::fprintf(m_File, ",%u,", static_cast<unsigned>((readData != nullptr) ? readLen : 0));
  if (ok && readData != nullptr) {
    PutHex(m_File, readData, readLen);
  }
  std::fputc('\n', m_File);
  std::fflush(m_File);
}
