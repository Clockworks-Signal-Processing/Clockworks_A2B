/**
 * @file bench_console.h
 * @brief The bench console's commands, the same on every host: pick or load a board,
 *        start and stop the bus, show its status, set link profiles, ignore the EEPROMs,
 *        read, write and check sub-nodes' EEPROMs, trace the I2C, read and write registers
 *        and peripherals, pause discovery, reject a node, connect channel groups and route
 *        them.
 *
 * A2B_BENCH_FILES (1 unless the build sets it) gives the commands that need a file system
 * (bench_console_files.cpp). Built with A2B_BENCH_FILES 0 (the RP2040 console), there are no
 * `eeprom` commands, no `i2cTrace`, and no `board FILE`, `module N file FILE` or `module N
 * BOARD FILE`: those say so when typed, and `help` leaves them out.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_EXAMPLES_BENCH_BENCH_CONSOLE_H_
#define A2BCPP_EXAMPLES_BENCH_BENCH_CONSOLE_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "a2b/board_config.h"
#include "a2b/link_profile.h"
#include "a2b/module_info.h"
#include "a2b/network.h"
#include "a2b/op_list.h"
#include "a2b/platform.h"
#include "a2b/routing.h"
#include "clockworks_boards.h"
#include "sigma_xml.h"
#include "ss_plus_import.h"

#ifndef A2B_BENCH_FILES
#define A2B_BENCH_FILES 1  ///< The commands that need a file system; the RP2040 build sets 0.
#endif
#if A2B_BENCH_FILES
#include "eeprom_job.h"
#include "ss_plus_file.h"
#include "trace_platform.h"
#endif

/// One A2B bus driven by typed commands. The host's main reads lines and calls Execute(),
/// calls Poll() often, and prints what TakeOutput() gives.
class BenchConsole : public a2b::NetworkListener {
 public:
  /// One SigmaStudio+ export of a built-in board, for one main-node chip.
  struct BoardExport {
    uint8_t mainProduct;     ///< The export's main node, as PRODUCT reads it (0x37: AD2437).
    const ADI_A2B_BCD* bus;  ///< The export's sBusDescription, compiled in.
  };

  /// A board built into the console: its SigmaStudio+ exports, one per main-node chip.
  struct BuiltInBoard {
    const char* name;         ///< What `board` takes.
    const char* description;  ///< What `board` lists.
    /** Its exports, the default first. With more than one, `start` reads the main node's
        chip and takes the export for it. */
    std::vector<BoardExport> exports;
    /** The board and its modules are UTP on both ports: every link's B side straight,
        whatever the export's XCVRBINV. */
    bool linksUtp;
  };

  explicit BenchConsole(a2b::Platform& host);  ///< No board; nothing sent.
  ~BenchConsole() override;

  BenchConsole(const BenchConsole&) = delete;
  BenchConsole& operator=(const BenchConsole&) = delete;

  bool Execute(const char* line);  ///< Runs one command line; false after `quit`.
  void Poll();                     ///< Polls the bus once (and an EEPROM job's next step).
  std::string TakeOutput();        ///< What the console printed since the last call.
#if !A2B_BENCH_FILES
  bool Busy() const { return false; }  ///< No `eeprom` jobs without files.
#else
  /// An `eeprom` job is running: the host waits for it before the next command it queued.
  bool Busy() const { return m_EepromJob.Running(); }
