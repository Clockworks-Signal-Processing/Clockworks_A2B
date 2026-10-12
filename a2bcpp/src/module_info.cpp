/**
 * @file module_info.cpp
 * @brief ParseModuleInfo(): checks a module-info image and takes out what the stack uses.
 *
 * Follows the header checks and the register settings of a2b_ReadVerify_ModuleInfo
 * (a2bpnp_calc.c) with the layout of a2bpnp_local.h, checked against ADI's
 * images in adi/module_info/:
 *
 * | Offset | What |
 * |---|---|
 * | 0x00 | Marker 0xAA |
 * | 0x01-0x04 | Module vendor and product IDs, 16 bits each, low byte first |
 * | 0x05 | Silicon version |
 * | 0x0D-0x10 | Address of the primary function block, 32 bits, low byte first: 0x20 |
 * | 0x1F | CRC-8 of bytes 0x00-0x1E |
 * | 0x20-0x21 | Module-info block: type (high nibble, 3) and size (12 bits) |
 * | 0x23 | Power configuration (high nibble) and local power (low nibble, not 0) |
 * | 0x24-0x27 | Most and least power the module draws, 16 bits each, low byte first |
 * | 0x29-0x2A | Most current it draws, 16 bits, low byte first |
 * | 0x7A | Number of peripheral configuration blocks (their layout: module_script.cpp) |
 * | 0x80-0x8F | Sub-node register settings (the table below); 0x85 is I2SRATE |
 * | 0x100- | Channel groups: TX groups, then RX groups (ParseChannelGroups()) |
 * | block end - 1 | The module-info block's CRC-8 (of 0x20 to the byte before it) |
 *
 * The power figures feed the power budget (BoardPolicy). Their unit isn't in
 * ADI's code; ADI's API's budget was in mW and mA, and ADI's XLR image gives
 * 5000, 500 and 200 (5 W, 0.5 W, 200 mA), so they are taken as mW and mA. The cable
 * type (0x28) is not used (decisions.md, "Dropped").
 *
 * **Channel groups** (ADI's "PnP Content (Module Information) Specification",
 * docs/ADI_PnP/A2B_PnP_EEPROM_ContentSpec.pdf, address 0x0100 on; ADI's
 * a2b_ReadVerify_ModuleInfo): a count of TX groups (at most 15), each a 37-byte record (its
 * ID, a 32-byte name, a 4-byte channel bitmap: bit j of byte i is channel 8i + j), then a
 * count of RX groups and their records. The table runs to the end of the module-info block,
 * whose size is at 0x20-0x21 (12 bits, the 3-byte block header not counted) and whose last
 * byte is the block's CRC-8: ADI's RJ45 image (adi/module_info/) has one TX and one RX group
 * (channels 0-1) and a block of 0x12A bytes; the bench modules' block (0x105 bytes, in the boot4
 * golden) leaves room for exactly one group. The block CRC is not checked, as ADI's
 * stack never checked it; the names are not kept.
 *
 * Fixed, not ported (decisions.md, "Bugs fixed"): ADI's check required module vendor 0xAD
 * and product 0x37, and never checked the CRC. Here any module ID passes (the app's
 * factory and listener decide what to accept), and the CRC must match. The power
 * configuration is reported, not required to be CFG4.
 *
 * Register settings are kept as ADI's stack kept them: a value equal to the chip's
 * power-on value (0, or 0x01 for PINCFG) is not written. ADI's stack also left
 * I2CCFG (0x7F) of the image unused; so does this port (the stack sets I2CCFG.FRAMERATE
 * from the board's frame rate, NodeSetup).
 *
 * **I2SRATE** (0x85): the module's own image says whether
 * its I2S/TDM port runs at 1x, 2x or 4x the superframe rate (48, 96 or 192 kHz at a 48 kHz
 * SYNC), as ADI's stack never applied it; a module without an image runs at 1x, the
 * register's power-on value. The reduced rate is out of scope: an image with SHARE or
 * REDUCE set, or an I2SRATE field other than 0 (1x), 5 (2x) or 6 (4x), fails as
 * Result::kInvalidData, rather than leave the node on a rate the routing doesn't plan for.
 * BCLKRATE is passed through.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "a2b/module_info.h"

#include "crc8.h"
#include "detail/regdefs.h"

namespace a2b {
namespace {

constexpr uint8_t kMarker = 0xAA;            ///< Byte 0x00 of a valid image.
constexpr size_t kHeaderCrc = 0x1F;          ///< Offset of the header CRC-8.
constexpr size_t kPrimaryFunction = 0x0D;    ///< Offset of the primary function address.
constexpr size_t kModuleInfoBlock = 0x20;    ///< Offset (and address) of the module-info block.
constexpr uint8_t kModuleInfoBlockType = 3;  ///< Type of the module-info block.
constexpr size_t kPowerConfig = 0x23;        ///< Offset of the power configuration.
constexpr size_t kMaxPower = 0x24;           ///< Offset of the most power drawn.
constexpr size_t kMinPower = 0x26;           ///< Offset of the least power drawn.
constexpr size_t kMaxCurrent = 0x29;         ///< Offset of the most current drawn.
constexpr size_t kPeripheralBlocks = 0x7A;   ///< Offset of the peripheral block count.
constexpr size_t kBlockPayload = 0x23;       ///< First byte after the module-info block's header.
constexpr size_t kGroupRecordLen = 37;       ///< A group's record: ID, name, channel bitmap.
constexpr size_t kGroupBitmap = 33;          ///< Offset of the channel bitmap in a record.
constexpr uint8_t kMaxImageGroups = 15;      ///< Most groups an image lists in each direction.
constexpr size_t kI2sRate = 0x85;            ///< Offset of the I2S rate setting.

/** One register setting in the image: its offset, its register, and the value not written. */
struct RegField {
  size_t offset;
  uint16_t reg;
  uint8_t powerOnValue;
};

