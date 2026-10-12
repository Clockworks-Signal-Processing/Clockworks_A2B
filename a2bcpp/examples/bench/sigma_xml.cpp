/**
 * @file sigma_xml.cpp
 * @brief Reads SigmaStudio's XML program export into I2C writes and waits.
 *
 * SigmaStudio writes a codec's program as a `<ROM>` file of `<action>` elements, in the
 * order they are to run, in two layouts: with each write's `i2caddr` and `addr_width`, or
 * without them. The AB0020's two ADAU1761 programs
 * (`tools/SS+reference/2x_AB0020_EVM_with_2437/AB0020-main-node-adau1761.xml` and
 * `AB0020-sub-node-ADAU1761.xml`) are the second kind, with a `read` of the codec's PLL
 * register after the program.
 *
 * The actions:
 * - `instr="writeXbytes"`: the attributes `addr` (the sub-address, decimal), `len`
 *   (sub-address and data bytes), `addr_width` (the sub-address's bytes; else `len` less
 *   the data) and `i2caddr` (7-bit, decimal; else the caller's default); the content is
 *   the data, in hex bytes.
 * - `instr="delay"`: no attribute read; the content is the wait in ms, in hex bytes, high
 *   byte first.
 * - `instr="read"`: skipped and counted, as a peripheral script has no reads.
 *
 * The delay's unit and byte order were checked against the bench project's export
 * (`adi_a2b_busconfig.c`, beside the XMLs), whose codec scripts are the same programs: the
 * same wait at the same place is an `A2B_DEALY_OP` of 0x64, the 100 ms of the AB0020's
 * codec (scenario 13), where the XML has `00 64`. A write's sub-address goes first, high byte
 * first, as the export's writes have them (`addr="16619"`, 0x40EB). Other attributes (`CellName`,
 * `ParamName`, `data_width`, `AddrIncr`) are not used. Anything else is refused with its
 * line: another action, a `len` that doesn't match, a write or wait too long for a
 * peripheral-script unit.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sigma_xml.h"

#include <cstdio>
#include <cstdlib>

namespace {

/** The line of @p text that offset @p at is on, from 1. */
size_t LineOf(const std::string& text, size_t at) {
  size_t line = 1;
  for (size_t i = 0; i < at && i < text.size(); ++i) {
    line += (text[i] == '\n') ? 1 : 0;
  }
  return line;
}

/**
 * Finds attribute @p name in an element's attribute text.
 * @return True with its value in @p value, or false if the element hasn't got it.
 */
bool Attribute(const std::string& attributes, const char* name, std::string& value) {
  const std::string key = std::string(" ") + name + "=\"";
  const size_t at = (" " + attributes).find(key);
  if (at == std::string::npos) {
    return false;
  }
  const size_t from = at + key.size() - 1;  // Less the space added in front.
  const size_t to = attributes.find('"', from);
  if (to == std::string::npos) {
    return false;
  }
  value = attributes.substr(from, to - from);
  return true;
}

/** Reads a decimal number, or a hexadecimal one with 0x; false if @p text isn't one. */
bool ToNumber(const std::string& text, unsigned long& value) {
  if (text.empty()) {
    return false;
  }
  const bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
  char* end = nullptr;
  value = std::strtoul(text.c_str(), &end, hex ? 16 : 10);
  return *end == '\0';
}

/** Reads white-space separated hex bytes ("00 64"); false if one isn't a byte. */
bool ToBytes(const std::string& text, std::vector<uint8_t>& bytes) {
  bytes.clear();
  size_t at = 0;
  while (true) {
    at = text.find_first_not_of(" \t\r\n", at);
    if (at == std::string::npos) {
      return true;
    }
    const size_t end = text.find_first_of(" \t\r\n", at);
    const std::string word = text.substr(at, end - at);
    char* stop = nullptr;
    const unsigned long value = std::strtoul(word.c_str(), &stop, 16);
    if (word.size() > 2 || *stop != '\0' || value > 0xFF) {
      return false;
    }
    bytes.push_back(static_cast<uint8_t>(value));
    if (end == std::string::npos) {
      return true;
    }
    at = end;
  }
}

/**
 * Reads one `writeXbytes` action into @p action.
 * @return An empty string, or what is wrong with it.
 */
