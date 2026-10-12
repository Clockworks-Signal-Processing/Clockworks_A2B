/**
 * @file test_module_info.cpp
 * @brief Unit tests for ParseModuleInfo(), against ADI's module-info images in adi/module_info/ and
 *        hand-made ones.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "a2b/module_info.h"
#include "crc8.h"
#include "detail/regdefs.h"
#include "test_harness.h"

namespace a2b {
namespace {

/** Reads a file of the repository, or returns nothing. */
std::vector<uint8_t> ReadRepoFile(const char* path) {
  std::vector<uint8_t> data;
  const std::string full = std::string(A2B_REPO_ROOT) + "/" + path;
  std::FILE* file = std::fopen(full.c_str(), "rb");
  if (file == nullptr) {
    return data;
  }
  uint8_t buf[512];
  size_t got = 0;
  while ((got = std::fread(buf, 1, sizeof(buf), file)) > 0) {
    data.insert(data.end(), buf, buf + got);
  }
  std::fclose(file);
  return data;
}

/** A minimal valid image: marker, primary function address, block type, CRC. */
std::vector<uint8_t> MinimalImage() {
  std::vector<uint8_t> image(kModuleInfoLen, 0x00);
  image[0x00] = 0xAA;
  image[0x0D] = 0x20;
  image[0x20] = 0x30;
  image[0x8E] = 0x01;  // PINCFG at its power-on value.
  image[0x1F] = Crc8(image.data(), 0x1F);
  return image;
}

A2B_TEST(ParsesAdisRj45Image) {
  const std::vector<uint8_t> image =
      ReadRepoFile("adi/module_info/adi_a2b_node_ModuleInfo_1.3.0.0_RJ45.dat");
  A2B_CHECK(image.size() > kModuleInfoLen);
  ModuleInfo info;
  A2B_CHECK(ParseModuleInfo(image.data(), image.size(), info) == Result::kOk);
  A2B_CHECK_EQ(info.vendorId, 0x00AD);
  A2B_CHECK_EQ(info.productId, 0x0037);
  A2B_CHECK_EQ(info.siliconVersion, 0x10);
  A2B_CHECK_EQ(info.powerConfig, 4);
  A2B_CHECK(!info.localPower);
  A2B_CHECK_EQ(info.peripheralBlocks, 3);  // ADAU1701, BD28623, microphones.
  // Its settings, in ADI's write order, power-on values left out: I2SGCFG 0xE0,
  // I2SCFG 0x19, PINCFG 0x81, CLK2CFG 0x81, SPICFG 0x19, SPICKDIV 0x17, SPIPINCFG 0x10.
  const uint16_t regs[] = {reg::kI2sgcfg, reg::kI2scfg,   reg::kPincfg,   reg::kClk2cfg,
                           reg::kSpicfg,  reg::kSpickdiv, reg::kSpipincfg};
  const uint8_t values[] = {0xE0, 0x19, 0x81, 0x81, 0x19, 0x17, 0x10};
  A2B_CHECK_EQ(info.regCount, 7u);
  for (size_t i = 0; i < 7 && i < info.regCount; ++i) {
    A2B_CHECK_EQ(info.reg[i], regs[i]);
    A2B_CHECK_EQ(info.value[i], values[i]);
  }
}

A2B_TEST(ParsesAdisXlrImage) {
  const std::vector<uint8_t> image =
      ReadRepoFile("adi/module_info/adi_a2b_node_ModuleInfo_1.3.0.0_XLR.dat");
  ModuleInfo info;
  A2B_CHECK(ParseModuleInfo(image.data(), image.size(), info) == Result::kOk);
  A2B_CHECK_EQ(info.productId, 0x0037);
  // Its power figures: 5000 and 500 (mW), 200 (mA). The RJ45 image leaves them 0.
  A2B_CHECK_EQ(info.maxPowerMw, 5000);
  A2B_CHECK_EQ(info.minPowerMw, 500);
  A2B_CHECK_EQ(info.maxCurrentMa, 200);
  const std::vector<uint8_t> rj45 =
      ReadRepoFile("adi/module_info/adi_a2b_node_ModuleInfo_1.3.0.0_RJ45.dat");
  A2B_CHECK(ParseModuleInfo(rj45.data(), rj45.size(), info) == Result::kOk);
  A2B_CHECK_EQ(info.maxPowerMw, 0);
  A2B_CHECK_EQ(info.maxCurrentMa, 0);
}

