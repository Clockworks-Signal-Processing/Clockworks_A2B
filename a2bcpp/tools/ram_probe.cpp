/**
 * @file ram_probe.cpp
 * @brief Sizes of the stack's objects on the target, for tools/ram_report.py.
 *
 * Each a2b_sizeof_<Name> array is as big as the object, and ram_report.py reads its size
 * back with nm, so nothing has to run on the target. Add a line for each class that holds
 * per-bus state as it is written. Network holds the internal ones (Transport to
 * SubNodeInfo, the sub-node slots, the peripheral-script buffer, the event queue of
 * Events, and the routing: ConnectionTable, RoutingPlanner and its Input, two RoutingPlans)
 * in its fixed storage;
 * BoardConfig and NodeFactory are the app's, one per bus or shared.
 *
 * The functions at the end are call paths the report measures like the stack's own: one
 * thread polling two buses in turn (the two-bus Poll() high-water mark), and a listener's
 * handler on one bus that calls the other bus's peripheral I/O.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "a2b/board_config.h"
#include "a2b/eeprom_source.h"
#include "a2b/network.h"
#include "a2b/sub_node.h"
#include "chip_caps.h"
#include "connection_table.h"
#include "discovery.h"
#include "interrupt_poller.h"
#include "module_script.h"
#include "node_setup.h"
#include "routing_planner.h"
#include "script_runner.h"
#include "transport.h"

extern "C" {
char a2b_sizeof_Network[sizeof(a2b::Network)];
char a2b_sizeof_Transport[sizeof(a2b::Transport)];
char a2b_sizeof_ScriptRunner[sizeof(a2b::ScriptRunner)];
char a2b_sizeof_InterruptPoller[sizeof(a2b::InterruptPoller)];
char a2b_sizeof_Discovery[sizeof(a2b::Discovery)];
char a2b_sizeof_RegisterImage[sizeof(a2b::RegisterImage)];
char a2b_sizeof_SubNodeInfo[sizeof(a2b::SubNodeInfo)];
char a2b_sizeof_DiscoveryLink[sizeof(a2b::Discovery::Link)];
char a2b_sizeof_NodeSetup[sizeof(a2b::NodeSetup)];
char a2b_sizeof_ModuleScript[sizeof(a2b::ModuleScript)];
char a2b_sizeof_EepromModuleInfoSource[sizeof(a2b::EepromModuleInfoSource)];
char a2b_sizeof_SubNode[sizeof(a2b::SubNode)];
char a2b_sizeof_LinkProfile[sizeof(a2b::LinkProfile)];
char a2b_sizeof_NodeFactory[sizeof(a2b::NodeFactory)];
char a2b_sizeof_BoardConfig[sizeof(a2b::BoardConfig)];
char a2b_sizeof_ChipCaps[sizeof(a2b::ChipCaps)];
char a2b_sizeof_Event[sizeof(a2b::Event)];
char a2b_sizeof_ConnectionTable[sizeof(a2b::ConnectionTable)];
char a2b_sizeof_RoutingPlanner[sizeof(a2b::RoutingPlanner)];
char a2b_sizeof_RoutingPlannerInput[sizeof(a2b::RoutingPlanner::Input)];
char a2b_sizeof_RoutingPlan[sizeof(a2b::RoutingPlan)];
char a2b_sizeof_ChannelGroups[sizeof(a2b::ChannelGroups)];
}

/** One thread polls two buses in turn: its worst path is the two-bus Poll() high-water mark. */
extern "C" void a2b_PollTwoBuses(a2b::Network* bus0, a2b::Network* bus1, uint32_t nowMs) {
  bus0->Poll(nowMs);
  bus1->Poll(nowMs);
}

/** A handler on one bus writes to a peripheral on the other (what it adds to Dispatch). */
extern "C" a2b::Result a2b_HandlerWritesOtherBus(a2b::Network* other) {
  const uint8_t data[2] = {0x00, 0x01};
  return other->PeripheralWrite(0, a2b::PeripheralAddress::I2c(0x50), data, sizeof(data));
}
