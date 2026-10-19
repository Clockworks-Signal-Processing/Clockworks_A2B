/**
 * @file module_info.cpp
 * @brief Builds module-info images for the simulated EEPROMs.
 *
 * The layout follows ADI's images (adi/module_info/adi_a2b_node_ModuleInfo_*.dat) and ADI's
 * readers (a2bpnp/a2bpnp_local.h, a2bstack/a2bplugin-master/src/discovery.h). Only the
 * fields the stack reads are filled in (see ParseModuleInfo() in src/module_info.cpp);
 * the rest of the image is zero.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sim/module_info.h"

#include "a2b/crc8.h"

namespace a2b {
namespace sim {

/**
 * Builds a valid image: marker, IDs, the primary function address (0x20), the module-info
 * block header (type 3 and its size), the power configuration and figures, the register
 * settings, and the header CRC.
 * @param header Header fields.
 * @param regs Register settings.
 * @param size Image size; raised to 0x90 (the register settings' end) if smaller.
 * @return The image.
 */
std::vector<uint8_t> MakeModuleInfoImage(const ModuleInfoHeader& header, const ModuleInfoRegs& regs,
                                         size_t size) {
  constexpr size_t kMinSize = 0x90;
  std::vector<uint8_t> image(size < kMinSize ? kMinSize : size, 0x00);
  image[0x00] = kModuleInfoMarker;
  image[0x01] = static_cast<uint8_t>(header.vendorId & 0xFF);
  image[0x02] = static_cast<uint8_t>(header.vendorId >> 8);
  image[0x03] = static_cast<uint8_t>(header.productId & 0xFF);
  image[0x04] = static_cast<uint8_t>(header.productId >> 8);
  image[0x05] = header.siliconVersion;
  image[0x07] = static_cast<uint8_t>(header.targetChip & 0xFF);
  image[0x08] = static_cast<uint8_t>(header.targetChip >> 8);
  image[0x0D] = 0x20;  // Primary function block address, low byte first.
  const size_t blockSize = (image.size() - 0x22) & 0x0FFF;
  image[0x20] = static_cast<uint8_t>(0x30 | (blockSize >> 8));
  image[0x21] = static_cast<uint8_t>(blockSize & 0xFF);
  image[kModuleInfoPowerConfig] =
      static_cast<uint8_t>((header.powerConfig << 4) | (header.localPower ? 1 : 0));
  image[0x28] = header.bPortType;
  const uint16_t figures[][2] = {{0x24, header.maxPowerMw},
                                 {0x26, header.minPowerMw},
                                 {0x29, header.maxCurrentMa},
                                 {0x6C, header.bCableM}};
  for (const auto& figure : figures) {
    image[figure[0]] = static_cast<uint8_t>(figure[1] & 0xFF);
    image[figure[0] + 1] = static_cast<uint8_t>(figure[1] >> 8);
  }
  const uint8_t fields[] = {regs.spicfg,  regs.spickdiv, regs.spipincfg, regs.i2scfg,
                            regs.i2sgcfg, regs.i2srate,  regs.pdmctl,    regs.pdmctl2,
                            regs.clk1cfg, regs.clk2cfg,  regs.gpiooen,   regs.gpioien,
                            regs.pinten,  regs.pintinv,  regs.pincfg,    regs.gpiodat};
  for (size_t i = 0; i < sizeof(fields); ++i) {
    image[0x80 + i] = fields[i];
  }
  SealModuleInfoHeader(image);
  return image;
}

/**
 * Makes a write unit.
 * @param addr The peripheral's 7-bit I2C address.
 * @param bytes The bytes to write, sub-address first.
 * @param withCrc Type B (a CRC-8 follows the bytes) if true, type A if false.
 * @return The unit.
 */
ConfigUnit ConfigUnit::Write(uint8_t addr, const std::vector<uint8_t>& bytes, bool withCrc) {
  ConfigUnit unit;
  unit.type = withCrc ? Type::kB : Type::kA;
  unit.i2cAddr = addr;
  unit.data = bytes;
  return unit;
}

/**
 * Makes a wait unit.
 * @param ms Milliseconds.
 * @return The unit.
 */
ConfigUnit ConfigUnit::Wait(uint16_t ms) {
  ConfigUnit unit;
  unit.type = Type::kC;
  unit.delayMs = ms;
  return unit;
}

/**
 * Recomputes the header CRC at 0x1F, e.g. after a test has edited the header. An image too
 * short to hold the CRC is left alone.
 * @param image The image to fix.
 */
void SealModuleInfoHeader(std::vector<uint8_t>& image) {
  if (image.size() > kModuleInfoHeaderCrc) {
    image[kModuleInfoHeaderCrc] = Crc8(image.data(), kModuleInfoHeaderLen);
  }
}

namespace {

/** Appends a block or unit header: type, 12-bit size, and @p third. */
void PutHeader(std::vector<uint8_t>& out, uint8_t type, size_t size, uint8_t third) {
  out.push_back(static_cast<uint8_t>((type << 4) | ((size >> 8) & 0x0F)));
  out.push_back(static_cast<uint8_t>(size & 0xFF));
  out.push_back(third);
}

/** Appends the CRC-8 of the bytes from @p from on. */
void PutCrc(std::vector<uint8_t>& out, size_t from) {
  out.push_back(Crc8(out.data() + from, out.size() - from));
}

}  // namespace

