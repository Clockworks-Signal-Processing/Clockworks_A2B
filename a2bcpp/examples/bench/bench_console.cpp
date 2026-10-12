/**
 * @file bench_console.cpp
 * @brief BenchConsole: the bench console's commands (`help` lists them; the console guide
 *        is a2bcpp/docs/bench_console.md).
 *
 * The console owns one Network on the host's Platform (wrapped in a TracePlatform for
 * `i2cTrace`). A board is one chain of a SigmaStudio+ export: built in (the repository's
 * exports, compiled into the console), or read from an export's C file at run time
 * (SsPlusFile), so a new board needs no rebuild. `start` imports it into a fresh
 * BoardConfig and starts the bus; the link profiles set with `link` and the module-info
 * source of `ignoreEEPROM` apply to it. Every event is printed as it comes. The register
 * and peripheral commands reach the nodes at once, through the Network's raw access
 * (a write to a register the stack owns is the user's risk, as for any app). The
 * connection commands keep connections in the Network (by chip ID, so they outlast a
 * `start`), and `applyRouting` routes them; the main node's channel groups are the
 * console's (`group main`), as an export has none.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "bench_console.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "ss_plus_import.h"

extern "C" {
extern ADI_A2B_BCD a2b_bench_board_ref;  ///< adi/exports/rp2040/12_a2b_busconfig.c (board_ref.c).
/// tools/SS+reference/2x_AB0020_EVM_with_2437/adi_a2b_busconfig.c (board_ab0020.c).
extern ADI_A2B_BCD a2b_bench_board_ab0020;
/// tools/SS+reference/2x_AB0020_EVM_with_2428/adi_a2b_busconfig.c (board_ab0020_2428.c).
extern ADI_A2B_BCD a2b_bench_board_ab0020_2428;
/// tools/SS+reference/AB0310_AB0312_EVM_with_2437/adi_a2b_busconfig.c (board_ab0310.c).
extern ADI_A2B_BCD a2b_bench_board_ab0310;
}

namespace {

constexpr size_t kMaxWarningsShown = 10;  ///< A loaded export's warnings printed in full.

/** INTTYPE PWRERR, cable disconnected, open circuit or wrong port (AD242x TRM, Table 5-1). */
constexpr uint8_t kIntOpenLine = 12;
/** INTTYPE PWRERR, cable reverse connected or wrong port; an open line at a low VIN. */
constexpr uint8_t kIntOpenLineLowVin = 13;

constexpr long kLastRegister = 0x1FF;  ///< The last register of page 1 (AD243x).
constexpr long kMaxCount = 256;        ///< Registers or peripheral bytes one command reads.
constexpr size_t kBytesPerLine = 16;   ///< Values printed on one line.

/** A name for a state. */
const char* Name(a2b::NetworkState state) {
  switch (state) {
    case a2b::NetworkState::kStopped:
      return "stopped";
    case a2b::NetworkState::kStarting:
      return "starting the main node";
    case a2b::NetworkState::kConfiguring:
      return "configuring the main node";
    case a2b::NetworkState::kDiscovering:
      return "bringing up the chain";
    case a2b::NetworkState::kRunning:
      return "running";
    case a2b::NetworkState::kFailed:
      return "failed";
  }
  return "?";
}

/** A name for an error. */
const char* Name(a2b::NetworkError error) {
  switch (error) {
    case a2b::NetworkError::kNone:
      return "none";
    case a2b::NetworkError::kI2cError:
      return "the main node does not answer";
    case a2b::NetworkError::kWrongMainNode:
      return "the main node is not the board's chip";
    case a2b::NetworkError::kMainNodeNotRunning:
      return "no MSTR_RUNNING (is SYNC there?)";
    case a2b::NetworkError::kHostScriptFailed:
      return "the board's host script failed";
  }
  return "?";
}

/** A name for a rejection reason. */
const char* Name(a2b::RejectReason reason) {
  switch (reason) {
    case a2b::RejectReason::kNone:
      return "none";
    case a2b::RejectReason::kUnsupportedChip:
      return "unsupported chip";
    case a2b::RejectReason::kNoModuleInfo:
      return "no module info (no EEPROM at 0x50-0x54, or a blank one)";
    case a2b::RejectReason::kBadModuleInfo:
      return "bad module info (marker, header CRC or block)";
    case a2b::RejectReason::kConfigFailed:
      return "configuration failed";
    case a2b::RejectReason::kPeripheralFailed:
      return "peripheral script failed";
    case a2b::RejectReason::kPowerBudget:
      return "over the power budget";
    case a2b::RejectReason::kByApp:
      return "by the app";
  }
  return "?";
}

/** A name for a removal reason. */
const char* Name(a2b::RemovalReason reason) {
  switch (reason) {
    case a2b::RemovalReason::kDropped:
      return "dropped (unplugged, or a node before it was)";
    case a2b::RemovalReason::kRejected:
      return "rejected by the app";
    case a2b::RemovalReason::kRestart:
      return "the bus starts again";
    case a2b::RemovalReason::kStopped:
      return "the bus stopped";
  }
  return "?";
}

/** A name for a result. */
const char* Name(a2b::Result result) {
  switch (result) {
    case a2b::Result::kOk:
      return "ok";
    case a2b::Result::kI2cError:
      return "I2C error";
    case a2b::Result::kInvalidArgument:
      return "invalid argument";
    case a2b::Result::kNotSupported:
      return "not supported";
    case a2b::Result::kNoSpace:
      return "no space";
    case a2b::Result::kInvalidData:
      return "invalid data";
  }
  return "?";
}

/**
 * The link profile's name, as `link` takes it: utp (B side straight) or rj45 (B side
 * inverted: XCVRBINV, which only RJ45 links have by default), both powered over the A2B
 * pair; custom (own power), inverted if its cable is RJ45. The rj45 links are single-step
 * until two-step discovery is built.
 */
std::string ProfileName(const a2b::LinkProfile& link) {
  if (link.power == a2b::LinkProfile::Power::kOnA2bPair) {
    return link.bSideInverted ? "rj45" : "utp";
  }
  return link.bSideInverted ? "custom inverted" : "custom";
}

/** True if @p a and @p b are the same word, ignoring case. */
bool Same(const std::string& a, const char* b) {
  if (a.size() != std::strlen(b)) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    const char x = (a[i] >= 'A' && a[i] <= 'Z') ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
    const char y = (b[i] >= 'A' && b[i] <= 'Z') ? static_cast<char>(b[i] - 'A' + 'a') : b[i];
    if (x != y) {
      return false;
    }
  }
  return true;
}

/**
 * Splits a command line into words at white space; "double quotes" keep spaces in a word
 * (a Windows path, for instance).
 */
std::vector<std::string> Split(const char* line) {
  std::vector<std::string> words;
  const char* p = line;
  for (;;) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
      ++p;
    }
    if (*p == '\0') {
      return words;
    }
    std::string word;
    if (*p == '"') {
      ++p;
      while (*p != '\0' && *p != '"') {
        word += *p++;
      }
      if (*p == '"') {
        ++p;
      }
    } else {
      while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
        word += *p++;
      }
    }
    words.push_back(word);
  }
}

/** Reads a whole number from 0 to @p max; false if @p text isn't one. */
bool ToNumber(const std::string& text, long max, long& value) {
  if (text.empty()) {
    return false;
  }
  char* end = nullptr;
  value = std::strtol(text.c_str(), &end, 0);
  return *end == '\0' && value >= 0 && value <= max;
}

/**
 * Reads a cable length in metres with at most one decimal ("4", "2.5", "0.3") into
 * decimetres, 1 to a2b::kMaxCableDm; false if @p text isn't one.
 */
bool ToDecimetres(const std::string& text, uint16_t& decimetres) {
  const size_t dot = text.find('.');
  const std::string whole = text.substr(0, dot);
  const std::string tenths = (dot == std::string::npos) ? "0" : text.substr(dot + 1);
  if (whole.empty() || tenths.size() != 1 ||
      whole.find_first_not_of("0123456789") != std::string::npos ||
      tenths.find_first_not_of("0123456789") != std::string::npos || whole.size() > 3) {
    return false;
  }
  const long value = std::strtol(whole.c_str(), nullptr, 10) * 10 + (tenths[0] - '0');
  if (value < 1 || value > a2b::kMaxCableDm) {
    return false;
  }
  decimetres = static_cast<uint16_t>(value);
  return true;
}

/** A cable length in decimetres as metres, "4.0 m". */
std::string Metres(uint16_t decimetres) {
  return std::to_string(decimetres / 10) + "." + std::to_string(decimetres % 10) + " m";
}

/**
 * Reads a node for the peripheral commands: `main` (or `m`) for the main node, else a
 * sub-node 0-15; false if @p text is neither.
 */
bool ToNode(const std::string& text, a2b::NodeAddr& node) {
  if (Same(text, "main") || Same(text, "m")) {
    node = a2b::kMainNode;
    return true;
  }
  long position = 0;
  if (!ToNumber(text, a2b::kMaxSubNodes - 1, position)) {
    return false;
  }
  node = static_cast<a2b::NodeAddr>(position);
  return true;
}

/** Reads the bytes in @p args from @p first on, each 0-255; false if one isn't. */
bool ToBytes(const std::vector<std::string>& args, size_t first, std::vector<uint8_t>& bytes) {
  bytes.clear();
  for (size_t i = first; i < args.size(); ++i) {
    long value = 0;
    if (!ToNumber(args[i], 0xFF, value)) {
      return false;
    }
    bytes.push_back(static_cast<uint8_t>(value));
  }
  return true;
}

/** "main node" or "sub-node N"; "the main node" with @p article. */
std::string NodeName(a2b::NodeAddr node, bool article = true) {
  if (node == a2b::kMainNode) {
    return article ? "the main node" : "main node";
  }
  return "sub-node " + std::to_string(static_cast<int>(node));
}

/** Formats @p len bytes as hex, separated by spaces. */
std::string Hex(const uint8_t* data, size_t len) {
  std::string text;
  char byte[4];
  for (size_t i = 0; i < len; ++i) {
    std::snprintf(byte, sizeof(byte), i == 0 ? "%02X" : " %02X", data[i]);
    text += byte;
  }
  return text;
}

/**
 * Formats a group's channels: "0-1", "0,2,4", or "none".
 */
