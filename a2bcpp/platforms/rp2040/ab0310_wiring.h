/**
 * @file ab0310_wiring.h
 * @brief The AB0310 board's wiring for the RP2040 examples: the A2B main node on i2c0
 *        (GPIO16 SDA, GPIO17 SCL) at 400 kHz, its RESETn on GP2; the UART console on uart0
 *        (GP0/GP1), the Debug Probe's UART port. Another board changes these.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_PLATFORMS_RP2040_AB0310_WIRING_H_
#define A2BCPP_PLATFORMS_RP2040_AB0310_WIRING_H_

#include "rp2040_platform.h"

namespace a2b {
namespace ab0310 {

inline constexpr unsigned kSdaPin = 16;   ///< The main node's SDA.
inline constexpr unsigned kSclPin = 17;   ///< The main node's SCL.
inline constexpr unsigned kResetPin = 2;  ///< The main node's RESETn.

/** Sets up @p platform (made on i2c0 with #kSdaPin and #kSclPin) with RESETn on #kResetPin. */
inline void SetUp(Rp2040Platform& platform) {
  platform.SetResetPin(kResetPin);
  platform.Init();
}

}  // namespace ab0310
}  // namespace a2b

#endif  // A2BCPP_PLATFORMS_RP2040_AB0310_WIRING_H_
