/**
 * @file minimal_app.h
 * @brief The minimal example app, the same on every host: brings up one bus from a
 *        SigmaStudio+ export and reports what it finds.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_EXAMPLES_MINIMAL_MINIMAL_APP_H_
#define A2BCPP_EXAMPLES_MINIMAL_MINIMAL_APP_H_

#include <cstddef>
#include <cstdint>

#include "a2b/board_config.h"
#include "a2b/module_info.h"
#include "a2b/network.h"
#include "a2b/op_list.h"
#include "a2b/platform.h"
#include "ss_plus_import.h"

/// One bus: its BoardConfig, its Network, and the module info it gives sub-nodes if the
/// modules' EEPROMs are to be ignored. It listens to the bus and prints what happens.
class MinimalApp : public a2b::NetworkListener {
 public:
  explicit MinimalApp(a2b::Platform& platform, bool ignoreEeprom = false);  ///< Nothing started.

  a2b::Result SetUtpLink(a2b::NodeAddr position);  ///< The link into a sub-node is UTP.
  a2b::Result Setup(const ADI_A2B_BCD& bcd);       ///< Imports the export and starts the bus.
  void Loop();                                     ///< Polls once; the events are printed.

  const a2b::Network& Bus() const { return m_Network; }  ///< The bus.

  void OnEvent(a2b::Network& network, const a2b::Event& event) override;  ///< Prints it.

 private:
  /// Module info from the app ("ignore EEPROM"): one image for every module, whatever its
  /// EEPROM holds.
  class Source : public a2b::ModuleInfoSource {
   public:
    size_t GetModuleInfo(const a2b::SubNodeIdentity& node, uint8_t* buf,
                         size_t bufLen) override;  ///< The same image for every node.
  };

  void ReportState(a2b::NetworkState state);  ///< Prints the new state.
  void ReportNode(a2b::NodeAddr position);    ///< Prints a sub-node that came in.

  a2b::Platform& m_Platform;
  a2b::BoardConfig m_Board;
  a2b::FixedOpList<64> m_HostScript;  ///< One Op per SigmaStudio+ config unit.
  Source m_Source;
  a2b::Network m_Network;
};

#endif  // A2BCPP_EXAMPLES_MINIMAL_MINIMAL_APP_H_
