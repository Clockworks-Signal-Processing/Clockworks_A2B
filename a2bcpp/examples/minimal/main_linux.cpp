/**
 * @file main_linux.cpp
 * @brief The minimal example on Linux (the Raspberry Pi):
 *        `a2b_minimal [--ignore-eeprom] [--trace FILE] [--utp N]... [/dev/i2c-N]`.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "a2b_build_stamp.h"
#include "linux_platform.h"
#include "minimal_app.h"

extern "C" {
extern ADI_A2B_BCD sBusDescription;  ///< From the SigmaStudio+ export linked in.
}

/**
 * Opens the I2C bus, starts the A2B bus and polls it every millisecond until killed.
 * @param argc Argument count.
 * @param argv `--ignore-eeprom` to give sub-nodes the app's module info instead of reading
 *        their EEPROMs; `--trace FILE` to write every I2C transfer to FILE (I2CT lines,
 *        as ADI's stack's traces, for a2bcpp/tools/trace_decode.py); `--utp N` (as
 *        often as needed) to make the link into sub-node N a UTP link, B side not
 *        inverted, where the others keep the export's; and the I2C device, /dev/i2c-1 if
 *        none is given.
 * @return 1 if it couldn't start.
 */
int main(int argc, char** argv) {
  std::printf("a2b_minimal built %s\n", A2B_BUILD_STAMP);
  const char* device = "/dev/i2c-1";
  bool ignoreEeprom = false;
  const char* trace = nullptr;
  int utpLinks[a2b::kMaxSubNodes];
  int utpCount = 0;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--ignore-eeprom") == 0) {
      ignoreEeprom = true;
    } else if (std::strcmp(argv[i], "--trace") == 0 && i + 1 < argc) {
      trace = argv[++i];
    } else if (std::strcmp(argv[i], "--utp") == 0 && i + 1 < argc) {
      const int position = std::atoi(argv[++i]);
      if (position < 0 || position >= a2b::kMaxSubNodes || utpCount >= a2b::kMaxSubNodes) {
        std::fprintf(stderr, "--utp takes a sub-node position, 0-15\n");
        return 1;
      }
      utpLinks[utpCount++] = position;
    } else {
      device = argv[i];
    }
  }
  static a2b::LinuxPlatform platform;
  if (platform.Open(device) != a2b::Result::kOk) {
    std::fprintf(stderr, "Cannot open %s (is I2C enabled, and may this user use it?)\n", device);
    return 1;
  }
  if (trace != nullptr) {
    std::FILE* out = std::fopen(trace, "w");
    if (out == nullptr) {
      std::fprintf(stderr, "Cannot write %s\n", trace);
      return 1;
    }
    platform.SetTrace(out);  // Left open until the program is killed; each line is flushed.
    std::printf("Tracing I2C to %s\n", trace);
  }
  std::printf("Module info from %s\n", ignoreEeprom ? "the app (EEPROMs ignored)" : "the EEPROMs");
  static MinimalApp app(platform, ignoreEeprom);
  for (int i = 0; i < utpCount; ++i) {
    app.SetUtpLink(static_cast<a2b::NodeAddr>(utpLinks[i]));
    std::printf("Link into sub-node %d: UTP (B side not inverted)\n", utpLinks[i]);
  }
  if (app.Setup(sBusDescription) != a2b::Result::kOk) {
    return 1;
  }
  for (;;) {
    app.Loop();
    std::fflush(stdout);
    usleep(1000);
  }
}
