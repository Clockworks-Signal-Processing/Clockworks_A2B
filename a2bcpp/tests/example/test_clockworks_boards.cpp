/**
 * @file test_clockworks_boards.cpp
 * @brief The bench console's Clockworks board profiles (examples/clockworks/clockworks_boards.cpp):
 *        each profile's settings against its SigmaStudio+ export's sub-node, so the code
 *        can't drift from SS+, and the image built from a profile, read back by the
 *        stack's own parsers.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "a2b/module_info.h"
#include "clockworks_boards.h"
#include "ss_plus_import.h"
#include "test_harness.h"

extern "C" {
/// The AB0020 bench project's export, compiled into the console (board_ab0020.c).
extern ADI_A2B_BCD a2b_bench_board_ab0020;
/// The AB0310 and AB0312 project's export, compiled into the console (board_ab0310.c).
extern ADI_A2B_BCD a2b_bench_board_ab0310;
}

namespace {

/** One bit from an export flag, as ss_plus_import.cpp's Bit(). */
uint8_t Bit(unsigned flag, unsigned pos) { return static_cast<uint8_t>((flag & 1u) << pos); }

/** True if @p actual is @p expected; prints both if not. */
bool Is(const char* what, unsigned actual, unsigned expected) {
  if (actual != expected) {
    std::printf("  %s: profile 0x%02X, export 0x%02X\n", what, expected, actual);
    return false;
  }
  return true;
}

/**
 * Checks a profile's settings against an export's sub-node, converted with
 * ss_plus_import.cpp's formulas for the main node (I2S, pins, CLKOUT, SPI, GPIO).
 */
void CheckSettings(const ADI_A2B_SLAVE_NCD& sub, const clockworks::ModuleSettings& s) {
  const auto& i2s = sub.sI2SSettings;
  A2B_CHECK(Is("I2SGCFG",
               Bit(i2s.bEarlySync, 6) | i2s.nTDMMode | i2s.nTDMChSize | i2s.nSyncMode |
                   Bit(i2s.nSyncPolarity, 7),
               s.i2sgcfg));
  const unsigned rxPins = (sub.oPinAssignSettings.oSio0.eFunc == FUNC_RX0) ? 1 : 0;
  const unsigned txPins = (sub.oPinAssignSettings.oSio4.eFunc == FUNC_TX0) ? 1 : 0;
  A2B_CHECK(Is("I2SCFG",
               Bit(i2s.nBclkRxPolarity, 7) | Bit(i2s.nBclkTxPolarity, 3) | (rxPins << 4) | txPins,
               s.i2scfg));
  const auto& clk = sub.sClkOutSettings;
  const unsigned clk2Enabled = (sub.oPinAssignSettings.oADR2.eFunc == FUNC_I2C_CLKOUT2) ? 1 : 0;
  A2B_CHECK(Is("CLK2CFG",
               Bit(clk2Enabled, 7) | clk.bClk2Div | (clk.bClk2PreDiv << 5) | (clk.bClk2Inv << 6),
               s.clk2cfg));
  A2B_CHECK(Is("SPICFG.SPIMODE", sub.oSpiSettings.nSPIMode, s.spicfg & 0x03u));
  const auto& gpio = sub.sGPIOSettings;
  A2B_CHECK(Is("PINCFG",
               Bit(gpio.bHighDriveStrength, 0) | Bit(gpio.bIRQInv, 4) | Bit(gpio.bIRQTriState, 5),
               s.pincfg));
  const bool io7Out = (gpio.sPinMuxSettings.bGPIO7PinUsage == A2B_GPIO_7_OUTPUT);
  const bool io7In = (gpio.sPinMuxSettings.bGPIO7PinUsage == A2B_GPIO_7_INPUT);
  A2B_CHECK(Is("GPIOOEN", Bit(io7Out ? 1 : 0, 7), s.gpiooen));
  A2B_CHECK(Is("GPIODAT", Bit(io7Out ? gpio.sOutPinVal.bGPIO7Val : 0, 7), s.gpiodat));
  A2B_CHECK(Is("GPIOIEN.IO7", Bit(io7In ? 1 : 0, 7), s.gpioien & 0x80u));
}

