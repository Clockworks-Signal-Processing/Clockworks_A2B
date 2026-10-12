/**
 * @file sim_steps.h
 * @brief Steps the simulator tests share: bring the main node up and discover sub-nodes
 *        through Transport, the way ADI's stack does it.
 *
 * These are test scaffolding, not the stack: Network has the real sequences.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_TESTS_SCENARIO_SIM_STEPS_H_
#define A2BCPP_TESTS_SCENARIO_SIM_STEPS_H_

#include <cstdint>

#include "detail/regdefs.h"
#include "sim/simulator.h"
#include "transport.h"

namespace a2b {
namespace test {

/** The wait between the soft reset and MSTR (nRediscWaitTime, at least 25 ms). */
inline constexpr uint32_t kResetWaitMs = 25;

/** Response cycles the bench used for the first sub-node. */
inline constexpr uint8_t kBenchRespCycles = 0xB9;

/**
 * DISCVRY of each position with the main node's RESPCYCS at kBenchRespCycles and 4 m
 * cables: the TRMs' SLV_RESPCYCS[n] = ((185 x 4 + 7) - (n x 7.5 + n x 8.3 + 2 x 6 x 4n /
 * 20.345 + 7)) / 4, rounded (AD2431-35 TRM Rev 1.2, Formula B), worked out with floating
 * point apart from the stack: 185, 180.46, 175.92, 171.38 and so on, 4.54 less a position.
 * ADI's PnP layer wrote 4 less per position.
 */
inline constexpr uint8_t kBenchDiscvry[16] = {0xB9, 0xB4, 0xB0, 0xAB, 0xA7, 0xA2, 0x9E, 0x99,
                                              0x95, 0x90, 0x8C, 0x87, 0x83, 0x7E, 0x79, 0x75};

/** Reads INTSRC and, if it shows an interrupt, INTTYPE. Returns the type, or -1 for none. */
inline int ReadInterrupt(Transport& transport) {
  uint8_t source = 0;
  if (transport.ReadReg(kMainNode, reg::kIntsrc, source) != Result::kOk || source == 0) {
    return -1;
  }
  uint8_t type = 0;
  if (transport.ReadReg(kMainNode, reg::kInttype, type) != Result::kOk) {
    return -1;
  }
  return type;
}

/**
 * Soft reset, wait, MSTR (with @p controlBits), then wait for MSTR_RUNNING.
 * @return True if MSTR_RUNNING came.
 */
inline bool StartMainNode(sim::Simulator& sim, Transport& transport, uint8_t controlBits = 0) {
  transport.WriteReg(kMainNode, reg::kControl, reg::kControlSoftrst);
  transport.ForgetAll();
  sim.Advance(kResetWaitMs);
  transport.WriteReg(kMainNode, reg::kControl,
                     static_cast<uint8_t>(reg::kControlMstr | controlBits));
  sim.Advance(sim.GetTiming().pllLockMs);
  return ReadInterrupt(transport) == reg::kInttypeMstrRunning;
}

/**
 * Discovers the next sub-node: ENSW on @p upstream, DISCVRY = @p respCycles, then waits
 * for DSCDONE (which needs INTMSK2.DSCDONE, set here).
 * @return True if DSCDONE came.
 */
inline bool DiscoverNext(sim::Simulator& sim, Transport& transport, NodeAddr upstream,
                         uint8_t respCycles = kBenchRespCycles) {
  transport.WriteReg(kMainNode, reg::kIntmsk2, reg::kIntmsk2Dscdone);
  transport.WriteReg(upstream, reg::kSwctl, reg::kSwctlEnsw);
  transport.WriteReg(kMainNode, reg::kDiscvry, respCycles);
  sim.Advance(sim.GetTiming().discoveryMs);
  return ReadInterrupt(transport) == reg::kInttypeDscdone;
}

}  // namespace test
}  // namespace a2b

#endif  // A2BCPP_TESTS_SCENARIO_SIM_STEPS_H_
