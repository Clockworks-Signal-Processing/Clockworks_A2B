/**
 * @file types.h
 * @brief Types shared by the whole stack: result codes, node addresses and time.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_A2B_TYPES_H_
#define A2BCPP_A2B_TYPES_H_

#include <cstdint>

/// The C++ A2B plug-and-play stack.
namespace a2b {

/** @brief Result of a stack or platform operation. */
enum class Result : uint8_t {
  kOk = 0,           ///< Success.
  kI2cError,         ///< A host I2C transfer failed (NAK, bus error, driver timeout).
  kInvalidArgument,  ///< A parameter is out of range.
  kNotSupported,     ///< The platform or chip does not have this feature.
  kNoSpace,          ///< A fixed-size table or buffer is full.
  kInvalidData,      ///< Data read or given is malformed (e.g. a module-info image).
};

/** @brief Address of a node on one bus: #kMainNode or a sub-node position. */
using NodeAddr = int8_t;

/** @brief The main node (the node the host talks to directly). */
inline constexpr NodeAddr kMainNode = -1;

/** @brief Hardware limit on sub-nodes per bus (NODEADR.NODE is 4 bits). */
inline constexpr int kMaxSubNodes = 16;

/** @brief True for a sub-node position, 0 to #kMaxSubNodes - 1. */
inline constexpr bool IsSubNode(NodeAddr node) { return node >= 0 && node < kMaxSubNodes; }

/**
 * @brief How a peripheral of a node is reached: a 7-bit I2C address now. The SPI form
 *        (native slave select, or node GPIO lines) is reserved; SPI is not supported yet.
 */
struct PeripheralAddress {
  /** @brief The peripheral's interface. */
  enum class Kind : uint8_t {
    kI2c,  ///< On the node's I2C port, at #i2cAddr.
    kSpi,  ///< On the node's SPI port (not supported yet): calls return Result::kNotSupported.
  };

  Kind kind = Kind::kI2c;  ///< Its interface.
  uint8_t i2cAddr = 0;     ///< Kind::kI2c: its 7-bit I2C address.

  /** @brief An I2C peripheral at 7-bit address @p addr. */
  static constexpr PeripheralAddress I2c(uint8_t addr) {
    PeripheralAddress address;
    address.i2cAddr = addr;
    return address;
  }
};

/**
 * @brief Milliseconds from @p since to @p now on a free-running 32-bit counter.
 *
 * Correct across a counter wrap, for spans under 2^32 ms.
 */
inline constexpr uint32_t ElapsedMs(uint32_t now, uint32_t since) { return now - since; }

/**
 * @brief True once @p now has reached @p deadline on a free-running 32-bit counter.
 *
 * Correct across a counter wrap, for deadlines less than 2^31 ms away.
 */
inline constexpr bool TimeReached(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}

}  // namespace a2b

#endif  // A2BCPP_A2B_TYPES_H_
