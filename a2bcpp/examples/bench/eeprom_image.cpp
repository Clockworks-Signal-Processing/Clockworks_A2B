/**
 * @file eeprom_image.cpp
 * @brief Module-info images around an EEPROM: their length, their soundness, their
 *        Supplier Information block, and a description.
 *
 * The layout is ADI's PnP format as ADI's images and the stack have it
 * (a2bcpp/docs/provisioning.md; the stack's readers: src/module_info.cpp and
 * src/module_script.cpp). An image runs from the header to the last of:
 * - the module-info block (0x20, its channel groups and CRC);
 * - the Device Function (0x300) and Supplier Information (0x400) blocks, 256 bytes each
 *   with the CRC at their last byte, if there;
 * - the peripheral information block (0x800), each peripheral block it points to and the
 *   block's units.
 *
 * WalkImage() follows them through the bytes read so far: an EEPROM read back "to the end
 * of its image" reads on until the walk is complete, and a blank or foreign EEPROM is
 * told apart from an image. CheckImage() walks a whole image with every CRC checked, and
 * parses its start with the stack's own ParseModuleInfo(), so an image the stack would
 * refuse is not written. A wrong CRC the stack checks (the header's, the peripheral
 * blocks' and units') refuses the image; one it doesn't (the module-info block's, the
 * vendor blocks') is noted, so a module's image read back can always be written back.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "eeprom_image.h"

#include <cstdio>

#include "a2b/crc8.h"
#include "a2b/module_info.h"
#include "clockworks_boards.h"

namespace {

constexpr uint8_t kMarker = 0xAA;                ///< Byte 0 of an image.
constexpr size_t kHeaderCrc = 0x1F;              ///< The header's CRC-8.
constexpr size_t kHeaderLen = 3;                 ///< Every block's and unit's header.
constexpr uint32_t kModuleInfoAddr = 0x20;       ///< The module-info block (type 3).
constexpr uint32_t kDeviceFunctionAddr = 0x300;  ///< Device Function Information (type 7).
constexpr uint32_t kFixedBlockLen = 0x100;       ///< The two vendor blocks' size.
constexpr uint32_t kPeripheralInfoAddr = 0x800;  ///< Peripheral information (type 4).
constexpr size_t kPeripheralCount = 0x7A;        ///< Number of peripheral blocks.
constexpr size_t kPeripheralBlockLen = 83;       ///< A peripheral block (type 5), header too.
constexpr size_t kMaxPeripherals = 8;            ///< The stack's ModuleScript::kMaxBlocks.
constexpr size_t kMaxImage = 0x10000;            ///< 16-bit EEPROM addresses.

/** A block's or unit's type. */
uint8_t TypeOf(const uint8_t* header) { return static_cast<uint8_t>(header[0] >> 4); }

/** A block's or unit's 12-bit length. */
size_t SizeOf(const uint8_t* header) {
  return (static_cast<size_t>(header[0] & 0x0F) << 8) | header[1];
}

/** A 32-bit field, low byte first. */
uint32_t Get32(const uint8_t* at) {
  return static_cast<uint32_t>(at[0]) | (static_cast<uint32_t>(at[1]) << 8) |
         (static_cast<uint32_t>(at[2]) << 16) | (static_cast<uint32_t>(at[3]) << 24);
}

/** A 16-bit field, low byte first. */
unsigned Get16(const uint8_t* at) { return static_cast<unsigned>(at[0] | (at[1] << 8)); }

/** printf into a std::string. */
template <typename... Args>
std::string Format(const char* format, Args... args) {
  char text[256];
  std::snprintf(text, sizeof(text), format, args...);
  return text;
}

/** Text from a NUL-padded field. */
std::string Text(const uint8_t* at, size_t size) {
  std::string text;
  for (size_t i = 0; i < size && at[i] != 0; ++i) {
    text += (at[i] >= 0x20 && at[i] < 0x7F) ? static_cast<char>(at[i]) : '?';
  }
  return text;
}

