/**
 * @file bench_console_spi.cpp
 * @brief The bench console's `spi`, `spiflash` and `wait` commands: SPI over A2B
 *        (docs/SPI.md).
 *
 * The main node is controlled over I2C as always; the host's SPI to the main node
 * (Platform::SpiTransfer(): spidev on the Pi, `--spi`) carries the data tunnel's transactions.
 * A host without it (the USBi, the RP2040 for now) answers every `spi` command with one line
 * and sends nothing.
 *
 * `spi open N` opens the tunnel to sub-node N; the raw commands then run the TRM's
 * transaction types on its slave select: `spi write` (atomic write), `spi read` (atomic
 * read), `spi fd` (full duplex), `spi bulk` (bulk write), and, with files, `spi dump` and
 * `spi send` (the extended full duplex and bulk, any length, as an SpiJob). `spiflash` works
 * a SPI NOR flash there (the bench's W25Q128JV): its JEDEC ID, and erase, read, program and
 * verify as SpiJobs. `wait` holds the commands queued (a command line's) until a node is in
 * the bus, the tunnel is open or the bus runs; a check that fails (`wait`, `spiflash id ID`)
 * sets the exit code to 1, which `--spi-flash-check` uses.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "a2b/strings.h"
#include "bench_console.h"
#include "console_text.h"

using bench::Hex;
using bench::Same;
using bench::ToBytes;
using bench::ToNumber;

namespace {

/** What a host without SPI to the main node says. */
constexpr const char* kNoSpi =
    "this host has no SPI to the main node (the USBi has I2C only; on a Pi, SPI enabled and "
    "--spi /dev/spidev0.1)";

#if !A2B_BENCH_FILES
/** What a command that needs a file says on a host without files. */
constexpr const char* kNoFiles = "not available on the RP2040 (no file system)";
#else
/** Reads a whole file; false if it can't be read. */
bool ReadFile(const std::string& path, std::vector<uint8_t>& data) {
  std::FILE* in = std::fopen(path.c_str(), "rb");
  if (in == nullptr) {
    return false;
  }
  data.clear();
  uint8_t buffer[4096];
  size_t got = 0;
  while ((got = std::fread(buffer, 1, sizeof(buffer), in)) > 0) {
    data.insert(data.end(), buffer, buffer + got);
  }
  std::fclose(in);
  return true;
}

/** Writes a whole file; false if it can't be written. */
bool WriteFile(const std::string& path, const std::vector<uint8_t>& data) {
  std::FILE* out = std::fopen(path.c_str(), "wb");
  if (out == nullptr) {
    return false;
  }
  const bool ok = std::fwrite(data.data(), 1, data.size(), out) == data.size();
  return std::fclose(out) == 0 && ok;
}
#endif

/** "sub-node N". */
std::string SubNodeName(a2b::NodeAddr node) {
  return "sub-node " + std::to_string(static_cast<int>(node));
}

/** Reads a whole number up to @p max that may be large (an address, a length). */
bool ToSize(const std::string& text, unsigned long max, unsigned long& value) {
  if (text.empty()) {
    return false;
  }
  char* end = nullptr;
  value = std::strtoul(text.c_str(), &end, 0);
  return *end == '\0' && text[0] != '-' && value <= max;
}

/** Reads a JEDEC ID as 6 hex digits ("EF4018", "0xEF4018") into 3 bytes. */
bool ToJedecId(const std::string& text, uint8_t id[3]) {
  std::string digits = text;
  if (digits.size() > 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
    digits = digits.substr(2);
  }
  if (digits.size() != 6) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    char* end = nullptr;
    const std::string byte = digits.substr(static_cast<size_t>(2 * i), 2);
    id[i] = static_cast<uint8_t>(std::strtoul(byte.c_str(), &end, 16));
    if (*end != '\0') {
      return false;
    }
  }
  return true;
}

}  // namespace

/**
 * `spi ...`: the SPI commands. `spi` alone shows the last transaction's status; `spi check`
 * reads the main node's IDs over SPI and over I2C.
 * @param args The command line's words.
 */
