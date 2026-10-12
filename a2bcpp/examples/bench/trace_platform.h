/**
 * @file trace_platform.h
 * @brief A Platform that passes everything to another one and can write each I2C transfer
 *        to a file, as I2CT lines (the trace format of tools/README.md, for trace_decode.py).
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_EXAMPLES_BENCH_TRACE_PLATFORM_H_
#define A2BCPP_EXAMPLES_BENCH_TRACE_PLATFORM_H_

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include "a2b/platform.h"

/// Wraps the host's Platform (USBi, Linux i2c-dev, the simulator) and traces its I2C.
class TracePlatform : public a2b::Platform {
 public:
  explicit TracePlatform(a2b::Platform& host);  ///< Passes everything to @p host.
  ~TracePlatform() override;

  TracePlatform(const TracePlatform&) = delete;
  TracePlatform& operator=(const TracePlatform&) = delete;

  bool Start(const char* path);                       ///< Traces to a new file at @p path.
  void Stop();                                        ///< Stops tracing and closes the file.
  bool Tracing() const { return m_File != nullptr; }  ///< A trace file is open.
  uint32_t Count() const { return m_Seq; }            ///< Transfers traced so far.

  a2b::Result I2cWrite(uint8_t addr, const uint8_t* data, size_t len) override;
  a2b::Result I2cRead(uint8_t addr, uint8_t* data, size_t len) override;
  a2b::Result I2cWriteRead(uint8_t addr, const uint8_t* writeData, size_t writeLen,
                           uint8_t* readData, size_t readLen) override;
  uint32_t NowMs() override;
  a2b::Result SetMainNodeReset(uint8_t bus, bool asserted) override;
  bool IrqPending(uint8_t bus) override;
  bool PowerGood(uint8_t bus) override;
  void Log(a2b::LogLevel level, const char* text) override;
  void Lock() override;
  void Unlock() override;

 private:
  void Write(uint64_t startUs, uint8_t addr, const char* op, a2b::Result result,
             const uint8_t* writeData, size_t writeLen, const uint8_t* readData,
             size_t readLen);  ///< One I2CT line.

  a2b::Platform& m_Host;
  std::FILE* m_File = nullptr;  ///< The trace, or none.
  uint32_t m_Seq = 0;           ///< Next sequence number.
  uint64_t m_StartUs = 0;       ///< When the trace started.
};

#endif  // A2BCPP_EXAMPLES_BENCH_TRACE_PLATFORM_H_