/** One of the two 256-byte vendor blocks: its CRC at its last byte (ADI's images). */
bool FixedBlockCrcOk(const uint8_t* block) {
  return a2b::Crc8(block, kFixedBlockLen - 1) == block[kFixedBlockLen - 1];
}

/**
 * Walks an image. The steps share their state; each needs the bytes up to some offset,
 * and with fewer read it stops: kNeedMore, or kInvalid when no more will come.
 */
class Walker {
 public:
  Walker(const uint8_t* data, size_t len, bool final, bool checkCrcs)
      : m_Data(data), m_Len(len), m_Final(final), m_CheckCrcs(checkCrcs) {}

  ImageWalk Run(size_t& end, std::string& why);

 private:
  /** True if the bytes up to @p offset are there; else sets the outcome. */
  bool Have(size_t offset, const char* what);
  /** True if a part that may be absent is there to look at (absent in a final image). */
  bool Optional(size_t offset, bool& present);
  bool Fail(const std::string& why);
  bool Header();
  bool VendorBlock(uint32_t base, uint8_t type, const char* name);
  bool Peripherals();
  bool Units(size_t at, size_t count, size_t block);

  const uint8_t* m_Data;
  size_t m_Len;
  bool m_Final;
  bool m_CheckCrcs;
  size_t m_End = 0;  ///< The image's end, as far as walked.
  ImageWalk m_Outcome = ImageWalk::kComplete;
  size_t m_Need = 0;
  std::string m_Why;
  std::string m_Notes;  ///< CRCs wrong where the stack doesn't check them.
};

ImageWalk Walker::Run(size_t& end, std::string& why) {
  if (Header() && VendorBlock(kDeviceFunctionAddr, 7, "device function") &&
      VendorBlock(kSupplierInfoAddr, 8, "supplier information") && Peripherals()) {
    end = m_End;
    why = m_Notes;
    return ImageWalk::kComplete;
  }
  end = m_Need;
  why = m_Why;
  return m_Outcome;
}

bool Walker::Have(size_t offset, const char* what) {
  if (offset > kMaxImage) {
    return Fail(
        Format("%s runs to 0x%X, past 16-bit addresses", what, static_cast<unsigned>(offset)));
  }
  if (offset <= m_Len) {
    return true;
  }
  if (m_Final) {
    return Fail(Format("it ends at 0x%X; %s runs to 0x%X", static_cast<unsigned>(m_Len), what,
                       static_cast<unsigned>(offset)));
  }
  m_Outcome = ImageWalk::kNeedMore;
  m_Need = offset;
  return false;
}

bool Walker::Optional(size_t offset, bool& present) {
  present = offset <= m_Len;
  if (present || m_Final) {
    return true;
  }
  m_Outcome = ImageWalk::kNeedMore;
  m_Need = offset;
  return false;
}

bool Walker::Fail(const std::string& why) {
  m_Outcome = ImageWalk::kInvalid;
  m_Why = why;
  return false;
}

/** The header and the module-info block. */
bool Walker::Header() {
  if (!Have(a2b::kModuleInfoLen, "the image's start")) {
    return false;
  }
  if (m_Data[0] != kMarker) {
    return Fail(m_Data[0] == 0xFF ? "blank: byte 0 is 0xFF, not the marker 0xAA"
                                  : Format("byte 0 is 0x%02X, not the marker 0xAA", m_Data[0]));
  }
  if (m_CheckCrcs && a2b::Crc8(m_Data, kHeaderCrc) != m_Data[kHeaderCrc]) {
    return Fail("the header's CRC is wrong");
  }
  if (TypeOf(&m_Data[kModuleInfoAddr]) != 3 || SizeOf(&m_Data[kModuleInfoAddr]) == 0) {
    return Fail("no module-info block at 0x20");
  }
  m_End = kModuleInfoAddr + kHeaderLen + SizeOf(&m_Data[kModuleInfoAddr]);
  if (m_End > kPeripheralInfoAddr) {
    return Fail("the module-info block runs past 0x7FF");
  }
  if (!Have(m_End, "the module-info block")) {
    return false;
  }
  if (m_CheckCrcs &&
      a2b::Crc8(&m_Data[kModuleInfoAddr], m_End - 1 - kModuleInfoAddr) != m_Data[m_End - 1]) {
    m_Notes += "the module-info block's CRC is wrong (the stack doesn't check it); ";
  }
  return true;
}

