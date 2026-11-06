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
 * SPI (OpenSpi(), docs/SPI.md): one SPI_IOC_MESSAGE ioctl per transfer, so the slave select
 * stays asserted for all of it, as Platform::SpiTransfer() asks. spidev's default buffer is
 * 4096 bytes, far more than a tunnel transaction's 259. A transfer traces as an SPIT line,
 * `SPIT,<seq>,<t_us>,<bus>,<status>,<len>,<txHex>,<rxHex>`, in the same sequence as the I2CT
 * lines (trace_decode.py prints them; trace_diff.py leaves them out).
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
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

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

/** Closes the I2C bus and the SPI if they are open. */
LinuxPlatform::~LinuxPlatform() {
  Close();
  CloseSpi();
}

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
 * Opens the SPI to the main node's SPI slave port.
 * @param device Its spidev path: "/dev/spidev0.1" on the Pi main-node board (SPI0, CE1 to
 *        the main node's ADR1).
 * @param hz SCK, at most 15 MHz (the AD2437's 66.67 ns SCK period as a slave).
 * @param mode SPI mode 0-3, the main node's SPICFG.SPI_CPOL and SPI_CPHA (0 from reset).
 * @return Result::kOk, or Result::kSpiError if it can't be opened or set up (check that SPI
 *         is enabled, dtparam=spi=on, and that the user is in the spi group).
 */
Result LinuxPlatform::OpenSpi(const char* device, uint32_t hz, uint8_t mode) {
  CloseSpi();
  m_SpiFd = open(device, O_RDWR);
  if (m_SpiFd < 0) {
    return Result::kSpiError;
  }
  uint8_t spiMode = mode & 0x03;
  uint8_t bits = 8;
  if (ioctl(m_SpiFd, SPI_IOC_WR_MODE, &spiMode) < 0 ||
      ioctl(m_SpiFd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
      ioctl(m_SpiFd, SPI_IOC_WR_MAX_SPEED_HZ, &hz) < 0) {
    CloseSpi();
    return Result::kSpiError;
  }
  m_SpiHz = hz;
  return Result::kOk;
}

/** Closes the SPI. */
void LinuxPlatform::CloseSpi() {
  if (m_SpiFd >= 0) {
    close(m_SpiFd);
    m_SpiFd = -1;
  }
}

/**
 * One full-duplex SPI transfer under one slave select (one SPI_IOC_MESSAGE).
 * @param bus Bus index (one SPI port: the same for every bus).
 * @param tx Bytes to send.
 * @param rx Buffer for the bytes received (may be @p tx).
 * @param len Number of bytes.
 * @return Result::kOk, Result::kNotSupported without OpenSpi(), or Result::kSpiError.
 */
Result LinuxPlatform::SpiTransfer(uint8_t bus, const uint8_t* tx, uint8_t* rx, size_t len) {
  if (m_SpiFd < 0) {
    return Result::kNotSupported;
  }
  spi_ioc_transfer xfer = {};
  xfer.tx_buf = reinterpret_cast<uintptr_t>(tx);
  xfer.rx_buf = reinterpret_cast<uintptr_t>(rx);
  xfer.len = static_cast<__u32>(len);
  xfer.speed_hz = m_SpiHz;
  xfer.bits_per_word = 8;
  const uint64_t startUs = (m_Trace != nullptr) ? NowUs() : 0;
  uint8_t sent[300];
  const size_t kept = (len < sizeof(sent)) ? len : sizeof(sent);
  if (m_Trace != nullptr) {
    std::memcpy(sent, tx, kept);  // rx may be tx's buffer.
  }
  const Result result =
      (ioctl(m_SpiFd, SPI_IOC_MESSAGE(1), &xfer) >= 0) ? Result::kOk : Result::kSpiError;
  if (m_Trace != nullptr) {
    TraceSpi(startUs, bus, result, sent, rx, kept);
  }
  return result;
}

/** Writes one SPIT line for an SPI transfer that started at @p startUs. */
void LinuxPlatform::TraceSpi(uint64_t startUs, uint8_t bus, Result result, const uint8_t* tx,
                             const uint8_t* rx, size_t len) {
  const bool ok = (result == Result::kOk);
  std::fprintf(m_Trace, "SPIT,%u,%llu,%u,%u,%u,", static_cast<unsigned>(m_TraceSeq++),
               static_cast<unsigned long long>(startUs - m_TraceStartUs),
               static_cast<unsigned>(bus), ok ? 0u : 1u, static_cast<unsigned>(len));
  PutHex(m_Trace, tx, len);
  std::fputc(',', m_Trace);
  if (ok) {
    PutHex(m_Trace, rx, len);
  }
  std::fputc('\n', m_Trace);
  std::fflush(m_Trace);
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