void BenchConsole::Spi(const std::vector<std::string>& args) {
  if (args.size() == 1) {
    SpiShowStatus();
  } else if (Same(args[1], "check") && args.size() == 2) {
    SpiCheck();
  } else if (Same(args[1], "open")) {
    SpiOpen(args);
  } else if (Same(args[1], "close") && args.size() == 2) {
    if (m_Network.SpiTunnel() == nullptr) {
      Print("No SPI tunnel is open\n");
      return;
    }
    m_Network.CloseSpiTunnel();
    Print("SPI tunnel: closed at the next poll\n");
  } else if (Same(args[1], "abort") && args.size() == 2) {
    const bool job = m_SpiJob.Running();
    m_SpiJob.Abort();
    const a2b::Result result = m_Network.SpiAbort();
    if (job) {
      SpiJobFinished();
    }
    Print("SPI abort: %s\n", (result == a2b::Result::kOk) ? "sent" : SpiWhy(result).c_str());
  } else if (Same(args[1], "write") || Same(args[1], "read") || Same(args[1], "fd") ||
             Same(args[1], "bulk")) {
    SpiRaw(args);
  } else if (Same(args[1], "dump") || Same(args[1], "send")) {
#if !A2B_BENCH_FILES
    Print("spi %s: %s\n", args[1].c_str(), kNoFiles);
#else
    SpiFile(args);
#endif
  } else {
    Print("spi [check|open|close|write|read|fd|bulk|dump|send|abort] (help lists them)\n");
  }
}

/**
 * The tunnel's far end and the slave select the raw commands use (the lowest it enables),
 * or a message why there is none.
 * @return False if no tunnel is ready.
 */
bool BenchConsole::SpiTarget(a2b::NodeAddr& node, uint8_t& select) {
  const a2b::SpiTunnelConfig* tunnel = m_Network.SpiTunnel();
  if (tunnel == nullptr) {
    Print("No SPI tunnel is open (spi open N)\n");
    return false;
  }
  if (!m_Network.SpiTunnelReady()) {
    Print(
        "The SPI tunnel isn't ready yet (its plan is written once the chain is up, with its "
        "module in the bus; wait spi waits for it)\n");
    return false;
  }
  node = m_Network.Routing().tunnel.node;
  select = 0;
  while (((tunnel->selects >> select) & 1) == 0) {
    ++select;
  }
  return true;
}

/** `spi open N [ss S] [div D] [mode M] [slots T] [stretch]`: the data tunnel to sub-node N. */
void BenchConsole::SpiOpen(const std::vector<std::string>& args) {
  const char* usage =
      "spi open N [ss 0-2] [div 0-255] [mode 0-3] [slots 2-12] [stretch] (N: a sub-node 0-15)\n";
  long value = 0;
  if (args.size() < 3 || !ToNumber(args[2], a2b::kMaxSubNodes - 1, value)) {
    Print("%s", usage);
    return;
  }
  a2b::SpiTunnelConfig config;
  config.node = static_cast<a2b::NodeAddr>(value);
  for (size_t i = 3; i < args.size(); ++i) {
    if (Same(args[i], "stretch")) {
      config.fdClockStretch = true;
      continue;
    }
    const bool hasValue = i + 1 < args.size();
    if (Same(args[i], "ss") && hasValue && ToNumber(args[i + 1], 2, value)) {
      config.selects = static_cast<uint8_t>(1u << value);
    } else if (Same(args[i], "div") && hasValue && ToNumber(args[i + 1], 0xFF, value)) {
      config.clockDiv = static_cast<uint8_t>(value);
    } else if (Same(args[i], "mode") && hasValue && ToNumber(args[i + 1], 3, value)) {
      config.mode = static_cast<uint8_t>(value);
    } else if (Same(args[i], "slots") && hasValue && ToNumber(args[i + 1], 12, value) &&
               value >= 2) {
      config.slots = static_cast<uint8_t>(value);
    } else {
      Print("%s", usage);
      return;
    }
    ++i;
  }
  a2b::RoutingError why = a2b::RoutingError::kNone;
  const a2b::Result result = m_Network.OpenSpiTunnel(config, &why);
  if (result != a2b::Result::kOk) {
    std::string reason = (why != a2b::RoutingError::kNone) ? a2b::ToString(why)
                         : (result == a2b::Result::kNotSupported)
                             ? "the main node has no SPI port"
                             : SubNodeName(config.node) +
                                   " is not an AD2433 or AD2437 in the bus, or uses IO0, IO4-IO6 "
                                   "as GPIO with CLKOUT2 on (see the log)";
    Print("Can't open the SPI tunnel: %s\n", reason.c_str());
    return;
  }
  unsigned select = 0;
  while (((config.selects >> select) & 1) == 0) {
    ++select;
  }
  const unsigned khz = 49152u / (config.clockDiv + 1u);
  Print(
      "SPI tunnel to %s: SS%u, SCK %u kHz (at 48 kHz), mode %u, %u slot(s) each way; "
      "written at the next poll between probes\n",
      SubNodeName(config.node).c_str(), select, khz, static_cast<unsigned>(config.mode),
      static_cast<unsigned>(config.slots));
}

