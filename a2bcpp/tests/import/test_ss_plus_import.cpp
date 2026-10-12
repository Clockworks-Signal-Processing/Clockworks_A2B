/**
 * @file test_ss_plus_import.cpp
 * @brief Tests of ImportSsPlus() against the Pi's SigmaStudio+ export
 *        (adi/exports/rp2040/12_a2b_busconfig.c, linked in) and the G13 golden trace.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdio>

#include "detail/regdefs.h"
#include "ss_plus_import.h"
#include "test_harness.h"

extern "C" {
extern ADI_A2B_BCD sBusDescription;  // From the export.
}

namespace a2b {
namespace {

struct RegValue {
  uint16_t reg;
  uint8_t value;
};

/**
 * The main-node configuration ADI's stack wrote in the G13 golden
 * (tests/golden/G13_pi_run1.txt, events 55-180: a2b_dscvryNodeComplete, then the
 * interrupt masks, PLLCTL, SLOTFMT and DATCTL), in its order, first value of each.
 * DNSLOTS is 0 there because ADI's PnP layer zeroed it until routing ran
 * (a2bpnp_calc.c:1374); routing then wrote the export's 0x0C.
 */
constexpr RegValue kG13MainNode[] = {
    {0x00D, 0x00},  // DNSLOTS
    {0x00E, 0x00},  // UPSLOTS
    {0x03F, 0x00},  // I2CCFG
    {0x046, 0x00},  // SYNCOFFSET
    {0x044, 0x00},  // I2STXOFFSET
    {0x045, 0x00},  // I2SRXOFFSET
    {0x041, 0xE2},  // I2SGCFG
    {0x042, 0x21},  // I2SCFG
    {0x043, 0x00},  // I2SRATE
    {0x047, 0x00},  // PDMCTL
    {0x05D, 0x00},  // PDMCTL2
    {0x048, 0x00},  // ERRMGMT
    {0x04A, 0x80},  // GPIODAT
    {0x04D, 0x80},  // GPIOOEN
    {0x04E, 0x00},  // GPIOIEN
    {0x050, 0x00},  // PINTEN
    {0x051, 0x00},  // PINTINV
    {0x052, 0x01},  // PINCFG
    {0x059, 0x00},  // CLK1CFG
    {0x05A, 0x81},  // CLK2CFG
    {0x081, 0x00},  // GPIOD0MSK
    {0x082, 0x00},  // GPIOD1MSK
    {0x083, 0x00},  // GPIOD2MSK
    {0x084, 0x00},  // GPIOD3MSK
    {0x085, 0x00},  // GPIOD4MSK
    {0x086, 0x00},  // GPIOD5MSK
    {0x087, 0x00},  // GPIOD6MSK
    {0x088, 0x00},  // GPIOD7MSK
    {0x089, 0x00},  // GPIODDAT
    {0x08A, 0x00},  // GPIODINV
    {0x080, 0x00},  // GPIODEN
    {0x0B8, 0xFF},  // RXMASK0
    {0x0B9, 0xFF},  // RXMASK1
    {0x0BA, 0xFF},  // RXMASK2
    {0x0BB, 0xFF},  // RXMASK3
    {0x0BC, 0xFF},  // RXMASK4
    {0x0BD, 0xFF},  // RXMASK5
    {0x0BE, 0xFF},  // RXMASK6
    {0x0BF, 0xFF},  // RXMASK7
    {0x0C0, 0x00},  // TXXBAR0
    {0x0C1, 0x01},  // TXXBAR1
    {0x0C2, 0x02},  // TXXBAR2
    {0x0C3, 0x03},  // TXXBAR3
    {0x0C4, 0x04},  // TXXBAR4
    {0x0C5, 0x05},  // TXXBAR5
    {0x0C6, 0x06},  // TXXBAR6
    {0x0C7, 0x07},  // TXXBAR7
    {0x0C8, 0x08},  // TXXBAR8
    {0x0C9, 0x09},  // TXXBAR9
    {0x0CA, 0x0A},  // TXXBAR10
    {0x0CB, 0x0B},  // TXXBAR11
    {0x0CC, 0x0C},  // TXXBAR12
    {0x0CD, 0x0D},  // TXXBAR13
    {0x0CE, 0x0E},  // TXXBAR14
    {0x0CF, 0x0F},  // TXXBAR15
    {0x0D0, 0x10},  // TXXBAR16
    {0x0D1, 0x11},  // TXXBAR17
    {0x0D2, 0x12},  // TXXBAR18
    {0x0D3, 0x13},  // TXXBAR19
    {0x0D4, 0x14},  // TXXBAR20
    {0x0D5, 0x15},  // TXXBAR21
    {0x0D6, 0x16},  // TXXBAR22
    {0x0D7, 0x17},  // TXXBAR23
    {0x0D8, 0x18},  // TXXBAR24
    {0x0D9, 0x19},  // TXXBAR25
    {0x0DA, 0x1A},  // TXXBAR26
    {0x0DB, 0x1B},  // TXXBAR27
    {0x0DC, 0x1C},  // TXXBAR28
    {0x0DD, 0x1D},  // TXXBAR29
    {0x0DE, 0x1E},  // TXXBAR30
    {0x0DF, 0x1F},  // TXXBAR31
    {0x0B0, 0x00},  // SPICFG
    {0x0B2, 0x00},  // SPICKDIV
    {0x0B3, 0x00},  // SPIFDSIZE
    {0x0B4, 0x00},  // SPIFDTARG
    {0x0B5, 0x10},  // SPIPINCFG
    {0x0B6, 0x00},  // SPIINT
    {0x0B7, 0x00},  // SPIMSK
    {0x07C, 0x00},  // DTCFG
    {0x07D, 0x00},  // DTSLOTS
    {0x07E, 0x00},  // DTDNOFFS
    {0x07F, 0x00},  // DTUPOFFS
    {0x140, 0x00},  // PWMCFG
    {0x141, 0x00},  // PWMFREQ
    {0x142, 0x00},  // PWMBLINK1
    {0x143, 0x00},  // PWMBLINK2
    {0x148, 0x00},  // PWM1VALL
    {0x149, 0x00},  // PWM1VALH
    {0x14A, 0x00},  // PWM2VALL
    {0x14B, 0x00},  // PWM2VALH
    {0x14C, 0x00},  // PWM3VALL
    {0x14D, 0x00},  // PWM3VALH
    {0x14E, 0x00},  // PWMOEVALL
    {0x14F, 0x00},  // PWMOEVALH
    {0x100, 0x00},  // VMTR_VEN
    {0x101, 0x00},  // VMTR_INTEN
    {0x102, 0x00},  // VMTR_MXSTAT
    {0x103, 0x00},  // VMTR_MNSTAT
    {0x121, 0xFF},  // VMTR_VMAX0
    {0x122, 0x00},  // VMTR_VMIN0
    {0x124, 0xFF},  // VMTR_VMAX1
    {0x125, 0x00},  // VMTR_VMIN1
    {0x127, 0xFF},  // VMTR_VMAX2
    {0x128, 0x00},  // VMTR_VMIN2
    {0x12A, 0xFF},  // VMTR_VMAX3
    {0x12B, 0x00},  // VMTR_VMIN3
    {0x12D, 0xFF},  // VMTR_VMAX4
    {0x12E, 0x00},  // VMTR_VMIN4
    {0x130, 0x00},  // VMTR_VMAX5
    {0x131, 0x00},  // VMTR_VMIN5
    {0x133, 0xFF},  // VMTR_VMAX6
    {0x134, 0x00},  // VMTR_VMIN6
    {0x057, 0x00},  // I2SRRCTL
    {0x02E, 0x00},  // TXACTL
    {0x01B, 0x10},  // INTMSK0
    {0x01C, 0x00},  // INTMSK1
    {0x01D, 0x09},  // INTMSK2
    {0x01E, 0xEF},  // BECCTL
    {0x040, 0x00},  // PLLCTL
    {0x010, 0x44},  // SLOTFMT
    {0x011, 0x03},  // DATCTL
};

