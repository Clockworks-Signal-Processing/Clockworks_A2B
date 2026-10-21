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
 * console's (`group main`), as an export has none. The `eeprom` commands read, write,
 * verify and erase a sub-node's module-info EEPROM with an EepromJob, a transfer per poll
 * (Busy() while it runs), and `module N file` gives a node an image from a file instead.
 * The commands that need files are in bench_console_files.cpp. With A2B_BENCH_FILES 0 (the
 * RP2040 console) they are left out: typed, each says it is not available.
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

#include "a2b/strings.h"
#include "console_text.h"
#include "eeprom_image.h"
#include "ss_plus_import.h"

using bench::Hex;
using bench::Same;
using bench::Split;
using bench::ToNumber;

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

#if !A2B_BENCH_FILES
/// What a file command prints, after its name.
constexpr const char* kNoFiles = "not available on the RP2040 (no file system)";
#else
constexpr size_t kMaxWarningsShown = 10;  ///< A loaded export's warnings printed in full.
#endif

/** INTTYPE PWRERR, cable disconnected, open circuit or wrong port (AD242x TRM, Table 5-1). */
constexpr uint8_t kIntOpenLine = 12;
/** INTTYPE PWRERR, cable reverse connected or wrong port; an open line at a low VIN. */
constexpr uint8_t kIntOpenLineLowVin = 13;

constexpr long kLastRegister = 0x1FF;  ///< The last register of page 1 (AD243x).
constexpr long kMaxCount = 256;        ///< Registers or peripheral bytes one command reads.
constexpr size_t kBytesPerLine = 16;   ///< Values printed on one line.
/** `link`'s usage line. */
constexpr const char* kLinkUsage =
    "link N utp [two-step|vmtr]|rj45 [single]|xlr|custom [inverted]|default (N: 0-15)";

/**
 * The link profile's name, as `link` takes it. Powered over the A2B pair: rj45 (two-step,
 * B side inverted: XCVRBINV, which only RJ45 links have by default), rj45 single (the same,
 * single-step, as an export's inverted link), xlr (two-step, straight, with the power-good
 * and 24 V checks), utp (single-step, straight), utp two-step and utp vmtr (single-step with
 * the 24 V check). On its own power: custom, inverted if its cable is RJ45. A two-step link
 * runs single-step from a node without the 24 V switch (the stack logs it). A link from a B
 * port the stack can't run says so, as the stack doesn't probe past it.
 */
std::string ProfileName(const a2b::LinkProfile& link) {
  if (link.discovery == a2b::LinkProfile::Discovery::kUnsupported) {
    return "unsupported B port type (not probed)";
  }
  if (link.power != a2b::LinkProfile::Power::kOnA2bPair) {
    return link.bSideInverted ? "custom inverted" : "custom";
  }
  const bool twoStep = (link.discovery == a2b::LinkProfile::Discovery::kTwoStep);
  if (link.bSideInverted) {
    return twoStep ? "rj45" : "rj45 single";
  }
  if (twoStep) {
    return link.checkPowerGood ? "xlr" : "utp two-step";
  }
  return link.checkHighVoltage ? "utp vmtr" : "utp";
}

/**
 * Reads a cable length in metres with at most one decimal ("4", "2.5", "0.3") into
 * decimetres, 0.1 to 6553.5 m; false if @p text isn't one.
 */
