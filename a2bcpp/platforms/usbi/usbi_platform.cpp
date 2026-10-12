// clang-format off
/*=============================================================================
 *
 * Project: a2bstack
 *
 * Copyright (c) 2015 -2023 Analog Devices Inc. All Rights Reserved.
 * See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
 *
 *=============================================================================
 */
// clang-format on

/**
 * @file usbi_platform.cpp
 * @brief UsbiPlatform: the ADI USBi's I2C framing over a UsbiLink.
 *
 * Ported from ADI's Windows PAL, pal_i2c_usbi.c (Mentor
 * Graphics, Embedded Software Division, for ADI), with the same requests, values, timeouts
 * and waits:
 * - **Write:** vendor request 0xB2 to the device (bmRequestType 0x40), wValue the 7-bit
 *   I2C address, wIndex 0 (the I2C protocol), the bytes in the data stage. Then the status:
 *   wait 2 ms, then CHECK_WRITE, request 0xB5 from the device (0xC0), one byte, repeated every 10
 * ms, up to 30 more times, while the USBi reports NONE (not started) or WRITING.
 * - **Read:** request 0xB4 from the device, the bytes in the data stage. Then CHECK_READ,
 *   0xB6, in the same way, repeated while the USBi reports NONE.
 * - **Write-read:** request 0xB3 (write, then hold the bus for a read) with the bytes to
 *   write and its write status, then 1 ms, then the read as above.
 * - The USB timeout is 500 ms, four times that for a write or read of more than 16
 *   bytes (not for the write of a write-read, as in ADI's PAL).
 *
 * Only a SUCCESS status is a completed transfer. Until one transfer has succeeded, the
 * first failure most likely means the USBi can't reach the A2B board, so it is logged
 * once with a hint; later failures (a sub-node dropping off the bus) are the stack's.
 *
 * ADI's PAL's sleeps were 1 ms granularity only with timeBeginPeriod(1) on Windows;
 * LibusbLink sets that while it is open.
 *
 * C++ form by ClockWorks Signal Processing LLC.
 */

#include "usbi_platform.h"

#include <chrono>
#include <cstdio>