/** A vendor block, if the image has one: its CRC at its last byte. */
bool Walker::VendorBlock(uint32_t base, uint8_t type, const char* name) {
  bool present = false;
  if (!Optional(base + kHeaderLen, present)) {
    return false;
  }
  if (!present || TypeOf(&m_Data[base]) != type) {
    return true;
  }
  if (!Have(base + kFixedBlockLen, name)) {
    return false;
  }
  if (m_CheckCrcs && !FixedBlockCrcOk(&m_Data[base])) {
    m_Notes += Format("the %s block's CRC is wrong (the stack doesn't read it); ", name);
  }
  m_End = (m_End > base + kFixedBlockLen) ? m_End : base + kFixedBlockLen;
  return true;
}

/** The peripheral information block and each peripheral block with its units. */
bool Walker::Peripherals() {
  const size_t count = m_Data[kPeripheralCount];
  bool present = true;
  if (count == 0 && !Optional(kPeripheralInfoAddr + kHeaderLen, present)) {
    return false;
  }
  if (count == 0 && (!present || TypeOf(&m_Data[kPeripheralInfoAddr]) != 4)) {
    return true;
  }
  if (count > kMaxPeripherals) {
    return Fail(
        Format("%u peripheral blocks; the stack runs at most 8", static_cast<unsigned>(count)));
  }
  if (!Have(kPeripheralInfoAddr + kHeaderLen, "the peripheral information block")) {
    return false;
  }
  const uint8_t* info = &m_Data[kPeripheralInfoAddr];
  const size_t infoLen = SizeOf(info);
  if (TypeOf(info) != 4 || infoLen < 4 * count + 1) {
    return Fail("no peripheral information block at 0x800 for its peripheral blocks");
  }
  const size_t infoEnd = kPeripheralInfoAddr + kHeaderLen + infoLen;
  if (!Have(infoEnd, "the peripheral information block")) {
    return false;
  }
  if (m_CheckCrcs && a2b::Crc8(info, infoEnd - 1 - kPeripheralInfoAddr) != m_Data[infoEnd - 1]) {
    return Fail("the peripheral information block's CRC is wrong");
  }
  m_End = (m_End > infoEnd) ? m_End : infoEnd;
  for (size_t i = 0; i < count; ++i) {
    const size_t at = Get32(&info[kHeaderLen + 4 * i]);
    if (!Have(at + kPeripheralBlockLen, "a peripheral block")) {
      return false;
    }
    const uint8_t* block = &m_Data[at];
    if (TypeOf(block) != 5 || SizeOf(block) != kPeripheralBlockLen - kHeaderLen) {
      return Fail(Format("no peripheral block at 0x%X", static_cast<unsigned>(at)));
    }
    if (m_CheckCrcs &&
        a2b::Crc8(block, kPeripheralBlockLen - 1) != block[kPeripheralBlockLen - 1]) {
      return Fail(
          Format("the peripheral block at 0x%X has a wrong CRC", static_cast<unsigned>(at)));
    }
    const uint8_t* payload = block + kHeaderLen;
    const size_t units = (static_cast<size_t>(payload[77] & 0x0F) << 8) | payload[78];
    if (!Units(at + kPeripheralBlockLen, units, at)) {
      return false;
    }
  }
  return true;
}