/** The settings, in the order ADI's stack wrote them (a2b_dscvryNodeComplete), with
    I2SRATE after I2SCFG. */
constexpr RegField kRegFields[] = {
    {0x84, reg::kI2sgcfg, 0x00},   {0x83, reg::kI2scfg, 0x00},  {0x85, reg::kI2srate, 0x00},
    {0x86, reg::kPdmctl, 0x00},    {0x87, reg::kPdmctl2, 0x00}, {0x8F, reg::kGpiodat, 0x00},
    {0x8A, reg::kGpiooen, 0x00},   {0x8B, reg::kGpioien, 0x00}, {0x8C, reg::kPinten, 0x00},
    {0x8D, reg::kPintinv, 0x00},   {0x8E, reg::kPincfg, 0x01},  {0x88, reg::kClk1cfg, 0x00},
    {0x89, reg::kClk2cfg, 0x00},   {0x80, reg::kSpicfg, 0x00},  {0x81, reg::kSpickdiv, 0x00},
    {0x82, reg::kSpipincfg, 0x00},
};
static_assert(sizeof(kRegFields) / sizeof(kRegFields[0]) <= ModuleInfo::kMaxRegs,
              "ModuleInfo::kMaxRegs is too small");

/** A 16-bit field, low byte first. */
uint16_t Get16(const uint8_t* image, size_t offset) {
  return static_cast<uint16_t>(image[offset] | (image[offset + 1] << 8));
}

/** A 32-bit field, low byte first. */
uint32_t Get32(const uint8_t* image, size_t offset) {
  return static_cast<uint32_t>(image[offset]) | (static_cast<uint32_t>(image[offset + 1]) << 8) |
         (static_cast<uint32_t>(image[offset + 2]) << 16) |
         (static_cast<uint32_t>(image[offset + 3]) << 24);
}

}  // namespace

/**
 * Checks a module-info image and parses the parts the stack uses.
 * @param image The image, from offset 0.
 * @param len Its length; at least #kModuleInfoLen.
 * @param info Filled with the module's IDs, power settings and figures and register
 *        settings; left unchanged on failure.
 * @return Result::kOk; Result::kInvalidArgument if @p image is null or shorter than
 *         #kModuleInfoLen; Result::kInvalidData if the marker, header CRC, primary function
 *         address or module-info block type is wrong, or the I2S rate setting (0x85) is a
 *         reduced rate.
 */
Result ParseModuleInfo(const uint8_t* image, size_t len, ModuleInfo& info) {
  if (image == nullptr || len < kModuleInfoLen) {
    return Result::kInvalidArgument;
  }
  if (image[0] != kMarker || Crc8(image, kHeaderCrc) != image[kHeaderCrc] ||
      Get32(image, kPrimaryFunction) != kModuleInfoBlock ||
      (image[kModuleInfoBlock] >> 4) != kModuleInfoBlockType) {
    return Result::kInvalidData;
  }
  const uint8_t rate = image[kI2sRate] & reg::kI2srateRate;
  if ((image[kI2sRate] & (reg::kI2srateShare | reg::kI2srateReduce)) != 0 ||
      (rate != 0 && rate != reg::kI2srateX2 && rate != reg::kI2srateX4)) {
    return Result::kInvalidData;
  }
  // Every check is made: filled in place (a local ModuleInfo, with its channel groups,
  // took 272 B of stack on the RP2040).
  ModuleInfo& parsed = info;
  parsed.groupTableLen = 0;
  parsed.groups.rxCount = 0;
  parsed.groups.txCount = 0;
  parsed.regCount = 0;
  parsed.vendorId = Get16(image, 0x01);
  parsed.productId = Get16(image, 0x03);
  parsed.siliconVersion = image[0x05];
  parsed.powerConfig = static_cast<uint8_t>(image[kPowerConfig] >> 4);
  parsed.localPower = (image[kPowerConfig] & 0x0F) != 0;
  parsed.maxPowerMw = Get16(image, kMaxPower);
  parsed.minPowerMw = Get16(image, kMinPower);
  parsed.maxCurrentMa = Get16(image, kMaxCurrent);
  parsed.peripheralBlocks = image[kPeripheralBlocks];
  const size_t blockSize =
      static_cast<size_t>(((image[kModuleInfoBlock] & 0x0F) << 8) | image[kModuleInfoBlock + 1]);
  const size_t crcAt = kBlockPayload + blockSize - 1;  // The block's last byte.
  if (blockSize > 0 && crcAt > kGroupTableOffset) {
    parsed.groupTableLen = static_cast<uint16_t>(crcAt - kGroupTableOffset);
  }
  for (const RegField& field : kRegFields) {
    const uint8_t value = image[field.offset];
    if (value != field.powerOnValue) {
      parsed.reg[parsed.regCount] = field.reg;
      parsed.value[parsed.regCount] = value;
      ++parsed.regCount;
    }
  }
  return Result::kOk;
}

