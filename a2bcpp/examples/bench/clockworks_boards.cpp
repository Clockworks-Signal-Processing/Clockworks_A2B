/**
 * @file clockworks_boards.cpp
 * @brief Clockworks boards as sub-nodes without EEPROMs: the profiles, the module-info
 *        image built from one, and BoardModuleSource.
 *
 * **How a board without an EEPROM is supported.** A sub-node is set up from its
 * module-info image (a2bcpp/docs/main_and_sub_node_programming.md, section 4): register
 * settings, channel groups, and peripheral scripts such as a codec program. A module's
 * EEPROM normally holds the image. Here the app holds it instead, as a profile per board,
 * and builds the image when the node is found:
 *
 * 1. Write the board's settings: the values its SigmaStudio+ project gives the sub-node's
 *    transceiver (the bench project's register list, `EVM_EVM.ssprj`, or its export's
 *    sub-node description), limited to the registers a module-info image carries
 *    (ModuleSettings). The stack writes the rest: slots, crossbars, switches, interrupt
 *    masks.
 * 2. List its channel groups: the TDM channels its peripherals send (RX) and play (TX).
 *    Connections name these.
 * 3. Give the codec program, SigmaStudio's XML export of it (read with ParseSigmaXml()).
 *    The stack runs it as the node's peripheral script at every set-up, after the
 *    settings, so a clock the codec needs (a CLKOUT) runs first.
 * 4. Install a source that gives each position its board's image (BoardModuleSource):
 *    which board is where is the app's to know, here by position.
 *
 * With an EEPROM, none of this is needed: the same image, written to the EEPROM, is read
 * by the stack's EepromModuleInfoSource, and the app only decides whether the nodes found
 * make a valid system (NetworkListener::OnNodeIdentified()) and what to connect.
 *
 * **The boards.** AB0020: an ADAU1761 codec on I2S TDM2 32-bit (RX 0 and TX 0 on channels
 * 0-1), its MCLK from the transceiver's CLKOUT2 (12.288 MHz). Its settings are the bench
 * project's sub-node (`tools/SS+reference/2x_AB0020_EVM_with_2437`): I2SGCFG 0xE0, I2SCFG
 * 0x11, GPIODAT and GPIOOEN 0x80 (IO7 driven high), PINCFG 0x01, CLK2CFG 0x81 and SPICFG
 * 0x02. SPI is off because it powers on enabled, and "ADR2/CLKOUT2 is not functional when
 * SPI is enabled with the default GPIO select" (AD2437 TRM, note to Table 5-28): without
 * it the codec has no MCLK.
 *
 * AB0312: no codec; its I2S on a header, programmed as the AB0020's (RX 0 and TX 0 on
 * channels 0-1). Its settings are its project's sub-node
 * (`tools/SS+reference/AB0310_AB0312_EVM_with_2437`), the same values as the AB0020's,
 * CLKOUT2 and IO7 included as the project has them. The project's links are RJ45 (its
 * CONTROL sets XCVRBINV), which the stack sets per link, not from the image.
 *
 * AB0204: an AD2427 with four MEMS PDM microphones, two on DRX0 and two on DRX1, clocked
 * from the BCLK pin. From the AD242x TRM ("PDM Clocking Options", "PDM
 * Sampling Edge of a Connected Microphone") and the AD2426/27/28 data sheet (Table 1: the
 * AD2427 has no I2S/TDM, only the PDM inputs): PDMCTL 0x1F turns on both PDM pins with two
 * microphones each and the high-pass filter (1 Hz); with a PDM pin on,
 * "the BCLK pin is an output, typically producing a 3.072 MHz clock for the TDM2 setting",
 * 64 x SYNC, which I2SGCFG's power-on value (TDM2, 32-bit) gives; PDMCTL2 stays 0 (the
 * clock on BCLK, not PDMCLK/IO7; the data to the bus only; rising-edge data first). Its
 * RX channels: 0 and 1 are DRX0's rising- and falling-edge microphones, 2 and 3 DRX1's.
 * RX 0 is all four, RX 1 and RX 2 the pairs, for the stereo ports of the AB0020 bench.
 *
 * **No EEPROM.** An AD2427 without an EEPROM image (none answers with the marker: absent or
 * blank) is taken to be an AB0204 (NoEepromDefault()), and so is one
 * whose EEPROM isn't read (`ignoreEEPROM 1`): the generic image is I2S, which an AD2427
 * hasn't got. An EEPROM that answers but can't be read still has the node rejected.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "clockworks_boards.h"

#include <cctype>
#include <cstring>

#include "crc8.h"

namespace clockworks {
namespace {

constexpr uint8_t kAd2437 = 0x37;  ///< PRODUCT of the AD2437.
constexpr uint8_t kAd2433 = 0x33;  ///< PRODUCT of the AD2433.
constexpr uint8_t kAd2428 = 0x28;  ///< PRODUCT of the AD2428.
constexpr uint8_t kAd2427 = 0x27;  ///< PRODUCT of the AD2427.

/** Puts a channel group's 37-byte record (ID, name, channel bitmap) in @p image at @p at. */
void PutGroup(std::vector<uint8_t>& image, size_t at, const a2b::ChannelGroup& group) {
  image[at] = group.id;
  const std::string name = "G" + std::to_string(group.id);
  for (size_t i = 0; i < name.size(); ++i) {
    image[at + 1 + i] = static_cast<uint8_t>(name[i]);
  }
  for (size_t i = 0; i < 4; ++i) {
    image[at + 33 + i] = static_cast<uint8_t>(group.channels >> (8 * i));
  }
}