/** The image the Pi's export gives. */
BoardConfig ImportPi() {
  BoardConfig board;
  FixedOpList<4> script;
  A2B_CHECK(ImportSsPlus(sBusDescription, 0, board, script) == Result::kOk);
  A2B_CHECK_EQ(script.Size(), 0u);  // The Pi's export has no host peripherals.
  board.hostScript = nullptr;
  return board;
}

A2B_TEST(MainNodeImageMatchesG13) {
  const BoardConfig board = ImportPi();
  const RegisterImage& image = board.mainNode;
  const size_t count = sizeof(kG13MainNode) / sizeof(kG13MainNode[0]);
  A2B_CHECK(image.Size() >= count);
  for (size_t i = 0; i < count && i < image.Size(); ++i) {
    A2B_CHECK_EQ(image.RegAt(i), kG13MainNode[i].reg);
    const uint8_t expected = (kG13MainNode[i].reg == reg::kDnslots) ? 0x0C : kG13MainNode[i].value;
    if (image.ValueAt(i) != expected) {
      std::printf("  register 0x%03X\n", image.RegAt(i));
    }
    A2B_CHECK_EQ(image.ValueAt(i), expected);
  }
}

A2B_TEST(DiscoveryRegistersComeLast) {
  // The export's values; the stack adds the CONTROL command bits and ENSW itself. In
  // G13 ADI's stack wrote them as RESPCYCS 0xB9, CONTROL 0x91 (MSTR | XCVRBINV |
  // NEWSTRCT) and SWCTL 0x11.
  const BoardConfig board = ImportPi();
  const RegisterImage& image = board.mainNode;
  const RegValue tail[] = {{reg::kRespcycs, 0xB9}, {reg::kControl, 0x10}, {reg::kSwctl, 0x11}};
  const size_t start = sizeof(kG13MainNode) / sizeof(kG13MainNode[0]);
  A2B_CHECK_EQ(image.Size(), start + 3);
  for (size_t i = 0; i < 3 && start + i < image.Size(); ++i) {
    A2B_CHECK_EQ(image.RegAt(start + i), tail[i].reg);
    A2B_CHECK_EQ(image.ValueAt(start + i), tail[i].value);
  }
}