A2B_TEST(AnyModuleIdAndPowerConfigPass) {
  // ADI's check wanted vendor 0xAD, product 0x37 and power configuration 4.
  std::vector<uint8_t> image = MinimalImage();
  image[0x01] = 0x34;
  image[0x02] = 0x12;
  image[0x03] = 0x02;
  image[0x04] = 0x01;
  image[0x23] = 0x11;
  image[0x1F] = Crc8(image.data(), 0x1F);
  ModuleInfo info;
  A2B_CHECK(ParseModuleInfo(image.data(), image.size(), info) == Result::kOk);
  A2B_CHECK_EQ(info.vendorId, 0x1234);
  A2B_CHECK_EQ(info.productId, 0x0102);
  A2B_CHECK_EQ(info.powerConfig, 1);
  A2B_CHECK(info.localPower);
  A2B_CHECK_EQ(info.regCount, 0u);
}

A2B_TEST(PinCfgZeroIsWritten) {
  // PINCFG powers on as 0x01, so 0x00 is a setting (G13's sub-nodes).
  std::vector<uint8_t> image = MinimalImage();
  image[0x8E] = 0x00;
  image[0x8B] = 0x7F;
  ModuleInfo info;
  A2B_CHECK(ParseModuleInfo(image.data(), image.size(), info) == Result::kOk);
  A2B_CHECK_EQ(info.regCount, 2u);
  A2B_CHECK_EQ(info.reg[0], reg::kGpioien);
  A2B_CHECK_EQ(info.value[0], 0x7F);
  A2B_CHECK_EQ(info.reg[1], reg::kPincfg);
  A2B_CHECK_EQ(info.value[1], 0x00);
}

A2B_TEST(I2sRateIsTakenFromTheImage) {
  // 0x85: 0 (1x) is the power-on value and is not written; 5 and 6 (2x, 4x) are, after
  // I2SCFG and before the settings that follow it in ADI's order.
  const uint8_t kRates[] = {0x00, 0x05, 0x06, 0x1D};  // The last has BCLKRATE (bits 5:3) set.
  for (const uint8_t rate : kRates) {
    std::vector<uint8_t> image = MinimalImage();
    image[0x83] = 0x19;
    image[0x85] = rate;
    image[0x86] = 0x20;
    ModuleInfo info;
    A2B_CHECK(ParseModuleInfo(image.data(), image.size(), info) == Result::kOk);
    const size_t expected = (rate == 0) ? 2u : 3u;
    A2B_CHECK_EQ(info.regCount, expected);
    A2B_CHECK_EQ(info.reg[0], reg::kI2scfg);
    if (rate != 0) {
      A2B_CHECK_EQ(info.reg[1], reg::kI2srate);
      A2B_CHECK_EQ(info.value[1], rate);
    }
    A2B_CHECK_EQ(info.reg[expected - 1], reg::kPdmctl);
  }
}