A2B_TEST(Ab0020MatchesItsExport) {
  // The bench project's sub-node 0 (an AB0020 with an AD2437).
  const ADI_A2B_SLAVE_NCD& sub = *a2b_bench_board_ab0020.apNetworkconfig[0]->apSlaveConfig[0];
  const clockworks::ModuleProfile* board = clockworks::FindBoard("AB0020");
  A2B_CHECK(board != nullptr);
  if (board == nullptr) {
    return;
  }
  CheckSettings(sub, board->settings);
  // Its codec: the export's sub-node peripheral is the same program as the built-in XML.
  SigmaProgram program;
  std::string error;
  A2B_CHECK(clockworks::DefaultProgram(*board, program, error));
  const A2B_PERIPHERAL_DEVICE_CONFIG& peri = *sub.apPeriConfig[0];
  size_t writes = 0;
  bool same = true;
  for (size_t u = 0; u < peri.nNumPeriConfigUnit; ++u) {
    const ADI_A2B_PERI_CONFIG_UNIT& unit = peri.paPeriConfigUnit[u];
    if (unit.eOpCode != A2B_WRITE_OP) {
      continue;
    }
    while (writes < program.actions.size() && program.actions[writes].wait) {
      ++writes;
    }
    if (writes >= program.actions.size()) {
      same = false;
      break;
    }
    const std::vector<uint8_t>& bytes = program.actions[writes].bytes;
    same = same && bytes.size() == unit.nAddrWidth + unit.nDataCount &&
           bytes[0] == static_cast<uint8_t>(unit.nAddr >> 8) &&
           bytes[1] == static_cast<uint8_t>(unit.nAddr) &&
           std::memcmp(&bytes[2], unit.paConfigData, unit.nDataCount) == 0;
    ++writes;
  }
  A2B_CHECK(same);
}

A2B_TEST(Ab0312MatchesItsExport) {
  // The AB0310 and AB0312 project's sub-node 0 (an AB0312 with an AD2437): the settings, no
  // codec, and the export's sub-node peripherals (its dummies) with nothing to program.
  const ADI_A2B_SLAVE_NCD& sub = *a2b_bench_board_ab0310.apNetworkconfig[0]->apSlaveConfig[0];
  const clockworks::ModuleProfile* board = clockworks::FindBoard("AB0312");
  A2B_CHECK(board != nullptr);
  if (board == nullptr) {
    return;
  }
  CheckSettings(sub, board->settings);  // IO7 an input: the AB0312's power-good.
  A2B_CHECK(board->codecName == nullptr);
  for (size_t i = 0; i < sub.nNumPeriDevice; ++i) {
    A2B_CHECK(sub.apPeriConfig[i] == nullptr || sub.apPeriConfig[i]->nNumPeriConfigUnit == 0);
  }
  SigmaProgram program;
  std::string error;
  A2B_CHECK(clockworks::DefaultProgram(*board, program, error));
  A2B_CHECK(program.actions.empty());
  // Its image: the same settings and groups as the AB0020's, no peripheral block.
  const std::vector<uint8_t> image = clockworks::MakeModuleImage(*board, 0x37, program);
  a2b::ModuleInfo info;
  A2B_CHECK(a2b::ParseModuleInfo(image.data(), image.size(), info) == a2b::Result::kOk);
  A2B_CHECK_EQ(info.powerConfig, 4);
  A2B_CHECK(info.localPower);  // By default; the board can be set for bus power.
  A2B_CHECK_EQ(info.peripheralBlocks, 0);
}

