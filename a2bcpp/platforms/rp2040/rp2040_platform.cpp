/**
 * @file rp2040_platform.cpp
 * @brief Rp2040Platform: host I2C with the Pico SDK's blocking transfers.
 *
 * A write-read is a write without a stop (the SDK's nostop), then a read: a repeated
 * start, as the Platform contract asks. Each transfer has a timeout of 1 ms per byte plus
 * 1 ms, like ADI's RP2040 PAL, which also
 * retried a failed transfer once; this platform leaves retries to the stack.
 *
 * The fixed app and the bench console run on the AB0310 board with this platform and its
 * reset line. TODO(test on hardware): the minimal example.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "rp2040_platform.h"

#include <cstdio>

#include "hardware/gpio.h"
#include "pico/time.h"

namespace a2b {
namespace {

/** Timeout for a transfer of @p len bytes. */
unsigned TimeoutUs(size_t len) { return static_cast<unsigned>(1000 * (len + 1)); }

/** True if the SDK call moved all @p len bytes. */
bool Moved(int result, size_t len) { return result == static_cast<int>(len); }

}  // namespace

/**
 * Makes the platform; Init() sets up the hardware.
 * @param i2c The I2C block, i2c0 or i2c1.
 * @param sdaPin GPIO for SDA.
 * @param sclPin GPIO for SCL.
 * @param baudHz Bus speed (400 kHz is the usual).
 */
Rp2040Platform::Rp2040Platform(i2c_inst_t* i2c, unsigned sdaPin, unsigned sclPin, unsigned baudHz)
    : m_I2c(i2c), m_SdaPin(sdaPin), m_SclPin(sclPin), m_BaudHz(baudHz) {}

/**
 * Gives bus 0 a RESETn line, which the stack pulses at Network::Start(). Without one,
 * SetMainNodeReset() returns Result::kNotSupported. Call it before Init().
 * @param pin The GPIO wired to the main node's RESETn (active low), e.g. GP2 on the
 *        AB0310 board.
 */
void Rp2040Platform::SetResetPin(unsigned pin) { m_ResetPin = static_cast<int>(pin); }

/**
 * Sets up the I2C block at its speed, and its two pins with pull-ups; and RESETn, if
 * there is one, as an output driven high (not in reset).
 */
void Rp2040Platform::Init() {
  i2c_init(m_I2c, m_BaudHz);
  gpio_set_function(m_SdaPin, GPIO_FUNC_I2C);
  gpio_set_function(m_SclPin, GPIO_FUNC_I2C);
  gpio_pull_up(m_SdaPin);
  gpio_pull_up(m_SclPin);
  if (m_ResetPin >= 0) {
    const unsigned pin = static_cast<unsigned>(m_ResetPin);
    gpio_init(pin);
    gpio_put(pin, true);
    gpio_set_dir(pin, GPIO_OUT);
  }
}

/**
 * Writes bytes in one transfer ending in a stop.
 * @param addr 7-bit I2C address.
 * @param data Bytes to write.
 * @param len Number of bytes.
 * @return Result::kOk, or Result::kI2cError (NAK or timeout).
 */
Result Rp2040Platform::I2cWrite(uint8_t addr, const uint8_t* data, size_t len) {
  const int result = i2c_write_timeout_us(m_I2c, addr, data, len, false, TimeoutUs(len));
  return Moved(result, len) ? Result::kOk : Result::kI2cError;
}

/**
 * Reads bytes in one transfer.
 * @param addr 7-bit I2C address.
 * @param data Buffer for the bytes.
 * @param len Number of bytes.
 * @return Result::kOk, or Result::kI2cError.
 */
Result Rp2040Platform::I2cRead(uint8_t addr, uint8_t* data, size_t len) {
  const int result = i2c_read_timeout_us(m_I2c, addr, data, len, false, TimeoutUs(len));
  return Moved(result, len) ? Result::kOk : Result::kI2cError;
}

/**
 * Writes, then reads after a repeated start.
 * @param addr 7-bit I2C address.
 * @param writeData Bytes to write.
 * @param writeLen Number of bytes to write.
 * @param readData Buffer for the bytes read.
 * @param readLen Number of bytes to read.
 * @return Result::kOk, or Result::kI2cError.
 */
Result Rp2040Platform::I2cWriteRead(uint8_t addr, const uint8_t* writeData, size_t writeLen,
                                    uint8_t* readData, size_t readLen) {
  int result = i2c_write_timeout_us(m_I2c, addr, writeData, writeLen, true, TimeoutUs(writeLen));
  if (!Moved(result, writeLen)) {
    return Result::kI2cError;
  }
  result = i2c_read_timeout_us(m_I2c, addr, readData, readLen, false, TimeoutUs(readLen));
  return Moved(result, readLen) ? Result::kOk : Result::kI2cError;
}

/**
 * Milliseconds since boot; wraps at 2^32.
 * @return The time.
 */
uint32_t Rp2040Platform::NowMs() { return to_ms_since_boot(get_absolute_time()); }

/**
 * Drives bus 0's RESETn.
 * @param bus Bus index; only bus 0 has a line.
 * @param asserted True drives it low (reset), false high.
 * @return Result::kOk, or Result::kNotSupported without a line (SetResetPin()) or for
 *         another bus.
 */
Result Rp2040Platform::SetMainNodeReset(uint8_t bus, bool asserted) {
  if (m_ResetPin < 0 || bus != 0) {
    return Result::kNotSupported;
  }
  gpio_put(static_cast<unsigned>(m_ResetPin), !asserted);
  return Result::kOk;
}

/**
 * Prints a log line to stdio.
 * @param level Severity.
 * @param text The message.
 */
void Rp2040Platform::Log(LogLevel level, const char* text) {
  static const char* const kPrefixes[] = {"error", "warning", "info", "debug"};
  std::printf("%s: %s\n", kPrefixes[static_cast<int>(level)], text);
}

}  // namespace a2b
