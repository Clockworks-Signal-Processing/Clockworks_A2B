/**
 * @file bench_console_files.cpp
 * @brief The bench console's commands that need a file system: `module N file`, the
 *        `eeprom` commands (an EepromJob, a transfer per poll) and `i2cTrace`. Built only
 *        with A2B_BENCH_FILES (the desktop hosts); the RP2040 console leaves them out.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdio>
#include <string>
#include <vector>

#include "a2b/module_info.h"
#include "bench_console.h"
#include "console_text.h"
#include "eeprom_image.h"

using bench::Hex;
using bench::Same;
using bench::ToNumber;

namespace {

/** Reads a whole file; false (and nothing read) if it can't be opened or is over 64 KB. */
bool ReadBinary(const std::string& path, std::vector<uint8_t>& data) {
  data.clear();
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    return false;
  }
  uint8_t buffer[4096];
  size_t got = 0;
  while ((got = std::fread(buffer, 1, sizeof(buffer), file)) > 0 && data.size() <= 0x10000) {
    data.insert(data.end(), buffer, buffer + got);
  }
  std::fclose(file);
  return data.size() <= 0x10000;
}

/** Writes @p data to a file; false if it can't be written. */
bool WriteBinary(const std::string& path, const std::vector<uint8_t>& data) {
  std::FILE* file = std::fopen(path.c_str(), "wb");
  if (file == nullptr) {
    return false;
  }
  const bool written = std::fwrite(data.data(), 1, data.size(), file) == data.size();
  return (std::fclose(file) == 0) && written;
}

}  // namespace
/**
 * `module N|all file FILE`: the positions' module info from an image file (a .dat, as the
 * image tool writes it and `eeprom read` reads one), checked as `eeprom write` checks it,
 * for nodes identified from then on, EEPROMs read or not.
 * @param first The first position.
 * @param last The last.
 * @param path The file.
 */
void BenchConsole::ModuleImage(int first, int last, const std::string& path) {
  std::vector<uint8_t> image;
  if (!ReadBinary(path, image)) {
    Print("Can't read %s (or it is over 64 KB)\n", path.c_str());
    return;
  }
  std::string why;
  if (!CheckImage(image, 0x10000, why)) {
    Print("%s: %s\n", path.c_str(), why.c_str());
    return;
  }
  for (int n = first; n <= last; ++n) {
    m_Modules.SetImage(static_cast<a2b::NodeAddr>(n), image);
    m_ImageFiles[n] = path;
    m_ModuleFiles[n].clear();
    m_ModuleWrites[n] = 0;
  }
  const std::string who = (first == last) ? "Sub-node " + std::to_string(first) : "Every sub-node";
  Print("%s: the image %s, %u bytes (for nodes identified from now on)\n", who.c_str(),
        path.c_str(), static_cast<unsigned>(image.size()));
  Print("%s", DescribeImage(image).c_str());
  if (!why.empty()) {
    Print("  note: %s\n", why.c_str());
  }
  a2b::ModuleInfo info;
  if (a2b::ParseModuleInfo(image.data(), image.size(), info) != a2b::Result::kOk) {
    return;
  }
  for (int n = first; n <= last; ++n) {
    const a2b::SubNodeInfo* node = m_Network.SubNodeInfoAt(static_cast<a2b::NodeAddr>(n));
    if (node != nullptr && !a2b::ImageFitsChip(info, node->identity.productId)) {
      Print(
          "  note: it is for the %s; sub-node %d is an AD24%02X, which the stack would "
          "reject with it\n",
          TargetChipName(info.targetChip).c_str(), n, node->identity.productId);
    }
  }
}

/**
 * `eeprom ...`: a sub-node's module-info EEPROM read into a file, written from one,
 * compared with one, described or erased, by an EepromJob that Poll() runs on; or the
 * EEPROM settings. A file is checked before anything is sent (CheckImage()).
 * @param args The command line's words.
 */