A2B_TEST(BoardSettings) {
  const BoardConfig board = ImportPi();
  A2B_CHECK_EQ(board.mainAddr, 0x68);
  A2B_CHECK_EQ(board.busAddr, 0x69);
  A2B_CHECK_EQ(board.mainProduct, 0x37);
  A2B_CHECK(board.mainLocalPower);
  A2B_CHECK_EQ(board.highPowerSwitchCfg, 4);
  A2B_CHECK_EQ(board.timings.discoveryStartDelayMs, 25u);
  A2B_CHECK_EQ(board.timings.rediscoveryWaitMs, 250u);
  A2B_CHECK_EQ(board.policy.maxSubNodes, 15);  // Not the import's business.
  // The Pi's export sets CONTROL.XCVRBINV, so its default link is UTP with the B side
  // inverted (G13 has CONTROL 0x91 on the main node and 0x10 on the sub-nodes).
  A2B_CHECK(board.defaultLink.discovery == LinkProfile::Discovery::kSingleStep);
  A2B_CHECK(board.defaultLink.power == LinkProfile::Power::kOnA2bPair);
  A2B_CHECK(board.defaultLink.bSideInverted);
}

A2B_TEST(Ad2428MainNode) {
  // The same settings on an AD2428: no AD243x blocks; the LVDS registers instead.
  ADI_A2B_MASTER_NCD main = *sBusDescription.apNetworkconfig[0]->pMasterConfig;
  main.sAuthSettings.nProductID = 0x28;
  main.sRegSettings.nTXACTL = 0x81;
  main.sRegSettings.nTXBCTL = 0x82;
  main.sI2SSettings.bSerialRxOnDTx1 = A2B_ENABLED;
  ADI_A2B_MASTER_SLAVE_CONFIG chain = *sBusDescription.apNetworkconfig[0];
  chain.pMasterConfig = &main;
  ADI_A2B_BCD bcd = sBusDescription;
  bcd.apNetworkconfig[0] = &chain;

  BoardConfig board;
  FixedOpList<4> script;
  A2B_CHECK(ImportSsPlus(bcd, 0, board, script) == Result::kOk);
  const RegisterImage& image = board.mainNode;
  A2B_CHECK_EQ(board.mainProduct, 0x28);
  A2B_CHECK(!image.Has(reg::kTxxbar0));
  A2B_CHECK(!image.Has(reg::kRxmask0));
  A2B_CHECK(!image.Has(reg::kSpicfg));
  A2B_CHECK(!image.Has(reg::kPwmcfg));
  A2B_CHECK(!image.Has(reg::kVmtrVen));
  uint8_t value = 0;
  A2B_CHECK(image.Get(reg::kTxactl, value) && value == 0x81);
  A2B_CHECK(image.Get(reg::kTxbctl, value) && value == 0x82);
  // RXONDTX1 on AD242x; the AD243x pin counts in I2SCFG are not applied.
  A2B_CHECK(image.Get(reg::kI2sgcfg, value) && value == (0xE2 | reg::kI2sgcfgRxondtx1));
  A2B_CHECK(image.Get(reg::kI2scfg, value) && value == 0x00);
  A2B_CHECK(image.Get(reg::kGpiodat, value) && value == 0x80);  // GPIO7 output, high.
  A2B_CHECK_EQ(image.Size(), 44u);
}