bool ToDecimetres(const std::string& text, uint16_t& decimetres) {
  const size_t dot = text.find('.');
  const std::string whole = text.substr(0, dot);
  const std::string tenths = (dot == std::string::npos) ? "0" : text.substr(dot + 1);
  if (whole.empty() || tenths.size() != 1 ||
      whole.find_first_not_of("0123456789") != std::string::npos ||
      tenths.find_first_not_of("0123456789") != std::string::npos || whole.size() > 4) {
    return false;
  }
  const long value = std::strtol(whole.c_str(), nullptr, 10) * 10 + (tenths[0] - '0');
  if (value < 1 || value > 0xFFFF) {
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
 * The codec program of `module N BOARD [FILE]`: FILE's (SigmaStudio's XML), or the board's.
 * @param board The board.
 * @param args The command line's words; a fourth is FILE.
 * @param program The program read.
 * @param error Why it can't be read.
 * @return True with @p program read.
 */
bool CodecProgram(const clockworks::ModuleProfile& board, const std::vector<std::string>& args,
                  SigmaProgram& program, std::string& error) {
#if A2B_BENCH_FILES
  if (args.size() == 4) {
    return ReadSigmaXml(args[3], board.codecAddr, program, error);
  }
#else
  (void)args;
#endif
  return clockworks::DefaultProgram(board, program, error);
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
  clockworks::UseBoardNodes(m_Factory);       // IO7 an input but on an AB0020.
  m_Network.SetNodeFactory(&m_Factory);
  m_Network.SetErrorWatch(true);  // `errors off` turns it off.
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
      {"AB0020",
       "AB0020: AD2437 or AD2428 main node (the export for the chip found at start), I2S "
       "TDM2, UTP links, ADAU1761 codec programmed at start "
       "(tools/SS+reference/2x_AB0020_EVM_with_2437, _with_2428)",
       {{0x37, &a2b_bench_board_ab0020}, {0x28, &a2b_bench_board_ab0020_2428}},
       true},
      // The AB0310 makes SYNC and BCLK itself and has no peripheral to program; its links
      // are RJ45, as its export's XCVRBINV says.
      {"AB0310",
       "AB0310: AD2437 main node, I2S TDM2, RJ45 links, SYNC and BCLK from the board, no "
       "peripherals (tools/SS+reference/AB0310_AB0312_EVM_with_2437)",
       {{0x37, &a2b_bench_board_ab0310}},
       false},
  };
  return kBoards;
}

/**
 * Tells whether a host should run @p line at once while an `eeprom` job runs, ahead of the
 * lines queued before it: `eeprom abort`, `quit` and `exit`. Other lines wait for the job.
 * @param line A command line.
 * @return True for those commands.
 */
bool BenchConsole::RunsWhileBusy(const char* line) {
  const std::vector<std::string> args = Split(line);
  if (args.size() == 1) {
    return Same(args[0], "quit") || Same(args[0], "exit");
  }
  return args.size() == 2 && Same(args[0], "eeprom") && Same(args[1], "abort");
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
#if A2B_BENCH_FILES
  if (Busy() && (Same(command, "start") || Same(command, "stop") || Same(command, "board") ||
                 Same(command, "reject") ||
                 (Same(command, "eeprom") && args.size() > 1 && !Same(args[1], "abort")))) {
    Print("An EEPROM job is running on sub-node %u (eeprom abort stops it)\n",
          static_cast<unsigned>(m_EepromJob.Node()));
    return true;
  }
#endif
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
#if !A2B_BENCH_FILES
  } else if (Same(command, "eeprom") || Same(command, "i2cTrace")) {
    Print("%s: %s\n", command.c_str(), kNoFiles);
#else
  } else if (Same(command, "eeprom")) {
    Eeprom(args);
  } else if (Same(command, "i2cTrace")) {
    Trace(args);
#endif
  } else if (Same(command, "interrupts")) {
    Interrupts(args);
  } else if (Same(command, "errors")) {
    Errors(args);
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
#if !A2B_BENCH_FILES
    m_Network.Stop();
#else
    m_EepromJob.Abort();
    m_Network.Stop();
    m_Trace.Stop();
#endif
    return false;
  } else {
    Print("Unknown command '%s' (help lists them)\n", command.c_str());
  }
  return true;
}

/**
 * Polls the bus once at the host's time (the events are printed at the end of it), then
 * runs an `eeprom` job on by a transfer, and reports it when it ends.
 */
void BenchConsole::Poll() {
  m_Network.Poll(m_Trace.NowMs());
#if A2B_BENCH_FILES
  if (m_EepromJob.Running() &&
      m_EepromJob.Poll(m_Network, m_Trace.NowMs()) != EepromJob::State::kRunning) {
    EepromFinished();
  }
#endif
}

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
      Print("bus: %s\n", a2b::ToString(event.state));
      if (event.state == a2b::NetworkState::kFailed) {
        Print("  error: %s\n", a2b::ToString(m_Network.Error()));
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
      Print("  sub-node %u rejected: %s\n", node, a2b::ToString(event.rejection));
      return;
    case a2b::EventType::kNodeRemoved:
      m_OpenLineHinted = a2b::kNoNode;
      Print("  sub-node %u removed: %s\n", node, a2b::ToString(event.removal));
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
        Print("  interrupt from the main node: type %u (%s)\n",
              static_cast<unsigned>(event.interruptType), a2b::InterruptName(event.interruptType));
      } else {
        Print("  interrupt from sub-node %u: type %u (%s)\n", node,
              static_cast<unsigned>(event.interruptType), a2b::InterruptName(event.interruptType));
      }
      if (openLineAtEnd) {
        HintOpenLine(event.node, event.interruptType);
      }
      return;
    }
    case a2b::EventType::kErrorReport:
      PrintErrorReport();
      return;
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
      Print("routing failed: %s%s\n", a2b::ToString(event.routingError), where.c_str());
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
#if !A2B_BENCH_FILES
      "                         CHAIN: the export's chain (default 0)\n"