namespace a2b {
namespace {

constexpr uint8_t kToDevice = 0x40;        ///< Vendor request, host to device, to the device.
constexpr uint8_t kFromDevice = 0xC0;      ///< Vendor request, device to host, to the device.
constexpr uint8_t kCmdWrite = 0xB2;        ///< I2C write, ending in a stop.
constexpr uint8_t kCmdWriteRead = 0xB3;    ///< I2C write, followed by a read.
constexpr uint8_t kCmdRead = 0xB4;         ///< I2C read.
constexpr uint8_t kCmdCheckWrite = 0xB5;   ///< Status of the last write.
constexpr uint8_t kCmdCheckRead = 0xB6;    ///< Status of the last read.
constexpr uint16_t kProtocolI2c = 0;       ///< wIndex: the I2C protocol.
constexpr unsigned kTimeoutMs = 500;       ///< USB timeout of one transfer.
constexpr size_t kLongTransfer = 16;       ///< Longer transfers get four times the timeout.
constexpr unsigned kFirstCheckMs = 2;      ///< Wait before the first status request.
constexpr unsigned kCheckIntervalMs = 10;  ///< Wait between status requests.
constexpr int kMaxChecks = 30;             ///< Status requests after the first.
constexpr unsigned kWriteToReadMs = 1;     ///< Wait between a write-read's two halves.

}  // namespace

/**
 * Makes the platform; nothing is sent until the first transfer.
 * @param link The USBi (LibusbLink, once opened), which must outlive the platform.
 */
UsbiPlatform::UsbiPlatform(UsbiLink& link) : m_Link(link) {}

/**
 * Writes bytes in one transfer ending in a stop (USBi request 0xB2).
 * @param addr 7-bit I2C address.
 * @param data Bytes to write.
 * @param len Number of bytes, 1 to #kMaxTransfer.
 * @return Result::kOk; Result::kI2cError if the USB transfer failed or the USBi reported
 *         anything but success (a NAK, a bus error, no result within ~300 ms);
 *         Result::kInvalidArgument for no bytes or too many.
 */
Result UsbiPlatform::I2cWrite(uint8_t addr, const uint8_t* data, size_t len) {
  return Write(kCmdWrite, addr, data, len, TimeoutMs(len));
}

/**
 * Reads bytes in one transfer (USBi request 0xB4), then waits for its status.
 * @param addr 7-bit I2C address.
 * @param data Buffer for the bytes.
 * @param len Number of bytes, 1 to #kMaxTransfer.
 * @return Result::kOk, Result::kI2cError or Result::kInvalidArgument, as for I2cWrite().
 */
Result UsbiPlatform::I2cRead(uint8_t addr, uint8_t* data, size_t len) {
  if (data == nullptr || len == 0 || len > kMaxTransfer) {
    return Result::kInvalidArgument;
  }
  const int got = m_Link.ControlTransfer(kFromDevice, kCmdRead, addr, kProtocolI2c, data,
                                         static_cast<uint16_t>(len), TimeoutMs(len));
  if (got != static_cast<int>(len)) {
    return Finish(addr, Status::kException);
  }
  return Finish(addr, WaitForRead(addr));
}

/**
 * Writes, then reads, joined by a repeated start: USBi request 0xB3 with the bytes to
 * write and its write status, 1 ms, then the read as I2cRead().
 * @param addr 7-bit I2C address.
 * @param writeData Bytes to write.
 * @param writeLen Number of bytes to write, 1 to #kMaxTransfer.
 * @param readData Buffer for the bytes read.
 * @param readLen Number of bytes to read, 1 to #kMaxTransfer.
 * @return Result::kOk, Result::kI2cError or Result::kInvalidArgument, as for I2cWrite().
 */
Result UsbiPlatform::I2cWriteRead(uint8_t addr, const uint8_t* writeData, size_t writeLen,
                                  uint8_t* readData, size_t readLen) {
  if (readData == nullptr || readLen == 0 || readLen > kMaxTransfer) {
    return Result::kInvalidArgument;
  }
  const Result result = Write(kCmdWriteRead, addr, writeData, writeLen, kTimeoutMs);
  if (result != Result::kOk) {
    return result;
  }
  m_Link.SleepMs(kWriteToReadMs);
  return I2cRead(addr, readData, readLen);
}

/**
 * Milliseconds from the host's steady clock; wraps at 2^32 like any Platform::NowMs().
 * @return The time.
 */
uint32_t UsbiPlatform::NowMs() {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

/**
 * Writes a log line to stderr.
 * @param level Severity.
 * @param text The message.
 */
void UsbiPlatform::Log(LogLevel level, const char* text) {
  static const char* const kPrefixes[] = {"error", "warning", "info", "debug"};
  std::fprintf(stderr, "%s: %s\n", kPrefixes[static_cast<int>(level)], text);
}

/**
 * Sends a write request (0xB2 or 0xB3) and waits for its status.
 * @param request The USBi command.
 * @param addr 7-bit I2C address.
 * @param data Bytes to write.
 * @param len Number of bytes.
 * @param timeoutMs USB timeout.
 * @return As I2cWrite().
 */
Result UsbiPlatform::Write(uint8_t request, uint8_t addr, const uint8_t* data, size_t len,
                           unsigned timeoutMs) {
  if (data == nullptr || len == 0 || len > kMaxTransfer) {
    return Result::kInvalidArgument;
  }
  // libusb takes a non-const buffer for both directions; an OUT transfer only reads it.
  const int sent =
      m_Link.ControlTransfer(kToDevice, request, addr, kProtocolI2c, const_cast<uint8_t*>(data),
                             static_cast<uint16_t>(len), timeoutMs);
  if (sent != static_cast<int>(len)) {
    return Finish(addr, Status::kException);
  }
  return Finish(addr, WaitForWrite(addr));
}

/**
 * Waits 2 ms, then asks for the write status, every 10 ms up to 30 more times while the
 * USBi hasn't started (NONE) or is still writing, as ADI's PAL does: with 1 ms sleeps
 * on Windows the first check can come before the USBi has begun.
 * @param addr The I2C address (wValue of the request).
 * @return The last status.
 */
UsbiPlatform::Status UsbiPlatform::WaitForWrite(uint8_t addr) {
  m_Link.SleepMs(kFirstCheckMs);
  Status status = Check(kCmdCheckWrite, addr);
  for (int i = 0; i < kMaxChecks && (status == Status::kNone || status == Status::kWriting); ++i) {
    m_Link.SleepMs(kCheckIntervalMs);
    status = Check(kCmdCheckWrite, addr);
  }
  return status;
}

/**
 * Waits 2 ms, then asks for the read status, every 10 ms up to 30 more times while the
 * USBi reports NONE.
 * @param addr The I2C address (wValue of the request).
 * @return The last status.
 */
UsbiPlatform::Status UsbiPlatform::WaitForRead(uint8_t addr) {
  m_Link.SleepMs(kFirstCheckMs);
  Status status = Check(kCmdCheckRead, addr);
  for (int i = 0; i < kMaxChecks && status == Status::kNone; ++i) {
    m_Link.SleepMs(kCheckIntervalMs);
    status = Check(kCmdCheckRead, addr);
  }
  return status;
}

/**
 * One status request: one byte from the device.
 * @param request CHECK_WRITE (0xB5) or CHECK_READ (0xB6).
 * @param addr The I2C address (wValue).
 * @return The status; Status::kException if the request failed or the byte is not a
 *         status.
 */
UsbiPlatform::Status UsbiPlatform::Check(uint8_t request, uint8_t addr) {
  uint8_t status = static_cast<uint8_t>(Status::kException);
  const int got = m_Link.ControlTransfer(kFromDevice, request, addr, 0, &status, 1, kTimeoutMs);
  if (got != 1 || status > static_cast<uint8_t>(Status::kException)) {
    return Status::kException;
  }
  return static_cast<Status>(status);
}

/**
 * Turns a transfer's status into a Result, and logs the hint if no transfer has worked
 * yet.
 * @param addr The I2C address, for the hint.
 * @param status The USBi's status, or Status::kException for a failed USB transfer.
 * @return Result::kOk for Status::kSuccess, otherwise Result::kI2cError.
 */
Result UsbiPlatform::Finish(uint8_t addr, Status status) {
  if (status == Status::kSuccess) {
    m_TransferOk = true;
    return Result::kOk;
  }
  if (!m_TransferOk && !m_HintShown) {
    m_HintShown = true;
    const char* how = (status == Status::kFailed)  ? "no acknowledge from the device"
                      : (status == Status::kError) ? "I2C error reported by the USBi"
                                                   : "no result from the USBi";
    char text[256];
    std::snprintf(text, sizeof(text),
                  "I2C transfer to address 0x%02X failed (%s), and none has worked yet. Is the "
                  "USBi connected to the A2B board, is the board powered, and is there a device "
                  "at that address?",
                  addr, how);
    Log(LogLevel::kWarning, text);
  }
  return Result::kI2cError;
}

/**
 * The USB timeout of a write or read: four times longer for more than 16 bytes.
 * @param len Bytes in the transfer.
 * @return The timeout in milliseconds.
 */
unsigned UsbiPlatform::TimeoutMs(size_t len) {
  return (len > kLongTransfer) ? 4 * kTimeoutMs : kTimeoutMs;
}

}  // namespace a2b