/** A peripheral block's units, from @p at. */
bool Walker::Units(size_t at, size_t count, size_t block) {
  m_End = (m_End > at) ? m_End : at;
  for (size_t n = 0; n < count; ++n) {
    if (!Have(at + kHeaderLen, "a peripheral unit")) {
      return false;
    }
    const uint8_t* head = &m_Data[at];
    const uint8_t type = TypeOf(head);
    const size_t size = SizeOf(head);
    if (type == 2) {
      if (m_CheckCrcs && a2b::Crc8(head, 2) != head[2]) {
        return Fail(Format("a wait's CRC at 0x%X is wrong", static_cast<unsigned>(at)));
      }
      at += kHeaderLen;
    } else if (type == 0 || type == 1) {
      if (!Have(at + kHeaderLen + size, "a peripheral write")) {
        return false;
      }
      if (m_CheckCrcs &&
          (size < static_cast<size_t>(type == 1 ? 2 : 1) || head[2] > 0x7F ||
           (type == 1 && a2b::Crc8(head, kHeaderLen + size - 1) != head[kHeaderLen + size - 1]))) {
        return Fail(Format("the write at 0x%X is not sound (length, address or CRC)",
                           static_cast<unsigned>(at)));
      }
      at += kHeaderLen + size;
    } else {
      return Fail(Format("unit type %u at 0x%X in the peripheral block at 0x%X",
                         static_cast<unsigned>(type), static_cast<unsigned>(at),
                         static_cast<unsigned>(block)));
    }
    m_End = (m_End > at) ? m_End : at;
  }
  return true;
}

}  // namespace

/**
 * Finds an image's length from the bytes read so far: its blocks are followed as the stack
 * would read them.
 * @param data The bytes from EEPROM address 0.
 * @param len How many.
 * @param final No more bytes will come (a file, or the whole EEPROM read): a part that
 *        isn't there is absent (or, if needed, the image is cut short).
 * @param checkCrcs Check each block's and unit's CRC-8 on the way.
 * @param end Set to the image's length (kComplete) or the bytes needed to go on
 *        (kNeedMore).
 * @param why Set to what is wrong (kInvalid); with kComplete, the CRCs the stack doesn't
 *        check found wrong, or empty.
 * @return Where the walk got to.
 */
ImageWalk WalkImage(const uint8_t* data, size_t len, bool final, bool checkCrcs, size_t& end,
                    std::string& why) {
  Walker walker(data, len, final, checkCrcs);
  return walker.Run(end, why);
}

/**
 * Checks an image before it is written: its blocks and units are sound with every CRC
 * right, the stack's parser takes its start, and it fits the EEPROM.
 * @param image The image from EEPROM address 0 (a .dat file; it may run on past the image,
 *        e.g. a whole EEPROM read back).
 * @param eepromSize The EEPROM's size in bytes.
 * @param why What is wrong; on success, what to know (empty if nothing).
 * @return True if it may be written.
 */
bool CheckImage(const std::vector<uint8_t>& image, size_t eepromSize, std::string& why) {
  why.clear();
  size_t end = 0;
  std::string problem;
  if (WalkImage(image.data(), image.size(), true, true, end, problem) != ImageWalk::kComplete) {
    why = "not a sound image: " + problem;
    return false;
  }
  const std::string notes = problem.empty() ? problem : problem.substr(0, problem.size() - 2);
  if (image.size() > eepromSize) {
    why = Format("%u bytes: more than the EEPROM's %u", static_cast<unsigned>(image.size()),
                 static_cast<unsigned>(eepromSize));
    return false;
  }
  a2b::ModuleInfo info;
  if (a2b::ParseModuleInfo(image.data(), image.size(), info) != a2b::Result::kOk) {
    why = "the stack would refuse it (ParseModuleInfo: a reduced I2S rate?)";
    return false;
  }
  a2b::ChannelGroups groups;
  const a2b::Result grouped =
      a2b::ParseChannelGroups(info.groupTableLen != 0 ? &image[a2b::kGroupTableOffset] : nullptr,
                              info.groupTableLen, groups);
  if (grouped == a2b::Result::kInvalidData) {
    why = "its channel-group table is not sound";
    return false;
  }
  why = notes;
  if (grouped == a2b::Result::kNoSpace) {
    why += std::string(why.empty() ? "" : "; ") +
           "more than 8 groups in a direction: the stack keeps the first 8";
  }
  if (image.size() > end) {
    why += Format("%sthe file runs on past the image's %u bytes (all of it is written)",
                  why.empty() ? "" : "; ", static_cast<unsigned>(end));
  }
  return true;
}