#endif
  /// The host runs @p line at once even while Busy(): `eeprom abort` and `quit`.
  static bool RunsWhileBusy(const char* line);

  const a2b::Network& Bus() const { return m_Network; }     ///< The bus.
  static const std::vector<BuiltInBoard>& BuiltInBoards();  ///< The boards built in.

  void OnEvent(a2b::Network& network, const a2b::Event& event) override;  ///< Prints it.
  void OnAudioMute(a2b::Network& network,
                   const a2b::MuteScope& scope) override;  ///< Prints what is muted.
  void OnAudioUnmute(a2b::Network& network,
                     const a2b::MuteScope& scope) override;  ///< Prints that it is done.

 private:
  void Print(const char* format, ...);               ///< Adds to the output.
  void Help();                                       ///< `help`.
  void Board(const std::vector<std::string>& args);  ///< `board`.
  void Start();                                      ///< `start`.
  bool ImportBoard(const ADI_A2B_BCD& bus);          ///< Imports an export into a fresh #m_Board.
  bool ReadMainChip(uint8_t mainAddr, uint8_t& product);     ///< The main node's PRODUCT.
  std::string ExportChips(const BuiltInBoard& board) const;  ///< "AD2437 or AD2428".
  void StopBus();                                            ///< `stop`.
  void Status();                                             ///< `status`.
  void Link(const std::vector<std::string>& args);           ///< `link`.
  void Cable(const std::vector<std::string>& args);          ///< `cable`.
  void Module(const std::vector<std::string>& args);         ///< `module`.
  void IgnoreEeprom(const std::vector<std::string>& args);   ///< `ignoreEEPROM`.
  // The commands that need files (bench_console_files.cpp).
#if A2B_BENCH_FILES
  void ModuleImage(int first, int last, const std::string& path);  ///< `module N file FILE`.
  void Eeprom(const std::vector<std::string>& args);               ///< `eeprom`.
  void EepromConfigure(const std::vector<std::string>& args);      ///< `eeprom config`.
  bool EepromNode(const std::string& word, a2b::NodeAddr& node);   ///< A sub-node in the bus.
  EepromConfig EepromFor(a2b::NodeAddr node) const;  ///< The settings, with its address.
  void EepromFinished();                             ///< Reports a job that has ended.
  void Trace(const std::vector<std::string>& args);  ///< `i2cTrace`.
#endif
  void Interrupts(const std::vector<std::string>& args);                       ///< `interrupts`.
  void Errors(const std::vector<std::string>& args);                           ///< `errors`.
  void PrintErrorState(a2b::NodeAddr node, const a2b::NodeErrorState& state);  ///< One node.
  void PrintErrorReport();                                     ///< The error watch's last report.
  void ReadRegisters(const std::vector<std::string>& args);    ///< `rdm`, `rds`.
  void WriteRegisters(const std::vector<std::string>& args);   ///< `wrm`, `wrs`.
  void PeripheralRead(const std::vector<std::string>& args);   ///< `periRead`.
  void PeripheralWrite(const std::vector<std::string>& args);  ///< `periWrite`.
  void Pause();                                                ///< `pause`.
  void Resume();                                               ///< `resume`.
  void Reject(const std::vector<std::string>& args);           ///< `reject`.
  void AddConnection(const std::vector<std::string>& args);    ///< `addCon`, `removeCon`.
  void ListConnections();                                      ///< `listCon`.
  std::string ChannelsCarried(const a2b::Connection& connection) const;  ///< Its channels.
  void ApplyRouting();                                                   ///< `applyRouting`.
  void SetRoute(const std::vector<std::string>& args);                   ///< `setRoute`.
  void ImportConnections(const std::vector<std::string>& args);          ///< `importCon`.
  void MuteBus(const std::vector<std::string>& args);                    ///< `mute`.
  void MuteMode(const std::vector<std::string>& args);                   ///< `muteMode`.
  void ShowBandwidth();                                                  ///< `bandwidth`.
  void ShowRouting();                                                    ///< `routing`.
  void ShowGroups();                                                     ///< `groups`.
  void SetGroup(const std::vector<std::string>& args);                   ///< `group`.
  std::string EndName(const a2b::ConnectionEnd& end) const;              ///< A connection end.
  std::string Why(a2b::NodeAddr node, a2b::Result result,
                  bool registers) const;                ///< Why an access failed.
  void ReportNode(a2b::NodeAddr position);              ///< One sub-node's line.
  void HintOpenLine(a2b::NodeAddr node, uint8_t type);  ///< Why the end reports an open line.
  std::string LinkName(a2b::NodeAddr position) const;   ///< The link into a sub-node.
  void ApplyBoardLinks();  ///< `link all board`: the links not set get the board's.
  bool Running() const;    ///< Started, not stopped or failed.