/**
 * Parses a channel-group table: the TX groups' count and records, then the RX groups'.
 * A group listed twice keeps its last record. Groups past ChannelGroups::kMaxGroups in a
 * direction are dropped.
 * @param table The table, from its first byte (image offset #kGroupTableOffset).
 * @param len Bytes of it (ModuleInfo::groupTableLen); 0 for none.
 * @param groups Filled with the groups; emptied first.
 * @return Result::kOk; Result::kNoSpace if groups were dropped (the first kMaxGroups of
 *         each direction are in @p groups); Result::kInvalidData if a count is over 15 or
 *         the records run past @p len (@p groups is then empty); Result::kInvalidArgument if
 *         @p table is null and @p len isn't 0.
 */
Result ParseChannelGroups(const uint8_t* table, size_t len, ChannelGroups& groups) {
  groups = ChannelGroups();
  if (len == 0) {
    return Result::kOk;
  }
  if (table == nullptr) {
    return Result::kInvalidArgument;
  }
  const size_t txCount = table[0];
  const size_t rxCountAt = 1 + txCount * kGroupRecordLen;
  if (txCount > kMaxImageGroups || rxCountAt >= len) {
    return Result::kInvalidData;
  }
  const size_t rxCount = table[rxCountAt];
  if (rxCount > kMaxImageGroups || rxCountAt + 1 + rxCount * kGroupRecordLen > len) {
    return Result::kInvalidData;
  }
  Result result = Result::kOk;
  for (size_t i = 0; i < txCount + rxCount; ++i) {
    const bool tx = (i < txCount);
    const uint8_t* record = tx ? &table[1 + i * kGroupRecordLen]
                               : &table[rxCountAt + 1 + (i - txCount) * kGroupRecordLen];
    const uint32_t channels = Get32(record, kGroupBitmap);
    const Result added = tx ? groups.AddTx(record[0], channels) : groups.AddRx(record[0], channels);
    if (added != Result::kOk) {
      result = added;
    }
  }
  return result;
}

/**
 * Finds an RX group.
 * @param id The group's ID.
 * @return The group, or nullptr if the node has none with that ID.
 */
const ChannelGroup* ChannelGroups::FindRx(uint8_t id) const {
  for (size_t i = 0; i < rxCount; ++i) {
    if (rx[i].id == id) {
      return &rx[i];
    }
  }
  return nullptr;
}

/**
 * Finds a TX group.
 * @param id The group's ID.
 * @return The group, or nullptr if the node has none with that ID.
 */
const ChannelGroup* ChannelGroups::FindTx(uint8_t id) const {
  for (size_t i = 0; i < txCount; ++i) {
    if (tx[i].id == id) {
      return &tx[i];
    }
  }
  return nullptr;
}

/**
 * Adds an RX group, or gives an existing one new channels.
 * @param id The group's ID.
 * @param channels Its channels: bit n is TDM channel n.
 * @return Result::kOk, or Result::kNoSpace if #kMaxGroups RX groups are there already.
 */
Result ChannelGroups::AddRx(uint8_t id, uint32_t channels) {
  for (size_t i = 0; i < rxCount; ++i) {
    if (rx[i].id == id) {
      rx[i].channels = channels;
      return Result::kOk;
    }
  }
  if (rxCount >= kMaxGroups) {
    return Result::kNoSpace;
  }
  rx[rxCount].id = id;
  rx[rxCount].channels = channels;
  ++rxCount;
  return Result::kOk;
}

/**
 * Adds a TX group, or gives an existing one new channels.
 * @param id The group's ID.
 * @param channels Its channels: bit n is TDM channel n.
 * @return Result::kOk, or Result::kNoSpace if #kMaxGroups TX groups are there already.
 */
Result ChannelGroups::AddTx(uint8_t id, uint32_t channels) {
  for (size_t i = 0; i < txCount; ++i) {
    if (tx[i].id == id) {
      tx[i].channels = channels;
      return Result::kOk;
    }
  }
  if (txCount >= kMaxGroups) {
    return Result::kNoSpace;
  }
  tx[txCount].id = id;
  tx[txCount].channels = channels;
  ++txCount;
  return Result::kOk;
}

}  // namespace a2b