/**
 * Reads an image's Supplier Information block.
 * @param image The image.
 * @param info Filled with its vendor ID, data and CRC check.
 * @return False if the image has no such block (too short, or another type at 0x400).
 */
bool ParseSupplierInfo(const std::vector<uint8_t>& image, SupplierInfo& info) {
  info = SupplierInfo();
  if (image.size() < kSupplierInfoAddr + kFixedBlockLen || TypeOf(&image[kSupplierInfoAddr]) != 8) {
    return false;
  }
  info.vendorId = Text(&image[kSupplierInfoAddr + kHeaderLen], kSupplierVendorIdLen);
  info.data.assign(image.begin() + kSupplierDataAddr,
                   image.begin() + kSupplierDataAddr + kSupplierDataLen);
  info.crcOk = FixedBlockCrcOk(&image[kSupplierInfoAddr]);
  return true;
}

/**
 * Describes an image: its identity, power, settings, groups, peripherals and vendor data,
 * one line each, for the bench console's `eeprom info`. A fuller decoder is the image
 * tool's (`a2b_eeprom.py decode`).
 * @param image The image from EEPROM address 0.
 * @return The text; it says so if the image isn't sound.
 */
std::string DescribeImage(const std::vector<uint8_t>& image) {
  size_t end = 0;
  std::string why;
  const ImageWalk walk = WalkImage(image.data(), image.size(), true, true, end, why);
  if (walk != ImageWalk::kComplete) {
    return "  Not a sound image: " + why + "\n";
  }
  const std::string notes = why;
  const uint8_t* d = image.data();
  std::string text =
      Format("  Image: %u bytes; module %04X:%04X, version 0x%04X, SW %u.%u.%u.%u, for %s\n",
             static_cast<unsigned>(end), Get16(&d[0x01]), Get16(&d[0x03]), Get16(&d[0x05]), d[0x09],
             d[0x0A], d[0x0B], d[0x0C], TargetChipName(Get16(&d[0x07])).c_str());
  text += Format("  Name '%s', CFG-%u, %s powered, most %u mW, least %u mW, most %u mA\n",
                 Text(&d[0x2B], 60).c_str(), static_cast<unsigned>(d[0x23] >> 4),
                 (d[0x23] & 0x01) != 0 ? "locally" : "bus", Get16(&d[0x24]), Get16(&d[0x26]),
                 Get16(&d[0x29]));
  const unsigned cableM = Get16(&d[0x6C]);
  text += Format("  B port %s, its cable %s\n", BPortTypeName(d[0x28]).c_str(),
                 cableM == 0 ? "not given" : Format("%u m", cableM).c_str());
  static const char* const kRegs[] = {"I2CCFG",  "SPICFG",  "SPICKDIV", "SPIPINCFG", "I2SCFG",
                                      "I2SGCFG", "I2SRATE", "PDMCTL",   "PDMCTL2",   "CLK1CFG",
                                      "CLK2CFG", "GPIOOEN", "GPIOIEN",  "PINTEN",    "PINTINV",
                                      "PINCFG",  "GPIODAT"};
  std::string regs;
  for (size_t i = 0; i < sizeof(kRegs) / sizeof(kRegs[0]); ++i) {
    const uint8_t value = d[0x7F + i];
    const uint8_t powerOn = (0x7F + i == 0x8E) ? 0x01 : 0x00;
    if (value != powerOn) {
      regs += Format("%s%s 0x%02X", regs.empty() ? "" : ", ", kRegs[i], value);
    }
  }
  text += "  Settings: " + (regs.empty() ? std::string("all at power-on values") : regs) + "\n";
  if ((d[0x7F] & 0x01) != 0) {
    text += "  I2C port: 400 kHz once the start is read (I2CCFG.DATARATE)\n";
  }
  a2b::ModuleInfo info;
  if (a2b::ParseModuleInfo(image.data(), image.size(), info) == a2b::Result::kOk &&
      info.groupTableLen != 0) {
    size_t at = a2b::kGroupTableOffset;
    for (const char* direction : {"TX", "RX"}) {
      const size_t count = d[at++];
      for (size_t g = 0; g < count && at + 37 <= image.size(); ++g, at += 37) {
        text += Format("  %s group %u '%s': channels 0x%08X\n", direction, d[at],
                       Text(&d[at + 1], 32).c_str(), static_cast<unsigned>(Get32(&d[at + 33])));
      }
    }
  }
  const size_t count = d[kPeripheralCount];
  for (size_t i = 0; i < count; ++i) {
    const size_t at = Get32(&d[kPeripheralInfoAddr + kHeaderLen + 4 * i]);
    const uint8_t* payload = &d[at + kHeaderLen];
    const size_t units = (static_cast<size_t>(payload[77] & 0x0F) << 8) | payload[78];
    static const char* const kInterfaces[] = {"I2C", "SPI"};
    text += Format("  Peripheral %u '%s': %s at 0x%02X, %u unit(s)\n", static_cast<unsigned>(i + 1),
                   Text(&payload[1], 64).c_str(),
                   payload[0] < 2 ? kInterfaces[payload[0]] : "not programmable", payload[65],
                   static_cast<unsigned>(units));
  }
  if (!notes.empty()) {
    text += "  Note: " + notes.substr(0, notes.size() - 2) + "\n";
  }
  SupplierInfo supplier;
  if (ParseSupplierInfo(image, supplier)) {
    text += Format("  Vendor '%s'%s\n", supplier.vendorId.c_str(),
                   supplier.crcOk ? "" : " (its block's CRC is wrong)");
    clockworks::ClockworksSection section;
    if (clockworks::ParseClockworksSection(supplier.vendorId, supplier.data.data(),
                                           supplier.data.size(), section)) {
      text += "    ID '" + section.id + "'\n";
      for (size_t i = 0; i < section.features.size(); ++i) {
        const clockworks::ClockworksFeature& f = section.features[i];
        text += Format("    Feature %u: UID 0x%08X%08X '%s' %ld %ld '%s'\n",
                       static_cast<unsigned>(i + 1), static_cast<unsigned>(f.uid >> 32),
                       static_cast<unsigned>(f.uid & 0xFFFFFFFFu), f.name.c_str(),
                       static_cast<long>(f.value0), static_cast<long>(f.value1), f.text.c_str());
      }
    }
  }
  return text;
}