/**
 * `spi write|read|fd|bulk ...`: one transaction on the tunnel's far end. A read's bytes and
 * a full duplex's come back in hex.
 */
void BenchConsole::SpiRaw(const std::vector<std::string>& args) {
  const std::string& kind = args[1];
  std::vector<uint8_t> bytes;
  unsigned long readLen = 0;
  const bool read = Same(kind, "read");
  const bool parsed =
      read ? (args.size() >= 3 && ToSize(args[2], a2b::kSpiMaxData, readLen) && readLen > 0 &&
              ToBytes(args, 3, bytes) && bytes.size() < a2b::kSpiMaxData)
           : (args.size() >= 3 && ToBytes(args, 2, bytes) && bytes.size() <= a2b::kSpiMaxData);
  if (!parsed) {
    Print(read ? "spi read LEN [BYTE...] (LEN 1-256; up to 255 BYTEs, 0x00-0xFF)\n"
               : "spi %s BYTE... (1-256 bytes, 0x00-0xFF)\n",
          kind.c_str());
    return;
  }
  a2b::NodeAddr node = 0;
  uint8_t select = 0;
  if (!SpiTarget(node, select)) {
    return;
  }
  const a2b::PeripheralAddress addr = a2b::PeripheralAddress::Spi(select);
  a2b::Result result = a2b::Result::kOk;
  std::vector<uint8_t> in(read ? readLen : bytes.size());
  if (Same(kind, "write")) {
    result = m_Network.PeripheralWrite(node, addr, bytes.data(), bytes.size());
  } else if (read) {
    result = bytes.empty() ? m_Network.PeripheralRead(node, addr, in.data(), in.size())
                           : m_Network.PeripheralWriteRead(node, addr, bytes.data(), bytes.size(),
                                                           in.data(), in.size());
  } else if (Same(kind, "fd")) {
    result = m_Network.SpiFullDuplex(select, bytes.data(), in.data(), bytes.size());
  } else {
    result = m_Network.SpiBulkWrite(select, bytes.data(), bytes.size());
  }
  if (result != a2b::Result::kOk) {
    Print("spi %s failed: %s\n", kind.c_str(), SpiWhy(result).c_str());
    return;
  }
  const std::string where = SubNodeName(node) + " SS" + std::to_string(select);
  if (read) {
    Print("Read %s: %s\n", where.c_str(), Hex(in.data(), in.size()).c_str());
  } else if (Same(kind, "fd")) {
    std::vector<uint8_t> now(bytes.size());
    const a2b::Result fifo = m_Network.SpiReadTunnelFifo(now.data(), now.size());
    Print("Full duplex %s: sent %s\n  the last one's data: %s\n  this one's (FIFO): %s\n",
          where.c_str(), Hex(bytes.data(), bytes.size()).c_str(), Hex(in.data(), in.size()).c_str(),
          (fifo == a2b::Result::kOk) ? Hex(now.data(), now.size()).c_str() : SpiWhy(fifo).c_str());
  } else {
    Print("Wrote %s (%s): %s\n", where.c_str(), Same(kind, "write") ? "atomic" : "bulk",
          Hex(bytes.data(), bytes.size()).c_str());
  }
}

#if A2B_BENCH_FILES
/**
 * `spi dump FILE LEN [BYTE...]` (the bytes, then LEN read into FILE) and `spi send FILE
 * [BYTE...]` (the bytes, then FILE's), each one remote transfer, as an SpiJob.
 */