#else
      "board FILE [CHAIN]       load a SigmaStudio+ export (adi_a2b_busconfig.c); quote a\n"
      "                         path with spaces. CHAIN: the export's chain (default 0)\n"
#endif
      "start                    import the board and start the bus (again if running)\n"
      "stop                     stop the bus (nothing more is sent)\n"
#if !A2B_BENCH_FILES
      "status                   the bus, its sub-nodes, links and module info\n"
#else
      "status                   the bus, its sub-nodes, links, module info and trace\n"
#endif
      "link                     list the links set\n"
      "link N utp|rj45|xlr      the link into sub-node N (0-15), powered over the A2B pair.\n"
      "                         rj45 inverts the B side (XCVRBINV on the node before it).\n"
      "                         rj45 and xlr are two-step from an AD2437: found at 5 V, then\n"
      "                         the 24 V switched on and checked; single-step from others\n"
      "link N rj45 single       the RJ45 link single-step (as an export's inverted link)\n"
      "link N utp two-step      a UTP link run two-step; utp vmtr: single-step, 24 V checked\n"
      "link N custom            the node on its own power; custom inverted: an RJ45 cable\n"
      "link N default           back to the link the node before it gives (its image's B\n"
      "                         port; the board's into sub-node 0); all: link all default\n"
      "link all board           every link the board's, whatever the images' B ports say\n"
      "cable                    list the cable lengths set\n"
      "cable N METRES           the cable into sub-node N (0-15) for the response cycles:\n"
      "                         from 0.1, e.g. 2.5 (the real length: a longer one can stop\n"
      "                         discovery), in place of the image's of the node before it\n"
      "                         and the board's (4 m). Re-routes a routed bus if it changes\n"
      "cable N default          back to the image's, else the board's (all: cable all default)\n"
      "module                   list the boards set at sub-node positions (or the boards)\n"
#if !A2B_BENCH_FILES
      "module N BOARD           sub-node N (0-15, or all) is a Clockworks BOARD (AB0020,\n"
      "                         AB0312): its settings, groups and the board's codec\n"
      "                         program, even with the EEPROMs read\n"
#else
      "module N BOARD [FILE]    sub-node N (0-15, or all) is a Clockworks BOARD (AB0020,\n"
      "                         AB0312): its settings, groups and codec program, the\n"
      "                         board's or FILE's (SigmaStudio's XML), even with the\n"
      "                         EEPROMs read\n"
      "module N file FILE       sub-node N's module info from an image file (.dat, the\n"
      "                         image tool's), even with the EEPROMs read: try an image\n"
      "                         before writing it\n"
#endif
      "module N default         back to the EEPROM or ignoreEEPROM's image (or all)\n"
      "ignoreEEPROM 1|0         give sub-nodes the console's module info / read EEPROMs\n"
#if A2B_BENCH_FILES
      "eeprom                   the EEPROM settings, and the job running\n"
      "eeprom config [addr A] [size BYTES] [page BYTES] [cycle MS] [chunk BYTES]\n"
      "                         the sub-nodes' EEPROM: 7-bit address (default: where the\n"
      "                         image was found, else 0x50; addr found goes back to that),\n"
      "                         size (32768), write page (64), write cycle wait (6 ms),\n"
      "                         bytes per read (256)\n"
      "eeprom read N FILE [LEN|all]\n"
      "                         read sub-node N's EEPROM into FILE: to the end of its image\n"
      "                         (all of it if there's none), LEN bytes, or all of it\n"
      "eeprom write N FILE      write an image (.dat) to sub-node N's EEPROM: checked first,\n"
      "                         pages that differ, read back, the marker 0xAA last\n"
      "eeprom verify N FILE     compare sub-node N's EEPROM with an image\n"
      "eeprom info N            read sub-node N's image and say what it holds\n"
      "eeprom erase N           clear sub-node N's marker: no module info there\n"
      "eeprom abort             stop the job (a write stopped leaves the node blank)\n"
      "i2cTrace on FILE         write every I2C transfer to FILE (I2CT lines)\n"
      "i2cTrace off|status      stop tracing / show the trace\n"
