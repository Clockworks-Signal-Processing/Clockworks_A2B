/**
 * @file fixed_app.h
 * @brief The fixed bench app, the same on every host: brings up one bus from a SigmaStudio+
 *        export, gives chosen positions a Clockworks board's module info, and routes a
 *        fixed set of connections once their nodes are in the bus, with nothing typed.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_EXAMPLES_FIXED_FIXED_APP_H_
#define A2BCPP_EXAMPLES_FIXED_FIXED_APP_H_

#include <cstddef>
#include <cstdint>

#include "a2b/board_config.h"
#include "a2b/network.h"
#include "a2b/op_list.h"
#include "a2b/platform.h"
#include "a2b/routing.h"
#include "clockworks_boards.h"
#include "ss_plus_import.h"

/// One connection by the nodes' positions: a source node's RX group to a sink node's TX
/// group (a2b::kMainNode for the main node).
struct FixedConnection {
  a2b::NodeAddr source;  ///< The node sending.
  uint8_t sourceGroup;   ///< Its RX group.
  a2b::NodeAddr sink;    ///< The node playing.
  uint8_t sinkGroup;     ///< Its TX group.
};

/// What the app sets up: the boards, the image for the other nodes, the main node's
/// channel groups and the connections.
struct FixedSetup {
  /** The board at each sub-node position, or nullptr: the fallback (or the EEPROM). */
  const clockworks::ModuleProfile* boards[a2b::kMaxSubNodes] = {};
  /** The image for every other node, or nullptr to read their EEPROMs. */
  const clockworks::ModuleProfile* fallback = nullptr;
  a2b::ChannelGroups mainGroups;                 ///< The main node's channel groups.
  const FixedConnection* connections = nullptr;  ///< The connections to route.
  size_t connectionCount = 0;                    ///< How many.
  /** Gives the heap in use in bytes, printed once routing is applied; nullptr: not shown. */
  size_t (*heapInUse)() = nullptr;
};

/// One bus with a fixed set-up. It listens to the bus and prints what happens.
class FixedApp : public a2b::NetworkListener {
 public:
  explicit FixedApp(a2b::Platform& platform);  ///< Nothing started.

  a2b::Result Setup(const ADI_A2B_BCD& bcd, const FixedSetup& setup);  ///< Starts the bus.
  void Loop();  ///< Polls once; routes the connections once their nodes are in.

  const a2b::Network& Bus() const { return m_Network; }  ///< The bus.
  bool Connected() const { return m_Connected; }         ///< The connections are added.

  void OnEvent(a2b::Network& network, const a2b::Event& event) override;  ///< Prints it.

 private:
  bool NodesPresent() const;                  ///< Every node the connections name is in.
  void Connect();                             ///< Adds the connections and routes them.
  void ReportState(a2b::NetworkState state);  ///< Prints the new state.
  void ReportNode(a2b::NodeAddr position);    ///< Prints a sub-node that came in.

  a2b::Platform& m_Platform;
  a2b::BoardConfig m_Board;
  a2b::FixedOpList<64> m_HostScript;  ///< One Op per SigmaStudio+ config unit.
  a2b::Network m_Network;
  clockworks::BoardModuleSource m_Modules;  ///< The boards' images, else the EEPROMs.
  a2b::NodeFactory m_Factory;               ///< Every sub-node a clockworks::BoardNode.
  FixedSetup m_Setup;
  bool m_Connected = false;  ///< The connections are in the Network (by chip ID).
};

#endif  // A2BCPP_EXAMPLES_FIXED_FIXED_APP_H_