/** Appends a block or unit header: type, 12-bit size, and @p third. */
void PutHeader(std::vector<uint8_t>& out, uint8_t type, size_t size, uint8_t third) {
  out.push_back(static_cast<uint8_t>((type << 4) | ((size >> 8) & 0x0F)));
  out.push_back(static_cast<uint8_t>(size & 0xFF));
  out.push_back(third);
}

/**
 * Adds one I2C peripheral script to a module-info image, as ADI's images lay them out and
 * a2bcpp/src/module_script.cpp reads them: the block count (1) at 0x7A, the peripheral
 * info block at 0x800 (type 4: the block's address, a CRC-8), the peripheral block (type 5:
 * I2C, @p name, the unit count, a CRC-8), then a unit per action: a write with a CRC-8
 * (type B) or a wait (type C). The image grows to fit. ParseSigmaXml()'s limits keep each
 * unit in its 12-bit size.
 * @param image The image.
 * @param name The peripheral's name, up to 63 characters.
 * @param program The writes and waits.
 */
void AddPeripheralScript(std::vector<uint8_t>& image, const char* name,
                         const std::vector<SigmaAction>& program) {
  constexpr size_t kInfoAddr = 0x800;
  std::vector<uint8_t> out;
  PutHeader(out, 4, 5, 0x00);
  const uint32_t blockAddr = static_cast<uint32_t>(kInfoAddr + out.size() + 5);
  for (size_t i = 0; i < 4; ++i) {
    out.push_back(static_cast<uint8_t>(blockAddr >> (8 * i)));
  }
  out.push_back(a2b::Crc8(out.data(), out.size()));
  const size_t blockStart = out.size();
  PutHeader(out, 5, 80, 0x00);
  std::vector<uint8_t> payload(79, 0x00);  // Interface 0: I2C.
  for (size_t i = 0; name[i] != '\0' && i < 63; ++i) {
    payload[1 + i] = static_cast<uint8_t>(name[i]);
  }
  payload[77] = static_cast<uint8_t>((program.size() >> 8) & 0x0F);  // Units, high byte first.
  payload[78] = static_cast<uint8_t>(program.size() & 0xFF);
  out.insert(out.end(), payload.begin(), payload.end());
  out.push_back(a2b::Crc8(out.data() + blockStart, out.size() - blockStart));
  for (const SigmaAction& action : program) {
    const size_t unitStart = out.size();
    if (action.wait) {
      out.push_back(static_cast<uint8_t>(0x20 | ((action.waitMs >> 8) & 0x0F)));
      out.push_back(static_cast<uint8_t>(action.waitMs & 0xFF));
      out.push_back(a2b::Crc8(out.data() + unitStart, 2));
      continue;
    }
    PutHeader(out, 1, action.bytes.size() + 1, action.i2cAddr);
    out.insert(out.end(), action.bytes.begin(), action.bytes.end());
    out.push_back(a2b::Crc8(out.data() + unitStart, out.size() - unitStart));
  }
  if (image.size() < kInfoAddr + out.size()) {
    image.resize(kInfoAddr + out.size(), 0x00);
  }
  std::memcpy(&image[kInfoAddr], out.data(), out.size());
  image[0x7A] = 1;  // Peripheral blocks; not in the header the CRC at 0x1F covers.
}

