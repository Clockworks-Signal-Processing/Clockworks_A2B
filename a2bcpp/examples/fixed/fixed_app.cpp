/**
 * @file fixed_app.cpp
 * @brief FixedApp: an app that brings up one A2B bus with a fixed set-up and routes its
 *        audio, with nothing typed.
 *
 * What it does, in order:
 * 1. Fill a BoardConfig from the SigmaStudio+ export (ImportSsPlus()), and give it the
 *    main node's channel groups, which an export hasn't got.
 * 2. Give the sub-nodes their module info from a clockworks::BoardModuleSource: the boards
 *    set by position (a board without an EEPROM, such as the AB0312), the fallback image for
 *    the others (as the bench console's `ignoreEEPROM 1`), or their EEPROMs; an AD2427 gets
 *    the AB0204's four PDM microphones in place of the fallback or a missing EEPROM image.
 * 3. Start() once, then Poll() often, here from Loop().
 * 4. Once every node the connections name is in the bus, add the connections by position
 *    (the Network keeps them by chip ID) and ApplyRouting(), once. From then on the stack
 *    routes them again as the chain changes: a node unplugged and plugged back in is
 *    found, set up and routed with nothing done by the app.
 * The events are printed as the bus gives them out, as the bench console prints them.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fixed_app.h"

#include <cstdio>

namespace {

/** A name for a state, for the report. */
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

/** A name for a rejection reason, for the report. */
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

/** A name for a removal reason, for the report. */
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

/** A name for a routing error, for the report. */
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
 * Makes the app; nothing is sent until Setup().
 * @param platform The host's I2C, time and logging; must outlive the app.
 */
FixedApp::FixedApp(a2b::Platform& platform)
    : m_Platform(platform), m_Network(platform, m_Board), m_Modules(m_Network) {
  m_Network.SetListener(this);
  m_Network.SetModuleInfoSource(&m_Modules);
}

/**
 * Fills the BoardConfig from chain 0 of the export, sets up the module info and starts the
 * bus. The connections are routed later, from Loop(), once their nodes are in.
 * @param bcd The export's sBusDescription.
 * @param setup The boards, the fallback image, the main node's groups and the connections;
 *        what it points to must outlive the app.
 * @return Result::kOk, or why the import or Network::Start() refused.
 */
a2b::Result FixedApp::Setup(const ADI_A2B_BCD& bcd, const FixedSetup& setup) {
  m_Setup = setup;
  a2b::Result result = a2b::ImportSsPlus(bcd, 0, m_Board, m_HostScript);
  if (result != a2b::Result::kOk) {
    std::printf("SigmaStudio+ import failed (%d)\n", static_cast<int>(result));
    return result;
  }
  m_Board.routing.groups = setup.mainGroups;
  for (int position = 0; position < a2b::kMaxSubNodes; ++position) {
    m_Modules.SetBoard(static_cast<a2b::NodeAddr>(position), setup.boards[position],
                       SigmaProgram());
  }
  m_Modules.SetFallback(setup.fallback);
  std::printf("Main node AD24%02X at 0x%02X, %u host script operation(s), links %s\n",
              static_cast<unsigned>(m_Board.mainProduct), static_cast<unsigned>(m_Board.mainAddr),
              static_cast<unsigned>(m_HostScript.Size()),
              m_Board.defaultLink.bSideInverted ? "rj45" : "utp");
  result = m_Network.Start();
  if (result != a2b::Result::kOk) {
    std::printf("Network::Start() failed (%d)\n", static_cast<int>(result));
  }
  return result;
}

/**
 * Polls the bus once (OnEvent() prints what happened), then adds and routes the
 * connections if their nodes have all come in and they aren't added yet.
 */
void FixedApp::Loop() {
  m_Network.Poll(m_Platform.NowMs());
  if (!m_Connected && m_Setup.connectionCount > 0 && NodesPresent()) {
    Connect();
  }
}

/** True if every sub-node the connections name is in the bus. */
bool FixedApp::NodesPresent() const {
  for (size_t i = 0; i < m_Setup.connectionCount; ++i) {
    const FixedConnection& c = m_Setup.connections[i];
    for (const a2b::NodeAddr node : {c.source, c.sink}) {
      if (node != a2b::kMainNode && m_Network.SubNode(node) == nullptr) {
        return false;
      }
    }
  }
  return true;
}