std::string ChannelList(uint32_t channels) {
  std::string text;
  for (unsigned c = 0; c < 32; ++c) {
    if ((channels & (1u << c)) == 0) {
      continue;
    }
    unsigned last = c;
    while (last + 1 < 32 && (channels & (1u << (last + 1))) != 0) {
      ++last;
    }
    text += (text.empty() ? "" : ",") + std::to_string(c);
    if (last > c) {
      text += "-" + std::to_string(last);
    }
    c = last;
  }
  return text.empty() ? "none" : text;
}

/** A node's groups on one line: "rx G0 0-1, tx G0 0-1". */
std::string GroupsLine(const a2b::ChannelGroups& groups) {
  std::string text;
  for (size_t i = 0; i < groups.rxCount; ++i) {
    text += (text.empty() ? "" : ", ") + std::string("rx G") + std::to_string(groups.rx[i].id) +
            " " + ChannelList(groups.rx[i].channels);
  }
  for (size_t i = 0; i < groups.txCount; ++i) {
    text += (text.empty() ? "" : ", ") + std::string("tx G") + std::to_string(groups.tx[i].id) +
            " " + ChannelList(groups.tx[i].channels);
  }
  return text.empty() ? "no channel groups" : text;
}

/**
 * Reads a channel list, "0-1", "0,2,4-7" or "none", into a bitmap of channels 0-31; false
 * if @p text isn't one.
 */
bool ToChannels(const std::string& text, uint32_t& channels) {
  channels = 0;
  if (Same(text, "none")) {
    return true;
  }
  size_t at = 0;
  while (at < text.size()) {
    const size_t comma = text.find(',', at);
    const std::string item =
        text.substr(at, (comma == std::string::npos) ? std::string::npos : comma - at);
    const size_t dash = item.find('-');
    long first = 0;
    long last = 0;
    if (!ToNumber(item.substr(0, dash), 31, first) ||
        (dash != std::string::npos && !ToNumber(item.substr(dash + 1), 31, last)) ||
        (dash != std::string::npos && last < first)) {
      return false;
    }
    if (dash == std::string::npos) {
      last = first;
    }
    for (long c = first; c <= last; ++c) {
      channels |= 1u << c;
    }
    if (comma == std::string::npos) {
      break;
    }
    at = comma + 1;
  }
  return channels != 0;
}

/**
 * Reads a connection end, "NODE:GROUP" (NODE: main, m or -1 for the main node, else a
 * sub-node 0-15; GROUP: 0-255); false if @p text isn't one.
 */
bool ToEnd(const std::string& text, a2b::NodeAddr& node, uint8_t& group) {
  const size_t colon = text.find(':');
  long id = 0;
  if (colon == std::string::npos || !ToNumber(text.substr(colon + 1), 255, id)) {
    return false;
  }
  const std::string where = text.substr(0, colon);
  if (where == "-1") {
    node = a2b::kMainNode;
  } else if (!ToNode(where, node)) {
    return false;
  }
  group = static_cast<uint8_t>(id);
  return true;
}

/**
 * Reads a connection end for `importCon`, "NODE:GROUP", NODE being main (or m), or a chip ID
 * of 12 hex digits; false if @p text isn't one.
 */
bool ToChipEnd(const std::string& text, a2b::ConnectionEnd& end) {
  const size_t colon = text.find(':');
  long id = 0;
  if (colon == std::string::npos || !ToNumber(text.substr(colon + 1), 255, id)) {
    return false;
  }
  end = a2b::ConnectionEnd();
  end.group = static_cast<uint8_t>(id);
  const std::string where = text.substr(0, colon);
  if (Same(where, "main") || Same(where, "m")) {
    end.mainNode = true;
    return true;
  }
  if (where.size() != 2 * sizeof(end.chipId)) {
    return false;
  }
  for (size_t i = 0; i < sizeof(end.chipId); ++i) {
    const std::string pair = where.substr(2 * i, 2);
    if (!std::isxdigit(static_cast<unsigned char>(pair[0])) ||
        !std::isxdigit(static_cast<unsigned char>(pair[1]))) {
      return false;
    }
    end.chipId[i] = static_cast<uint8_t>(std::strtol(pair.c_str(), nullptr, 16));
  }
  return true;
}

/** A name for a routing mute mode. */
const char* Name(a2b::RoutingMute mode) {
  switch (mode) {
    case a2b::RoutingMute::kLocal:
      return "local (the nodes that would play a wrong channel)";
    case a2b::RoutingMute::kGlobal:
      return "global (the whole bus)";
    case a2b::RoutingMute::kNone:
      return "none";
  }
  return "?";
}

/** A name for a routing error. */
const char* Name(a2b::RoutingError error) {
  switch (error) {
    case a2b::RoutingError::kNone:
      return "none";
    case a2b::RoutingError::kNotSupported:
      return "not supported";
    case a2b::RoutingError::kNoSuchGroup:
      return "a connection names a group its node hasn't got";
    case a2b::RoutingError::kSameNode:
      return "a connection's source and sink are the same node";
    case a2b::RoutingError::kTooManySlots:
      return "more than 32 slots on a link";
    case a2b::RoutingError::kTooManyChannels:
      return "a node would play more than 32 channels";
    case a2b::RoutingError::kNoResponseCycles:
      return "the slots don't fit the superframe (no response-cycle window)";
    case a2b::RoutingError::kI2cError:
      return "I2C error while writing it";
    case a2b::RoutingError::kNoSuchChannel:
      return "a connection uses a channel its node's I2S/TDM port doesn't give";
    case a2b::RoutingError::kChannelOrder:
      return "a node without a crossbar (AD242x) can't play its channels in that order";
  }
  return "?";
}

}  // namespace

/**
 * Makes the console: no board, the bus stopped, module info from the EEPROMs.
 * @param host The host's platform (USBi, Linux i2c-dev, the simulator); must outlive the
 *        console.
 */
BenchConsole::BenchConsole(a2b::Platform& host)
    : m_Trace(host), m_Network(m_Trace, m_Board), m_Modules(m_Network) {
  m_Network.SetListener(this);
  m_Network.SetModuleInfoSource(&m_Modules);  // The EEPROMs, but for `module N`.
  // A stereo codec on both sides, as the AB0020 has (its ADAU1761): TDM channels 0-1 each
  // way, in group 0.
  (void)m_MainGroups.AddRx(0, 0x3);
  (void)m_MainGroups.AddTx(0, 0x3);
  // The image with the EEPROMs ignored; `group image` edits the console's copy.
  m_Plain = clockworks::GenericModule();
}

/** Stops the bus (nothing is sent) and closes the trace. */
BenchConsole::~BenchConsole() { m_Network.Stop(); }

/**
 * The boards built into the console: the repository's SigmaStudio+ exports.
 * @return Their names, descriptions and exports.
 */
const std::vector<BenchConsole::BuiltInBoard>& BenchConsole::BuiltInBoards() {
  static const std::vector<BuiltInBoard> kBoards = {
      {"ref",
       "reference board: AD2437 main node (adi/exports/rp2040/12_a2b_busconfig.c, as the Pi uses)",
       {{0x37, &a2b_bench_board_ref}},
       false},
      // The AB0020 is UTP on both ports, so every link is straight. Its AD2437 and AD2428
      // modules have an export each; `start` takes the one for the chip it finds.
      {"ab0020",
       "AB0020: AD2437 or AD2428 main node (the export for the chip found at start), I2S "
       "TDM2, UTP links, ADAU1761 codec programmed at start "
       "(tools/SS+reference/2x_AB0020_EVM_with_2437, _with_2428)",
       {{0x37, &a2b_bench_board_ab0020}, {0x28, &a2b_bench_board_ab0020_2428}},
       true},
      // The AB0310 makes SYNC and BCLK itself and has no peripheral to program; its links
      // are RJ45, as its export's XCVRBINV says.
      {"ab0310",
       "AB0310: AD2437 main node, I2S TDM2, RJ45 links, SYNC and BCLK from the board, no "
       "peripherals (tools/SS+reference/AB0310_AB0312_EVM_with_2437)",
       {{0x37, &a2b_bench_board_ab0310}},
       false},
  };
  return kBoards;
}

/**
 * Runs one command line. Unknown commands and bad arguments print a message; nothing
 * else happens.
 * @param line The command and its arguments.
 * @return False after `quit` (or `exit`): the host should stop.
 */
bool BenchConsole::Execute(const char* line) {
  const std::vector<std::string> args = Split(line);
  if (args.empty()) {
    return true;
  }
  const std::string& command = args[0];
  if (Same(command, "help") || command == "?") {
    Help();
  } else if (Same(command, "board")) {
    Board(args);
  } else if (Same(command, "start")) {
    Start();
  } else if (Same(command, "stop")) {
    StopBus();
  } else if (Same(command, "status")) {
    Status();
  } else if (Same(command, "link")) {
    Link(args);
  } else if (Same(command, "cable")) {
    Cable(args);
  } else if (Same(command, "module")) {
    Module(args);
  } else if (Same(command, "ignoreEEPROM")) {
    IgnoreEeprom(args);
  } else if (Same(command, "i2cTrace")) {
    Trace(args);
  } else if (Same(command, "interrupts")) {
    Interrupts(args);
  } else if (Same(command, "rdm") || Same(command, "rds")) {
    ReadRegisters(args);
  } else if (Same(command, "wrm") || Same(command, "wrs")) {
    WriteRegisters(args);
  } else if (Same(command, "periRead")) {
    PeripheralRead(args);
  } else if (Same(command, "periWrite")) {
    PeripheralWrite(args);
  } else if (Same(command, "pause")) {
    Pause();
  } else if (Same(command, "resume")) {
    Resume();
  } else if (Same(command, "reject")) {
    Reject(args);
  } else if (Same(command, "addCon") || Same(command, "removeCon")) {
    AddConnection(args);
  } else if (Same(command, "clearAllCon")) {
    m_Network.ClearConnections();
    Print("Every connection removed (routed until applyRouting or a change of the chain)\n");
  } else if (Same(command, "listCon")) {
    ListConnections();
  } else if (Same(command, "applyRouting")) {
    ApplyRouting();
  } else if (Same(command, "setRoute")) {
    SetRoute(args);
  } else if (Same(command, "importCon")) {
    ImportConnections(args);
  } else if (Same(command, "mute")) {
    MuteBus(args);
  } else if (Same(command, "muteMode")) {
    MuteMode(args);
  } else if (Same(command, "bandwidth")) {
    ShowBandwidth();
  } else if (Same(command, "routing")) {
    ShowRouting();
  } else if (Same(command, "groups")) {
    ShowGroups();
  } else if (Same(command, "group")) {
    SetGroup(args);
  } else if (Same(command, "quit") || Same(command, "exit")) {
    m_Network.Stop();
    m_Trace.Stop();
    return false;
  } else {
    Print("Unknown command '%s' (help lists them)\n", command.c_str());
  }
  return true;
}