A2B_TEST(Refusals) {
  ModuleInfo info;
  info.productId = 0x5555;
  const std::vector<uint8_t> good = MinimalImage();
  A2B_CHECK(ParseModuleInfo(nullptr, kModuleInfoLen, info) == Result::kInvalidArgument);
  A2B_CHECK(ParseModuleInfo(good.data(), kModuleInfoLen - 1, info) == Result::kInvalidArgument);
  struct Case {
    size_t offset;
    uint8_t value;
    bool reseal;
  };
  const Case kCases[] = {
      {0x00, 0xFF, true},   // No marker (an erased EEPROM).
      {0x05, 0x11, false},  // Header CRC wrong.
      {0x0D, 0x40, true},   // Primary function block elsewhere.
      {0x20, 0x20, false},  // Not a module-info block.
      {0x85, 0x40, false},  // I2SRATE.REDUCE: a reduced rate is out of scope.
      {0x85, 0x80, false},  // I2SRATE.SHARE.
      {0x85, 0x01, false},  // I2SRATE of the superframe rate / 2.
      {0x85, 0x07, false},  // An undefined I2SRATE.
  };
  for (const Case& testCase : kCases) {
    std::vector<uint8_t> image = good;
    image[testCase.offset] = testCase.value;
    if (testCase.reseal) {
      image[0x1F] = Crc8(image.data(), 0x1F);
    }
    A2B_CHECK(ParseModuleInfo(image.data(), image.size(), info) == Result::kInvalidData);
  }
  A2B_CHECK_EQ(info.productId, 0x5555);  // Left alone.
}

// Channel groups.

A2B_TEST(ParsesAdisRj45ImageGroups) {
  // ADI's image: one TX and one RX group, ID 0, channels 0-1 ("ADAU1701"), and a module-info
  // block of 0x12A bytes whose last byte (0x14C) is its CRC.
  const std::vector<uint8_t> image =
      ReadRepoFile("adi/module_info/adi_a2b_node_ModuleInfo_1.3.0.0_RJ45.dat");
  ModuleInfo info;
  A2B_CHECK(ParseModuleInfo(image.data(), image.size(), info) == Result::kOk);
  A2B_CHECK_EQ(info.groupTableLen, 0x14C - 0x100);
  A2B_CHECK_EQ(info.groups.txCount, 0);  // Read when the node is set up, not here.
  ChannelGroups groups;
  A2B_CHECK(ParseChannelGroups(&image[kGroupTableOffset], info.groupTableLen, groups) ==
            Result::kOk);
  A2B_CHECK_EQ(groups.txCount, 1);
  A2B_CHECK_EQ(groups.rxCount, 1);
  A2B_CHECK(groups.FindTx(0) != nullptr && groups.FindTx(0)->channels == 0x3);
  A2B_CHECK(groups.FindRx(0) != nullptr && groups.FindRx(0)->channels == 0x3);
  A2B_CHECK(groups.FindTx(1) == nullptr);
  // The block's CRC-8 covers 0x20 to the byte before it, as for the other blocks.
  A2B_CHECK_EQ(Crc8(&image[0x20], 0x14C - 0x20), image[0x14C]);
}

A2B_TEST(GroupTableLengthFollowsTheBlockSize) {
  std::vector<uint8_t> image = MinimalImage();
  ModuleInfo info;
  A2B_CHECK(ParseModuleInfo(image.data(), image.size(), info) == Result::kOk);
  A2B_CHECK_EQ(info.groupTableLen, 0);  // Size 0: no block past the header.
  image[0x20] = 0x31;                   // The bench modules' block: 0x105 bytes, so up to 0x127.
  image[0x21] = 0x05;
  A2B_CHECK(ParseModuleInfo(image.data(), image.size(), info) == Result::kOk);
  A2B_CHECK_EQ(info.groupTableLen, 39);  // One group: 1 + 37 + 1, the CRC after it.
  image[0x20] = 0x30;                    // A block that ends before 0x100.
  image[0x21] = 0x6E;
  A2B_CHECK(ParseModuleInfo(image.data(), image.size(), info) == Result::kOk);
  A2B_CHECK_EQ(info.groupTableLen, 0);
}