std::string ReadWrite(const std::string& attributes, const std::vector<uint8_t>& data,
                      uint8_t defaultI2cAddr, SigmaAction& action) {
  std::string text;
  unsigned long i2cAddr = defaultI2cAddr;
  unsigned long addr = 0;
  unsigned long width = 0;
  unsigned long len = 0;
  const bool hasLen = Attribute(attributes, "len", text);
  if (hasLen && (!ToNumber(text, len) || len < data.size())) {
    return "a len shorter than the data";
  }
  const bool hasWidth = Attribute(attributes, "addr_width", text);
  if (hasWidth && !ToNumber(text, width)) {
    return "an addr_width that isn't a number";
  }
  if (!hasWidth && !hasLen) {
    return "neither addr_width nor len, so no sub-address size";
  }
  width = hasWidth ? width : len - data.size();
  if (hasWidth && hasLen && len != width + data.size()) {
    return "len is not addr_width plus the data bytes";
  }
  if (Attribute(attributes, "i2caddr", text) && !ToNumber(text, i2cAddr)) {
    return "an i2caddr that isn't a number";
  }
  if (i2cAddr == 0 || i2cAddr > 0x7F) {
    return "no 7-bit i2caddr";
  }
  if (width > 4 || !Attribute(attributes, "addr", text) || !ToNumber(text, addr) ||
      (width < 4 && addr >= (1UL << (8 * width)))) {
    return "no addr that fits its 0-4 bytes";
  }
  action.wait = false;
  action.i2cAddr = static_cast<uint8_t>(i2cAddr);
  action.bytes.clear();
  for (unsigned long i = width; i > 0; --i) {
    action.bytes.push_back(static_cast<uint8_t>(addr >> (8 * (i - 1))));
  }
  action.bytes.insert(action.bytes.end(), data.begin(), data.end());
  if (action.bytes.size() > kMaxSigmaWrite) {
    return "a write longer than " + std::to_string(kMaxSigmaWrite) + " bytes";
  }
  if (data.empty()) {
    return "a write with no data";
  }
  return std::string();
}

}  // namespace

/**
 * Reads a SigmaStudio XML program from its text: each `<action>` in order.
 * @param text The file's text.
 * @param defaultI2cAddr The 7-bit address of writes without `i2caddr`; 0 for none.
 * @param program Filled with the writes and waits and the reads skipped; emptied first.
 * @param error Set to what is wrong, with its line, on failure.
 * @return True if every action was read and there is at least one write or wait.
 */
bool ParseSigmaXml(const std::string& text, uint8_t defaultI2cAddr, SigmaProgram& program,
                   std::string& error) {
  program = SigmaProgram();
  std::vector<SigmaAction>& actions = program.actions;
  size_t at = 0;
  while ((at = text.find("<action", at)) != std::string::npos) {
    const size_t line = LineOf(text, at);
    const size_t open = text.find('>', at);
    const size_t close = (open == std::string::npos) ? open : text.find("</action>", open);
    if (close == std::string::npos) {
      error = "line " + std::to_string(line) + ": an unclosed <action>";
      return false;
    }
    const std::string attributes = text.substr(at + 7, open - at - 7);
    std::vector<uint8_t> data;
    std::string instr;
    std::string why;
    if (!ToBytes(text.substr(open + 1, close - open - 1), data)) {
      why = "content that isn't hex bytes";
    } else if (!Attribute(attributes, "instr", instr)) {
      why = "no instr";
    } else if (instr == "writeXbytes") {
      SigmaAction action;
      why = ReadWrite(attributes, data, defaultI2cAddr, action);
      if (why.empty()) {
        actions.push_back(action);
      }
    } else if (instr == "delay") {
      unsigned long ms = 0;
      for (uint8_t byte : data) {
        ms = (ms << 8) | byte;
      }
      if (data.empty() || data.size() > 2 || ms > kMaxSigmaWaitMs) {
        why = "a delay that isn't 0-" + std::to_string(kMaxSigmaWaitMs) + " ms in 1 or 2 bytes";
      } else {
        SigmaAction action;
        action.wait = true;
        action.waitMs = static_cast<uint16_t>(ms);
        actions.push_back(action);
      }
    } else if (instr == "read") {
      ++program.readsSkipped;
    } else {
      why = "instr \"" + instr + "\", which the console doesn't run";
    }
    if (!why.empty()) {
      error = "line " + std::to_string(line) + ": " + why;
      return false;
    }
    at = close;
  }
  if (actions.empty()) {
    error = "no <action> elements";
    return false;
  }
  return true;
}

/**
 * Reads a SigmaStudio XML program from a file; see ParseSigmaXml().
 * @param path The file.
 * @param defaultI2cAddr The 7-bit address of writes without `i2caddr`; 0 for none.
 * @param program Filled with the writes and waits and the reads skipped.
 * @param error Set to what is wrong on failure.
 * @return True if the file was read and every action in it.
 */
bool ReadSigmaXml(const std::string& path, uint8_t defaultI2cAddr, SigmaProgram& program,
                  std::string& error) {
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    error = "can't open " + path;
    return false;
  }
  std::string text;
  char buffer[4096];
  size_t got = 0;
  while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
    text.append(buffer, got);
  }
  std::fclose(file);
  if (!ParseSigmaXml(text, defaultI2cAddr, program, error)) {
    error = path + ", " + error;
    return false;
  }
  return true;
}
