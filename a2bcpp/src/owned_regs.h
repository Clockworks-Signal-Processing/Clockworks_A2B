/**
 * @file owned_regs.h
 * @brief The registers the stack writes itself, which an app's register image may not set.
 *        Internal.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_OWNED_REGS_H_
#define A2BCPP_OWNED_REGS_H_

#include <cstdint>

#include "detail/regdefs.h"

namespace a2b {

/**
 * @brief True for the routing registers, which the routing plan writes: the
 *        slot registers BCDNSLOTS to UPSLOTS, UPMASK0-3, UPOFFSET, DNMASK0-3, DNOFFSET, and
 *        the AD243x crossbars RXMASK0-7 and TXXBAR0-31. RESPCYCS is the stack's too.
 */
inline bool IsRoutingRegister(uint16_t reg) {
  return (reg >= reg::kBcdnslots && reg <= reg::kUpslots) ||
         (reg >= reg::kUpmask0 && reg <= reg::kDnoffset) ||
         (reg >= reg::kRxmask0 && reg < reg::kTxxbar0 + 32);
}

/**
 * @brief True for registers the stack writes itself: CHIP, NODEADR, SWCTL, RESPCYCS,
 *        CONTROL, DISCVRY, the interrupt status and pending registers, DISCSTAT, MMRPAGE
 *        (on both pages) and the routing registers (IsRoutingRegister()). Register images
 *        skip them when they are written.
 */
inline bool IsOwnedByStack(uint16_t reg) {
  if (IsRoutingRegister(reg)) {
    return true;
  }
  switch (reg) {
    case reg::kChip:
    case reg::kNodeadr:
    case reg::kSwctl:
    case reg::kRespcycs:
    case reg::kControl:
    case reg::kDiscvry:
    case reg::kIntstat:
    case reg::kIntsrc:
    case reg::kInttype:
    case reg::kIntpnd0:
    case reg::kIntpnd1:
    case reg::kIntpnd2:
    case reg::kDiscstat:
    case reg::kMmrpage:
    case reg::kMmrpage + 0x100:
      return true;
    default:
      return false;
  }
}

}  // namespace a2b

#endif  // A2BCPP_OWNED_REGS_H_