/**
 * Adds peripheral scripts: the block count at 0x7A, the peripheral info block at 0x800,
 * then each peripheral block followed by its units, with their CRCs, as ADI's images lay
 * them out. The image grows to fit; the header CRC is recomputed.
 * @param image A module-info image (MakeModuleInfoImage()).
 * @param blocks The peripheral blocks, at most 8.
 */
void AddPeripheralBlocks(std::vector<uint8_t>& image, const std::vector<PeripheralBlock>& blocks) {
  constexpr size_t kInfoAddr = 0x800;
  std::vector<uint8_t> out;
  const size_t infoSize = 4 * blocks.size() + 1;
  PutHeader(out, 4, infoSize, 0x00);
  const size_t addrAt = out.size();
  out.resize(out.size() + 4 * blocks.size());
  const size_t infoCrcAt = out.size();
  out.push_back(0x00);  // The CRC, once the addresses are in.
  for (size_t b = 0; b < blocks.size(); ++b) {
    const PeripheralBlock& block = blocks[b];
    const uint32_t addr = static_cast<uint32_t>(kInfoAddr + out.size());
    for (size_t i = 0; i < 4; ++i) {
      out[addrAt + 4 * b + i] = static_cast<uint8_t>(addr >> (8 * i));
    }
    const size_t start = out.size();
    PutHeader(out, 5, 80, 0x00);
    std::vector<uint8_t> payload(79, 0x00);
    payload[0] = block.interface;
    for (size_t i = 0; i < block.name.size() && i < 63; ++i) {
      payload[1 + i] = static_cast<uint8_t>(block.name[i]);
    }
    payload[65] = block.i2cAddr;
    payload[77] = static_cast<uint8_t>((block.units.size() >> 8) & 0x0F);
    payload[78] = static_cast<uint8_t>(block.units.size() & 0xFF);
    out.insert(out.end(), payload.begin(), payload.end());
    PutCrc(out, start);
    for (const ConfigUnit& unit : block.units) {
      const size_t unitStart = out.size();
      if (unit.type == ConfigUnit::Type::kC) {
        out.push_back(static_cast<uint8_t>(0x20 | ((unit.delayMs >> 8) & 0x0F)));
        out.push_back(static_cast<uint8_t>(unit.delayMs & 0xFF));
        out.push_back(Crc8(out.data() + unitStart, 2));
        continue;
      }
      const bool withCrc = (unit.type == ConfigUnit::Type::kB);
      PutHeader(out, static_cast<uint8_t>(unit.type), unit.data.size() + (withCrc ? 1 : 0),
                unit.i2cAddr);
      out.insert(out.end(), unit.data.begin(), unit.data.end());
      if (withCrc) {
        PutCrc(out, unitStart);
      }
    }
  }
  out[infoCrcAt] = Crc8(out.data(), infoCrcAt);
  if (image.size() < kInfoAddr + out.size()) {
    image.resize(kInfoAddr + out.size(), 0x00);
  }
  for (size_t i = 0; i < out.size(); ++i) {
    image[kInfoAddr + i] = out[i];
  }
  image[0x7A] = static_cast<uint8_t>(blocks.size());
  SealModuleInfoHeader(image);
}

/**
 * Puts a channel-group table at 0x100 (ADI's PnP EEPROM format: the TX groups' count and
 * 37-byte records, ID, name "Gn" and channel bitmap, then the RX groups'), and makes the
 * module-info block end just after it with its CRC-8 (of 0x20 to the byte before it, as in
 * ADI's images). The image grows if it is shorter; nothing past the block is touched.
 * @param image The image, from MakeModuleInfoImage().
 * @param groups The groups.
 */
void AddChannelGroups(std::vector<uint8_t>& image, const ChannelGroups& groups) {
  constexpr size_t kRecordLen = 37;
  const size_t crcAt = kGroupTableOffset + 2 + kRecordLen * (groups.txCount + groups.rxCount);
  if (image.size() < crcAt + 1) {
    image.resize(crcAt + 1, 0x00);
  }
  size_t at = kGroupTableOffset;
  const auto put = [&image, &at](const ChannelGroup& group) {
    for (size_t i = 0; i < kRecordLen; ++i) {
      image[at + i] = 0x00;
    }
    image[at] = group.id;
    image[at + 1] = 'G';
    image[at + 2] = static_cast<uint8_t>('0' + group.id % 10);
    for (size_t i = 0; i < 4; ++i) {
      image[at + 33 + i] = static_cast<uint8_t>(group.channels >> (8 * i));
    }
    at += kRecordLen;
  };
  image[at++] = groups.txCount;
  for (size_t i = 0; i < groups.txCount; ++i) {
    put(groups.tx[i]);
  }
  image[at++] = groups.rxCount;
  for (size_t i = 0; i < groups.rxCount; ++i) {
    put(groups.rx[i]);
  }
  const size_t blockSize = crcAt + 1 - 0x23;  // The block header's 3 bytes not counted.
  image[0x20] = static_cast<uint8_t>(0x30 | (blockSize >> 8));
  image[0x21] = static_cast<uint8_t>(blockSize & 0xFF);
  image[crcAt] = Crc8(&image[0x20], crcAt - 0x20);
}

}  // namespace sim
}  // namespace a2b