#if !A2B_BENCH_FILES
  a2b::Platform& m_Trace;  ///< The host's platform (not traced: there is no file to trace to).
#else
  TracePlatform m_Trace;  ///< The host's platform, traced on request.
#endif
  a2b::BoardConfig m_Board;            ///< Imported at each `start`.
  a2b::FixedOpList<256> m_HostScript;  ///< The board's host script.
  a2b::Network m_Network;              ///< The bus.
  a2b::ChannelGroups m_MainGroups;     ///< The main node's channel groups (`group main`).
  /** The image every sub-node gets with `ignoreEEPROM 1`: the minimal example's, with the
      console's groups (`group image`). */
  clockworks::ModuleProfile m_Plain;
  clockworks::BoardModuleSource m_Modules;  ///< Each node's module info (`module N`).
  a2b::NodeFactory m_Factory;               ///< Every sub-node a clockworks::BoardNode.
#if A2B_BENCH_FILES
  a2b::SsPlusFile m_File;  ///< A board loaded from a file.
#endif
  const ADI_A2B_BCD* m_Bcd = nullptr;       ///< The board picked (a built-in's default), or none.
  const BuiltInBoard* m_BuiltIn = nullptr;  ///< The built-in board picked; none for a file.
  std::string m_BoardName;                  ///< Its name or file.
  uint8_t m_Chain = 0;                      ///< Its chain (main node) to use.
  bool m_LinksUtp = false;                  ///< BuiltInBoard::linksUtp of the board.
  bool m_IgnoreEeprom = false;              ///< `ignoreEEPROM 1`.
  a2b::RoutingMute m_MuteMode = a2b::RoutingMute::kLocal;  ///< `muteMode`, for each `start`.
  bool m_LinkSet[a2b::kMaxSubNodes] = {};                  ///< `link N` set the link into N.
  a2b::LinkProfile m_Links[a2b::kMaxSubNodes];             ///< What it set.
  /** `link all board`: every link not set with `link N` is the board's, whatever the images'
      B ports say (applied at each start, from the export). */
  bool m_AllLinksBoard = false;
  uint16_t m_Cables[a2b::kMaxSubNodes] = {};      ///< `cable N`: the cable into N in dm; 0: none.
  std::string m_ModuleFiles[a2b::kMaxSubNodes];   ///< `module N NAME [FILE]`: its program.
  std::string m_ImageFiles[a2b::kMaxSubNodes];    ///< `module N file FILE`: the image file.
  size_t m_ModuleWrites[a2b::kMaxSubNodes] = {};  ///< The program's writes.
  bool m_ShowAllInterrupts = false;               ///< `interrupts all`.
  uint32_t m_HiddenInterrupts = 0;                ///< Open-line interrupts not printed.
  /** The last sub-node HintOpenLine() gave its hint for, until the chain changes. */
  a2b::NodeAddr m_OpenLineHinted = a2b::kNoNode;
#if A2B_BENCH_FILES
  std::string m_TraceFile;       ///< The trace file, while tracing.
  EepromJob m_EepromJob;         ///< The `eeprom` job, running or last run.
  EepromConfig m_EepromConfig;   ///< `eeprom config`.
  bool m_EepromAddrSet = false;  ///< `eeprom config addr` given: not the one found.
  std::string m_EepromFile;      ///< `eeprom read`: where the bytes go.
  bool m_EepromInfo = false;     ///< The read is `eeprom info`'s.
#endif
  std::string m_Output;  ///< Printed, not yet taken.
};

#endif  // A2BCPP_EXAMPLES_BENCH_BENCH_CONSOLE_H_
