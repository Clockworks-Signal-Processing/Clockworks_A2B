/**
 * @file main_rp2040.cpp
 * @brief The fixed app on the RP2040, set up for the AB0310 and AB0312 bench: the AB0310's
 *        SigmaStudio+ export, an AB0312 at sub-node 0 (no EEPROM), the generic image for
 *        every other sub-node, and the main node's stereo pair routed to the AB0312 and
 *        back. The AB0310 board's wiring (i2c0 on GPIO16/17, the main node's RESETn on
 *        GP2) and its UART console (uart0, 230400 baud).
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstddef>
#include <cstdio>

#include "a2b_build_stamp.h"
#include "fixed_app.h"
#include "pico/stdlib.h"
#include "rp2040_platform.h"

extern "C" {
/// tools/SS+reference/AB0310_AB0312_EVM_with_2437/adi_a2b_busconfig.c, linked in.
extern ADI_A2B_BCD sBusDescription;
size_t FixedHeapInUse(void);  ///< The heap in use (rp2040_heap.c).
}

namespace {

constexpr unsigned kSdaPin = 16;   ///< The board's SDA.
constexpr unsigned kSclPin = 17;   ///< The board's SCL.
constexpr unsigned kResetPin = 2;  ///< The main node's RESETn.

/// The main node's RX group 0 (its DRX0 pair) to the AB0312's TX group 0, and the AB0312's
/// RX group 0 back to the main node's TX group 0.
constexpr FixedConnection kConnections[] = {{a2b::kMainNode, 0, 0, 0}, {0, 0, a2b::kMainNode, 0}};

}  // namespace

/**
 * Sets up stdio and I2C, starts the A2B bus and polls it every millisecond.
 * @return Never returns.
 */
int main() {
  stdio_init_all();
  std::printf("a2b_fixed built %s\n", A2B_BUILD_STAMP);
  std::printf("a2b fixed app: AB0310 main node, AB0312 at sub-node 0\n");
  static a2b::Rp2040Platform platform(i2c0, kSdaPin, kSclPin);
  platform.SetResetPin(kResetPin);
  platform.Init();
  static const clockworks::ModuleProfile kGeneric = clockworks::GenericModule();
  static FixedSetup setup;
  setup.boards[0] = clockworks::FindBoard("ab0312");
  setup.fallback = &kGeneric;            // Every other sub-node, as the console's --ignore-eeprom.
  (void)setup.mainGroups.AddRx(0, 0x3);  // DRX0: TDM channels 0-1.
  (void)setup.mainGroups.AddTx(0, 0x3);  // DTX0: TDM channels 0-1.
  setup.connections = kConnections;
  setup.connectionCount = sizeof(kConnections) / sizeof(kConnections[0]);
  setup.heapInUse = FixedHeapInUse;
  static FixedApp app(platform);
  if (app.Setup(sBusDescription, setup) != a2b::Result::kOk) {
    std::printf("Set-up failed\n");
  }
  for (;;) {
    app.Loop();
    sleep_ms(1);
  }
}