#endif
      "interrupts quiet|all     hide (the default) or show the open-line interrupts (types\n"
      "                         12, 13) of the last sub-node, which each probe behind it\n"
      "                         gives when nothing is plugged in there\n"
      "errors                   each node's bit errors (summed), latched errors, switch\n"
      "                         status and NODE.LAST, read now\n"
      "errors on|off            the error watch (on from the start): bit-error counting,\n"
      "                         the SRF-missed and BECOVF interrupts, and a report of what\n"
      "                         the nodes held when a probe fails or before a drop\n"
      "errors clear             zero the sums and clear the latched errors\n"
      "errors report            print the last report again\n"
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
#if !A2B_BENCH_FILES
      "quit                     stop the bus (the console keeps running)\n");
#else
      "quit                     stop the bus and leave\n");
#endif
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
#if !A2B_BENCH_FILES
    Print("Board: %s", m_Bcd != nullptr ? m_BoardName.c_str() : "none (board NAME)");
#else
    Print("Board: %s", m_Bcd != nullptr ? m_BoardName.c_str() : "none (board NAME or FILE)");
#endif
    Print(m_Bcd != nullptr ? ", chain %u\n" : "\n", static_cast<unsigned>(m_Chain));
    return;
  }
  long chain = 0;
  if (args.size() > 2 && !ToNumber(args[2], 255, chain)) {
    Print("board: CHAIN is a number\n");
    return;
  }
#if !A2B_BENCH_FILES
  bool builtIn = false;
  for (const BuiltInBoard& board : BuiltInBoards()) {
    builtIn = builtIn || Same(args[1], board.name);
  }
  if (!builtIn) {
    Print("board %s: not a built-in board (board lists them); board FILE is %s\n", args[1].c_str(),
          kNoFiles);
    return;
  }
#endif
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
#if A2B_BENCH_FILES
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
#endif
}

