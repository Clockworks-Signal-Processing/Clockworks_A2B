/**
 * @file main_rp2040.cpp
 * @brief The minimal example on the RP2040, with the AB0310 board's wiring (i2c0 on
 *        GPIO16/17, the main node's RESETn on GP2) and its UART console.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdio>

#include "a2b_build_stamp.h"
#include "minimal_app.h"
#include "pico/stdlib.h"
#include "rp2040_platform.h"

extern "C" {
extern ADI_A2B_BCD sBusDescription;  ///< From the SigmaStudio+ export linked in.
}

namespace {

constexpr unsigned kSdaPin = 16;   ///< The board's SDA.
constexpr unsigned kSclPin = 17;   ///< The board's SCL.
constexpr unsigned kResetPin = 2;  ///< The main node's RESETn.
/** Give sub-nodes the app's module info instead of reading their EEPROMs. */
constexpr bool kIgnoreEeprom = false;

}  // namespace

/**
 * Sets up stdio and I2C, starts the A2B bus and polls it every millisecond.
 * @return Never returns.
 */
int main() {
  stdio_init_all();
  std::printf("a2b_minimal built %s\n", A2B_BUILD_STAMP);
  static a2b::Rp2040Platform platform(i2c0, kSdaPin, kSclPin);
  platform.SetResetPin(kResetPin);
  platform.Init();
  static MinimalApp app(platform, kIgnoreEeprom);
  if (app.Setup(sBusDescription) != a2b::Result::kOk) {
    std::printf("Set-up failed\n");
  }
  for (;;) {
    app.Loop();
    sleep_ms(1);
  }
}
