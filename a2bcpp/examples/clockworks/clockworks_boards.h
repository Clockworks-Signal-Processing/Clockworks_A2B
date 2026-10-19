/**
 * @file clockworks_boards.h
 * @brief Clockworks boards as sub-nodes without EEPROMs: what each board's module-info
 *        image holds, how the image is built, and a ModuleInfoSource that gives chosen
 *        positions their board's image and every other node its EEPROM's. The bench
 *        console (`module N NAME`) and the fixed app (examples/fixed) use it; an app that
 *        supports its own boards without EEPROMs can copy it.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_EXAMPLES_BENCH_CLOCKWORKS_BOARDS_H_
#define A2BCPP_EXAMPLES_BENCH_CLOCKWORKS_BOARDS_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "a2b/eeprom_source.h"
#include "a2b/image_source.h"
#include "a2b/module_info.h"
#include "a2b/network.h"
#include "sigma_xml.h"

namespace clockworks {

/// A sub-node's register settings as a module-info image holds them (image offsets
/// 0x80-0x8F; layout: a2bcpp/src/module_info.cpp). Each defaults to its register's
/// power-on value, which the stack doesn't write. SPI settings reach AD243x nodes only.
struct ModuleSettings {
  uint8_t spicfg = 0x00;     ///< 0x80: SPICFG.
  uint8_t spickdiv = 0x00;   ///< 0x81: SPICKDIV.
  uint8_t spipincfg = 0x00;  ///< 0x82: SPIPINCFG.
  uint8_t i2scfg = 0x00;     ///< 0x83: I2SCFG.
  uint8_t i2sgcfg = 0x00;    ///< 0x84: I2SGCFG.
  uint8_t i2srate = 0x00;    ///< 0x85: I2SRATE (0: 1x; 5: 2x; 6: 4x the superframe rate).
  uint8_t pdmctl = 0x00;     ///< 0x86: PDMCTL.
  uint8_t pdmctl2 = 0x00;    ///< 0x87: PDMCTL2.
  uint8_t clk1cfg = 0x00;    ///< 0x88: CLK1CFG.
  uint8_t clk2cfg = 0x00;    ///< 0x89: CLK2CFG.
  uint8_t gpiooen = 0x00;    ///< 0x8A: GPIOOEN.
  uint8_t gpioien = 0x00;    ///< 0x8B: GPIOIEN.
  uint8_t pinten = 0x00;     ///< 0x8C: PINTEN.
  uint8_t pintinv = 0x00;    ///< 0x8D: PINTINV.
  uint8_t pincfg = 0x01;     ///< 0x8E: PINCFG.
  uint8_t gpiodat = 0x00;    ///< 0x8F: GPIODAT.
};

/// A board as a sub-node: what its module-info image holds.
struct ModuleProfile {
  const char* name = "";            ///< What `module N NAME` takes.
  const char* description = "";     ///< What the console lists.
  ModuleSettings settings;          ///< Its transceiver's settings.
  a2b::ChannelGroups groups;        ///< Its channel groups.
  const char* codecName = nullptr;  ///< Its codec, programmed at set-up; nullptr: none.
  uint8_t codecAddr = 0;            ///< The codec's 7-bit I2C address.
  /** The codec's default program, SigmaStudio's XML export compiled in; nullptr: none. */
  const char* codecProgramXml = nullptr;
  /** The transceivers (PRODUCT) the board is used with; 0 ends the list; none: any. */
  uint8_t chips[4] = {};
  uint16_t moduleVendor = 0x00AD;   ///< Its image's module vendor ID.
  uint16_t moduleProduct = 0x0037;  ///< Its image's module product ID: a board's number.
  /** Its image's target chip: a part number (0x2427), a2b::kChipExternal for a board whose
      transceiver is a plug-in module, or a2b::kChipNotStated. */
  uint16_t targetChip = a2b::kChipNotStated;
  uint8_t bPortType = 2;  ///< Its B port (a2b::BPortType): UTP unless set.
  /** Its image's local power bit (0x23 bit 0): the board has its own supply. The AB0020 and
      AB0312 can also be set for bus power; the AD2427 microphone boards are bus powered only. */
  bool localPower = false;
  /** The codec's A2B pins in its peripheral block (RX and TX interface): bits 7:5 the number of
      pins, bits 4:0 the AD243x's SIO pins; 0x21 is SIO0 (DRX0), 0x30 SIO4 (DTX0), as in ADI's
      images. */
  uint16_t codecRxPins = 0;
  uint16_t codecTxPins = 0;  ///< As #codecRxPins.
};

/// Clockworks' module vendor ID in a module-info image's header: 0x0C54, "CSP".
inline constexpr uint16_t kClockworksModuleVendor = 0x0C54;

/// Clockworks' vendor ID in a module-info image's Supplier Information block.
inline constexpr char kClockworksVendorId[] = "CLKWORKS";
/// Most features Clockworks' vendor section holds.
inline constexpr size_t kClockworksMaxFeatures = 6;

