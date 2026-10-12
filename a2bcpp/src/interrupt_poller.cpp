/**
 * @file interrupt_poller.cpp
 * @brief InterruptPoller: reads the main node's interrupt registers at a set interval.
 *
 * Follows the polling half of ADI's interrupt handling (a2b_intrQueryIrq in
 * a2bstack/src/interrupt.c). Each check reads INTSRC. If it shows a main-node or
 * sub-node interrupt, INTTYPE is read too, which clears that interrupt in the main node.
 *
 * Checks come every `intervalMs` (ADI's stack polled every 20 ms). After an interrupt
 * the next poll checks again at once, so a queue of interrupts drains without waiting, as
 * in ADI's traces (INTSRC read again straight after INTTYPE). Platform::IrqPending()
 * gates each check: a host with an IRQ line reads the registers only when the line is
 * active; the default reads them at every check.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "interrupt_poller.h"

#include "detail/regdefs.h"

namespace a2b {

/**
 * Makes a poller whose first Poll() checks.
 * @param platform For Platform::IrqPending(); must outlive the poller.
 * @param transport The bus; must outlive the poller.
 * @param busIndex The bus index Platform::IrqPending() takes.
 * @param intervalMs Time between checks.
 */
InterruptPoller::InterruptPoller(Platform& platform, Transport& transport, uint8_t busIndex,
                                 uint32_t intervalMs)
    : m_Platform(platform),
      m_Transport(transport),
      m_BusIndex(busIndex),
      m_IntervalMs(intervalMs) {}

/**
 * Changes the bus index and the interval, e.g. when the bus's configuration changes
 * before a start-up. The next Poll() checks.
 * @param busIndex The bus index Platform::IrqPending() takes.
 * @param intervalMs Time between checks.
 */
void InterruptPoller::Configure(uint8_t busIndex, uint32_t intervalMs) {
  m_BusIndex = busIndex;
  m_IntervalMs = intervalMs;
  m_CheckAtNextPoll = true;
}

/**
 * Checks for an interrupt if a check is due: at the first poll, after an interrupt, after
 * CheckAtNextPoll(), and every interval.
 * @param nowMs Platform::NowMs().
 * @param irq Set to the interrupt when one is found.
 * @param found Set to true if an interrupt was found.
 * @return Result::kOk, or Result::kI2cError if a read failed.
 */
Result InterruptPoller::Poll(uint32_t nowMs, Interrupt& irq, bool& found) {
  found = false;
  if (!m_CheckAtNextPoll && !TimeReached(nowMs, m_NextCheck)) {
    return Result::kOk;
  }
  m_CheckAtNextPoll = false;
  m_NextCheck = nowMs + m_IntervalMs;
  const Result result = CheckNow(irq, found);
  if (found) {
    m_CheckAtNextPoll = true;
  }
  return result;
}

/**
 * Checks for an interrupt now, whatever the interval: e.g. a last look before a timeout
 * is reported. Platform::IrqPending() still gates it.
 * @param irq Set to the interrupt when one is found.
 * @param found Set to true if an interrupt was found.
 * @return Result::kOk, or Result::kI2cError if a read failed.
 */
Result InterruptPoller::CheckNow(Interrupt& irq, bool& found) {
  found = false;
  if (!m_Platform.IrqPending(m_BusIndex)) {
    return Result::kOk;
  }
  uint8_t source = 0;
  Result result = m_Transport.ReadReg(kMainNode, reg::kIntsrc, source);
  if (result != Result::kOk || (source & (reg::kIntsrcMstint | reg::kIntsrcSlvint)) == 0) {
    return result;
  }
  uint8_t type = 0;
  result = m_Transport.ReadReg(kMainNode, reg::kInttype, type);
  if (result != Result::kOk) {
    return result;
  }
  irq.source = source;
  irq.type = type;
  found = true;
  return Result::kOk;
}

}  // namespace a2b