/** The AB0020 as a sub-node: the bench SigmaStudio+ project's sub-node (file comment). */
ModuleProfile Ab0020() {
  ModuleProfile board;
  board.name = "ab0020";
  board.description = "AB0020: ADAU1761 on I2S (RX 0, TX 0: channels 0-1), MCLK on CLKOUT2";
  board.settings.i2sgcfg = 0xE0;  // TDM2, 32-bit, SYNC inverted, early, 50% duty.
  board.settings.i2scfg = 0x11;   // DRX0 and DTX0, both on BCLK's rising edge.
  board.settings.gpiodat = 0x80;  // IO7 high ...
  board.settings.gpiooen = 0x80;  // ... and driven.
  board.settings.pincfg = 0x01;   // High drive strength.
  board.settings.clk2cfg = 0x81;  // CLKOUT2 on ADR2: the PLL / 2 / 4, 12.288 MHz at 48 kHz.
  board.settings.spicfg = 0x02;   // SPI off, so that CLKOUT2 reaches ADR2.
  (void)board.groups.AddRx(0, 0x3);
  (void)board.groups.AddTx(0, 0x3);
  board.codecName = "ADAU1761";
  board.codecAddr = 0x39;
  board.codecProgramXml = kAb0020SubNodeProgram;
  board.chips[0] = kAd2437;
  board.chips[1] = kAd2433;
  board.chips[2] = kAd2428;
  return board;
}

/** The AB0312 as a sub-node: its SigmaStudio+ project's sub-node (file comment). */
ModuleProfile Ab0312() {
  ModuleProfile board;
  board.name = "ab0312";
  board.description = "AB0312: I2S on a header (RX 0, TX 0: channels 0-1), no codec";
  board.settings.i2sgcfg = 0xE0;  // TDM2, 32-bit, SYNC inverted, early, 50% duty.
  board.settings.i2scfg = 0x11;   // DRX0 and DTX0, both on BCLK's rising edge.
  board.settings.gpiodat = 0x80;  // IO7 high ...
  board.settings.gpiooen = 0x80;  // ... and driven.
  board.settings.pincfg = 0x01;   // High drive strength.
  board.settings.clk2cfg = 0x81;  // CLKOUT2 on ADR2: the PLL / 2 / 4, 12.288 MHz at 48 kHz.
  board.settings.spicfg = 0x02;   // SPI off.
  (void)board.groups.AddRx(0, 0x3);
  (void)board.groups.AddTx(0, 0x3);
  board.chips[0] = kAd2437;
  board.chips[1] = kAd2433;
  board.chips[2] = kAd2428;
  return board;
}

/** The AB0204 as a sub-node: four PDM microphones on an AD2427 (file comment). */
ModuleProfile Ab0204() {
  ModuleProfile board;
  board.name = "ab0204";
  board.description = "AB0204: four PDM mics (RX 0: channels 0-3, RX 1: 0-1, RX 2: 2-3)";
  board.settings.pdmctl = 0x1F;  // PDM0EN, PDM0SLOTS, PDM1EN, PDM1SLOTS, HPFEN.
  // PDMCTL2 0: the clock on BCLK, 3.072 MHz from I2SGCFG's power-on TDM2 32-bit.
  (void)board.groups.AddRx(0, 0xF);  // DRX0's two microphones, then DRX1's.
  (void)board.groups.AddRx(1, 0x3);  // DRX0's pair.
  (void)board.groups.AddRx(2, 0xC);  // DRX1's pair.
  board.chips[0] = kAd2427;
  return board;
}

}  // namespace