void BenchConsole::Eeprom(const std::vector<std::string>& args) {
  const char* usage =
      "eeprom [config ...], eeprom read N FILE [LEN|all], eeprom write|verify N FILE, eeprom "
      "info|erase N, eeprom abort (N: a sub-node in the bus)\n";
  if (args.size() == 1) {
    const EepromConfig& c = m_EepromConfig;
    Print("EEPROM: address %s, %u bytes, %u-byte pages, %u ms write cycle, %u-byte reads\n",
          m_EepromAddrSet ? Hex(&c.i2cAddr, 1).insert(0, "0x").c_str() : "where found (else 0x50)",
          static_cast<unsigned>(c.size), static_cast<unsigned>(c.pageSize),
          static_cast<unsigned>(c.writeCycleMs), static_cast<unsigned>(c.readChunk));
    if (Busy()) {
      Print("A job is running on sub-node %u\n", static_cast<unsigned>(m_EepromJob.Node()));
    }
    return;
  }
  const std::string& what = args[1];
  if (Same(what, "config")) {
    EepromConfigure(args);
    return;
  }
  if (Same(what, "abort")) {
    if (!Busy()) {
      Print("No EEPROM job is running\n");
      return;
    }
    m_EepromJob.Abort();
    EepromFinished();
    return;
  }
  a2b::NodeAddr node = 0;
  const bool withFile = Same(what, "read") || Same(what, "write") || Same(what, "verify");
  const bool alone = Same(what, "info") || Same(what, "erase");
  if ((!withFile && !alone) || args.size() < 3 || (alone && args.size() != 3) ||
      (withFile && args.size() < 4) || args.size() > 5 ||
      (args.size() == 5 && !Same(what, "read"))) {
    Print("%s", usage);
    return;
  }
  if (!EepromNode(args[2], node)) {
    return;
  }
  const EepromConfig config = EepromFor(node);
  m_EepromInfo = false;
  m_EepromFile.clear();
  if (Same(what, "read")) {
    long len = 0;
    if (args.size() == 5) {
      if (Same(args[4], "all")) {
        len = static_cast<long>(config.size);
      } else if (!ToNumber(args[4], static_cast<long>(config.size), len) || len == 0) {
        Print("eeprom read: LEN is 1-%u, or all\n", static_cast<unsigned>(config.size));
        return;
      }
    }
    m_EepromFile = args[3];
    m_EepromJob.StartRead(node, static_cast<size_t>(len), config);
    Print("Reading sub-node %u's EEPROM at 0x%02X%s ...\n", static_cast<unsigned>(node),
          config.i2cAddr, len == 0 ? " to the end of its image" : "");
  } else if (Same(what, "info")) {
    m_EepromInfo = true;
    m_EepromJob.StartRead(node, 0, config);
    Print("Reading sub-node %u's EEPROM at 0x%02X ...\n", static_cast<unsigned>(node),
          config.i2cAddr);
  } else if (Same(what, "erase")) {
    m_EepromJob.StartErase(node, config);
    Print("Clearing sub-node %u's marker (EEPROM 0x%02X) ...\n", static_cast<unsigned>(node),
          config.i2cAddr);
  } else {
    std::vector<uint8_t> image;
    if (!ReadBinary(args[3], image)) {
      Print("Can't read %s (or it is over 64 KB)\n", args[3].c_str());
      return;
    }
    std::string why;
    if (!CheckImage(image, config.size, why)) {
      Print("%s: %s; nothing sent\n", args[3].c_str(), why.c_str());
      return;
    }
    if (!why.empty()) {
      Print("  note: %s\n", why.c_str());
    }
    const bool write = Same(what, "write");
    const a2b::SubNodeInfo* target = m_Network.SubNodeInfoAt(static_cast<a2b::NodeAddr>(node));
    a2b::ModuleInfo info;
    if (write && target != nullptr &&
        a2b::ParseModuleInfo(image.data(), image.size(), info) == a2b::Result::kOk &&
        !a2b::ImageFitsChip(info, target->identity.productId)) {
      Print(
          "%s is for the %s; sub-node %u is an AD24%02X, which the stack would reject with "
          "it: nothing sent\n",
          args[3].c_str(), TargetChipName(info.targetChip).c_str(), static_cast<unsigned>(node),
          target->identity.productId);
      return;
    }
    if (write) {
      m_EepromJob.StartWrite(node, image, config);
    } else {
      m_EepromJob.StartVerify(node, image, config);
    }
    Print("%s %s (%u bytes) %s sub-node %u's EEPROM at 0x%02X ...\n",
          write ? "Writing" : "Comparing", args[3].c_str(), static_cast<unsigned>(image.size()),
          write ? "to" : "with", static_cast<unsigned>(node), config.i2cAddr);
  }
  if (!Busy()) {
    EepromFinished();  // Refused at once.
  }
}

/**
 * `eeprom config [addr A|found] [size BYTES] [page BYTES] [cycle MS] [chunk BYTES]`: the
 * sub-nodes' EEPROM, for the jobs started from then on.
 * @param args The command line's words.
 */
void BenchConsole::EepromConfigure(const std::vector<std::string>& args) {
  EepromConfig c = m_EepromConfig;
  bool addrSet = m_EepromAddrSet;
  for (size_t i = 2; i < args.size(); i += 2) {
    long value = 0;
    const std::string& key = args[i];
    const bool hasValue = i + 1 < args.size();
    if (Same(key, "addr") && hasValue && Same(args[i + 1], "found")) {
      addrSet = false;
    } else if (Same(key, "addr") && hasValue && ToNumber(args[i + 1], 0x7F, value) && value != 0) {
      c.i2cAddr = static_cast<uint8_t>(value);
      addrSet = true;
    } else if (Same(key, "size") && hasValue && ToNumber(args[i + 1], 0x10000, value) &&
               value >= 0x100) {
      c.size = static_cast<size_t>(value);
    } else if (Same(key, "page") && hasValue && ToNumber(args[i + 1], 256, value) && value >= 1 &&
               (value & (value - 1)) == 0) {
      c.pageSize = static_cast<size_t>(value);
    } else if (Same(key, "cycle") && hasValue && ToNumber(args[i + 1], 100, value)) {
      c.writeCycleMs = static_cast<uint32_t>(value);
    } else if (Same(key, "chunk") && hasValue && ToNumber(args[i + 1], 4096, value) && value >= 1) {
      c.readChunk = static_cast<size_t>(value);
    } else {
      Print(
          "eeprom config [addr 0x01-0x7F|found] [size 256-65536] [page 1-256, a power of two] "
          "[cycle 0-100 ms] [chunk 1-4096]\n");
      return;
    }
  }
  m_EepromConfig = c;
  m_EepromAddrSet = addrSet;
  Eeprom({"eeprom"});
}