/** Polls the bus once at the host's time; the events are printed at the end of it. */
void BenchConsole::Poll() { m_Network.Poll(m_Trace.NowMs()); }

/**
 * Gives what the console printed since the last call, and forgets it.
 * @return The text.
 */
std::string BenchConsole::TakeOutput() {
  std::string text;
  text.swap(m_Output);
  return text;
}

/**
 * Prints one event: a new state, a sub-node added (with its chip, module and link),
 * rejected or removed, or an interrupt the stack passed on.
 * @param network The bus.
 * @param event What happened.
 */
void BenchConsole::OnEvent(a2b::Network& network, const a2b::Event& event) {
  (void)network;
  const unsigned node = static_cast<unsigned>(event.node);
  switch (event.type) {
    case a2b::EventType::kStateChanged:
      Print("bus: %s\n", Name(event.state));
      if (event.state == a2b::NetworkState::kFailed) {
        Print("  error: %s\n", Name(m_Network.Error()));
        uint8_t product = 0;
        if (m_Network.Error() == a2b::NetworkError::kWrongMainNode &&
            ReadMainChip(m_Board.mainAddr, product)) {
          Print(
              "  the main node is an AD24%02X, the board's export is for an AD24%02X (board "
              "lists the built-in boards; board FILE loads an export)\n",
              product, m_Board.mainProduct);
        }
      } else if (event.state == a2b::NetworkState::kRunning) {
        Print("  %u sub-node(s)\n", static_cast<unsigned>(m_Network.SubNodeCount()));
      }
      return;
    case a2b::EventType::kNodeAdded:
      m_OpenLineHinted = a2b::kNoNode;
      ReportNode(event.node);
      return;
    case a2b::EventType::kNodeRejected:
      Print("  sub-node %u rejected: %s\n", node, Name(event.rejection));
      return;
    case a2b::EventType::kNodeRemoved:
      m_OpenLineHinted = a2b::kNoNode;
      Print("  sub-node %u removed: %s\n", node, Name(event.removal));
      return;
    case a2b::EventType::kInterrupt: {
      // The last sub-node reports its open B side each time a probe closes its switch
      // with nothing plugged in behind it (AD242x line diagnostics): a known condition,
      // hidden, with a hint the first time.
      const bool openLineAtEnd =
          event.node != a2b::kMainNode &&
          static_cast<size_t>(event.node) + 1 == m_Network.SubNodeCount() &&
          (event.interruptType == kIntOpenLine || event.interruptType == kIntOpenLineLowVin);
      if (openLineAtEnd && !m_ShowAllInterrupts) {
        ++m_HiddenInterrupts;
      } else if (event.node == a2b::kMainNode) {
        Print("  interrupt from the main node: type %u\n",
              static_cast<unsigned>(event.interruptType));
      } else {
        Print("  interrupt from sub-node %u: type %u\n", node,
              static_cast<unsigned>(event.interruptType));
      }
      if (openLineAtEnd) {
        HintOpenLine(event.node, event.interruptType);
      }
      return;
    }
    case a2b::EventType::kRoutingApplied: {
      const a2b::RoutingPlan& plan = m_Network.Routing();
      Print("routing applied: %u connection(s) routed, main node RESPCYCS 0x%02X (window %u-%u)\n",
            static_cast<unsigned>(plan.routed), plan.main.respCycles,
            static_cast<unsigned>(plan.respCycles.minMain),
            static_cast<unsigned>(plan.respCycles.maxMain));
      return;
    }
    case a2b::EventType::kRoutingFailed: {
      const std::string where =
          (event.node == a2b::kNoNode) ? std::string() : " (" + NodeName(event.node) + ")";
      Print("routing failed: %s%s\n", Name(event.routingError), where.c_str());
      return;
    }
  }
}

/** Adds printf-style text to the output, however long. */
void BenchConsole::Print(const char* format, ...) {
  va_list args;
  va_start(args, format);
  va_list again;
  va_copy(again, args);
  const int len = std::vsnprintf(nullptr, 0, format, args);
  va_end(args);
  if (len > 0) {
    std::string text(static_cast<size_t>(len) + 1, '\0');
    std::vsnprintf(&text[0], text.size(), format, again);
    text.resize(static_cast<size_t>(len));
    m_Output += text;
  }
  va_end(again);
}

/** `help`: the commands. */
void BenchConsole::Help() {
  Print(
      "board                    list the built-in boards; show the one picked\n"
      "board NAME [CHAIN]       pick a built-in board (one with exports for several main\n"
      "                         node chips takes the one for the chip found at start)\n"
      "board FILE [CHAIN]       load a SigmaStudio+ export (adi_a2b_busconfig.c); quote a\n"
      "                         path with spaces. CHAIN: the export's chain (default 0)\n"
      "start                    import the board and start the bus (again if running)\n"
      "stop                     stop the bus (nothing more is sent)\n"
      "status                   the bus, its sub-nodes, links, module info and trace\n"
      "link                     list the links set\n"
      "link N utp|rj45|custom   the link into sub-node N (0-15). utp and rj45 power the\n"
      "                         next node over the A2B pair; rj45 also inverts the B side\n"
      "                         (XCVRBINV on the node before it), single-step for now.\n"
      "                         custom leaves the node on its own power\n"
      "link N custom inverted   the same, with an RJ45 cable (B side inverted)\n"
      "link N default           back to the board's link (or all: link all default)\n"
      "cable                    list the cable lengths set\n"
      "cable N METRES           the cable into sub-node N (0-15) for the response cycles:\n"
      "                         0.1-30, e.g. 2.5; the board's is 4 m. Re-routes a routed bus\n"
      "cable N default          back to the board's length (or all: cable all default)\n"
      "module                   list the boards set at sub-node positions (or the boards)\n"
      "module N BOARD [FILE]    sub-node N (0-15, or all) is a Clockworks BOARD (ab0020,\n"
      "                         ab0312): its settings, groups and codec program, the\n"
      "                         board's or FILE's (SigmaStudio's XML), even with the\n"
      "                         EEPROMs read\n"
      "module N default         back to the EEPROM or ignoreEEPROM's image (or all)\n"
      "ignoreEEPROM 1|0         give sub-nodes the console's module info / read EEPROMs\n"
      "i2cTrace on FILE         write every I2C transfer to FILE (I2CT lines)\n"
      "i2cTrace off|status      stop tracing / show the trace\n"
      "interrupts quiet|all     hide (the default) or show the open-line interrupts (types\n"
      "                         12, 13) of the last sub-node, which each probe behind it\n"
      "                         gives when nothing is plugged in there\n"
      "rdm REG [COUNT]          read main-node registers from REG (0x100-0x1FF: page 1,\n"
      "                         AD243x only); COUNT 1-256, default 1\n"
      "rds N REG [COUNT]        read sub-node N's registers\n"
      "wrm REG VALUE...         write main-node registers from REG, one VALUE each\n"
      "wrs N REG VALUE...       write sub-node N's registers. Registers the stack owns\n"
      "                         (CONTROL, SWCTL, DISCVRY, the interrupt registers, ...) at\n"
      "                         your risk; reading INTTYPE takes its interrupt from the\n"
      "                         stack. MMRPAGE can't be written: REG picks the page\n"
      "periRead NODE ADDR COUNT [BYTE...]\n"
      "                         read COUNT bytes (1-256) from the I2C peripheral at ADDR\n"
      "                         (7-bit) of NODE (main, or a sub-node 0-15); with BYTEs,\n"
      "                         write them first (a sub-address), then a repeated start\n"
      "periWrite NODE ADDR BYTE...\n"
      "                         write the bytes to the peripheral\n"
      "pause                    no new probes for sub-nodes (the chain is still checked)\n"
      "resume                   probe again at the end of the chain\n"
      "reject N                 take sub-node N, and those after it, out of the bus\n"
      "addCon S:G D:G           connect RX group G of node S to TX group G of node D (S, D:\n"
      "                         main, m or -1 for the main node, 0-15 for a sub-node in the\n"
      "                         bus); kept by chip ID, routed at applyRouting\n"
      "removeCon S:G D:G        remove that connection\n"
      "clearAllCon              remove every connection\n"
      "listCon                  list the connections, and where their nodes are\n"
      "setRoute [append] S:G D:G...\n"
      "                         one audio stream: RX group G of node S to TX group G of each\n"
      "                         node D; replaces every connection unless append (all or\n"
      "                         nothing); routed at applyRouting\n"
      "importCon S:G D:G [S:G D:G...]\n"
      "                         replace every connection with these; a node is main, m or\n"
      "                         a chip ID of 12 hex digits (listCon shows them), so modules\n"
      "                         that aren't in the bus yet can be named\n"
      "applyRouting             route the connections, now and whenever the chain changes\n"
      "mute [on|off]            mute or unmute the whole bus (DATCTL); without on or off,\n"
      "                         say which\n"
      "muteMode [local|global|none]\n"
      "                         what the stack mutes while it writes a routing plan: the\n"
      "                         nodes that would play a wrong channel meanwhile (local; the\n"
      "                         whole bus if one has no crossbar), the whole bus, or\n"
      "                         nothing (the setting lasts across start)\n"
      "bandwidth                how full the bus is with the plan built last\n"
      "routing                  the routing written last: each node's slots, masks,\n"
      "                         offsets, RESPCYCS and crossbar\n"
      "groups                   the channel groups: the main node's, the console's image's\n"
      "                         and each sub-node's\n"
      "group main|image rx|tx ID CHANNELS\n"
      "                         set a group of the main node (used at the next start) or\n"
      "                         of the console's module info (ignoreEEPROM); CHANNELS: 0-1,\n"
      "                         0,2,4-7, or none to remove the group\n"
      "quit                     stop the bus and leave\n");
}

