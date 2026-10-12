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
};

/// The text of the AB0020's sub-node ADAU1761 program (bench_files.cpp, generated at
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

/// Module info for boards without EEPROMs: the board chosen for a position gets its image,
/// a fallback board (if set) every other node, and the rest read their EEPROMs; a chip with
/// a no-EEPROM default (the AD2427: the AB0204) gets it when there is no EEPROM image.
class BoardModuleSource : public a2b::ModuleInfoSource {
 public:
  explicit BoardModuleSource(a2b::Network& network);  ///< Every node from its EEPROM.

  size_t GetModuleInfo(const a2b::SubNodeIdentity& node, uint8_t* buf,
                       size_t bufLen) override;  ///< The start of the node's image.
  a2b::Result ReadModuleInfo(const a2b::SubNodeIdentity& node, uint32_t offset, uint8_t* buf,
                             size_t len) override;  ///< More of it: groups, codec program.

  void SetBoard(a2b::NodeAddr position, const ModuleProfile* profile,
                const SigmaProgram& program);      ///< The board at a position; nullptr clears.
  void SetFallback(const ModuleProfile* profile);  ///< For every other node; nullptr: EEPROM.
  const ModuleProfile* Board(a2b::NodeAddr position) const;  ///< The board set, or nullptr.
  const ModuleProfile* ImageFor(
      const a2b::SubNodeIdentity& node) const;  ///< Its profile, without its EEPROM read.
  const ModuleProfile* Using(
      a2b::NodeAddr position) const;  ///< The profile a node's image came from, or nullptr.

 private:
  a2b::EepromModuleInfoSource m_Eeprom;                   ///< For the nodes without one.
  const ModuleProfile* m_Boards[a2b::kMaxSubNodes] = {};  ///< The board at each position.
  SigmaProgram m_Programs[a2b::kMaxSubNodes];             ///< Its codec program.
  const ModuleProfile* m_Fallback = nullptr;              ///< For every other node.
  const ModuleProfile* m_Using[a2b::kMaxSubNodes] = {};   ///< What each node's image is.
};

}  // namespace clockworks

#endif  // A2BCPP_EXAMPLES_BENCH_CLOCKWORKS_BOARDS_H_