/**
 * `start`: imports the board into a fresh BoardConfig and starts the bus; a bus already
 * started is stopped and started again. The links set with `link` stay set. A built-in
 * board with an export for more than one main-node chip (AB0020) first reads the main
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
  ApplyBoardLinks();
  Print(
      "Main node AD24%02X at 0x%02X (sub-nodes 0x%02X), %u host script operation(s) (%u "
      "before the main node), links %s by default\n",
      m_Board.mainProduct, m_Board.mainAddr, m_Board.busAddr,
      static_cast<unsigned>(m_HostScript.Size()),
      static_cast<unsigned>(m_Board.hostOpsBeforeMainNode),
      ProfileName(m_Board.defaultLink).c_str());
  const a2b::Result started = m_Network.Start();
  if (started != a2b::Result::kOk) {
    Print("Can't start: %s\n", a2b::ToString(started));
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
          a2b::ToString(imported));
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
  Print("Bus: %s", a2b::ToString(m_Network.State()));
  if (m_Network.State() == a2b::NetworkState::kFailed) {
    Print(" (%s)", a2b::ToString(m_Network.Error()));
  }
  if (m_Network.State() != a2b::NetworkState::kStopped) {
    Print(", main node AD24%02X at 0x%02X", m_Board.mainProduct, m_Board.mainAddr);
  }
  Print(", %u sub-node(s)%s\n", static_cast<unsigned>(m_Network.SubNodeCount()),
        m_Network.DiscoveryPaused() ? ", discovery paused" : "");
  for (size_t i = 0; i < m_Network.SubNodeCount(); ++i) {
    ReportNode(static_cast<a2b::NodeAddr>(i));
  }
  Print("Last rejection: %s\n", a2b::ToString(m_Network.LastRejection()));
  Print("Links: %s by default%s", ProfileName(m_Board.defaultLink).c_str(),
        m_AllLinksBoard ? " (every link)" : " (into sub-node 0; the others from the images)");
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
    const bool image = m_Modules.HasImage(static_cast<a2b::NodeAddr>(n));
    if (board != nullptr || image) {
      Print("%s%d %s", anyModule ? ", " : "\nBoards: sub-node ", n,
            image ? ("image " + m_ImageFiles[n]).c_str() : board->name);
      anyModule = true;
    }
  }
  Print("\nMute: bus %s, routing mute %s\n", m_Network.NetworkMuted() ? "muted" : "not muted",
        a2b::ToString(m_MuteMode));
  Print("Module info: %s\n", m_IgnoreEeprom ? "the console's (EEPROMs ignored)" : "the EEPROMs");
#if A2B_BENCH_FILES
  if (Busy()) {
    Print("EEPROM: a job is running on sub-node %u\n", static_cast<unsigned>(m_EepromJob.Node()));
  }
#endif
  Print("Interrupts: %s, %u open-line interrupt(s) of the last sub-node hidden\n",
        m_ShowAllInterrupts ? "all shown" : "quiet", static_cast<unsigned>(m_HiddenInterrupts));
#if A2B_BENCH_FILES
  if (m_Trace.Tracing()) {
    Print("I2C trace: %s, %u transfer(s)\n", m_TraceFile.c_str(),
          static_cast<unsigned>(m_Trace.Count()));
  } else {
    Print("I2C trace: off\n");
  }
#endif
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
      Print("No links set: ");
    }
    if (m_AllLinksBoard) {
      Print("%severy other link is the board's (%s)\n", any ? "Then " : "",
            ProfileName(m_Board.defaultLink).c_str());
    } else if (!any) {
      Print(
          "into sub-node 0 the board's (%s); into the others, what the node before it "
          "gives (its image's B port)\n",
          ProfileName(m_Board.defaultLink).c_str());
    }
    return;
  }
  if (args.size() == 3 && Same(args[1], "all") && Same(args[2], "default")) {
    m_AllLinksBoard = false;
    for (int n = 0; n < a2b::kMaxSubNodes; ++n) {
      m_Network.ClearLinkProfile(static_cast<a2b::NodeAddr>(n));
      m_LinkSet[n] = false;
    }
    Print(
        "Every link: into sub-node 0 the board's; into the others, the image's B port of the "
        "node before it\n");
    return;
  }
  if (args.size() == 3 && Same(args[1], "all") && Same(args[2], "board")) {
    m_AllLinksBoard = true;
    if (Running()) {
      ApplyBoardLinks();
    }
    Print(
        "Every link not set with link N is the board's, whatever the images' B ports say "
        "(%s)\n",
        Running() ? ProfileName(m_Board.defaultLink).c_str() : "the export's, at start");
    return;
  }
  long position = 0;
  if (args.size() < 3 || !ToNumber(args[1], a2b::kMaxSubNodes - 1, position)) {
    Print("%s, link all default|board\n", kLinkUsage);
    return;
  }
  const a2b::NodeAddr node = static_cast<a2b::NodeAddr>(position);
  if (Same(args[2], "default") && args.size() == 3) {
    m_LinkSet[position] = false;
    if (m_AllLinksBoard) {
      (void)m_Network.SetLinkProfile(node, m_Board.defaultLink);
    } else {
      m_Network.ClearLinkProfile(node);
    }
    Print("Link into sub-node %ld: %s\n", position,
          (position == 0 || m_AllLinksBoard)
              ? "the board's"
              : "what the node before it gives (its image's B port)");
    return;
  }
  a2b::LinkProfile profile;
  bool inverted = false;
  const std::string option = (args.size() == 4) ? args[3] : std::string();
  if (Same(args[2], "utp") &&
      (args.size() == 3 || Same(option, "two-step") || Same(option, "vmtr"))) {
    profile = a2b::LinkProfile::Utp();
    if (Same(option, "two-step")) {
      profile.discovery = a2b::LinkProfile::Discovery::kTwoStep;
    }
    profile.checkHighVoltage = Same(option, "vmtr");
  } else if (Same(args[2], "rj45") && (args.size() == 3 || Same(option, "single"))) {
    profile = a2b::LinkProfile::Rj45();  // Two-step from an AD2437, B side inverted.
    if (Same(option, "single")) {
      profile.discovery = a2b::LinkProfile::Discovery::kSingleStep;
      profile.checkPowerGood = false;
      profile.checkHighVoltage = false;
    }
    inverted = true;
  } else if (Same(args[2], "xlr") && args.size() == 3) {
    profile = a2b::LinkProfile::Xlr();
  } else if (Same(args[2], "custom") && (args.size() == 3 || Same(option, "inverted"))) {
    profile = a2b::LinkProfile::Custom();
    inverted = (args.size() == 4);
  } else {
    Print("%s\n", kLinkUsage);
    return;
  }
  profile.bSideInverted = inverted;
  const a2b::Result result = m_Network.SetLinkProfile(node, profile);
  if (result != a2b::Result::kOk) {
    Print("Can't set the link: %s\n", a2b::ToString(result));
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
      "cable N METRES|default (N: 0-15; METRES: from 0.1, one decimal; or cable all default)\n";
  if (args.size() < 2) {
    bool any = false;
    for (int n = 0; n < a2b::kMaxSubNodes; ++n) {
      if (m_Cables[n] != 0) {
        Print("cable %d %s\n", n, Metres(m_Cables[n]).c_str());
        any = true;
      }
    }
    if (!any) {
      Print(
          "No cables set: each link's is the image's of the node before it, else the "
          "board's (%s)\n",
          Metres(m_Board.routing.cableDm).c_str());
    }
    return;
  }
  if (args.size() == 3 && Same(args[1], "all") && Same(args[2], "default")) {
    for (int n = 0; n < a2b::kMaxSubNodes; ++n) {
      (void)m_Network.ClearCableLength(static_cast<a2b::NodeAddr>(n));
      m_Cables[n] = 0;
    }
    Print("Every cable is the image's of the node before it, else the board's (%s)\n",
          Metres(m_Board.routing.cableDm).c_str());
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
    Print("Cable into sub-node %ld: the image's of the node before it, else the board's (%s)\n",
          position, Metres(m_Board.routing.cableDm).c_str());
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
#if !A2B_BENCH_FILES
  const std::string usage = "module N|all " + names + " or module N|all default (N: 0-15)\n";
#else
  const std::string usage = "module N|all " + names +
                            " [FILE], module N|all file IMAGE, or module N|all default (N: "
                            "0-15; FILE: the codec's SigmaStudio XML program, in place of the "
                            "board's; IMAGE: a module-info image, .dat)\n";
#endif
  if (args.size() == 1) {
    bool any = false;
    for (int n = 0; n < a2b::kMaxSubNodes; ++n) {
      const clockworks::ModuleProfile* board = m_Modules.Board(static_cast<a2b::NodeAddr>(n));
      if (m_Modules.HasImage(static_cast<a2b::NodeAddr>(n))) {
        Print("module %d file %s\n", n, m_ImageFiles[n].c_str());
        any = true;
      } else if (board != nullptr) {
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
      Print(
          "No boards or images set: sub-nodes get their EEPROM's or ignoreEEPROM's module "
          "info\n");
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
      m_ImageFiles[n].clear();
    }
    Print("%s: no board or image set (for nodes identified from now on)\n", who.c_str());
    return;
  }
#if !A2B_BENCH_FILES
  if (args.size() == 4) {
    Print("module N %s FILE: %s\n", args[2].c_str(), kNoFiles);
    return;
  }
#else
  if (args.size() == 4 && Same(args[2], "file")) {
    ModuleImage(first, last, args[3]);
    return;
  }
#endif
  const clockworks::ModuleProfile* board = clockworks::FindBoard(args[2]);
  if (board == nullptr || args.size() > 4 || (args.size() == 4 && board->codecName == nullptr)) {
    Print("%s", usage.c_str());
    return;
  }
  SigmaProgram program;
  std::string error;
  const bool fromFile = (args.size() == 4);
  if (!CodecProgram(*board, args, program, error)) {
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
    m_ImageFiles[n].clear();
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
 * `errors [on|off|clear|report]`: the error watch (a2b::Network::SetErrorWatch(), on from
 * the start). With no word, each node's error registers read now, with its bit errors
 * summed since the watch was turned on or cleared.
 * @param args The command line's words.
 */
