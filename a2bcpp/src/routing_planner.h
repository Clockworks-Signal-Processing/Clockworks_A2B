/**
 * @file routing_planner.h
 * @brief Builds a routing plan (slots, slot masks, offsets, crossbars) for a chain from its
 *        connections, through AD243x crossbars and AD242x TDM order. Internal; Network
 *        drives it.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_ROUTING_PLANNER_H_
#define A2BCPP_ROUTING_PLANNER_H_

#include <cstddef>
#include <cstdint>

#include "a2b/module_info.h"
#include "a2b/routing.h"
#include "a2b/types.h"

namespace a2b {

/// The slot and crossbar calculation, with its working storage. One thread per object.
class RoutingPlanner {
 public:
  /** @brief Nodes of a chain, by index: 0 is the main node, n + 1 sub-node n. */
  static constexpr size_t kMaxNodes = kMaxSubNodes + 1;

  /** @brief A connection whose ends are in the chain, by node index. */
  struct Route {
    uint8_t source = 0;       ///< The source node's index.
    uint8_t sourceGroup = 0;  ///< Its RX group's ID.
    uint8_t sink = 0;         ///< The sink node's index.
    uint8_t sinkGroup = 0;    ///< Its TX group's ID.
  };

  /** @brief What a plan is built from. */
  struct Input {
    size_t subNodeCount = 0;                      ///< Sub-nodes in the chain.
    ChannelMap map[kMaxNodes] = {};               ///< Each node's crossbar and channels.
    const ChannelGroups* groups[kMaxNodes] = {};  ///< Each node's channel groups.
    Route routes[kMaxConnections];                ///< The connections to carry.
    size_t routeCount = 0;                        ///< How many.
  };

  /** @brief The TX frame buffer location the plan points unused TX channels at. */
  static constexpr uint8_t kUnusedLocation = 31;

  RoutingPlanner() = default;  ///< Ready.

  RoutingPlanner(const RoutingPlanner&) = delete;
  RoutingPlanner& operator=(const RoutingPlanner&) = delete;

  RoutingError Build(const Input& input, RoutingPlan& plan,
                     NodeAddr& node);    ///< Builds every register but RESPCYCS.
  static NodeAddr NodeOf(size_t index);  ///< The NodeAddr of a node index.
  static void Clear(RoutingPlan& plan);  ///< Empties a plan in place.
  static uint8_t SlotBits(uint8_t slotFormat, bool upstream);  ///< Bus bits of a data slot.
  static uint16_t RespOffset(uint8_t i2sgcfg, bool receives);  ///< The main node's RESPOFFS.

 private:
  static constexpr size_t kMaxSlots = 32;  ///< Data slots on a link, each way.

  RoutingError Check(const Input& input, NodeAddr& node);  ///< Groups and channels.
  RoutingError Downstream(NodeAddr& node);                 ///< Down slots and masks.
  RoutingError Upstream(NodeAddr& node);                   ///< Up slots, masks, main node.
  size_t Contribute(size_t index, uint32_t channels, uint8_t& offset,
                    size_t at);  ///< Appends a node's slots to m_Out.
  bool Consume(size_t index, uint16_t signal, size_t location);  ///< Maps a played slot.
  bool Needed(uint16_t signal, size_t from, size_t to) const;    ///< A sink in a range wants it.
  NodeRouting& Regs(size_t index);                               ///< A node's registers.
  bool Crossbar(size_t index) const { return m_Input->map[index].crossbar; }  ///< Has one.

  const Input* m_Input = nullptr;
  RoutingPlan* m_Plan = nullptr;
  const ChannelGroup* m_SourceGroup[kMaxConnections] = {};  ///< Each route's RX group.
  const ChannelGroup* m_SinkGroup[kMaxConnections] = {};    ///< Each route's TX group.
  uint32_t m_Paired[kMaxConnections] = {};  ///< Each route's source channels that have a sink.
  uint32_t m_Used[kMaxNodes] = {};          ///< Each node's channels sent anywhere.
  uint32_t m_Down[kMaxNodes] = {};          ///< Each node's channels sent downstream.
  uint32_t m_Up[kMaxNodes] = {};            ///< Each node's channels sent upstream.
  uint8_t m_Played[kMaxNodes] = {};         ///< TX frame buffer locations used, per node.
  uint16_t m_In[kMaxSlots] = {};            ///< The slots arriving at a node.
  uint16_t m_Out[2 * kMaxSlots] = {};       ///< The slots it sends on.
  size_t m_InLen = 0;
  /** Consume() gave a node without a crossbar a slot at a location other than its sink
      channel. */
  bool m_Misplaced = false;
};

}  // namespace a2b

#endif  // A2BCPP_ROUTING_PLANNER_H_