/**
 * The Clockworks boards that can be sub-nodes, for `module N NAME`.
 * @return The profiles; they live as long as the program.
 */
const std::vector<ModuleProfile>& Boards() {
  static const std::vector<ModuleProfile> kBoards = {Ab0020(), Ab0312(), Ab0204()};
  return kBoards;
}

/**
 * The board a node is taken to be when it has no EEPROM image, or its EEPROM isn't read:
 * the AB0204 for an AD2427 (file comment, "No EEPROM").
 * @param productId The node's PRODUCT.
 * @return The profile, or nullptr: the chip has no default.
 */
const ModuleProfile* NoEepromDefault(uint8_t productId) {
  return (productId == kAd2427) ? FindBoard("ab0204") : nullptr;
}

/**
 * The image for a node with no board set whose EEPROM isn't read (the bench console's
 * `ignoreEEPROM 1`, the fixed app's unknown nodes): the minimal example's settings (GPIO0-6
 * inputs, PINCFG 0), I2S on DRX0 and DTX0 as the Clockworks boards have it, so that RX 0
 * and TX 0 on channels 0-1 are on the port (the stack refuses channels a port doesn't give;
 * an image without I2S gave the bench its "no SYNC"), no codec, any chip. I2SGCFG 0xE0 and I2SCFG
 * 0x11 mean the same on the AD242x (TX0EN, RX0EN) and the AD243x (one pin each way).
 * @return The profile, named "generic".
 */
ModuleProfile GenericModule() {
  ModuleProfile module;
  module.name = "generic";
  module.description = "no board: GPIO0-6 inputs, I2S (RX 0, TX 0: channels 0-1)";
  module.settings.i2sgcfg = 0xE0;  // TDM2, 32-bit, SYNC inverted, early, 50% duty.
  module.settings.i2scfg = 0x11;   // DRX0 and DTX0, both on BCLK's rising edge.
  module.settings.gpioien = 0x7F;
  module.settings.pincfg = 0x00;
  (void)module.groups.AddRx(0, 0x3);
  (void)module.groups.AddTx(0, 0x3);
  return module;
}

/**
 * Finds a board by name.
 * @param name As `module N NAME` takes it, in any case.
 * @return The profile, or nullptr if there is none of that name.
 */
const ModuleProfile* FindBoard(const std::string& name) {
  for (const ModuleProfile& board : Boards()) {
    const std::string boardName = board.name;
    bool same = boardName.size() == name.size();
    for (size_t i = 0; same && i < name.size(); ++i) {
      same = (std::tolower(static_cast<unsigned char>(name[i])) == boardName[i]);
    }
    if (same) {
      return &board;
    }
  }
  return nullptr;
}

/**
 * Tells whether a board is used with a transceiver.
 * @param profile The board.
 * @param productId The node's PRODUCT.
 * @return True if it is in the board's list, or the board lists none.
 */
bool Supports(const ModuleProfile& profile, uint8_t productId) {
  if (profile.chips[0] == 0) {
    return true;
  }
  for (uint8_t chip : profile.chips) {
    if (chip == productId) {
      return true;
    }
  }
  return false;
}

/**
 * Reads a board's compiled-in codec program.
 * @param profile The board.
 * @param program Filled with its writes and waits; empty if the board has no codec.
 * @param error What is wrong, if it can't be read.
 * @return True unless the compiled-in program can't be read.
 */
bool DefaultProgram(const ModuleProfile& profile, SigmaProgram& program, std::string& error) {
  program = SigmaProgram();
  if (profile.codecProgramXml == nullptr) {
    return true;
  }
  return ParseSigmaXml(profile.codecProgramXml, profile.codecAddr, program, error);
}