/**
 * A module-info image's target chip in words (header 0x07-0x08, ModuleInfo::targetChip).
 * @param targetChip The field.
 * @return "AD2437" for 0x2437, "External" (a plug-in transceiver module), or "not stated"
 *         (0x0000, as ADI's images have it, or 0xFFFF).
 */
std::string TargetChipName(uint16_t targetChip) {
  if (targetChip == a2b::kChipNotStated || targetChip == 0xFFFF) {
    return "not stated";
  }
  if (targetChip == a2b::kChipExternal) {
    return "External";
  }
  return Format("AD%04X", static_cast<unsigned>(targetChip));
}

/**
 * A module's B port type in words (image byte 0x28, a2b::BPortType).
 * @param bPortType The field.
 * @return Its name: XLR, RJ45, UTP, RJ45-CUSTOM, RJ45-CUSTOM-INV, CUSTOM, or "reserved (N)".
 */
std::string BPortTypeName(uint8_t bPortType) {
  static const char* const kNames[] = {"XLR",   "RJ45", "UTP", "RJ45-CUSTOM", "RJ45-CUSTOM-INV",
                                       "CUSTOM"};
  if (bPortType < sizeof(kNames) / sizeof(kNames[0])) {
    return kNames[bPortType];
  }
  return Format("reserved (%u)", static_cast<unsigned>(bPortType));
}
