/**
 * @file platform.h
 * @brief The interface a host implements to run the stack: host I2C, time, GPIO, logging.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_A2B_PLATFORM_H_
#define A2BCPP_A2B_PLATFORM_H_

#include <cstddef>
#include <cstdint>

#include "a2b/types.h"

namespace a2b {

/** @brief Severity of a log message. */
enum class LogLevel : uint8_t {
  kError,    ///< The bus failed, or something the app must act on.
  kWarning,  ///< A node dropped or was rejected, a transfer failed, a plan failed.
  kInfo,     ///< What the bus did: a node found, the chain up, a plan written.
  kDebug,    ///< Detail for debugging; the stack itself logs none.
};

/**
 * @brief Host services the stack needs. One instance may serve several buses.
 *
 * Only the I2C transfers and NowMs() must be implemented; the rest have defaults
 * that suit a host without the matching GPIO lines. The stack never blocks inside
 * these calls apart from the I2C transfers themselves, and never asks for a delay.
 *
 * Each @p bus parameter is the bus index the app gave that bus's configuration, so a
 * platform with several A2B buses can pick the right GPIO line.
 *
 * See the porting guide (a2bcpp/docs/porting.md) for the full contract.
 */
class Platform {
 public:
  virtual ~Platform() = default;

  /**
   * @brief Writes bytes to a device on the host I2C bus, in one transfer ending in a stop.
   * @param addr 7-bit I2C address.
   * @param data Bytes to write.
   * @param len Number of bytes, at least 1.
   * @return Result::kOk, or Result::kI2cError if the transfer failed.
   */
  virtual Result I2cWrite(uint8_t addr, const uint8_t* data, size_t len) = 0;

  /**
   * @brief Reads bytes from a device on the host I2C bus, in one transfer.
   * @param addr 7-bit I2C address.
   * @param data Buffer for the bytes read.
   * @param len Number of bytes, at least 1.
   * @return Result::kOk, or Result::kI2cError if the transfer failed.
   */
  virtual Result I2cRead(uint8_t addr, uint8_t* data, size_t len) = 0;

  /**
   * @brief Writes then reads, joined by a repeated start (no stop in between).
   * @param addr 7-bit I2C address.
   * @param writeData Bytes to write, usually a register address.
   * @param writeLen Number of bytes to write, at least 1.
   * @param readData Buffer for the bytes read.
   * @param readLen Number of bytes to read, at least 1.
   * @return Result::kOk, or Result::kI2cError if the transfer failed.
   */
  virtual Result I2cWriteRead(uint8_t addr, const uint8_t* writeData, size_t writeLen,
                              uint8_t* readData, size_t readLen) = 0;

  /**
   * @brief Milliseconds from a free-running counter. It may wrap at 2^32.
   *
   * The stack only takes differences (see ElapsedMs() and TimeReached()).
   */
  virtual uint32_t NowMs() = 0;

  /**
   * @brief Drives the main node's reset line.
   * @param bus Bus index.
   * @param asserted True to hold the main node in reset.
   * @return Result::kOk, or Result::kNotSupported (the default) if there is no reset
   *         line. The stack resets the main node through its CONTROL register either way,
   *         and with a line also pulses it at Network::Start().
   */
  virtual Result SetMainNodeReset(uint8_t bus, bool asserted) {
    (void)bus;
    (void)asserted;
    return Result::kNotSupported;
  }

  /**
   * @brief Reads the main node's IRQ line.
   * @param bus Bus index.
   * @return False only when the line shows no interrupt pending. The default returns
   *         true, so a host without the line has the stack read the interrupt
   *         registers at every check (BoardTimings::interruptPollMs).
   */
  virtual bool IrqPending(uint8_t bus) {
    (void)bus;
    return true;
  }

  /**
   * @brief Reads the power-good signal of the main node's downstream power supply.
   *
   * Used by two-step discovery. The default returns true (no power-good input).
   * @param bus Bus index.
   */
  virtual bool PowerGood(uint8_t bus) {
    (void)bus;
    return true;
  }

  /**
   * @brief Writes one log message. The default discards it.
   * @param level Severity.
   * @param text Message without a trailing newline.
   */
  virtual void Log(LogLevel level, const char* text) {
    (void)level;
    (void)text;
  }

  /**
   * @brief Takes exclusive use of the host I2C bus.
   *
   * Needed only when several threads share the I2C controller. The stack holds the
   * lock for each register or peripheral access, which may be several transfers
   * (e.g. NODEADR, then the register). The default does nothing.
   */
  virtual void Lock() {}

  /** @brief Releases the lock taken by Lock(). The default does nothing. */
  virtual void Unlock() {}
};

}  // namespace a2b

#endif  // A2BCPP_A2B_PLATFORM_H_