/**
 * Builds a board's module-info image for a node: a valid header for module 00AD:0037, the
 * power configuration the chip's data sheet gives it (CFG-4 for the AD2437; CFG-0 for the
 * AD2433, AD2431W-35W data sheet, Table 1; the AD242x have none), the board's settings, its
 * channel groups at 0x100 and the block's CRC, and its codec program as a peripheral
 * script. Layout: a2bcpp/src/module_info.cpp and module_script.cpp.
 * @param profile The board.
 * @param productId The node's PRODUCT.
 * @param program Its codec program; empty for none.
 * @return The image.
 */
std::vector<uint8_t> MakeModuleImage(const ModuleProfile& profile, uint8_t productId,
                                     const SigmaProgram& program) {
  const a2b::ChannelGroups& groups = profile.groups;
  const size_t tableLen = 2 + 37 * (groups.txCount + groups.rxCount);
  std::vector<uint8_t> image(a2b::kGroupTableOffset + tableLen + 1, 0x00);
  const size_t blockSize = image.size() - 0x23;                 // The header's 3 bytes not counted.
  image[0x00] = 0xAA;                                           // Marker.
  image[0x01] = 0xAD;                                           // Module vendor ID, low byte first.
  image[0x03] = 0x37;                                           // Module product ID.
  image[0x05] = 0x10;                                           // Silicon version.
  image[0x0D] = 0x20;                                           // The module-info block is at 0x20.
  image[0x1F] = 0x2E;                                           // CRC-8 of bytes 0x00-0x1E.
  image[0x20] = static_cast<uint8_t>(0x30 | (blockSize >> 8));  // Type 3, and its size.
  image[0x21] = static_cast<uint8_t>(blockSize);
  image[0x23] = (productId == kAd2437) ? 0x40 : 0x00;  // Power configuration, no local power.
  const ModuleSettings& s = profile.settings;
  image[0x80] = s.spicfg;
  image[0x81] = s.spickdiv;
  image[0x82] = s.spipincfg;
  image[0x83] = s.i2scfg;
  image[0x84] = s.i2sgcfg;
  image[0x86] = s.pdmctl;
  image[0x87] = s.pdmctl2;
  image[0x88] = s.clk1cfg;
  image[0x89] = s.clk2cfg;
  image[0x8A] = s.gpiooen;
  image[0x8B] = s.gpioien;
  image[0x8C] = s.pinten;
  image[0x8D] = s.pintinv;
  image[0x8E] = s.pincfg;
  image[0x8F] = s.gpiodat;
  size_t at = a2b::kGroupTableOffset;
  image[at++] = groups.txCount;
  for (size_t i = 0; i < groups.txCount; ++i, at += 37) {
    PutGroup(image, at, groups.tx[i]);
  }
  image[at++] = groups.rxCount;
  for (size_t i = 0; i < groups.rxCount; ++i, at += 37) {
    PutGroup(image, at, groups.rx[i]);
  }
  if (profile.codecName != nullptr && !program.actions.empty()) {
    AddPeripheralScript(image, profile.codecName, program.actions);
  }
  return image;  // The block's CRC (its last byte) is not checked.
}

/**
 * Makes a source that reads every node's EEPROM until boards are set.
 * @param network The bus, for the EEPROM reads.
 */
BoardModuleSource::BoardModuleSource(a2b::Network& network) : m_Eeprom(network) {}

/**
 * Gives the start of a node's image: its board's or the fallback's (ImageFor()), else its
 * EEPROM's, else, if no EEPROM answered with an image, its chip's default
 * (NoEepromDefault()). Using() tells which.
 * @param node The node: its position and PRODUCT pick the image.
 * @param buf Buffer for the image.
 * @param bufLen Its size.
 * @return Bytes copied; 0: no module info (the node is rejected).
 */
size_t BoardModuleSource::GetModuleInfo(const a2b::SubNodeIdentity& node, uint8_t* buf,
                                        size_t bufLen) {
  if (!a2b::IsSubNode(node.position)) {
    return 0;
  }
  m_Using[node.position] = nullptr;
  const ModuleProfile* profile = ImageFor(node);
  if (profile == nullptr) {
    const size_t len = m_Eeprom.GetModuleInfo(node, buf, bufLen);
    if (len != 0 || m_Eeprom.EepromAddr(node.position) != 0) {
      return len;  // Its EEPROM's image, or an EEPROM it couldn't read: rejected.
    }
    profile = NoEepromDefault(node.productId);
    if (profile == nullptr) {
      return 0;
    }
  }
  if (bufLen < a2b::kModuleInfoLen) {
    return 0;
  }
  m_Using[node.position] = profile;
  const bool own = (profile == Board(node.position));
  const std::vector<uint8_t> image =
      MakeModuleImage(*profile, node.productId, own ? m_Programs[node.position] : SigmaProgram());
  std::memcpy(buf, image.data(), a2b::kModuleInfoLen);
  return a2b::kModuleInfoLen;
}