void BenchConsole::SpiFile(const std::vector<std::string>& args) {
  const bool dump = Same(args[1], "dump");
  unsigned long len = 0;
  std::vector<uint8_t> header;
  const size_t first = dump ? 4 : 3;
  if (args.size() < first || (dump && (!ToSize(args[3], 16u << 20, len) || len == 0)) ||
      !ToBytes(args, first, header)) {
    Print(dump ? "spi dump FILE LEN [BYTE...] (LEN up to 16 MB)\n" : "spi send FILE [BYTE...]\n");
    return;
  }
  a2b::NodeAddr node = 0;
  uint8_t select = 0;
  if (!SpiTarget(node, select)) {
    return;
  }
  if (dump) {
    m_SpiFile = args[2];
    m_SpiJob.StartStream(node, select, header, len, m_Trace.NowMs());
    Print("spi dump: %lu byte(s) into %s\n", len, m_SpiFile.c_str());
    return;
  }
  std::vector<uint8_t> data;
  if (!ReadFile(args[2], data)) {
    Print("Can't read %s\n", args[2].c_str());
    return;
  }
  header.insert(header.end(), data.begin(), data.end());
  if (header.empty()) {
    Print("spi send: nothing to send\n");
    return;
  }
  m_SpiFile.clear();
  m_SpiJob.StartSend(node, select, header, m_Trace.NowMs());
  Print("spi send: %u byte(s)\n", static_cast<unsigned>(header.size()));
}
#endif

/**
 * `spiflash id [ID]`, `erase ADDR LEN`, `read ADDR LEN FILE`, `program ADDR FILE`, `verify
 * ADDR FILE`, `abort`: a SPI NOR flash on the tunnel's slave select.
 */
void BenchConsole::SpiFlash(const std::vector<std::string>& args) {
  const char* usage =
      "spiflash id [ID] | erase ADDR LEN | read ADDR LEN FILE | program ADDR "
      "FILE | verify ADDR FILE | abort\n";
  if (args.size() < 2) {
    Print("%s", usage);
    return;
  }
  const std::string& what = args[1];
  if (Same(what, "abort") && args.size() == 2) {
    const bool job = m_SpiJob.Running();
    m_SpiJob.Abort();
    if (job) {
      SpiJobFinished();
    } else {
      Print("No spiflash job is running\n");
    }
    return;
  }
  if (Same(what, "id")) {
    SpiFlashId(args);
    return;
  }
  unsigned long address = 0;
  unsigned long len = 0;
  const bool erase = Same(what, "erase");
  const bool read = Same(what, "read");
  if (!(erase || read || Same(what, "program") || Same(what, "verify")) || args.size() < 4 ||
      !ToSize(args[2], 0xFFFFFF, address)) {
    Print("%s", usage);
    return;
  }
#if !A2B_BENCH_FILES
  if (!erase) {
    Print("spiflash %s: %s\n", what.c_str(), kNoFiles);
    return;
  }
#endif
  if ((erase && (args.size() != 4 || !ToSize(args[3], 16u << 20, len) || len == 0)) ||
      (read && (args.size() != 5 || !ToSize(args[3], 16u << 20, len) || len == 0)) ||
      (!erase && !read && args.size() != 4)) {
    Print("%s", usage);
    return;
  }
  a2b::NodeAddr node = 0;
  uint8_t select = 0;
  if (!SpiTarget(node, select)) {
    return;
  }
  const uint32_t now = m_Trace.NowMs();
  if (erase) {
    m_SpiJob.StartErase(node, select, static_cast<uint32_t>(address), len, now);
    Print("spiflash erase: 0x%06lX, %lu byte(s), 4 KB sectors\n", address, len);
    return;
  }
#if A2B_BENCH_FILES
  if (read) {
    m_SpiFile = args[4];
    const std::vector<uint8_t> command = {0x03, static_cast<uint8_t>(address >> 16),
                                          static_cast<uint8_t>(address >> 8),
                                          static_cast<uint8_t>(address)};
    m_SpiJob.StartStream(node, select, command, len, now);
    Print("spiflash read: 0x%06lX, %lu byte(s) into %s\n", address, len, m_SpiFile.c_str());
    return;
  }
  std::vector<uint8_t> data;
  if (!ReadFile(args[3], data) || data.empty()) {
    Print("Can't read %s (or it is empty)\n", args[3].c_str());
    return;
  }
  m_SpiFile.clear();
  if (Same(what, "program")) {
    m_SpiJob.StartProgram(node, select, static_cast<uint32_t>(address), data, now);
  } else {
    m_SpiJob.StartVerify(node, select, static_cast<uint32_t>(address), data, now);
  }
  Print("spiflash %s: 0x%06lX, %u byte(s) from %s\n", what.c_str(), address,
        static_cast<unsigned>(data.size()), args[3].c_str());
#endif
}