void BenchConsole::Errors(const std::vector<std::string>& args) {
  if (args.size() == 2 && (Same(args[1], "on") || Same(args[1], "off"))) {
    m_Network.SetErrorWatch(Same(args[1], "on"));
  } else if (args.size() == 2 && Same(args[1], "clear")) {
    m_Network.ClearErrors();
    Print("Errors: cleared at the next poll\n");
    return;
  } else if (args.size() == 2 && Same(args[1], "report")) {
    if (m_Network.LastErrorReport().cause == a2b::ErrorReport::Cause::kNone) {
      Print("Errors: no report yet\n");
    } else {
      PrintErrorReport();
    }
    return;
  } else if (args.size() != 1) {
    Print("errors [on|off|clear|report]\n");
    return;
  }
  Print("Error watch: %s\n", m_Network.ErrorWatch() ? "on" : "off");
  if (args.size() != 1 || m_Network.State() == a2b::NetworkState::kStopped) {
    return;
  }
  for (int i = -1; i < static_cast<int>(m_Network.SubNodeCount()); ++i) {
    const a2b::NodeAddr node = static_cast<a2b::NodeAddr>(i);
    a2b::NodeErrorState state;
    (void)m_Network.ReadErrorState(node, state);
    PrintErrorState(node, state);
  }
}