/** Adds the connections (the Network keeps them by chip ID) and routes them. */
void FixedApp::Connect() {
  m_Connected = true;
  for (size_t i = 0; i < m_Setup.connectionCount; ++i) {
    const FixedConnection& c = m_Setup.connections[i];
    const a2b::Result result =
        m_Network.AddConnection(c.source, c.sourceGroup, c.sink, c.sinkGroup);
    if (result != a2b::Result::kOk) {
      std::printf("connection %u not added (%d)\n", static_cast<unsigned>(i),
                  static_cast<int>(result));
    }
  }
  const a2b::RoutingError error = m_Network.ApplyRouting();
  if (error != a2b::RoutingError::kNone) {
    std::printf("can't route: %s\n", Name(error));
  }
}

/**
 * Prints one event: a new state, a sub-node added (with its chip, module, link and board),
 * rejected or removed, an interrupt, or the routing applied or failed.
 * @param network The bus.
 * @param event What happened.
 */
void FixedApp::OnEvent(a2b::Network& network, const a2b::Event& event) {
  (void)network;
  const unsigned node = static_cast<unsigned>(event.node);
  switch (event.type) {
    case a2b::EventType::kStateChanged:
      ReportState(event.state);
      return;
    case a2b::EventType::kNodeAdded:
      ReportNode(event.node);
      return;
    case a2b::EventType::kNodeRejected:
      std::printf("  sub-node %u rejected: %s\n", node, Name(event.rejection));
      return;
    case a2b::EventType::kNodeRemoved:
      std::printf("  sub-node %u removed: %s\n", node, Name(event.removal));
      return;
    case a2b::EventType::kInterrupt:
      std::printf("  interrupt from %s %d: type %u\n",
                  event.node == a2b::kMainNode ? "the main node" : "sub-node",
                  event.node == a2b::kMainNode ? 0 : static_cast<int>(event.node),
                  static_cast<unsigned>(event.interruptType));
      return;
    case a2b::EventType::kRoutingApplied: {
      const a2b::RoutingPlan& plan = m_Network.Routing();
      std::printf("routing applied: %u connection(s) routed, main node RESPCYCS 0x%02X\n",
                  static_cast<unsigned>(plan.routed), static_cast<unsigned>(plan.main.respCycles));
      if (m_Setup.heapInUse != nullptr) {
        std::printf("  heap in use: %u bytes\n", static_cast<unsigned>(m_Setup.heapInUse()));
      }
      return;
    }
    case a2b::EventType::kRoutingFailed:
      std::printf("routing failed: %s\n", Name(event.routingError));
      return;
  }
}

/** Prints a new state, and what went wrong or how many sub-nodes are up. */
void FixedApp::ReportState(a2b::NetworkState state) {
  std::printf("bus: %s\n", Name(state));
  if (state == a2b::NetworkState::kFailed) {
    std::printf("  error %d\n", static_cast<int>(m_Network.Error()));
  }
  if (state == a2b::NetworkState::kRunning) {
    std::printf("  %u sub-node(s)\n", static_cast<unsigned>(m_Network.SubNodeCount()));
  }
}

/** Prints one sub-node's chip, chip ID, module, link and board. */
void FixedApp::ReportNode(a2b::NodeAddr position) {
  const a2b::SubNodeInfo* node = m_Network.SubNode(position);
  if (node == nullptr) {
    return;
  }
  const uint8_t* id = node->identity.chipId;
  const a2b::ModuleInfo& module = node->moduleInfo;
  const clockworks::ModuleProfile* board = m_Modules.Using(position);
  std::printf("  sub-node %d: AD24%02X version 0x%02X, chip ID %02X%02X%02X%02X%02X%02X\n",
              position, node->identity.productId, node->identity.version, id[0], id[1], id[2],
              id[3], id[4], id[5]);
  std::printf("    module %04X:%04X, CFG-%u, %u peripheral block(s), link %s, %s\n",
              module.vendorId, module.productId, static_cast<unsigned>(module.powerConfig),
              static_cast<unsigned>(module.peripheralBlocks),
              m_Board.defaultLink.bSideInverted ? "rj45" : "utp",
              board != nullptr ? board->name : "its EEPROM's image");
}