/**
 * `board`: lists the boards, or picks one by name or loads one from a file. A bus that is
 * running is stopped first: its board's settings are about to change.
 * @param args The command line's words.
 */
void BenchConsole::Board(const std::vector<std::string>& args) {
  if (args.size() < 2) {
    Print("Built-in boards:\n");
    for (const BuiltInBoard& board : BuiltInBoards()) {
      Print("  %-8s %s\n", board.name, board.description);
    }
    Print("Board: %s", m_Bcd != nullptr ? m_BoardName.c_str() : "none (board NAME or FILE)");
    Print(m_Bcd != nullptr ? ", chain %u\n" : "\n", static_cast<unsigned>(m_Chain));
    return;
  }
  long chain = 0;
  if (args.size() > 2 && !ToNumber(args[2], 255, chain)) {
    Print("board: CHAIN is a number\n");
    return;
  }
  if (Running()) {
    m_Network.Stop();
    Print("Bus stopped for the new board\n");
  }
  for (const BuiltInBoard& board : BuiltInBoards()) {
    if (Same(args[1], board.name)) {
      m_Bcd = board.exports.front().bus;
      m_BuiltIn = &board;
      m_BoardName = board.name;
      m_LinksUtp = board.linksUtp;
      m_Chain = static_cast<uint8_t>(chain);
      Print("Board %s, chain %ld\n", board.name, chain);
      return;
    }
  }
  m_Bcd = nullptr;  // The file's structures are freed by the load.
  m_BuiltIn = nullptr;
  m_BoardName.clear();
  m_LinksUtp = false;
  const a2b::Result result = m_File.Load(args[1].c_str());
  const std::vector<std::string>& warnings = m_File.Warnings();
  for (size_t i = 0; i < warnings.size() && i < kMaxWarningsShown; ++i) {
    Print("  warning: %s\n", warnings[i].c_str());
  }
  if (warnings.size() > kMaxWarningsShown) {
    Print("  ... and %u more warnings\n",
          static_cast<unsigned>(warnings.size() - kMaxWarningsShown));
  }
  if (result != a2b::Result::kOk) {
    Print("Can't load %s: %s\n", args[1].c_str(), m_File.Error().c_str());
    return;
  }
  m_Bcd = m_File.Bus();
  m_BoardName = args[1];
  m_Chain = static_cast<uint8_t>(chain);
  Print("Board %s, chain %ld (%u chain(s) in the export)\n", args[1].c_str(), chain,
        static_cast<unsigned>(m_Bcd->nNumMasterNode));
}

/**
 * `start`: imports the board into a fresh BoardConfig and starts the bus; a bus already
 * started is stopped and started again. The links set with `link` stay set. A built-in
 * board with an export for more than one main-node chip (ab0020) first reads the main
 * node's VENDOR and PRODUCT, at the default export's main-node address, and takes the
 * export for that chip; a chip it has none for is refused, and a main node that doesn't
 * answer gets the default export. A board from a file, or with one export, is used as it is
 * (the stack checks the chip at start, NetworkError::kWrongMainNode).
 */
void BenchConsole::Start() {
  if (m_Bcd == nullptr) {
    Print("No board: pick one first (board lists them)\n");
    return;
  }
  m_Network.Stop();
  m_OpenLineHinted = a2b::kNoNode;
  if (!ImportBoard(*m_Bcd)) {
    return;
  }
  if (m_BuiltIn != nullptr && m_BuiltIn->exports.size() > 1) {
    uint8_t product = 0;
    if (!ReadMainChip(m_Board.mainAddr, product)) {
      Print("The main node at 0x%02X didn't answer: board %s's AD24%02X export\n", m_Board.mainAddr,
            m_BoardName.c_str(), m_Board.mainProduct);
    } else {
      const BoardExport* found = nullptr;
      for (const BoardExport& candidate : m_BuiltIn->exports) {
        found = (candidate.mainProduct == product) ? &candidate : found;
      }
      if (found == nullptr) {
        Print(
            "The main node is an AD24%02X: board %s has exports for the %s only (board FILE "
            "loads another)\n",
            product, m_BoardName.c_str(), ExportChips(*m_BuiltIn).c_str());
        return;
      }
      Print("Main node AD24%02X found: board %s's export for it\n", product, m_BoardName.c_str());
      if (found->bus != m_Bcd && !ImportBoard(*found->bus)) {
        return;
      }
    }
  }
  m_Modules.SetFallback(m_IgnoreEeprom ? &m_Plain : nullptr);
  m_Network.SetModuleInfoSource(&m_Modules);
  m_Board.routing.groups = m_MainGroups;
  m_Board.routing.mute = m_MuteMode;
  if (m_LinksUtp) {
    m_Board.defaultLink.bSideInverted = false;  // UTP, whatever the export's XCVRBINV.
  }
  Print(
      "Main node AD24%02X at 0x%02X (sub-nodes 0x%02X), %u host script operation(s) (%u "
      "before the main node), links %s by default\n",
      m_Board.mainProduct, m_Board.mainAddr, m_Board.busAddr,
      static_cast<unsigned>(m_HostScript.Size()),
      static_cast<unsigned>(m_Board.hostOpsBeforeMainNode),
      ProfileName(m_Board.defaultLink).c_str());
  const a2b::Result started = m_Network.Start();
  if (started != a2b::Result::kOk) {
    Print("Can't start: %s\n", Name(started));
  }
}

/**
 * Imports chain #m_Chain of an export into a fresh #m_Board and #m_HostScript.
 * @param bus The export.
 * @return True if imported; false, with the reason printed, if not.
 */
bool BenchConsole::ImportBoard(const ADI_A2B_BCD& bus) {
  m_Board = a2b::BoardConfig();
  const a2b::Result imported = a2b::ImportSsPlus(bus, m_Chain, m_Board, m_HostScript);
  if (imported != a2b::Result::kOk) {
    Print("Can't import chain %u of %s: %s\n", static_cast<unsigned>(m_Chain), m_BoardName.c_str(),
          Name(imported));
    return false;
  }
  return true;
}

/**
 * Reads the main node's VENDOR and PRODUCT (registers 0x02 and 0x03) straight from the host,
 * through the trace (so an I2C trace shows the read, ahead of the stack's own). The stack
 * isn't running then: the bus is stopped before a start, or failed.
 * @param mainAddr The main node's I2C address.
 * @param product Set to PRODUCT.
 * @return True if the main node answered as an ADI chip (VENDOR 0xAD).
 */
bool BenchConsole::ReadMainChip(uint8_t mainAddr, uint8_t& product) {
  const uint8_t vendorReg = 0x02;
  uint8_t ids[2] = {};
  if (m_Trace.I2cWriteRead(mainAddr, &vendorReg, 1, ids, sizeof(ids)) != a2b::Result::kOk ||
      ids[0] != 0xAD) {
    return false;
  }
  product = ids[1];
  return true;
}

/**
 * Names the main-node chips a built-in board has exports for.
 * @param board The board.
 * @return "AD2437", "AD2437 or AD2428", ...
 */
std::string BenchConsole::ExportChips(const BuiltInBoard& board) const {
  std::string chips;
  char name[8];
  for (size_t i = 0; i < board.exports.size(); ++i) {
    std::snprintf(name, sizeof(name), "AD24%02X", board.exports[i].mainProduct);
    chips += (i == 0) ? "" : ((i + 1 == board.exports.size()) ? " or " : ", ");
    chips += name;
  }
  return chips;
}

/** `stop`: stops the bus; nothing more is sent. */
void BenchConsole::StopBus() {
  m_Network.Stop();
  Print("Bus stopped\n");
}

/** `status`: the board, the bus and its sub-nodes, the links, module info and trace. */
void BenchConsole::Status() {
  Print("Board: %s", m_Bcd != nullptr ? m_BoardName.c_str() : "none");
  Print(m_Bcd != nullptr ? ", chain %u\n" : "\n", static_cast<unsigned>(m_Chain));
  Print("Bus: %s", Name(m_Network.State()));
  if (m_Network.State() == a2b::NetworkState::kFailed) {
    Print(" (%s)", Name(m_Network.Error()));
  }
  if (m_Network.State() != a2b::NetworkState::kStopped) {
    Print(", main node AD24%02X at 0x%02X", m_Board.mainProduct, m_Board.mainAddr);
  }
  Print(", %u sub-node(s)%s\n", static_cast<unsigned>(m_Network.SubNodeCount()),
        m_Network.DiscoveryPaused() ? ", discovery paused" : "");
  for (size_t i = 0; i < m_Network.SubNodeCount(); ++i) {
    ReportNode(static_cast<a2b::NodeAddr>(i));
  }
  Print("Last rejection: %s\n", Name(m_Network.LastRejection()));
  Print("Links: %s by default", ProfileName(m_Board.defaultLink).c_str());
  for (int n = 0; n < a2b::kMaxSubNodes; ++n) {
    if (m_LinkSet[n]) {
      Print("; into %d %s", n, ProfileName(m_Links[n]).c_str());
    }
  }
  Print("\nCables: %s by default", Metres(m_Board.routing.cableDm).c_str());
  for (int n = 0; n < a2b::kMaxSubNodes; ++n) {
    if (m_Cables[n] != 0) {
      Print("; into %d %s", n, Metres(m_Cables[n]).c_str());
    }
  }
  bool anyModule = false;
  for (int n = 0; n < a2b::kMaxSubNodes; ++n) {
    const clockworks::ModuleProfile* board = m_Modules.Board(static_cast<a2b::NodeAddr>(n));
    if (board != nullptr) {
      Print("%s%d %s", anyModule ? ", " : "\nBoards: sub-node ", n, board->name);
      anyModule = true;
    }
  }
  Print("\nMute: bus %s, routing mute %s\n", m_Network.NetworkMuted() ? "muted" : "not muted",
        Name(m_MuteMode));
  Print("Module info: %s\n", m_IgnoreEeprom ? "the console's (EEPROMs ignored)" : "the EEPROMs");
  Print("Interrupts: %s, %u open-line interrupt(s) of the last sub-node hidden\n",
        m_ShowAllInterrupts ? "all shown" : "quiet", static_cast<unsigned>(m_HiddenInterrupts));
  if (m_Trace.Tracing()) {
    Print("I2C trace: %s, %u transfer(s)\n", m_TraceFile.c_str(),
          static_cast<unsigned>(m_Trace.Count()));
  } else {
    Print("I2C trace: off\n");
  }
}

