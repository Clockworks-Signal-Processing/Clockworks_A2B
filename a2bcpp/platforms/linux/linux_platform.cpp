/**
 * @file linux_platform.cpp
 * @brief LinuxPlatform: host I2C through the kernel's i2c-dev interface.
 *
 * Every transfer is one I2C_RDWR ioctl, so a write-read is joined by a repeated start, as
 * the Platform contract asks (ADI's Pi PAL does the same).
 * The kernel driver's own timeout and retries apply. The Pi's A2B bus is /dev/i2c-1
 * (GPIO2 = SDA, GPIO3 = SCL) once I2C is enabled in raspi-config.
 *
 * SetTrace() writes each transfer as an I2CT line, the I2CT trace format
 * (a2bcpp/tools/README.md), which a2bcpp/tools/trace_decode.py and trace_diff.py read:
 * `I2CT,<seq>,<t_us>,0,<addr>,<W|R|WR>,<status>,<wLen>,<wHex>,<rLen>,<rHex>`, with the
 * time the transfer started, from SetTrace(), and status 1 for a failed transfer. Each line
 * is flushed as soon as its transfer ends, so a transfer that never returns shows as the
 * line after the last one written.
 *
 * The reset and IRQ lines are not driven yet (ADI's Pi app used libgpiod): without
 * them the stack resets the main node through CONTROL and polls the interrupt registers.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "linux_platform.h"

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <cstdio>

namespace a2b {
namespace {

/** Runs one I2C_RDWR transaction of @p count messages. */
Result Transfer(int fd, i2c_msg* msgs, unsigned count) {
  if (fd < 0) {
    return Result::kI2cError;
  }
  i2c_rdwr_ioctl_data data = {msgs, count};
  return (ioctl(fd, I2C_RDWR, &data) == static_cast<int>(count)) ? Result::kOk : Result::kI2cError;
}

/** Microseconds from the monotonic clock. */
uint64_t NowUs() {
  timespec now = {};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<uint64_t>(now.tv_sec) * 1000000u + static_cast<uint64_t>(now.tv_nsec) / 1000u;
}

/** Writes @p len bytes as hex. */
void PutHex(std::FILE* out, const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    std::fprintf(out, "%02X", data[i]);
  }
}

}  // namespace

/** Closes the I2C bus if it is open. */
LinuxPlatform::~LinuxPlatform() { Close(); }

/**
 * Opens an I2C bus.
 * @param device Its i2c-dev path, e.g. "/dev/i2c-1".
 * @return Result::kOk, or Result::kI2cError if it can't be opened (check that I2C is
 *         enabled and that the user may use it, e.g. is in the i2c group).
 */
Result LinuxPlatform::Open(const char* device) {
  Close();
  m_Fd = open(device, O_RDWR);
  return (m_Fd >= 0) ? Result::kOk : Result::kI2cError;
}

/** Closes the I2C bus. */
void LinuxPlatform::Close() {
  if (m_Fd >= 0) {
    close(m_Fd);
    m_Fd = -1;
  }
}

/**
 * Starts or stops tracing the I2C transfers.
 * @param out Where the I2CT lines go (e.g. a file opened for writing), or nullptr to stop.
 *        Must stay open while tracing.
 */
void LinuxPlatform::SetTrace(std::FILE* out) {
  m_Trace = out;
  m_TraceSeq = 0;
  m_TraceStartUs = NowUs();
}

/**
 * Writes bytes in one transfer.
 * @param addr 7-bit I2C address.
 * @param data Bytes to write.
 * @param len Number of bytes.
 * @return Result::kOk, or Result::kI2cError (NAK, bus error, bus not open).
 */
Result LinuxPlatform::I2cWrite(uint8_t addr, const uint8_t* data, size_t len) {
  return Run(addr, data, len, nullptr, 0);
}

/**
 * Reads bytes in one transfer.
 * @param addr 7-bit I2C address.
 * @param data Buffer for the bytes.
 * @param len Number of bytes.
 * @return Result::kOk, or Result::kI2cError.
 */
Result LinuxPlatform::I2cRead(uint8_t addr, uint8_t* data, size_t len) {
  return Run(addr, nullptr, 0, data, len);
}

/**
 * Writes then reads, joined by a repeated start.
 * @param addr 7-bit I2C address.
 * @param writeData Bytes to write.
 * @param writeLen Number of bytes to write.
 * @param readData Buffer for the bytes read.
 * @param readLen Number of bytes to read.
 * @return Result::kOk, or Result::kI2cError.
 */
Result LinuxPlatform::I2cWriteRead(uint8_t addr, const uint8_t* writeData, size_t writeLen,
                                   uint8_t* readData, size_t readLen) {
  return Run(addr, writeData, writeLen, readData, readLen);
}

/**
 * One I2C_RDWR transaction: a write (@p readData null), a read (@p writeData null) or a
 * write-read joined by a repeated start; then its trace line, if tracing.
 */
Result LinuxPlatform::Run(uint8_t addr, const uint8_t* writeData, size_t writeLen,
                          uint8_t* readData, size_t readLen) {
  i2c_msg msgs[2];
  unsigned count = 0;
  if (writeData != nullptr) {
    msgs[count++] = {addr, 0, static_cast<__u16>(writeLen), const_cast<__u8*>(writeData)};
  }
  if (readData != nullptr) {
    msgs[count++] = {addr, I2C_M_RD, static_cast<__u16>(readLen), readData};
  }
  const uint64_t startUs = (m_Trace != nullptr) ? NowUs() : 0;
  const Result result = Transfer(m_Fd, msgs, count);
  if (m_Trace != nullptr) {
    const char* op = (writeData == nullptr) ? "R" : (readData == nullptr) ? "W" : "WR";
    const bool ok = (result == Result::kOk);
    std::fprintf(m_Trace, "I2CT,%u,%llu,0,%02X,%s,%u,%u,", static_cast<unsigned>(m_TraceSeq++),
                 static_cast<unsigned long long>(startUs - m_TraceStartUs), addr, op, ok ? 0u : 1u,
                 static_cast<unsigned>(writeLen));
    PutHex(m_Trace, writeData, (writeData != nullptr) ? writeLen : 0);
    std::fprintf(m_Trace, ",%u,", static_cast<unsigned>(readData != nullptr ? readLen : 0));
    if (ok && readData != nullptr) {
      PutHex(m_Trace, readData, readLen);
    }
    std::fputc('\n', m_Trace);
    std::fflush(m_Trace);
  }
  return result;
}

/**
 * Milliseconds from the monotonic clock; wraps at 2^32 like any Platform::NowMs().
 * @return The time.
 */
uint32_t LinuxPlatform::NowMs() {
  timespec now = {};
  clock_gettime(CLOCK_MONOTONIC, &now);
  return static_cast<uint32_t>(static_cast<uint64_t>(now.tv_sec) * 1000u +
                               static_cast<uint64_t>(now.tv_nsec) / 1000000u);
}

/**
 * Writes a log line to stderr.
 * @param level Severity.
 * @param text The message.
 */
void LinuxPlatform::Log(LogLevel level, const char* text) {
  static const char* const kPrefixes[] = {"error", "warning", "info", "debug"};
  std::fprintf(stderr, "%s: %s\n", kPrefixes[static_cast<int>(level)], text);
}

}  // namespace a2b