/**
 * Gives more of a node's image: the stack reads its channel-group table and its peripheral
 * scripts (the codec program) this way.
 * @param node The node.
 * @param offset Offset in the image.
 * @param buf Buffer for the bytes.
 * @param len Bytes to read.
 * @return a2b::Result::kOk, or a2b::Result::kInvalidArgument past the image's end.
 */
a2b::Result BoardModuleSource::ReadModuleInfo(const a2b::SubNodeIdentity& node, uint32_t offset,
                                              uint8_t* buf, size_t len) {
  const ModuleProfile* profile = Using(node.position);
  if (profile == nullptr) {
    return m_Eeprom.ReadModuleInfo(node, offset, buf, len);
  }
  const bool own = (profile == Board(node.position));
  const std::vector<uint8_t> image =
      MakeModuleImage(*profile, node.productId, own ? m_Programs[node.position] : SigmaProgram());
  if (offset > image.size() || len > image.size() - offset) {
    return a2b::Result::kInvalidArgument;
  }
  std::memcpy(buf, &image[offset], len);
  return a2b::Result::kOk;
}

/**
 * Sets the board at a position, for nodes identified from then on.
 * @param position 0-15.
 * @param profile The board, which must outlive the source; nullptr: none.
 * @param program Its codec program, copied (DefaultProgram(), or one read from a file).
 */
void BoardModuleSource::SetBoard(a2b::NodeAddr position, const ModuleProfile* profile,
                                 const SigmaProgram& program) {
  if (!a2b::IsSubNode(position)) {
    return;
  }
  m_Boards[position] = profile;
  m_Programs[position] = (profile != nullptr) ? program : SigmaProgram();
}

/**
 * Sets the board image every node without a board of its own gets (the bench console's
 * `ignoreEEPROM 1`), without a codec program.
 * @param profile The board, which must outlive the source; nullptr: their EEPROMs.
 */
void BoardModuleSource::SetFallback(const ModuleProfile* profile) { m_Fallback = profile; }

/** The board set at @p position, or nullptr. */
const ModuleProfile* BoardModuleSource::Board(a2b::NodeAddr position) const {
  return a2b::IsSubNode(position) ? m_Boards[position] : nullptr;
}

/**
 * The profile a node gets without its EEPROM being read: its position's board if that
 * board is used with the node's chip; else, with a fallback set (the EEPROMs ignored), its
 * chip's no-EEPROM default (NoEepromDefault()) if it has one, else the fallback.
 * @param node The node.
 * @return The profile, or nullptr: the node's EEPROM (and, with no image there, its chip's
 *         default; Using() tells once the node is identified).
 */
const ModuleProfile* BoardModuleSource::ImageFor(const a2b::SubNodeIdentity& node) const {
  const ModuleProfile* board = Board(node.position);
  if (board != nullptr && Supports(*board, node.productId)) {
    return board;
  }
  if (m_Fallback == nullptr) {
    return nullptr;
  }
  const ModuleProfile* chipDefault = NoEepromDefault(node.productId);
  return (chipDefault != nullptr) ? chipDefault : m_Fallback;
}

/**
 * The profile whose image a node got at its last identification.
 * @param position The node.
 * @return The profile (its board, the fallback or its chip's no-EEPROM default), or
 *         nullptr: its EEPROM's image, or none.
 */
const ModuleProfile* BoardModuleSource::Using(a2b::NodeAddr position) const {
  return a2b::IsSubNode(position) ? m_Using[position] : nullptr;
}

}  // namespace clockworks
