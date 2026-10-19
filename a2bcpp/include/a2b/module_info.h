/**
 * @file module_info.h
 * @brief A sub-node's module info: its identity and settings, from its EEPROM or from the
 *        app. The source interface, the parsed form and the parser.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_A2B_MODULE_INFO_H_
#define A2BCPP_A2B_MODULE_INFO_H_

#include <cstddef>
#include <cstdint>

#include "a2b/types.h"

namespace a2b {

/** @brief Bytes of a module-info image the stack reads first: the header and the
 *         module-info block up to the sub-node's register settings (0x00-0x8F). */
inline constexpr size_t kModuleInfoLen = 0x90;

/** @brief Offset of the channel-group table in a module-info image (ADI's PnP EEPROM
 *         format: the number of TX groups, their records, the number of RX groups, theirs). */
inline constexpr uint32_t kGroupTableOffset = 0x100;

/** @brief ModuleInfo::targetChip of an image that doesn't say which transceiver it is for
 *         (0x0000, as ADI's images have it; 0xFFFF, unprogrammed, reads as it too). */
inline constexpr uint16_t kChipNotStated = 0x0000;

/** @brief ModuleInfo::targetChip of a board whose transceiver is a plug-in module: the board
 *         with the EEPROM doesn't decide the chip, so any chip takes the image. */
inline constexpr uint16_t kChipExternal = 0x7FFF;

/** @brief One group of a node's I2S/TDM channels, as connections name it. */
struct ChannelGroup {
  uint32_t channels = 0;  ///< Bit n set: TDM channel n (0-31) is in the group.
  uint8_t id = 0;         ///< The group's ID.
};

/**
 * @brief A node's channel groups: the channels it can send onto the bus and those it can
 *        play from it. A sub-node's come from its module info; the main node's from
 *        BoardRouting::groups.
 *
 * A group's channels are paired in order with the other end's: the lowest channel of the
 * source's group goes to the lowest of the sink's, and so on. Channel n is the n-th TDM
 * channel of the node's I2S/TDM port, counted across its data pins as the TRMs number them
 * (the RXMASK bits for the RX side, the TXXBAR registers for the TX side).
 */
struct ChannelGroups {
  /** @brief Most groups kept in each direction (an image may list up to 15; the rest are
      dropped with a log line). */
  static constexpr size_t kMaxGroups = 8;

  /** RX groups: channels the node receives on its I2S/TDM inputs (DRXn) and can send onto
      the bus. A connection's source is one of them. */
  ChannelGroup rx[kMaxGroups];
  uint8_t rxCount = 0;  ///< RX groups in #rx.
  /** TX groups: channels the node transmits on its I2S/TDM outputs (DTXn), from the bus.
      A connection's sink is one of them. */
  ChannelGroup tx[kMaxGroups];
  uint8_t txCount = 0;  ///< TX groups in #tx.

  const ChannelGroup* FindRx(uint8_t id) const;  ///< The RX group @p id, or nullptr.
  const ChannelGroup* FindTx(uint8_t id) const;  ///< The TX group @p id, or nullptr.
  Result AddRx(uint8_t id, uint32_t channels);   ///< Adds or replaces an RX group.
  Result AddTx(uint8_t id, uint32_t channels);   ///< Adds or replaces a TX group.
};

/** @brief A discovered sub-node: where it is, and what its transceiver reports. */
struct SubNodeIdentity {
  NodeAddr position = 0;  ///< Position on the bus.
  /** The bus it is on: BoardConfig::busIndex, so a listener, source or factory shared by
      several buses can tell them apart. */
  uint8_t bus = 0;
  uint8_t vendorId = 0;    ///< VENDOR register.
  uint8_t productId = 0;   ///< PRODUCT register.
  uint8_t version = 0;     ///< VERSION register.
  uint8_t capability = 0;  ///< CAPABILITY register.
  uint8_t chipId[6] = {};  ///< CHIPID0-5: the transceiver's unique ID.
};

/** @brief A module-info image, parsed. See ParseModuleInfo(). */
struct ModuleInfo {
  /** @brief Most register settings a module-info image carries. */
  static constexpr size_t kMaxRegs = 16;

