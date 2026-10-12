/**
 * @file interrupt_poller.h
 * @brief Checks the main node's interrupt registers at a set interval. Internal.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_INTERRUPT_POLLER_H_
#define A2BCPP_INTERRUPT_POLLER_H_

#include <cstdint>

#include "a2b/platform.h"
#include "a2b/types.h"
#include "transport.h"

namespace a2b {

/// Reads INTSRC and INTTYPE of one bus's main node. One thread per poller.
class InterruptPoller {
 public:
  /** @brief One interrupt, as the main node reported it. */
  struct Interrupt {
    uint8_t source = 0;  ///< INTSRC: which node raised it.
    uint8_t type = 0;    ///< INTTYPE: what it is.
  };

  InterruptPoller(Platform& platform, Transport& transport, uint8_t busIndex,
                  uint32_t intervalMs);  ///< Checks at the first poll.

  InterruptPoller(const InterruptPoller&) = delete;
  InterruptPoller& operator=(const InterruptPoller&) = delete;

  Result Poll(uint32_t nowMs, Interrupt& irq, bool& found);  ///< Checks if a check is due.
  Result CheckNow(Interrupt& irq, bool& found);              ///< Checks at once.
  void CheckAtNextPoll() { m_CheckAtNextPoll = true; }       ///< Next Poll() checks.
  void Configure(uint8_t busIndex, uint32_t intervalMs);     ///< New bus index and interval.

 private:
  Platform& m_Platform;
  Transport& m_Transport;
  uint8_t m_BusIndex;
  uint32_t m_IntervalMs;
  uint32_t m_NextCheck = 0;
  bool m_CheckAtNextPoll = true;
};

}  // namespace a2b

#endif  // A2BCPP_INTERRUPT_POLLER_H_