/// One feature of Clockworks' vendor section.
struct ClockworksFeature {
  uint64_t uid = 0;    ///< Which feature it is: the same on every board that has it.
  std::string name;    ///< Up to 8 characters.
  int32_t value0 = 0;  ///< Its first value.
  int32_t value1 = 0;  ///< Its second value.
  std::string text;    ///< Up to 8 characters.
};

/// Clockworks' vendor section: the data of the Supplier Information block when its vendor ID
/// is kClockworksVendorId (layout: clockworks_boards.cpp, ParseClockworksSection()).
struct ClockworksSection {
  std::string id;                           ///< Up to 15 characters.
  std::vector<ClockworksFeature> features;  ///< Up to kClockworksMaxFeatures.
};

/// The text of the AB0020's sub-node ADAU1761 program (clockworks_files.cpp, generated at
/// build time from tools/SS+reference/2x_AB0020_EVM_with_2437/AB0020-sub-node-ADAU1761.xml).
extern const char kAb0020SubNodeProgram[];

const std::vector<ModuleProfile>& Boards();  ///< The Clockworks boards as sub-nodes.
ModuleProfile GenericModule();  ///< The image for a node without a board or an EEPROM read.
const ModuleProfile* NoEepromDefault(
    uint8_t productId);  ///< The board a chip without an EEPROM image is taken to be.
const ModuleProfile* FindBoard(const std::string& name);         ///< A board, or nullptr.
bool Supports(const ModuleProfile& profile, uint8_t productId);  ///< Used with the chip.
bool DefaultProgram(const ModuleProfile& profile, SigmaProgram& program,
                    std::string& error);  ///< The board's compiled-in codec program.
std::vector<uint8_t> MakeModuleImage(const ModuleProfile& profile, uint8_t productId,
                                     const SigmaProgram& program);  ///< Its image.
bool ParseClockworksSection(const std::string& vendorId, const uint8_t* data, size_t len,
                            ClockworksSection& section);  ///< Clockworks' vendor section.

/// Module info for boards without EEPROMs: an image file set for a position gives it, else
/// the board chosen for a position gets its image, a fallback board (if set) every other
/// node, and the rest read their EEPROMs; a chip with a no-EEPROM default (the AD2427: the
/// AB0204) gets it when there is no EEPROM image.
class BoardModuleSource : public a2b::ModuleInfoSource {
 public:
  explicit BoardModuleSource(a2b::Network& network);  ///< Every node from its EEPROM.

  size_t GetModuleInfo(const a2b::SubNodeIdentity& node, uint8_t* buf,
                       size_t bufLen) override;  ///< The start of the node's image.
  a2b::Result ReadModuleInfo(const a2b::SubNodeIdentity& node, uint32_t offset, uint8_t* buf,
                             size_t len) override;  ///< More of it: groups, codec program.

  void SetBoard(a2b::NodeAddr position, const ModuleProfile* profile,
                const SigmaProgram& program);  ///< The board at a position; nullptr clears.
  void SetImage(a2b::NodeAddr position,
                const std::vector<uint8_t>& image);  ///< An image at a position; empty clears.
  bool HasImage(a2b::NodeAddr position) const;       ///< An image is set at the position.
  bool UsingImage(a2b::NodeAddr position) const;     ///< The node's info came from one.
  uint8_t EepromAddr(a2b::NodeAddr position) const;  ///< Where its EEPROM image was, or 0.
  void SetFallback(const ModuleProfile* profile);    ///< For every other node; nullptr: EEPROM.
  const ModuleProfile* Board(a2b::NodeAddr position) const;  ///< The board set, or nullptr.
  const ModuleProfile* ImageFor(
      const a2b::SubNodeIdentity& node) const;  ///< Its profile, without its EEPROM read.
  const ModuleProfile* Using(
      a2b::NodeAddr position) const;  ///< The profile a node's image came from, or nullptr.

 private:
  a2b::EepromModuleInfoSource m_Eeprom;              ///< For the nodes without one.
  std::vector<uint8_t> m_Images[a2b::kMaxSubNodes];  ///< An image file at each position.
  /** The image each node was identified with: a SetImage() meanwhile doesn't change it. */
  std::vector<uint8_t> m_InUse[a2b::kMaxSubNodes];
  a2b::ImageModuleInfoSource m_ImageSource;               ///< Gives them.
  bool m_UsingImage[a2b::kMaxSubNodes] = {};              ///< A node's info is an image's.
  const ModuleProfile* m_Boards[a2b::kMaxSubNodes] = {};  ///< The board at each position.
  SigmaProgram m_Programs[a2b::kMaxSubNodes];             ///< Its codec program.
  const ModuleProfile* m_Fallback = nullptr;              ///< For every other node.
  const ModuleProfile* m_Using[a2b::kMaxSubNodes] = {};   ///< What each node's image is.
};

}  // namespace clockworks

#endif  // A2BCPP_EXAMPLES_BENCH_CLOCKWORKS_BOARDS_H_