A2B_TEST(BoardNodeMakesIo7AnInputButOnAnAb0020) {
  // An image that drives IO7 (as the AB0312's once did): an input on any board but the AB0020.
  for (uint16_t product : {0x0312, 0x0020, 0x0204}) {
    a2b::SubNodeInfo info;
    info.moduleInfo.vendorId = clockworks::kClockworksModuleVendor;
    info.moduleInfo.productId = product;
    a2b::LinkProfile boardLink;
    a2b::SubNode::Context context;
    context.info = &info;
    context.boardLink = &boardLink;
    clockworks::BoardNode node(context);
    a2b::RegisterImage image;
    A2B_CHECK(image.Set(0x4A, 0x81) == a2b::Result::kOk);  // GPIODAT: IO7 and IO0 high.
    A2B_CHECK(image.Set(0x4D, 0x81) == a2b::Result::kOk);  // GPIOOEN: both driven.
    node.TweakRegisters(image);
    uint8_t dat = 0;
    uint8_t oen = 0;
    uint8_t ien = 0;
    A2B_CHECK(image.Get(0x4A, dat) && image.Get(0x4D, oen));
    const bool led = (product == 0x0020);
    A2B_CHECK_EQ(dat, led ? 0x81 : 0x01);
    A2B_CHECK_EQ(oen, led ? 0x81 : 0x01);
    A2B_CHECK_EQ(image.Get(0x4E, ien), !led);
    A2B_CHECK_EQ(ien, led ? 0x00 : 0x80);
  }
  // A factory with UseBoardNodes() makes every node one.
  a2b::NodeFactory factory;
  clockworks::UseBoardNodes(factory);
  A2B_CHECK(factory.Find(a2b::SubNodeInfo()) == a2b::Make<clockworks::BoardNode>);
}

A2B_TEST(GenericModuleReadsBack) {
  // The image for nodes without a board: GPIO0-7 inputs, PINCFG 0, RX 0 and TX 0, no codec,
  // CFG-4 on an AD2437; any chip takes it.
  const clockworks::ModuleProfile generic = clockworks::GenericModule();
  A2B_CHECK(clockworks::Supports(generic, 0x27));
  const std::vector<uint8_t> image = clockworks::MakeModuleImage(generic, 0x37, SigmaProgram());
  a2b::ModuleInfo info;
  A2B_CHECK(a2b::ParseModuleInfo(image.data(), image.size(), info) == a2b::Result::kOk);
  A2B_CHECK_EQ(info.powerConfig, 4);
  A2B_CHECK_EQ(info.peripheralBlocks, 0);
  bool gpioien = false;
  bool pincfg = false;
  bool i2s = false;
  for (size_t i = 0; i < info.regCount; ++i) {
    gpioien = gpioien || (info.reg[i] == 0x4E && info.value[i] == 0xFF);
    pincfg = pincfg || (info.reg[i] == 0x52 && info.value[i] == 0x00);
    i2s = i2s || (info.reg[i] == 0x42 && info.value[i] == 0x11);  // DRX0 and DTX0.
  }
  A2B_CHECK(gpioien && pincfg && i2s);
  a2b::ChannelGroups groups;
  A2B_CHECK(a2b::ParseChannelGroups(&image[a2b::kGroupTableOffset], info.groupTableLen, groups) ==
            a2b::Result::kOk);
  A2B_CHECK(groups.rxCount == 1 && groups.rx[0].channels == 0x3);
  A2B_CHECK(groups.txCount == 1 && groups.tx[0].channels == 0x3);
}