/**
 * `link`: lists the links set, or sets or clears the link into a sub-node. It applies from
 * the next probe of that position.
 * @param args The command line's words.
 */
void BenchConsole::Link(const std::vector<std::string>& args) {
  if (args.size() < 2) {
    bool any = false;
    for (int n = 0; n < a2b::kMaxSubNodes; ++n) {
      if (m_LinkSet[n]) {
        Print("link %d %s\n", n, ProfileName(m_Links[n]).c_str());
        any = true;
      }
    }
    if (!any) {
      Print("No links set: every link is the board's (%s)\n",
            ProfileName(m_Board.defaultLink).c_str());
    }
    return;
  }
  if (args.size() == 3 && Same(args[1], "all") && Same(args[2], "default")) {
    for (int n = 0; n < a2b::kMaxSubNodes; ++n) {
      m_Network.ClearLinkProfile(static_cast<a2b::NodeAddr>(n));
      m_LinkSet[n] = false;
    }
    Print("Every link is the board's\n");
    return;
  }
  long position = 0;
  if (args.size() < 3 || !ToNumber(args[1], a2b::kMaxSubNodes - 1, position)) {
    Print(
        "link N utp|rj45|custom|default (N: 0-15; custom inverted for a locally powered RJ45 "
        "link)\n");
    return;
  }
  const a2b::NodeAddr node = static_cast<a2b::NodeAddr>(position);
  if (Same(args[2], "default") && args.size() == 3) {
    m_Network.ClearLinkProfile(node);
    m_LinkSet[position] = false;
    Print("Link into sub-node %ld: the board's\n", position);
    return;
  }
  a2b::LinkProfile profile;
  bool inverted = false;
  if (Same(args[2], "utp") && args.size() == 3) {
    profile = a2b::LinkProfile::Utp();
  } else if (Same(args[2], "rj45") && args.size() == 3) {
    profile = a2b::LinkProfile::Rj45();  // B side inverted; single-step for now.
    profile.discovery = a2b::LinkProfile::Discovery::kSingleStep;
    inverted = true;
  } else if (Same(args[2], "custom") &&
             (args.size() == 3 || (args.size() == 4 && Same(args[3], "inverted")))) {
    profile = a2b::LinkProfile::Custom();
    inverted = (args.size() == 4);
  } else {
    Print(
        "link N utp|rj45|custom|default (N: 0-15; custom inverted for a locally powered RJ45 "
        "link)\n");
    return;
  }
  profile.bSideInverted = inverted;
  const a2b::Result result = m_Network.SetLinkProfile(node, profile);
  if (result != a2b::Result::kOk) {
    Print("Can't set the link: %s\n", Name(result));
    return;
  }
  m_LinkSet[position] = true;
  m_Links[position] = profile;
  Print("Link into sub-node %ld: %s\n", position, ProfileName(profile).c_str());
}

/**
 * `cable`: lists the cable lengths set, or sets or clears the cable into a sub-node, for
 * the response cycles (Network::SetCableLength()). It applies to the next probe of that
 * position and, on a routed bus, to a new plan at once.
 * @param args The command line's words.
 */
void BenchConsole::Cable(const std::vector<std::string>& args) {
  const char* usage =
      "cable N METRES|default (N: 0-15; METRES: 0.1-30, one decimal; or cable all default)\n";
  if (args.size() < 2) {
    bool any = false;
    for (int n = 0; n < a2b::kMaxSubNodes; ++n) {
      if (m_Cables[n] != 0) {
        Print("cable %d %s\n", n, Metres(m_Cables[n]).c_str());
        any = true;
      }
    }
    if (!any) {
      Print("No cables set: every link is the board's (%s)\n",
            Metres(m_Board.routing.cableDm).c_str());
    }
    return;
  }
  if (args.size() == 3 && Same(args[1], "all") && Same(args[2], "default")) {
    for (int n = 0; n < a2b::kMaxSubNodes; ++n) {
      (void)m_Network.ClearCableLength(static_cast<a2b::NodeAddr>(n));
      m_Cables[n] = 0;
    }
    Print("Every cable is the board's (%s)\n", Metres(m_Board.routing.cableDm).c_str());
    return;
  }
  long position = 0;
  if (args.size() != 3 || !ToNumber(args[1], a2b::kMaxSubNodes - 1, position)) {
    Print("%s", usage);
    return;
  }
  const a2b::NodeAddr node = static_cast<a2b::NodeAddr>(position);
  if (Same(args[2], "default")) {
    (void)m_Network.ClearCableLength(node);
    m_Cables[position] = 0;
    Print("Cable into sub-node %ld: the board's (%s)\n", position,
          Metres(m_Board.routing.cableDm).c_str());
    return;
  }
  uint16_t decimetres = 0;
  if (!ToDecimetres(args[2], decimetres) ||
      m_Network.SetCableLength(node, decimetres) != a2b::Result::kOk) {
    Print("%s", usage);
    return;
  }
  m_Cables[position] = decimetres;
  Print("Cable into sub-node %ld: %s\n", position, Metres(decimetres).c_str());
}

/**
 * `module`: lists the boards set at sub-node positions, or sets one at a position (or at
 * all), with its codec's default program or one from a SigmaStudio XML file, or clears it.
 * A position with a board gets the board's image even with the EEPROMs read
 * (clockworks::BoardModuleSource). It applies to nodes identified from then on (a re-plug,
 * `start`).
 * @param args The command line's words.
 */
void BenchConsole::Module(const std::vector<std::string>& args) {
  std::string names;
  for (const clockworks::ModuleProfile& board : clockworks::Boards()) {
    names += (names.empty() ? "" : "|") + std::string(board.name);
  }
  const std::string usage = "module N|all " + names +
                            " [FILE], or module N|all default (N: 0-15; FILE: the codec's "
                            "SigmaStudio XML program, in place of the board's)\n";
  if (args.size() == 1) {
    bool any = false;
    for (int n = 0; n < a2b::kMaxSubNodes; ++n) {
      const clockworks::ModuleProfile* board = m_Modules.Board(static_cast<a2b::NodeAddr>(n));
      if (board != nullptr) {
        Print("module %d %s", n, board->name);
        if (board->codecName != nullptr) {
          Print(", %s program %s (%u writes)", board->codecName, m_ModuleFiles[n].c_str(),
                static_cast<unsigned>(m_ModuleWrites[n]));
        }
        Print("\n");
        any = true;
      }
    }
    if (!any) {
      Print("No boards set: sub-nodes get their EEPROM's or ignoreEEPROM's module info\n");
      for (const clockworks::ModuleProfile& board : clockworks::Boards()) {
        Print("  %-8s %s\n", board.name, board.description);
      }
    }
    return;
  }
  long position = 0;
  const bool all = args.size() >= 2 && Same(args[1], "all");
  if (args.size() < 3 || (!all && !ToNumber(args[1], a2b::kMaxSubNodes - 1, position))) {
    Print("%s", usage.c_str());
    return;
  }
  const int first = all ? 0 : static_cast<int>(position);
  const int last = all ? a2b::kMaxSubNodes - 1 : static_cast<int>(position);
  const std::string who = all ? "Every sub-node" : "Sub-node " + std::to_string(position);
  if (args.size() == 3 && Same(args[2], "default")) {
    for (int n = first; n <= last; ++n) {
      m_Modules.SetBoard(static_cast<a2b::NodeAddr>(n), nullptr, SigmaProgram());
      m_ModuleFiles[n].clear();
      m_ModuleWrites[n] = 0;
    }
    Print("%s: no board set (for nodes identified from now on)\n", who.c_str());
    return;
  }
  const clockworks::ModuleProfile* board = clockworks::FindBoard(args[2]);
  if (board == nullptr || args.size() > 4 || (args.size() == 4 && board->codecName == nullptr)) {
    Print("%s", usage.c_str());
    return;
  }
  SigmaProgram program;
  std::string error;
  const bool fromFile = (args.size() == 4);
  if (fromFile ? !ReadSigmaXml(args[3], board->codecAddr, program, error)
               : !clockworks::DefaultProgram(*board, program, error)) {
    Print("Can't read the codec program: %s\n", error.c_str());
    return;
  }
  size_t writes = 0;
  size_t bytes = 0;
  for (const SigmaAction& action : program.actions) {
    writes += action.wait ? 0 : 1;
    bytes += action.bytes.size();
  }
  for (int n = first; n <= last; ++n) {
    m_Modules.SetBoard(static_cast<a2b::NodeAddr>(n), board, program);
    m_ModuleFiles[n] = fromFile ? args[3] : "the board's default";
    m_ModuleWrites[n] = writes;
  }
  Print("%s: %s", who.c_str(), board->name);
  if (board->codecName != nullptr) {
    Print(", %s program %u writes, %u bytes, %u wait(s)", board->codecName,
          static_cast<unsigned>(writes), static_cast<unsigned>(bytes),
          static_cast<unsigned>(program.actions.size() - writes));
  }
  Print(" (for nodes identified from now on)\n");
  if (program.readsSkipped > 0) {
    Print("  %u read action(s) skipped: a module's peripheral script only writes and waits\n",
          static_cast<unsigned>(program.readsSkipped));
  }
}

/**
 * `ignoreEEPROM 1|0`: sub-nodes get the console's module info, or read their EEPROMs.
 * It applies to the next node identified.
 * @param args The command line's words.
 */