/** A group table: TX groups (ID, channels), then RX groups. */
std::vector<uint8_t> GroupTable(const std::vector<std::pair<uint8_t, uint32_t>>& tx,
                                const std::vector<std::pair<uint8_t, uint32_t>>& rx) {
  std::vector<uint8_t> table;
  for (const auto* list : {&tx, &rx}) {
    table.push_back(static_cast<uint8_t>(list->size()));
    for (const auto& group : *list) {
      std::vector<uint8_t> record(37, 0x00);
      record[0] = group.first;
      record[1] = 'G';
      for (size_t i = 0; i < 4; ++i) {
        record[33 + i] = static_cast<uint8_t>(group.second >> (8 * i));
      }
      table.insert(table.end(), record.begin(), record.end());
    }
  }
  return table;
}

A2B_TEST(ParsesGroupsAndTheirChannels) {
  const std::vector<uint8_t> table = GroupTable({{0, 0x3}, {5, 0xF0000000u}}, {{2, 0x0FFF}});
  ChannelGroups groups;
  A2B_CHECK(ParseChannelGroups(table.data(), table.size(), groups) == Result::kOk);
  A2B_CHECK_EQ(groups.txCount, 2);
  A2B_CHECK_EQ(groups.rxCount, 1);
  A2B_CHECK(groups.FindTx(5) != nullptr && groups.FindTx(5)->channels == 0xF0000000u);
  A2B_CHECK(groups.FindRx(2) != nullptr && groups.FindRx(2)->channels == 0x0FFF);
  A2B_CHECK(groups.FindRx(0) == nullptr);  // TX and RX IDs are apart.
  // Nothing at all is an empty table.
  A2B_CHECK(ParseChannelGroups(nullptr, 0, groups) == Result::kOk);
  A2B_CHECK_EQ(groups.txCount + groups.rxCount, 0);
}

A2B_TEST(RefusesMalformedGroupTables) {
  ChannelGroups groups;
  std::vector<uint8_t> table = GroupTable({{0, 0x3}}, {{0, 0x3}});
  // Records past the end.
  A2B_CHECK(ParseChannelGroups(table.data(), table.size() - 1, groups) == Result::kInvalidData);
  A2B_CHECK_EQ(groups.txCount, 0);
  // No room for the RX count.
  A2B_CHECK(ParseChannelGroups(table.data(), 38, groups) == Result::kInvalidData);
  // A count over the format's 15.
  table[0] = 16;
  A2B_CHECK(ParseChannelGroups(table.data(), table.size(), groups) == Result::kInvalidData);
  A2B_CHECK(ParseChannelGroups(nullptr, 4, groups) == Result::kInvalidArgument);
}

A2B_TEST(KeepsTheFirstEightGroups) {
  std::vector<std::pair<uint8_t, uint32_t>> many;
  for (uint8_t id = 0; id < 10; ++id) {
    many.emplace_back(id, 1u << id);
  }
  const std::vector<uint8_t> table = GroupTable(many, {{9, 0x1}});
  ChannelGroups groups;
  A2B_CHECK(ParseChannelGroups(table.data(), table.size(), groups) == Result::kNoSpace);
  A2B_CHECK_EQ(groups.txCount, ChannelGroups::kMaxGroups);
  A2B_CHECK(groups.FindTx(7) != nullptr && groups.FindTx(8) == nullptr);
  A2B_CHECK(groups.FindRx(9) != nullptr);  // The RX groups still come.
}

A2B_TEST(GroupsAreAddedAndReplaced) {
  ChannelGroups groups;
  A2B_CHECK(groups.AddTx(3, 0x3) == Result::kOk);
  A2B_CHECK(groups.AddTx(3, 0xC) == Result::kOk);  // Replaced, not added.
  A2B_CHECK_EQ(groups.txCount, 1);
  A2B_CHECK(groups.FindTx(3) != nullptr && groups.FindTx(3)->channels == 0xC);
  for (uint8_t id = 0; id < ChannelGroups::kMaxGroups; ++id) {
    A2B_CHECK(groups.AddRx(id, 1) == Result::kOk);
  }
  A2B_CHECK(groups.AddRx(100, 1) == Result::kNoSpace);
  A2B_CHECK(groups.AddRx(0, 2) == Result::kOk);  // An existing one can still change.
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