A2B_TEST(Ad243xPinSettings) {
  // GPIO0 as an output uses GPIO0's value (ADI's code read GPIO1's), SYNC disabled, and
  // interleaved RX pins.
  ADI_A2B_MASTER_NCD main = *sBusDescription.apNetworkconfig[0]->pMasterConfig;
  main.sGPIOSettings.sPinMuxSettings.bGPIO0PinUsage = A2B_GPIO_0_OUTPUT;
  main.sGPIOSettings.sOutPinVal.bGPIO0Val = A2B_HIGH;
  main.sGPIOSettings.sOutPinVal.bGPIO1Val = A2B_LOW;
  main.sI2SSettings.bSync = A2B_DISABLED;
  main.sI2SSettings.bRXInterleave = A2B_ENABLED;
  ADI_A2B_MASTER_SLAVE_CONFIG chain = *sBusDescription.apNetworkconfig[0];
  chain.pMasterConfig = &main;
  ADI_A2B_BCD bcd = sBusDescription;
  bcd.apNetworkconfig[0] = &chain;

  BoardConfig board;
  FixedOpList<4> script;
  A2B_CHECK(ImportSsPlus(bcd, 0, board, script) == Result::kOk);
  uint8_t value = 0;
  A2B_CHECK(board.mainNode.Get(reg::kGpiooen, value) && value == 0x81);
  A2B_CHECK(board.mainNode.Get(reg::kGpiodat, value) && value == 0x81);
  A2B_CHECK(board.mainNode.Get(reg::kI2sgcfg, value) && value == (0xE2 | reg::kI2sgcfgSyncdis));
  // RXPINS = 7 (two-pin interleave; it covers the AD242x RX2PINTL bit), TXPINS = 1.
  A2B_CHECK(board.mainNode.Get(reg::kI2scfg, value) && value == 0x71);
}

A2B_TEST(Refusals) {
  BoardConfig board;
  FixedOpList<4> script;
  A2B_CHECK(ImportSsPlus(sBusDescription, 1, board, script) == Result::kInvalidArgument);

  ADI_A2B_MASTER_NCD main = *sBusDescription.apNetworkconfig[0]->pMasterConfig;
  ADI_A2B_MASTER_SLAVE_CONFIG chain = *sBusDescription.apNetworkconfig[0];
  chain.pMasterConfig = &main;
  ADI_A2B_BCD bcd = sBusDescription;
  bcd.apNetworkconfig[0] = &chain;
  main.sAuthSettings.nProductID = 0x38;  // AD2438: not supported.
  A2B_CHECK(ImportSsPlus(bcd, 0, board, script) == Result::kNotSupported);
  main.sAuthSettings.nProductID = 0x37;
  chain.sCommonSetting.eA2bDeviceInterface = SPI;  // SPI host control: not supported yet.
  A2B_CHECK(ImportSsPlus(bcd, 0, board, script) == Result::kNotSupported);
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