void BenchConsole::IgnoreEeprom(const std::vector<std::string>& args) {
  if (args.size() != 2 ||
      !(args[1] == "1" || args[1] == "0" || Same(args[1], "on") || Same(args[1], "off"))) {
    Print("ignoreEEPROM 1|0\n");
    return;
  }
  m_IgnoreEeprom = (args[1] == "1" || Same(args[1], "on"));
  m_Modules.SetFallback(m_IgnoreEeprom ? &m_Plain : nullptr);
  Print(m_IgnoreEeprom ? "EEPROMs ignored: sub-nodes get the console's module info (CFG-4 on "
                         "an AD2437, CFG-0 on an AD2433)\n"
                       : "Module info from the EEPROMs\n");
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

/**
 * `interrupts quiet|all`: whether the last sub-node's open-line interrupts (types 12 and
 * 13: PWRERR, cable disconnected, open circuit or wrong port) are printed. Each probe
 * behind the last node closes its switch, and with nothing plugged in there an AD242x
 * reports its open B side; quiet (the default) counts them in `status` instead. Every
 * other interrupt is printed either way.
 * @param args The command line's words.
 */
void BenchConsole::Interrupts(const std::vector<std::string>& args) {
  if (args.size() == 2 && (Same(args[1], "quiet") || Same(args[1], "all"))) {
    m_ShowAllInterrupts = Same(args[1], "all");
  } else if (args.size() != 1) {
    Print("interrupts quiet|all\n");
    return;
  }
  Print("Interrupts: %s (%u open-line interrupt(s) of the last sub-node hidden so far)\n",
        m_ShowAllInterrupts ? "all shown" : "quiet, the last sub-node's open line hidden",
        static_cast<unsigned>(m_HiddenInterrupts));
}

/**
 * `rdm REG [COUNT]`, `rds N REG [COUNT]`: reads registers of the main node or a sub-node
 * and prints them, 16 to a line, each line from its first register.
 * @param args The command line's words.
 */
void BenchConsole::ReadRegisters(const std::vector<std::string>& args) {
  const bool main = Same(args[0], "rdm");
  const size_t at = main ? 1 : 2;  // REG's word.
  long position = 0;
  long reg = 0;
  long count = 1;
  if (args.size() < at + 1 || args.size() > at + 2 ||
      (!main && !ToNumber(args[1], a2b::kMaxSubNodes - 1, position)) ||
      !ToNumber(args[at], kLastRegister, reg) ||
      (args.size() == at + 2 && (!ToNumber(args[at + 1], kMaxCount, count) || count == 0))) {
    Print(main ? "rdm REG [COUNT] (REG: 0x00-0x1FF; COUNT: 1-256)\n"
               : "rds N REG [COUNT] (N: 0-15; REG: 0x00-0x1FF; COUNT: 1-256)\n");
    return;
  }
  const a2b::NodeAddr node = main ? a2b::kMainNode : static_cast<a2b::NodeAddr>(position);
  std::vector<uint8_t> values(static_cast<size_t>(count));
  const a2b::Result result =
      m_Network.ReadRegisters(node, static_cast<uint16_t>(reg), values.data(), values.size());
  if (result != a2b::Result::kOk) {
    Print("Can't read %s: %s\n", NodeName(node).c_str(), Why(node, result, true).c_str());
    return;
  }
  for (size_t done = 0; done < values.size(); done += kBytesPerLine) {
    const size_t len =
        (values.size() - done < kBytesPerLine) ? values.size() - done : kBytesPerLine;
    Print("%s 0x%02lX: %s\n", NodeName(node, false).c_str(), reg + static_cast<long>(done),
          Hex(&values[done], len).c_str());
  }
}

/**
 * `wrm REG VALUE...`, `wrs N REG VALUE...`: writes registers of the main node or a
 * sub-node, from REG on, one value each.
 * @param args The command line's words.
 */
void BenchConsole::WriteRegisters(const std::vector<std::string>& args) {
  const bool main = Same(args[0], "wrm");
  const size_t at = main ? 1 : 2;  // REG's word.
  long position = 0;
  long reg = 0;
  std::vector<uint8_t> values;
  if (args.size() < at + 2 || (!main && !ToNumber(args[1], a2b::kMaxSubNodes - 1, position)) ||
      !ToNumber(args[at], kLastRegister, reg) || !ToBytes(args, at + 1, values)) {
    Print(main ? "wrm REG VALUE... (REG: 0x00-0x1FF; VALUE: 0x00-0xFF)\n"
               : "wrs N REG VALUE... (N: 0-15; REG: 0x00-0x1FF; VALUE: 0x00-0xFF)\n");
    return;
  }
  const a2b::NodeAddr node = main ? a2b::kMainNode : static_cast<a2b::NodeAddr>(position);
  const a2b::Result result =
      m_Network.WriteRegisters(node, static_cast<uint16_t>(reg), values.data(), values.size());
  if (result != a2b::Result::kOk) {
    Print("Can't write %s: %s\n", NodeName(node).c_str(), Why(node, result, true).c_str());
    return;
  }
  Print("Wrote %s 0x%02lX: %s\n", NodeName(node, false).c_str(), reg,
        Hex(values.data(), values.size()).c_str());
}

/**
 * `periRead NODE ADDR COUNT [BYTE...]`: reads a peripheral of a node; with BYTEs, writes
 * them first and reads after a repeated start (a sub-address, then its data). Prints 16
 * bytes to a line, each line from its offset in the read.
 * @param args The command line's words.
 */
void BenchConsole::PeripheralRead(const std::vector<std::string>& args) {
  a2b::NodeAddr node = a2b::kMainNode;
  long addr = 0;
  long count = 0;
  std::vector<uint8_t> address;
  if (args.size() < 4 || !ToNode(args[1], node) || !ToNumber(args[2], 0x7F, addr) ||
      !ToNumber(args[3], kMaxCount, count) || count == 0 || !ToBytes(args, 4, address)) {
    Print(
        "periRead NODE ADDR COUNT [BYTE...] (NODE: main or 0-15; ADDR: 0x00-0x7F; COUNT: "
        "1-256)\n");
    return;
  }
  std::vector<uint8_t> data(static_cast<size_t>(count));
  const a2b::PeripheralAddress peripheral = a2b::PeripheralAddress::I2c(static_cast<uint8_t>(addr));
  const a2b::Result result =
      address.empty() ? m_Network.PeripheralRead(node, peripheral, data.data(), data.size())
                      : m_Network.PeripheralWriteRead(node, peripheral, address.data(),
                                                      address.size(), data.data(), data.size());
  if (result != a2b::Result::kOk) {
    Print("Can't read peripheral 0x%02lX of %s: %s\n", addr, NodeName(node).c_str(),
          Why(node, result, false).c_str());
    return;
  }
  Print("Peripheral 0x%02lX of %s:\n", addr, NodeName(node).c_str());
  for (size_t done = 0; done < data.size(); done += kBytesPerLine) {
    const size_t len = (data.size() - done < kBytesPerLine) ? data.size() - done : kBytesPerLine;
    Print("  %02X: %s\n", static_cast<unsigned>(done), Hex(&data[done], len).c_str());
  }
}

/**
 * `periWrite NODE ADDR BYTE...`: writes the bytes to a peripheral of a node, in one
 * transfer.
 * @param args The command line's words.
 */
void BenchConsole::PeripheralWrite(const std::vector<std::string>& args) {
  a2b::NodeAddr node = a2b::kMainNode;
  long addr = 0;
  std::vector<uint8_t> data;
  if (args.size() < 4 || !ToNode(args[1], node) || !ToNumber(args[2], 0x7F, addr) ||
      !ToBytes(args, 3, data)) {
    Print("periWrite NODE ADDR BYTE... (NODE: main or 0-15; ADDR: 0x00-0x7F; BYTE: 0x00-0xFF)\n");
    return;
  }
  const a2b::Result result = m_Network.PeripheralWrite(
      node, a2b::PeripheralAddress::I2c(static_cast<uint8_t>(addr)), data.data(), data.size());
  if (result != a2b::Result::kOk) {
    Print("Can't write peripheral 0x%02lX of %s: %s\n", addr, NodeName(node).c_str(),
          Why(node, result, false).c_str());
    return;
  }
  Print("Wrote peripheral 0x%02lX of %s: %s\n", addr, NodeName(node).c_str(),
        Hex(data.data(), data.size()).c_str());
}

/** `pause`: no new probes; the nodes in the bus stay, and the chain is still checked. */
void BenchConsole::Pause() {
  m_Network.PauseDiscovery();
  Print("Discovery paused: no new sub-nodes until resume\n");
}

/** `resume`: probes again at the end of the chain, at the next poll. */
void BenchConsole::Resume() {
  m_Network.ResumeDiscovery();
  Print("Discovery resumed\n");
}

/**
 * `reject N`: takes sub-node N and those after it out of the bus at the next poll (their
 * removal is printed then). The same node found there again is switched off again.
 * @param args The command line's words.
 */
void BenchConsole::Reject(const std::vector<std::string>& args) {
  long position = 0;
  if (args.size() != 2 || !ToNumber(args[1], a2b::kMaxSubNodes - 1, position)) {
    Print("reject N (N: 0-15)\n");
    return;
  }
  const a2b::NodeAddr node = static_cast<a2b::NodeAddr>(position);
  if (m_Network.RejectNode(node) != a2b::Result::kOk) {
    Print("Can't reject: %s\n", Why(node, a2b::Result::kInvalidArgument, false).c_str());
    return;
  }
  Print("Rejecting sub-node %ld and those after it\n", position);
}

/**
 * Explains a failed access to @p node: a sub-node not in the bus, the I2C error, or (for
 * @p registers) a register out of the node's range.
 */
std::string BenchConsole::Why(a2b::NodeAddr node, a2b::Result result, bool registers) const {
  if (result == a2b::Result::kInvalidArgument && node != a2b::kMainNode &&
      static_cast<size_t>(node) >= m_Network.SubNodeCount()) {
    return NodeName(node) + " is not in the bus";
  }
  if (result == a2b::Result::kInvalidArgument && registers) {
    return "invalid argument (past the end of the register page, page 1 on a chip without "
           "it, or MMRPAGE)";
  }
  if (result == a2b::Result::kI2cError) {
    return "I2C error (no answer)";
  }
  return Name(result);
}

/**
 * `addCon S:G D:G`, `removeCon S:G D:G`: adds or removes a connection between nodes in the
 * bus, by position.
 * @param args The command line's words.
 */
void BenchConsole::AddConnection(const std::vector<std::string>& args) {
  const bool add = Same(args[0], "addCon");
  a2b::NodeAddr source = a2b::kMainNode;
  a2b::NodeAddr sink = a2b::kMainNode;
  uint8_t sourceGroup = 0;
  uint8_t sinkGroup = 0;
  if (args.size() != 3 || !ToEnd(args[1], source, sourceGroup) ||
      !ToEnd(args[2], sink, sinkGroup)) {
    Print("%s S:G D:G (S, D: main or 0-15; G: a group ID)\n", add ? "addCon" : "removeCon");
    return;
  }
  const a2b::Result result = add ? m_Network.AddConnection(source, sourceGroup, sink, sinkGroup)
                                 : m_Network.RemoveConnection(source, sourceGroup, sink, sinkGroup);
  if (result != a2b::Result::kOk) {
    Print(add ? "Can't connect: %s\n" : "Can't remove: %s\n",
          (result == a2b::Result::kInvalidArgument)
              ? (add ? "a node isn't in the bus, hasn't got the group (groups lists them), or "
                       "both are the same node"
                     : "no such connection, or a node isn't in the bus")
              : Name(result));
    return;
  }
  Print("%s %s G%u -> %s G%u (applyRouting routes it)\n", add ? "Connected" : "Removed",
        NodeName(source, false).c_str(), static_cast<unsigned>(sourceGroup),
        NodeName(sink, false).c_str(), static_cast<unsigned>(sinkGroup));
}

/** `listCon`: every connection, its ends by chip ID and where their nodes are now. */
void BenchConsole::ListConnections() {
  const size_t count = m_Network.ConnectionCount();
  if (count == 0) {
    Print("No connections (addCon adds one)\n");
    return;
  }
  for (size_t i = 0; i < count; ++i) {
    const a2b::Connection& connection = *m_Network.GetConnection(i);
    Print("%u: %s G%u -> %s G%u: %s\n", static_cast<unsigned>(i),
          EndName(connection.source).c_str(), static_cast<unsigned>(connection.source.group),
          EndName(connection.sink).c_str(), static_cast<unsigned>(connection.sink.group),
          ChannelsCarried(connection).c_str());
  }
}

/**
 * Says what a connection carries, as the routing pairs it: the source's RX group channels
 * with the sink's TX group channels, lowest with lowest; a channel without a partner is not
 * sent (or not played).
 * @return "2 channels, 0-1 -> 0-1", with what is left over; or why nothing is paired.
 */
std::string BenchConsole::ChannelsCarried(const a2b::Connection& connection) const {
  const a2b::NodeAddr source = m_Network.ConnectionNode(connection.source);
  const a2b::NodeAddr sink = m_Network.ConnectionNode(connection.sink);
  if (source == a2b::kNoNode || sink == a2b::kNoNode) {
    return "waiting for a node that isn't in the bus";
  }
  const auto groupsOf = [this](a2b::NodeAddr node) -> const a2b::ChannelGroups& {
    return (node == a2b::kMainNode) ? m_Board.routing.groups
                                    : m_Network.SubNode(node)->moduleInfo.groups;
  };
  const a2b::ChannelGroup* rx = groupsOf(source).FindRx(connection.source.group);
  const a2b::ChannelGroup* tx = groupsOf(sink).FindTx(connection.sink.group);
  if (rx == nullptr || tx == nullptr) {
    return "no such group (groups lists them)";
  }
  uint32_t from = rx->channels;
  uint32_t to = tx->channels;
  uint32_t sent = 0;
  uint32_t played = 0;
  unsigned pairs = 0;
  while (from != 0 && to != 0) {
    sent |= from & (~from + 1);  // The lowest channel left of each.
    played |= to & (~to + 1);
    from &= from - 1;
    to &= to - 1;
    ++pairs;
  }
  if (pairs == 0) {
    return "nothing: a group has no channels";
  }
  const char* shape = (pairs == 1)   ? " channel (mono), "
                      : (pairs == 2) ? " channels (stereo), "
                                     : " channels, ";
  std::string text =
      std::to_string(pairs) + shape + ChannelList(sent) + " -> " + ChannelList(played);
  if ((rx->channels & ~sent) != 0) {
    text += "; source channel(s) " + ChannelList(rx->channels & ~sent) + " not sent";
  }
  if ((tx->channels & ~played) != 0) {
    text += "; sink channel(s) " + ChannelList(tx->channels & ~played) + " not played";
  }
  return text;
}

/** `applyRouting`: routes the connections; the result is printed when it is written. */
void BenchConsole::ApplyRouting() {
  const a2b::RoutingError error = m_Network.ApplyRouting();
  if (error != a2b::RoutingError::kNone) {
    Print("Can't route: %s\n", Name(error));
    return;
  }
  Print(Running() ? "Routing: written at the next poll, once the chain is up\n"
                  : "Routing: written once the bus is started and its chain is up\n");
}

/**
 * `setRoute [append] S:G D:G...`: one audio stream, an RX group of a node to a TX group of
 * each of the nodes after it, in the bus; replaces every connection unless `append`.
 * @param args The command line's words.
 */
void BenchConsole::SetRoute(const std::vector<std::string>& args) {
  const bool append = args.size() > 1 && Same(args[1], "append");
  const size_t first = append ? 2 : 1;
  a2b::AudioStream stream;
  bool valid = args.size() >= first + 2 && args.size() - first - 1 <= a2b::AudioStream::kMaxSinks &&
               ToEnd(args[first], stream.source, stream.sourceGroup);
  for (size_t i = first + 1; valid && i < args.size(); ++i) {
    a2b::AudioStream::Sink& sink = stream.sinks[stream.sinkCount++];
    valid = ToEnd(args[i], sink.node, sink.group);
  }
  if (!valid) {
    Print("setRoute [append] S:G D:G... (S, D: main or 0-15; G: a group ID; up to %u sinks)\n",
          static_cast<unsigned>(a2b::AudioStream::kMaxSinks));
    return;
  }
  const a2b::Result result = m_Network.SetAudioRoutes(&stream, 1, append);
  if (result != a2b::Result::kOk) {
    Print("Can't set the route: %s\n",
          (result == a2b::Result::kNoSpace)
              ? "too many connections"
              : "a node isn't in the bus, hasn't got the group (groups lists them), or a sink is "
                "the source");
    return;
  }
  Print("%u connection(s) kept (applyRouting routes them)\n",
        static_cast<unsigned>(m_Network.ConnectionCount()));
}

/**
 * `importCon S:G D:G [S:G D:G...]`: replaces every connection with the pairs given, by chip
 * ID.
 * @param args The command line's words.
 */
void BenchConsole::ImportConnections(const std::vector<std::string>& args) {
  std::vector<a2b::Connection> list;
  bool valid = args.size() % 2 == 1;
  for (size_t i = 1; valid && i + 1 < args.size(); i += 2) {
    list.emplace_back();
    valid = ToChipEnd(args[i], list.back().source) && ToChipEnd(args[i + 1], list.back().sink);
  }
  if (!valid) {
    Print(
        "importCon S:G D:G [S:G D:G...] (S, D: main, or a chip ID of 12 hex digits; G: a group "
        "ID)\n");
    return;
  }
  const a2b::Result result = m_Network.ImportConnections(list.data(), list.size());
  if (result != a2b::Result::kOk) {
    Print("Can't import: %s\n", (result == a2b::Result::kNoSpace)
                                    ? "too many connections"
                                    : "a connection joins a node to itself");
    return;
  }
  Print("%u connection(s) kept (applyRouting routes them)\n",
        static_cast<unsigned>(m_Network.ConnectionCount()));
}

/**
 * `mute [on|off]`: mutes or unmutes the whole bus; says which it is without an argument.
 * @param args The command line's words.
 */
void BenchConsole::MuteBus(const std::vector<std::string>& args) {
  if (args.size() == 1) {
    Print("The bus is %s\n",
          m_Network.NetworkMuted() ? "muted (mute off unmutes it)" : "not muted");
    return;
  }
  const bool on = args.size() == 2 && (Same(args[1], "on") || args[1] == "1");
  if (args.size() != 2 || !(on || Same(args[1], "off") || args[1] == "0")) {
    Print("mute [on|off]\n");
    return;
  }
  m_Network.MuteNetwork(on);
  Print("Bus %s at the next poll%s\n", on ? "muted" : "unmuted",
        Running() ? "" : " (it is written once the bus is started)");
}

/**
 * `muteMode [local|global|none]`: what the stack mutes while it writes a routing plan; shows
 * the mode without an argument.
 * @param args The command line's words.
 */
void BenchConsole::MuteMode(const std::vector<std::string>& args) {
  if (args.size() == 1) {
    Print("Routing mute: %s\n", Name(m_MuteMode));
    return;
  }
  if (args.size() != 2) {
    Print("muteMode [local|global|none]\n");
    return;
  }
  if (Same(args[1], "local")) {
    m_MuteMode = a2b::RoutingMute::kLocal;
  } else if (Same(args[1], "global")) {
    m_MuteMode = a2b::RoutingMute::kGlobal;
  } else if (Same(args[1], "none")) {
    m_MuteMode = a2b::RoutingMute::kNone;
  } else {
    Print("muteMode [local|global|none]\n");
    return;
  }
  m_Board.routing.mute = m_MuteMode;  // The bus reads the board at each plan.
  Print("Routing mute: %s (from the next plan)\n", Name(m_MuteMode));
}

/** `bandwidth`: how full the bus is with the plan built last. */
void BenchConsole::ShowBandwidth() {
  const a2b::BandwidthUsage usage = m_Network.Bandwidth();
  if (m_Network.SubNodeCount() == 0 || usage.bits == 0) {
    Print("No plan built since the bus started (applyRouting)\n");
    return;
  }
  Print("Busiest: sub-node %d, %u of the superframe's 1024 bits (%u%%)\n", usage.node,
        static_cast<unsigned>(usage.bits), static_cast<unsigned>(usage.percent));
}

/** Prints what the stack mutes for a plan, after the app was told to mute. */
void BenchConsole::OnAudioMute(a2b::Network& network, const a2b::MuteScope& scope) {
  std::string what = scope.network ? "the whole bus" : "";
  if (scope.network && m_Board.routing.mute == a2b::RoutingMute::kLocal &&
      !network.NetworkMuted()) {
    what += " (local: a node that would play a wrong channel can't be muted alone)";
  }
  for (int node = a2b::kMainNode; !scope.network && node < a2b::kMaxSubNodes; ++node) {
    if (scope.Includes(static_cast<a2b::NodeAddr>(node))) {
      what += (what.empty() ? "" : ", ") + NodeName(static_cast<a2b::NodeAddr>(node), false);
    }
  }
  Print("audio mute: %s\n", what.empty() ? "nothing muted by the stack" : what.c_str());
}

/** Prints that the plan is written and the stack has unmuted. */
void BenchConsole::OnAudioUnmute(a2b::Network& network, const a2b::MuteScope& scope) {
  (void)network;
  (void)scope;
  Print("audio unmute\n");
}

/** `routing`: the plan written last, node by node. */
void BenchConsole::ShowRouting() {
  const a2b::RoutingPlan& plan = m_Network.Routing();
  if (!plan.valid) {
    Print("No routing written since the bus started (applyRouting)\n");
    return;
  }
  Print("%u connection(s) routed; main node RESPCYCS 0x%02X (window %u-%u)\n",
        static_cast<unsigned>(plan.routed), plan.main.respCycles,
        static_cast<unsigned>(plan.respCycles.minMain),
        static_cast<unsigned>(plan.respCycles.maxMain));
  if (plan.subNodeCount > 0) {
    Print("busiest: sub-node %d, %u of the superframe's 1024 bits (%u%%)\n", plan.bandwidth.node,
          static_cast<unsigned>(plan.bandwidth.bits),
          static_cast<unsigned>(plan.bandwidth.percent));
  }
  for (size_t i = 0; i <= plan.subNodeCount; ++i) {
    const bool main = (i == 0);
    const a2b::NodeRouting& regs = main ? plan.main : plan.sub[i - 1];
    if (main) {
      Print("main node: DNSLOTS %u UPSLOTS %u RESPCYCS 0x%02X\n", regs.dnSlots, regs.upSlots,
            regs.respCycles);
    } else {
      Print(
          "sub-node %u: link in %u down / %u up; DNSLOTS %u UPSLOTS %u LDNSLOTS 0x%02X LUPSLOTS %u "
          "DNOFFSET %u UPOFFSET %u DNMASK 0x%08X UPMASK 0x%08X RESPCYCS 0x%02X\n",
          static_cast<unsigned>(i - 1), plan.linkDnSlots[i - 1], plan.linkUpSlots[i - 1],
          regs.dnSlots, regs.upSlots, regs.ldnSlots, regs.lupSlots, regs.dnOffset, regs.upOffset,
          static_cast<unsigned>(regs.dnMask), static_cast<unsigned>(regs.upMask), regs.respCycles);
    }
    a2b::PortSettings port;
    const a2b::SubNodeInfo* node =
        main ? nullptr : m_Network.SubNode(static_cast<a2b::NodeAddr>(i - 1));
    port.productId = main ? m_Board.mainProduct : (node != nullptr ? node->identity.productId : 0);
    if (!a2b::ComputeChannelMap(port).crossbar) {
      Print(
          "  no crossbar (AD24%02X): RXMASK and TXXBAR not written; its port's channels in "
          "order, TX channel n plays location n\n",
          port.productId);
      continue;
    }
    std::string played;
    for (unsigned c = 0; c < 32; ++c) {
      if (regs.txXbar[c] != 31) {
        played += (played.empty() ? "" : ", ") + std::to_string(c) + "<-" +
                  std::to_string(regs.txXbar[c]);
      }
    }
    Print("  RXMASK %s; TXXBAR channel<-location %s (others 31)\n",
          Hex(regs.rxMask, sizeof(regs.rxMask)).c_str(), played.empty() ? "none" : played.c_str());
  }
}

/** `groups`: the main node's channel groups, the console's image's, and each sub-node's. */
void BenchConsole::ShowGroups() {
  Print("main node (the console's, used at start): %s\n", GroupsLine(m_MainGroups).c_str());
  Print("console's module info (ignoreEEPROM): %s\n", GroupsLine(m_Plain.groups).c_str());
  for (size_t i = 0; i < m_Network.SubNodeCount(); ++i) {
    Print("sub-node %u: %s\n", static_cast<unsigned>(i),
          GroupsLine(m_Network.SubNode(static_cast<a2b::NodeAddr>(i))->moduleInfo.groups).c_str());
  }
}

/**
 * `group main|image rx|tx ID CHANNELS`: sets a channel group of the main node (it applies
 * at the next `start`) or of the console's module info (to nodes identified from then on).
 * @param args The command line's words.
 */
void BenchConsole::SetGroup(const std::vector<std::string>& args) {
  long id = 0;
  uint32_t channels = 0;
  const bool main = args.size() == 5 && Same(args[1], "main");
  if (args.size() != 5 || !(main || Same(args[1], "image")) ||
      !(Same(args[2], "rx") || Same(args[2], "tx")) || !ToNumber(args[3], 255, id) ||
      !ToChannels(args[4], channels)) {
    Print("group main|image rx|tx ID CHANNELS (ID: 0-255; CHANNELS: 0-1, 0,2,4-7 or none)\n");
    return;
  }
  const bool rx = Same(args[2], "rx");
  a2b::ChannelGroups& groups = main ? m_MainGroups : m_Plain.groups;
  a2b::ChannelGroups kept;  // The groups without this one; it is added back unless none.
  for (size_t i = 0; i < groups.rxCount; ++i) {
    if (!rx || groups.rx[i].id != id) {
      (void)kept.AddRx(groups.rx[i].id, groups.rx[i].channels);
    }
  }
  for (size_t i = 0; i < groups.txCount; ++i) {
    if (rx || groups.tx[i].id != id) {
      (void)kept.AddTx(groups.tx[i].id, groups.tx[i].channels);
    }
  }
  const uint8_t groupId = static_cast<uint8_t>(id);
  const a2b::Result result = (channels == 0) ? a2b::Result::kOk
                             : rx            ? kept.AddRx(groupId, channels)
                                             : kept.AddTx(groupId, channels);
  if (result != a2b::Result::kOk) {
    Print("Can't add the group: %u groups each way at most\n",
          static_cast<unsigned>(a2b::ChannelGroups::kMaxGroups));
    return;
  }
  groups = kept;
  Print("%s: %s%s\n", main ? "main node" : "console's module info", GroupsLine(groups).c_str(),
        main ? " (at the next start)" : " (for nodes identified from now on)");
}

/**
 * Names a connection end: the main node, or a sub-node by chip ID with its position, or
 * "not in the bus".
 */
std::string BenchConsole::EndName(const a2b::ConnectionEnd& end) const {
  if (end.mainNode) {
    return "main";
  }
  const a2b::NodeAddr node = m_Network.ConnectionNode(end);
  const std::string where =
      (node == a2b::kNoNode) ? "not in the bus" : "sub-node " + std::to_string(node);
  char id[16];
  std::snprintf(id, sizeof(id), "%02X%02X%02X%02X%02X%02X", end.chipId[0], end.chipId[1],
                end.chipId[2], end.chipId[3], end.chipId[4], end.chipId[5]);
  return std::string("chip ") + id + " (" + where + ")";
}

/**
 * Says, once for each last sub-node until the chain changes, why it reports an open line
 * (interrupt type 12 or 13) when a probe closes its switch toward the next position: either
 * nothing is plugged in there, or the node there draws no bias current (its own supply
 * behind an isolated A side, as an AB0020 with an AD2437 behind an AD242x), so the switch
 * never finishes closing (SWSTAT.FIN) and passes it no frames; `link N custom` bypasses
 * the switch for such a node. Nothing is said if that link already isn't powered over the
 * A2B pair.
 * @param node The last sub-node.
 * @param type The interrupt type it reported.
 */
void BenchConsole::HintOpenLine(a2b::NodeAddr node, uint8_t type) {
  const size_t next = static_cast<size_t>(node) + 1;
  if (m_OpenLineHinted == node || next >= static_cast<size_t>(a2b::kMaxSubNodes) ||
      (m_LinkSet[next] && m_Links[next].power != a2b::LinkProfile::Power::kOnA2bPair)) {
    return;
  }
  m_OpenLineHinted = node;
  Print(
      "  hint: sub-node %u reports an open line (interrupt type %u) toward position %u: nothing\n"
      "        is plugged in there, or the node there draws no bias current (its own supply\n"
      "        behind an isolated A side); for that, link %u custom\n",
      static_cast<unsigned>(node), static_cast<unsigned>(type), static_cast<unsigned>(next),
      static_cast<unsigned>(next));
}

/** Prints one sub-node's chip, chip ID, module and link, and the board whose image it got. */
void BenchConsole::ReportNode(a2b::NodeAddr position) {
  const a2b::SubNodeInfo* node = m_Network.SubNode(position);
  if (node == nullptr) {
    return;
  }
  const uint8_t* id = node->identity.chipId;
  const a2b::ModuleInfo& module = node->moduleInfo;
  Print(
      "  sub-node %d: AD24%02X version 0x%02X, chip ID %02X%02X%02X%02X%02X%02X, module "
      "%04X:%04X, CFG-%u, %u peripheral block(s), link %s\n",
      position, node->identity.productId, node->identity.version, id[0], id[1], id[2], id[3], id[4],
      id[5], module.vendorId, module.productId, static_cast<unsigned>(module.powerConfig),
      static_cast<unsigned>(module.peripheralBlocks), LinkName(position).c_str());
  Print("    %s\n", GroupsLine(module.groups).c_str());
  const clockworks::ModuleProfile* board = m_Modules.Board(position);
  if (board != nullptr) {
    if (!clockworks::Supports(*board, node->identity.productId)) {
      Print("    set as %s, which isn't made with an AD24%02X: not given its module info\n",
            board->name, node->identity.productId);
    } else if (board->codecName != nullptr) {
      Print("    %s: %s programmed from %s (%u writes)\n", board->name, board->codecName,
            m_ModuleFiles[position].c_str(), static_cast<unsigned>(m_ModuleWrites[position]));
    } else {
      Print("    %s\n", board->name);
    }
  }
  const clockworks::ModuleProfile* used = m_Modules.Using(position);
  if (used != nullptr && used != board &&
      used == clockworks::NoEepromDefault(node->identity.productId)) {
    Print("    %s (%s: the AD24%02X's default)\n", used->name,
          m_IgnoreEeprom ? "EEPROMs ignored" : "no EEPROM", node->identity.productId);
  }
}

/** The link into sub-node @p position: set with `link`, else the board's. */
std::string BenchConsole::LinkName(a2b::NodeAddr position) const {
  if (m_LinkSet[position]) {
    return ProfileName(m_Links[position]);
  }
  return ProfileName(m_Board.defaultLink) + " (board)";
}

/** True while the bus is started: not stopped, not failed. */
bool BenchConsole::Running() const {
  const a2b::NetworkState state = m_Network.State();
  return state != a2b::NetworkState::kStopped && state != a2b::NetworkState::kFailed;
}