A2B_TEST(ImageReadsBack) {
  // The image built from the AB0020 profile, for an AD2437, read by the stack's parsers:
  // the settings that differ from their power-on values, the groups, one peripheral block.
  const clockworks::ModuleProfile* board = clockworks::FindBoard("AB0020");  // Any case.
  A2B_CHECK(board != nullptr);
  if (board == nullptr) {
    return;
  }
  SigmaProgram program;
  std::string error;
  A2B_CHECK(clockworks::DefaultProgram(*board, program, error));
  const std::vector<uint8_t> image = clockworks::MakeModuleImage(*board, 0x37, program);
  a2b::ModuleInfo info;
  A2B_CHECK(a2b::ParseModuleInfo(image.data(), image.size(), info) == a2b::Result::kOk);
  A2B_CHECK_EQ(info.powerConfig, 4);
  A2B_CHECK(info.localPower);  // By default; the board can be set for bus power.
  A2B_CHECK_EQ(info.peripheralBlocks, 1);
  // The ADAU1761's peripheral block (after the info block's 8 bytes at 0x800): its address,
  // fed by TX group 0, feeding RX group 0, on DRX0 (0x21: one pin, SIO0) and DTX0 (0x30:
  // SIO4), as in ADI's images and the image tool's AB0020.json.
  A2B_CHECK(image.size() > 0x808 + 3 + 74);
  if (image.size() > 0x808 + 3 + 74) {
    const uint8_t* payload = &image[0x808 + 3];
    A2B_CHECK_EQ(payload[65], 0x39);
    const uint8_t pins[] = {0x01, 0x00, 0x01, 0x00, 0x21, 0x00, 0x30, 0x00};
    A2B_CHECK(std::memcmp(&payload[66], pins, sizeof(pins)) == 0);
  }
  // PINCFG 0x01 is its power-on value, so it isn't in the list the stack writes.
  const uint16_t expected[][2] = {{0x41, 0xE0}, {0x42, 0x11}, {0x4A, 0x80},
                                  {0x4D, 0x80}, {0x5A, 0x81}, {0xB0, 0x02}};
  for (const auto& reg : expected) {
    bool found = false;
    for (size_t i = 0; i < info.regCount; ++i) {
      found = found || (info.reg[i] == reg[0] && info.value[i] == reg[1]);
    }
    if (!found) {
      std::printf("  register 0x%02X = 0x%02X not in the image\n", reg[0], reg[1]);
    }
    A2B_CHECK(found);
  }
  a2b::ChannelGroups groups;
  A2B_CHECK(a2b::ParseChannelGroups(&image[a2b::kGroupTableOffset], info.groupTableLen, groups) ==
            a2b::Result::kOk);
  A2B_CHECK(groups.rxCount == 1 && groups.rx[0].id == 0 && groups.rx[0].channels == 0x3);
  A2B_CHECK(groups.txCount == 1 && groups.tx[0].id == 0 && groups.tx[0].channels == 0x3);
  // Not made with an AD2427; an AD2433 has CFG-0.
  A2B_CHECK(!clockworks::Supports(*board, 0x27));
  A2B_CHECK(clockworks::Supports(*board, 0x28));
  const std::vector<uint8_t> ad2433 = clockworks::MakeModuleImage(*board, 0x33, program);
  A2B_CHECK(a2b::ParseModuleInfo(ad2433.data(), ad2433.size(), info) == a2b::Result::kOk);
  A2B_CHECK_EQ(info.powerConfig, 0);
  A2B_CHECK(clockworks::FindBoard("ab0021") == nullptr);
}

A2B_TEST(Ab0204ReadsBack) {
  // The AB0204, an AD2427 with four PDM microphones: PDMCTL 0x1F (both PDM pins, two
  // microphones each, the high-pass filter) and nothing else written, so I2SGCFG stays at
  // TDM2 32-bit and BCLK clocks the microphones at 64 x SYNC; RX 0 all four, RX 1 and RX 2
  // the pairs; no TX group, no codec, CFG-0. It is the AD2427's no-EEPROM default.
  const clockworks::ModuleProfile* board = clockworks::FindBoard("AB0204");
  A2B_CHECK(board != nullptr);
  if (board == nullptr) {
    return;
  }
  A2B_CHECK(clockworks::Supports(*board, 0x27));
  A2B_CHECK(!clockworks::Supports(*board, 0x28) && !clockworks::Supports(*board, 0x37));
  A2B_CHECK(clockworks::NoEepromDefault(0x27) == board);
  for (uint8_t product : {0x28, 0x33, 0x37}) {
    A2B_CHECK(clockworks::NoEepromDefault(product) == nullptr);
  }
  const std::vector<uint8_t> image = clockworks::MakeModuleImage(*board, 0x27, SigmaProgram());
  a2b::ModuleInfo info;
  A2B_CHECK(a2b::ParseModuleInfo(image.data(), image.size(), info) == a2b::Result::kOk);
  A2B_CHECK_EQ(info.powerConfig, 0);
  A2B_CHECK(!info.localPower);  // Bus powered only, as every AD2427 microphone board.
  A2B_CHECK_EQ(info.peripheralBlocks, 0);
  A2B_CHECK_EQ(info.regCount, 1u);
  A2B_CHECK(info.regCount == 1 && info.reg[0] == 0x47 && info.value[0] == 0x1F);
  a2b::ChannelGroups groups;
  A2B_CHECK(a2b::ParseChannelGroups(&image[a2b::kGroupTableOffset], info.groupTableLen, groups) ==
            a2b::Result::kOk);
  A2B_CHECK_EQ(groups.txCount, 0);
  A2B_CHECK(groups.rxCount == 3 && groups.rx[0].channels == 0xF && groups.rx[1].channels == 0x3 &&
            groups.rx[2].channels == 0xC);
}

}  // namespace

A2B_TEST_MAIN()