/**
 * `spiflash id [ID]`: the flash's JEDEC ID (0x9F, an atomic read of 3 bytes), its maker and
 * size; with ID, a mismatch sets the exit code to 1. The hello-world of the SPI tunnel.
 */
void BenchConsole::SpiFlashId(const std::vector<std::string>& args) {
  uint8_t expected[3] = {};
  if (args.size() > 3 || (args.size() == 3 && !ToJedecId(args[2], expected))) {
    Print("spiflash id [ID] (ID: 6 hex digits, e.g. EF4018 for a W25Q128JV)\n");
    return;
  }
  a2b::NodeAddr node = 0;
  uint8_t select = 0;
  if (!SpiTarget(node, select)) {
    m_ExitCode = (args.size() == 3) ? 1 : m_ExitCode;
    return;
  }
  const uint8_t command = 0x9F;
  uint8_t id[3] = {};
  const a2b::Result result = m_Network.PeripheralWriteRead(
      node, a2b::PeripheralAddress::Spi(select), &command, 1, id, sizeof(id));
  if (result != a2b::Result::kOk) {
    Print("spiflash id failed: %s\n", SpiWhy(result).c_str());
    m_ExitCode = (args.size() == 3) ? 1 : m_ExitCode;
    return;
  }
  std::string what;
  if ((id[0] == 0x00 && id[1] == 0x00 && id[2] == 0x00) ||
      (id[0] == 0xFF && id[1] == 0xFF && id[2] == 0xFF)) {
    what = "no flash answers (MISO stuck)";
  } else {
    const char* maker = (id[0] == 0xEF)   ? "Winbond"
                        : (id[0] == 0xC2) ? "Macronix"
                        : (id[0] == 0x20) ? "Micron/ST"
                        : (id[0] == 0x9D) ? "ISSI"
                        : (id[0] == 0xC8) ? "GigaDevice"
                                          : "maker unknown";
    char text[96];
    if (id[2] >= 0x10 && id[2] <= 0x20) {
      const unsigned long bytes = 1ul << id[2];
      std::snprintf(text, sizeof(text), "%s, %lu %s", maker,
                    (bytes >= (1ul << 20)) ? bytes >> 20 : bytes >> 10,
                    (bytes >= (1ul << 20)) ? "MB" : "KB");
    } else {
      std::snprintf(text, sizeof(text), "%s", maker);
    }
    what = text;
  }
  Print("Flash on %s SS%u: JEDEC ID %s (%s)\n", SubNodeName(node).c_str(),
        static_cast<unsigned>(select), Hex(id, 3).c_str(), what.c_str());
  if (args.size() == 3) {
    const bool same = id[0] == expected[0] && id[1] == expected[1] && id[2] == expected[2];
    Print("spiflash id: %s %s\n", same ? "matches" : "doesn't match", Hex(expected, 3).c_str());
    if (!same) {
      m_ExitCode = 1;
    }
  }
}

/** Reports an `spi` or `spiflash` job that has ended, and writes a read's file. */
void BenchConsole::SpiJobFinished() {
  if (m_SpiJob.GetKind() == SpiJob::Kind::kNone) {
    return;
  }
  if (!m_SpiJob.Error().empty()) {
    Print("SPI job on %s failed: %s\n", SubNodeName(m_SpiJob.Node()).c_str(),
          m_SpiJob.Error().c_str());
    m_ExitCode = 1;
    return;
  }
  std::string saved;
#if A2B_BENCH_FILES
  if (m_SpiJob.GetKind() == SpiJob::Kind::kStream && !m_SpiFile.empty()) {
    saved = WriteFile(m_SpiFile, m_SpiJob.Data()) ? ", written to " + m_SpiFile
                                                  : ", but " + m_SpiFile + " can't be written";
  }
#endif
  Print("SPI job on %s done: %s%s%s\n", SubNodeName(m_SpiJob.Node()).c_str(),
        m_SpiJob.Summary().c_str(),
        (m_SpiJob.GetKind() == SpiJob::Kind::kVerify) ? ", the same" : "", saved.c_str());
}

