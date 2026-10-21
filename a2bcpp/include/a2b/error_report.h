/**
 * @file error_report.h
 * @brief What the nodes saw when the chain failed: the error watch's report
 *        (Network::SetErrorWatch(), Network::LastErrorReport()).
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_A2B_ERROR_REPORT_H_
#define A2BCPP_A2B_ERROR_REPORT_H_

#include <cstdint>

#include "a2b/routing.h"
#include "a2b/types.h"

namespace a2b {

/**
 * @brief A node's error registers, as one read found them. INTPNDn bits latch whatever
 *        INTMSKn says (the masks only gate the interrupt), so they hold what the node saw
 *        since they were last cleared.
 */
struct NodeErrorState {
  bool read = false;   ///< The node answered every read; the fields below are set.
  uint8_t swstat = 0;  ///< SWSTAT: FIN, FAULT and the cable fault code of its B side.
  /** INTPND0 (SRF missed, BECOVF, PWRERR, parity, CRC, decoding, header count, SRF CRC),
      INTPND1 (the GPIO pins) and INTPND2 (main node only: I2CERR, ICRCERR, DSCDONE). */
  uint8_t intpnd[3] = {};
  uint8_t intmsk0 = 0;  ///< INTMSK0: which INTPND0 bits raise an interrupt.
  uint8_t becctl = 0;   ///< BECCTL: what BECNT counts.
  uint8_t becnt = 0;    ///< BECNT: errors counted since its last clear.
  uint8_t node = 0;     ///< NODE (sub-nodes): LAST, NLAST, DISCVD and its number.
};

/**
 * @brief The access that failed: the node, the register, and how long the tries took
 *        (a NAK is quick; a host adapter that times out, such as the USBi, takes hundreds
 *        of milliseconds).
 */
struct FailedAccess {
  NodeAddr node = kNoNode;  ///< The node addressed, or #kNoNode for a broadcast.
  uint16_t reg = 0;         ///< Its first register.
  bool write = false;       ///< A write (else a read).
  uint8_t tries = 0;        ///< Tries made, all failed.
  uint32_t elapsedMs = 0;   ///< From the first try to the last failure.
};

/** @brief What the error watch took when a probe failed or before a node was dropped. */
struct ErrorReport {
  /** @brief When it was taken. */
  enum class Cause : uint8_t {
    kNone,         ///< No report yet.
    kProbeFailed,  ///< A probe of position #position failed; the chain is checked next.
    kDropped,      ///< Sub-node #position failed a chain check and is dropped next.
  };
  Cause cause = Cause::kNone;  ///< Why it was taken.
  NodeAddr position = 0;       ///< The position probed, or the node dropped.
  uint32_t timeMs = 0;         ///< Platform::NowMs() when it was taken.
  FailedAccess failed;         ///< The access that failed.
  /** kDropped: the node answered one more read of VENDOR and PRODUCT, made after the
      others were read; the IDs are in #lateIds. */
  bool answeredLate = false;
  uint8_t lateIds[2] = {};  ///< kDropped, #answeredLate: VENDOR and PRODUCT read then.
  NodeErrorState main;      ///< The main node.
  /** Sub-nodes 0 to #subCount - 1: those before #position, up to the first that didn't
      answer (NodeErrorState::read false; the nodes behind it aren't read). */
  NodeErrorState subs[kMaxSubNodes];
  uint8_t subCount = 0;  ///< Entries of #subs filled in.
};

}  // namespace a2b

#endif  // A2BCPP_A2B_ERROR_REPORT_H_
