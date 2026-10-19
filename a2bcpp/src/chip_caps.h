/**
 * @file chip_caps.h
 * @brief What each supported A2B transceiver can do, by vendor and product ID. Internal.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_CHIP_CAPS_H_
#define A2BCPP_CHIP_CAPS_H_

#include <cstdint>

namespace a2b {

/** @brief ADI's vendor ID, as read from the VENDOR register. */
inline constexpr uint8_t kAdiVendorId = 0xAD;

/** @brief Transceiver family. */
enum class ChipFamily : uint8_t {
  kAd242x,  ///< AD2427, AD2428: TDM slot mapping, no crossbar, no SPI.
  kAd243x,  ///< AD2433, AD2437: crossbar, SPI.
};

/** @brief Capabilities of one transceiver type. Each may be a sub-node; #mainCapable says
 *         whether it may be a main node. */
struct ChipCaps {
  uint8_t productId;       ///< PRODUCT register value.
  const char* name;        ///< Chip name, such as "AD2437".
  ChipFamily family;       ///< Family, for the family-specific operations.
  bool hasSpi;             ///< SPI port (SPI peripherals, SPI host control).
  bool hasCrossbar;        ///< Slot crossbar registers (used by routing).
  bool hasI2sMsInv;        ///< CONTROL.I2SMSINV bit.
  bool hasSpreadSpectrum;  ///< PLL spread-spectrum settings.
  bool hasVoltageMonitor;  ///< VMTR block on MMR page 1 (24 V check in two-step discovery).
  /** MMRPAGE and register page 1 (0x100-0x1FF). The AD242x register map ends at page 0's
      0x9B. */
  bool hasPage1;
  /** SWSTAT2 reports the power configuration the chip detected, and SWCTL2 with
      SWCTL.CFG_DET_OV overrides it. */
  bool hasSwitchConfig;
  /** SWCTL.MODE while the chip's switch powers the next node, from the TRMs' discovery
      flows, as the MODE bits alone: 0x10 (mode 1, SWCTL 0x11 with ENSW) on AD243x, 0x00
      (mode 0, SWCTL 0x01) on AD242x. */
  uint8_t switchMode;
  /** It may be a main node (the data sheets: not the AD2427). Every main node takes up to
      16 sub-nodes. */
  bool mainCapable;
  /** An I2S/TDM port (DTXn, DRXn). The AD2427 has none, only its PDM inputs. */
  bool hasI2s;
  /** Its power configuration, the only one the chip has (the data sheets): 4 (CFG-4) on the
      AD2437, 0 (CFG-0) on the AD2433, and 0 on the AD242x, which have none. A node's image
      gives way to it (ModuleInfo::powerConfig). */
  uint8_t powerConfig;
};

const ChipCaps* LookUpChipCaps(uint8_t vendorId, uint8_t productId);  ///< Table row, or nullptr.

}  // namespace a2b

#endif  // A2BCPP_CHIP_CAPS_H_