/**
 * Reads the sub-node an `eeprom` command names, which must be in the bus; says why not.
 * @return True with @p node set.
 */
bool BenchConsole::EepromNode(const std::string& word, a2b::NodeAddr& node) {
  long position = 0;
  if (!ToNumber(word, a2b::kMaxSubNodes - 1, position)) {
    Print("eeprom: N is a sub-node, 0-15\n");
    return false;
  }
  if (static_cast<size_t>(position) >= m_Network.SubNodeCount()) {
    Print(
        "Sub-node %ld isn't in the bus (%u sub-node(s)): start the bus; blank EEPROMs need "
        "ignoreEEPROM 1 first\n",
        position, static_cast<unsigned>(m_Network.SubNodeCount()));
    return false;
  }
  node = static_cast<a2b::NodeAddr>(position);
  return true;
}

/**
 * The EEPROM settings for a node: `eeprom config`'s, with the address set there, else the
 * one the node's image was found at, else the first the stack tries (0x50).
 */
EepromConfig BenchConsole::EepromFor(a2b::NodeAddr node) const {
  EepromConfig config = m_EepromConfig;
  if (!m_EepromAddrSet) {
    const uint8_t found = m_Modules.EepromAddr(node);
    config.i2cAddr = (found != 0) ? found : a2b::EepromModuleInfoSource::kFirstAddr;
  }
  return config;
}

/** Reports an `eeprom` job that has ended: what it did, or why it failed. */
void BenchConsole::EepromFinished() {
  const EepromJob& job = m_EepromJob;
  if (job.GetState() == EepromJob::State::kFailed) {
    Print("EEPROM job on sub-node %u failed: %s\n", static_cast<unsigned>(job.Node()),
          job.Error().c_str());
    if (job.GetKind() == EepromJob::Kind::kWrite &&
        (job.MarkerCleared() || job.PagesWritten() > 0)) {
      Print(
          "  The EEPROM is partly written, without its marker: the node has no module info "
          "until it is written again\n");
    }
    return;
  }
  if (job.GetState() != EepromJob::State::kDone) {
    return;
  }
  if (m_EepromInfo) {
    Print("Sub-node %u's EEPROM (0x%02X):\n", static_cast<unsigned>(job.Node()),
          job.Config().i2cAddr);
    Print("%s", job.WholeEeprom() ? ("  " + job.Note() + "\n").c_str()
                                  : DescribeImage(job.Data()).c_str());
    return;
  }
  Print("%s\n", job.Summary().c_str());
  if (!job.Note().empty()) {
    Print("  %s\n", job.Note().c_str());
  }
  if (job.GetKind() == EepromJob::Kind::kRead) {
    if (WriteBinary(m_EepromFile, job.Data())) {
      Print("  Saved to %s\n", m_EepromFile.c_str());
    } else {
      Print("  Can't write %s\n", m_EepromFile.c_str());
    }
  } else if (job.GetKind() == EepromJob::Kind::kWrite) {
    Print(
        "  The stack reads a node's module info when it finds it: start the bus again%s for "
        "the node to use it\n",
        m_IgnoreEeprom ? " with ignoreEEPROM 0" : "");
  }
}

/**
 * `i2cTrace on FILE`, `off`, `status`: the I2C trace.
 * @param args The command line's words.
 */
void BenchConsole::Trace(const std::vector<std::string>& args) {
  if (args.size() == 3 && Same(args[1], "on")) {
    if (!m_Trace.Start(args[2].c_str())) {
      Print("Can't write %s\n", args[2].c_str());
      m_TraceFile.clear();
      return;
    }
    m_TraceFile = args[2];
    Print("Tracing I2C to %s\n", args[2].c_str());
  } else if (args.size() == 2 && Same(args[1], "off")) {
    const bool was = m_Trace.Tracing();
    m_Trace.Stop();
    if (was) {
      Print("Trace %s closed: %u transfer(s)\n", m_TraceFile.c_str(),
            static_cast<unsigned>(m_Trace.Count()));
    } else {
      Print("Not tracing\n");
    }
    m_TraceFile.clear();
  } else if (args.size() == 1 || (args.size() == 2 && Same(args[1], "status"))) {
    if (m_Trace.Tracing()) {
      Print("Tracing I2C to %s: %u transfer(s)\n", m_TraceFile.c_str(),
            static_cast<unsigned>(m_Trace.Count()));
    } else {
      Print("Not tracing\n");
    }
  } else {
    Print("i2cTrace on FILE, i2cTrace off, i2cTrace status\n");
  }
}