/**
 * `wait node N [S]`, `wait spi [S]`, `wait running [S]`, `wait S`, `wait abort`: holds the
 * commands queued (Busy()) until the condition is met, or S seconds (default 10). A wait not
 * met sets the exit code to 1.
 */
void BenchConsole::Wait(const std::vector<std::string>& args) {
  const char* usage = "wait node N [S] | wait spi [S] | wait running [S] | wait S | wait abort\n";
  if (args.size() == 2 && Same(args[1], "abort")) {
    m_WaitFor = 0;
    Print("wait: stopped\n");
    return;
  }
  long node = 0;
  long seconds = 10;
  size_t secondsAt = 2;
  char kind = 0;
  if (args.size() >= 2 && Same(args[1], "node") && args.size() >= 3 &&
      ToNumber(args[2], a2b::kMaxSubNodes - 1, node)) {
    kind = 'n';
    secondsAt = 3;
  } else if (args.size() >= 2 && Same(args[1], "spi")) {
    kind = 's';
  } else if (args.size() >= 2 && Same(args[1], "running")) {
    kind = 'r';
  } else if (args.size() == 2 && ToNumber(args[1], 3600, seconds)) {
    kind = 't';
    secondsAt = 2;
  }
  if (kind == 0 || args.size() > secondsAt + 1 ||
      (args.size() == secondsAt + 1 &&
       (kind == 't' || !ToNumber(args[secondsAt], 3600, seconds)))) {
    Print("%s", usage);
    return;
  }
  m_WaitFor = kind;
  m_WaitNode = static_cast<a2b::NodeAddr>(node);
  m_WaitUntil = m_Trace.NowMs() + static_cast<uint32_t>(seconds) * 1000u;
  PollWait();
}

/** Ends a `wait` whose condition is met, or whose time is up (the exit code then 1). */
void BenchConsole::PollWait() {
  if (m_WaitFor == 0) {
    return;
  }
  bool met = false;
  const char* what = "";
  switch (m_WaitFor) {
    case 'n':
      met = static_cast<size_t>(m_WaitNode) < m_Network.SubNodeCount();
      what = "the sub-node";
      break;
    case 's':
      met = m_Network.SpiTunnelReady();
      what = "the SPI tunnel";
      break;
    case 'r':
      met = m_Network.State() == a2b::NetworkState::kRunning;
      what = "the bus running";
      break;
    default:
      what = "the time";
      break;
  }
  const bool timeUp = a2b::TimeReached(m_Trace.NowMs(), m_WaitUntil);
  if (!met && !timeUp) {
    return;
  }
  if (met) {
    Print("wait: %s is there\n", what);
  } else if (m_WaitFor == 't') {
    Print("wait: done\n");
  } else {
    Print("wait: %s didn't come in time\n", what);
    m_ExitCode = 1;
  }
  m_WaitFor = 0;
}

/** Explains a failed SPI call: no SPI on this host, the main node's report, or the result. */
std::string BenchConsole::SpiWhy(a2b::Result result) const {
  if (result == a2b::Result::kNotSupported) {
    return kNoSpi;
  }
  if (result == a2b::Result::kBusy) {
    return "busy: a node is joining or the tunnel is being set up; try again";
  }
  if (result != a2b::Result::kSpiError) {
    return a2b::ToString(result);
  }
  const a2b::SpiStatus status = m_Network.LastSpiStatus();
  if (status.hostFailed) {
    return "the host's SPI transfer failed";
  }
  if (status.timedOut) {
    return "the main node stayed busy (SPISTAT.SPIBUSY)";
  }
  return "the main node reported " + SpiBits(status);
}