/**
 * Prints one node's error registers: BECNT (and the sum the stack keeps), the latched
 * INTPND0 bits by name (the main node's INTPND2 too), SWSTAT and NODE.
 */
void BenchConsole::PrintErrorState(a2b::NodeAddr node, const a2b::NodeErrorState& state) {
  static const char* const kIntpnd0[] = {"header-count", "decoding", "CRC",        "parity",
                                         "PWRERR",       "BECOVF",   "SRF-missed", "SRF-CRC"};
  // SWSTAT.FAULT_CODE (both families' TRMs): 4 and 5 are the AD242x's, 6 the AD243x's.
  static const char* const kFault[] = {
      "no fault",  "short to GND", "short to VBUS",         "wires shorted",
      "open line", "reversed",     "open line or reversed", "undetermined"};
  if (!state.read) {
    Print("  %s: no answer\n", NodeName(node, false).c_str());
    return;
  }
  std::string text = "BECNT " + std::to_string(state.becnt) + " (sum " +
                     std::to_string(m_Network.BitErrorCount(node)) + ")";
  char hex[8];
  std::snprintf(hex, sizeof(hex), "0x%02X", state.intpnd[0]);
  text += std::string(", INTPND0 ") + hex;
  for (int bit = 7; bit >= 0; --bit) {
    if ((state.intpnd[0] & (1 << bit)) != 0) {
      text += std::string(" ") + kIntpnd0[bit];
    }
  }
  if (node == a2b::kMainNode) {
    std::snprintf(hex, sizeof(hex), "0x%02X", state.intpnd[2]);
    text += std::string(", INTPND2 ") + hex;
    text += ((state.intpnd[2] & 0x02) != 0) ? " I2CERR" : "";
    text += ((state.intpnd[2] & 0x04) != 0) ? " ICRCERR" : "";
  }
  std::snprintf(hex, sizeof(hex), "0x%02X", state.swstat);
  text += std::string(", SWSTAT ") + hex;
  text += ((state.swstat & 0x01) != 0) ? " FIN" : "";
  if ((state.swstat & 0x02) != 0) {
    text += std::string(" FAULT ") + kFault[(state.swstat >> 4) & 0x07];
  }
  if (node != a2b::kMainNode) {
    std::snprintf(hex, sizeof(hex), "0x%02X", state.node);
    text += std::string(", NODE ") + hex;
    text += ((state.node & 0x80) != 0) ? " LAST" : "";
  }
  Print("  %s: %s\n", NodeName(node, false).c_str(), text.c_str());
}