  uint16_t vendorId = 0;       ///< Module vendor ID (not the chip's).
  uint16_t productId = 0;      ///< Module product ID.
  uint8_t siliconVersion = 0;  ///< Silicon version the module was made for.
  /** Power configuration, e.g. 4 for CFG4 (24 V): the image's (0x23 bits 7:4) as parsed.
      Once the node is identified the stack sets its chip's own, the only one each supported
      chip has: CFG-4 on the AD2437, CFG-0 on the AD2433 and the AD242x. */
  uint8_t powerConfig = 0;
  bool localPower = false;  ///< The module has its own supply.
  /** The image's I2CCFG.DATARATE (0x7F bit 0): every device on the node's I2C port takes
      400 kHz. NodeSetup switches the port to it before the peripheral scripts. */
  bool fastI2c = false;
  /** Most power the module draws (0x24-0x25): mW in ADI's images and ADI's API's
      budget. The power budget counts it for a bus-powered module. */
  uint16_t maxPowerMw = 0;
  uint16_t minPowerMw = 0;    ///< Least power it draws (0x26-0x27), in the same unit.
  uint16_t maxCurrentMa = 0;  ///< Most current it draws (0x29-0x2A): mA in ADI's images.
  /** The transceiver the image is for (header 0x07-0x08): its part number in hex, 0x2437
      for the AD2437; #kChipExternal; or #kChipNotStated. A named chip must be the node's
      (ImageFitsChip()). */
  uint16_t targetChip = kChipNotStated;
  /** The cable on the module's B port, in decimetres, as Network::CableLength() takes cables
      (the image holds metres at 0x6C-0x6D; held to 65535 dm); 0: not given. It is the length
      of the link to the next sub-node, for the response cycles. */
  uint16_t bCableDm = 0;
  uint8_t peripheralBlocks = 0;  ///< Peripheral configuration blocks in the image.
  /** What the module's B port is (0x28, a BPortType): it gives the link to the next
      sub-node (SubNode::DownstreamLink(), LinkProfile::ForBPort()). */
  uint8_t bPortType = 0;
  /** Bytes of the channel-group table at #kGroupTableOffset: from there to the end of the
      module-info block, less its CRC (0x20-0x21 give the block's size); 0 if the block ends
      before it. The stack reads the table when it sets the node up. */
  uint16_t groupTableLen = 0;
  /** Its channel groups, read from the table when the node is set up (empty in the
      NodeReview; empty if the source can't read past #kModuleInfoLen). */
  ChannelGroups groups;
  size_t regCount = 0;           ///< Register settings to write.
  uint16_t reg[kMaxRegs] = {};   ///< Their registers, in write order.
  uint8_t value[kMaxRegs] = {};  ///< Their values.
};

/** @brief A sub-node taken into the bus: what its transceiver and its module info say. */
struct SubNodeInfo {
  SubNodeIdentity identity;  ///< What its transceiver reported.
  ModuleInfo moduleInfo;     ///< Its module info, parsed.
};

/**
 * @brief Where the stack gets a sub-node's module info.
 *
 * The default, when the app installs none, is the modules' own EEPROMs
 * (EepromModuleInfoSource). An app that provisions blank modules ("ignore EEPROM") gives
 * the images itself: it derives from this class and installs its source with
 * Network::SetModuleInfoSource(). See the customisation cookbook
 * (a2bcpp/docs/cookbook.md).
 *
 * Both calls come from Network::Poll() on the thread that polls the bus. They must not
 * wait; they may reach the node's peripherals through Network's peripheral calls, as the
 * EEPROM source does.
 */
class ModuleInfoSource {
 public:
  virtual ~ModuleInfoSource() = default;

  /**
   * @brief Gives the start of a sub-node's module-info image.
   *
   * Called when a sub-node has been discovered and identified.
   * @param node The discovered sub-node.
   * @param buf Buffer for the image.
   * @param bufLen Size of @p buf (#kModuleInfoLen).
   * @return Bytes copied to @p buf, up to @p bufLen; 0 if there is no module info for
   *         this node (it is rejected).
   */
  virtual size_t GetModuleInfo(const SubNodeIdentity& node, uint8_t* buf, size_t bufLen) = 0;

  /**
   * @brief Reads more of the image GetModuleInfo() gave, for the node's peripheral
   *        scripts (the peripheral blocks from 0x800 on and their configuration units).
   *
   * Called after GetModuleInfo(), while the node is configured, if its module info lists
   * peripheral blocks. The default has no more of the image: the node gets its register
   * settings but no peripheral scripts.
   * @param node The sub-node.
   * @param offset Offset in the image.
   * @param buf Buffer for the bytes.
   * @param len Bytes to read.
   * @return Result::kOk with @p len bytes in @p buf; Result::kNotSupported if the source
   *         has no more than the start of the image; anything else stops the scripts and
   *         rejects the node.
   */
  virtual Result ReadModuleInfo(const SubNodeIdentity& node, uint32_t offset, uint8_t* buf,
                                size_t len) {
    (void)node;
    (void)offset;
    (void)buf;
    (void)len;
    return Result::kNotSupported;
  }
};

Result ParseModuleInfo(const uint8_t* image, size_t len,
                       ModuleInfo& info);  ///< Checks and parses a module-info image.
bool ImageFitsChip(const ModuleInfo& info,
                   uint8_t productId);  ///< The image's target chip allows the node's chip.
Result ParseChannelGroups(const uint8_t* table, size_t len,
                          ChannelGroups& groups);  ///< Parses a channel-group table.

}  // namespace a2b

#endif  // A2BCPP_A2B_MODULE_INFO_H_