/** The error bits of an SPI status by name: "SPIINT 0x08 (SPIDTERR), SPISTAT 0x40 (DTABORT)". */
std::string BenchConsole::SpiBits(const a2b::SpiStatus& status) {
  static const char* const kIntNames[] = {"SPIDONE", "SPIREGERR", "SPII2CERR", "SPIDTERR",
                                          "BADCMD",  "FIFOOVF",   "FIFOUNF",   "?"};
  static const char* const kStatNames[] = {"SPIBUSY", "DTACTIVE",  "?",       "?",
                                           "?",       "DTINVALID", "DTABORT", "DTBADPKT"};
  const auto names = [](uint8_t bits, const char* const* table) {
    std::string text;
    for (int bit = 0; bit < 8; ++bit) {
      if ((bits & (1u << bit)) != 0) {
        text += std::string(text.empty() ? "" : " ") + table[bit];
      }
    }
    return text;
  };
  char line[96];
  std::snprintf(line, sizeof(line), "SPIINT 0x%02X", status.spiInt);
  std::string text = line;
  if (status.spiInt != 0) {
    text += " (" + names(status.spiInt, kIntNames) + ")";
  }
  if ((status.spiStat & 0xE0) != 0) {
    std::snprintf(line, sizeof(line), ", SPISTAT 0x%02X (", status.spiStat);
    text += line + names(static_cast<uint8_t>(status.spiStat & 0xE0), kStatNames) + ")";
  }
  return text;
}

/** `spi`: the tunnel, and the last SPI transaction's status. */
void BenchConsole::SpiShowStatus() {
  const a2b::SpiTunnelConfig* tunnel = m_Network.SpiTunnel();
  if (tunnel == nullptr) {
    Print("SPI tunnel: none (spi open N)\n");
  } else {
    const a2b::TunnelPlan& plan = m_Network.Routing().tunnel;
    if (m_Network.SpiTunnelReady()) {
      Print(
          "SPI tunnel: open to %s, %u slot(s) each way (main DTDNOFFS %u, DTUPOFFS %u; far "
          "end %u, %u)\n",
          SubNodeName(plan.node).c_str(), static_cast<unsigned>(plan.dnSlots),
          static_cast<unsigned>(plan.mainDnOffset), static_cast<unsigned>(plan.mainUpOffset),
          static_cast<unsigned>(plan.nodeDnOffset), static_cast<unsigned>(plan.nodeUpOffset));
    } else {
      Print(
          "SPI tunnel: asked for (opened at %s), not open now (its module isn't in the bus, "
          "or its plan isn't written yet)\n",
          SubNodeName(tunnel->node).c_str());
    }
  }
  if (m_SpiJob.Running()) {
    Print("SPI job: running on %s\n", SubNodeName(m_SpiJob.Node()).c_str());
  }
  const a2b::SpiStatus status = m_Network.LastSpiStatus();
  Print("SPI: last transaction %s\n", status.hostFailed ? "failed on the host"
                                      : status.timedOut ? "timed out (SPIBUSY)"
                                      : (status.spiInt != 0)
                                          ? ("ended with " + SpiBits(status)).c_str()
                                          : "ended without an error");
}

/**
 * `spi check`: reads VENDOR, PRODUCT and VERSION over SPI (the SPI local register read) and
 * over I2C, and compares them. The same three bytes say the host's SPI reaches the main
 * node (SCK, MOSI, MISO, and the slave select on ADR1), with no A2B link involved.
 */
void BenchConsole::SpiCheck() {
  uint8_t overI2c[3] = {};
  uint8_t spiCfg = 0;
  const bool i2cOk =
      m_Network.ReadRegisters(a2b::kMainNode, 0x02, overI2c, sizeof(overI2c)) == a2b::Result::kOk &&
      m_Network.ReadRegisters(a2b::kMainNode, 0xB0, &spiCfg, 1) == a2b::Result::kOk;
  if (!i2cOk) {
    Print("SPI check: the main node doesn't answer over I2C\n");
    return;
  }
  if ((spiCfg & 0x03) == 0x02) {
    Print(
        "SPI check: the main node's SPI port is off (SPICFG 0x%02X, from the board's export); "
        "spi open turns it on\n",
        spiCfg);
    return;
  }
  uint8_t overSpi[3] = {};
  const a2b::Result result = m_Network.SpiReadRegisters(0x02, overSpi, sizeof(overSpi));
  if (result != a2b::Result::kOk) {
    Print("SPI check: %s\n", SpiWhy(result).c_str());
    return;
  }
  const bool same =
      overSpi[0] == overI2c[0] && overSpi[1] == overI2c[1] && overSpi[2] == overI2c[2];
  Print("SPI check: VENDOR, PRODUCT, VERSION over SPI %s, over I2C %s: %s\n",
        Hex(overSpi, 3).c_str(), Hex(overI2c, 3).c_str(),
        same ? "the SPI to the main node works"
             : "they differ (check SCK, MOSI, MISO, the slave select on ADR1, and the SPI mode)");
}