/** Prints the error watch's last report: the failed access, then each node's registers. */
void BenchConsole::PrintErrorReport() {
  const a2b::ErrorReport& report = m_Network.LastErrorReport();
  const a2b::FailedAccess& failed = report.failed;
  const std::string where =
      (failed.node == a2b::kNoNode) ? "broadcast" : NodeName(failed.node, false);
  char access[96];
  std::snprintf(access, sizeof(access), "%s, 0x%02X %s, %u tr%s, %u ms", where.c_str(),
                static_cast<unsigned>(failed.reg), failed.write ? "write" : "read",
                static_cast<unsigned>(failed.tries), (failed.tries == 1) ? "y" : "ies",
                static_cast<unsigned>(failed.elapsedMs));
  const unsigned position = static_cast<unsigned>(report.position);
  if (report.cause == a2b::ErrorReport::Cause::kDropped) {
    Print("error report at %u ms: sub-node %u failed the chain check (%s); %s\n",
          static_cast<unsigned>(report.timeMs), position, access,
          report.answeredLate ? "it answered a read after that" : "no answer after that either");
  } else {
    Print("error report at %u ms: the probe for sub-node %u failed (%s)\n",
          static_cast<unsigned>(report.timeMs), position, access);
  }
  PrintErrorState(a2b::kMainNode, report.main);
  for (size_t i = 0; i < report.subCount; ++i) {
    PrintErrorState(static_cast<a2b::NodeAddr>(i), report.subs[i]);
  }
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
  return a2b::ToString(result);
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
              : a2b::ToString(result));
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
                                    : m_Network.SubNodeInfoAt(node)->moduleInfo.groups;
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
    Print("Can't route: %s\n", a2b::ToString(error));
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
 * ID. No pairs is an empty list: every connection goes.
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
    Print("Routing mute: %s\n", a2b::ToString(m_MuteMode));
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
  Print("Routing mute: %s (from the next plan)\n", a2b::ToString(m_MuteMode));
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
        main ? nullptr : m_Network.SubNodeInfoAt(static_cast<a2b::NodeAddr>(i - 1));
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
          GroupsLine(m_Network.SubNodeInfoAt(static_cast<a2b::NodeAddr>(i))->moduleInfo.groups)
              .c_str());
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
  const a2b::SubNodeInfo* node = m_Network.SubNodeInfoAt(position);
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
  Print("    image for %s; B port %s, its cable %s\n", TargetChipName(module.targetChip).c_str(),
        BPortTypeName(module.bPortType).c_str(),
        module.bCableDm == 0 ? "not given" : Metres(module.bCableDm).c_str());
  if (module.fastI2c) {
    Print("    its I2C port at 400 kHz (the image's I2CCFG.DATARATE)\n");
  }
  if (m_Modules.UsingImage(position)) {
    Print("    module info from the image %s\n", m_ImageFiles[position].c_str());
  }
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

/**
 * The link into sub-node @p position: set with `link`; else the one the node before it gives
 * (its image's B port and its chip, as SubNode::DownstreamLink() gives it: the bench's nodes
 * are plain SubNodes); else, into sub-node 0, the board's.
 */
std::string BenchConsole::LinkName(a2b::NodeAddr position) const {
  if (m_LinkSet[position]) {
    return ProfileName(m_Links[position]);
  }
  const a2b::SubNodeInfo* upstream =
      (m_AllLinksBoard || position == 0)
          ? nullptr
          : m_Network.SubNodeInfoAt(static_cast<a2b::NodeAddr>(position - 1));
  if (upstream == nullptr) {
    return ProfileName(m_Board.defaultLink) + " (board)";
  }
  const uint8_t port = upstream->moduleInfo.bPortType;
  return ProfileName(a2b::LinkProfile::ForBPort(port, upstream->identity.productId)) +
         " (sub-node " + std::to_string(position - 1) + "'s " + BPortTypeName(port) + " port)";
}

/**
 * `link all board`: gives every link not set with `link N` the board's (BoardConfig::
 * defaultLink, from the export), whatever the images' B ports say. Called at each start,
 * after the import, and when the setting is made on a running bus.
 */
void BenchConsole::ApplyBoardLinks() {
  if (!m_AllLinksBoard) {
    return;
  }
  for (int n = 0; n < a2b::kMaxSubNodes; ++n) {
    if (!m_LinkSet[n]) {
      (void)m_Network.SetLinkProfile(static_cast<a2b::NodeAddr>(n), m_Board.defaultLink);
    }
  }
}

/** True while the bus is started: not stopped, not failed. */
bool BenchConsole::Running() const {
  const a2b::NetworkState state = m_Network.State();
  return state != a2b::NetworkState::kStopped && state != a2b::NetworkState::kFailed;
}
