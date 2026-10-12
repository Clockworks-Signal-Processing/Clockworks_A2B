/**
 * @file connection_table.cpp
 * @brief ConnectionTable: the connections a Network keeps.
 *
 * Follows the connection manager of ADI's PnP layer (a2bpnp_conn_mng.cpp), in
 * fixed storage instead of a std::list and std::string chip IDs:
 * - a connection is kept by the chip IDs of its nodes (the main node is marked as such), so
 *   it follows a module to any position and waits while the module is out of the bus
 *   (ADI's: "connections will be persistent");
 * - adding a connection that is there already changes nothing; adding one whose sink (node
 *   and group) another connection has replaces that one, since a sink plays one source
 *   (ADI's addConnection);
 * - at most kMaxConnections (ADI's console's A2B_PNP_CFG_MAX_NUM_NW_CONNECTION).
 *
 * Duplicate or zero chip IDs (decisions.md, ConnectionTable): ADI's table took the first
 * node with the chip ID (and its removeNode() cleared one entry only, whatever the count).
 * Here an end whose chip ID one node in the bus has is that node, whatever its position. If
 * several nodes share the chip ID (all zero on a part without one, say), the end is the
 * node at the position where it was last seen, if that node still has the chip ID; else
 * it is not in the bus. The table remembers each end's position when it is added by
 * position, and each time the routing is built.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "connection_table.h"

#include <cstring>

namespace a2b {

/**
 * Adds a connection, or replaces the one that has its sink.
 * @param connection The connection.
 * @param sourceAt Where its source is now, if the caller knows (added by position), else
 *        kNoNode.
 * @param sinkAt Where its sink is now, or kNoNode.
 * @return Result::kOk; Result::kNoSpace if the table is full.
 */
Result ConnectionTable::Add(const Connection& connection, NodeAddr sourceAt, NodeAddr sinkAt) {
  size_t i = 0;
  while (i < m_Count && !SameEnd(m_Entries[i].connection.sink, connection.sink)) {
    ++i;
  }
  if (i == m_Count) {
    if (m_Count >= kMaxConnections) {
      return Result::kNoSpace;
    }
    ++m_Count;
  }
  Entry& entry = m_Entries[i];
  entry.connection = connection;
  entry.sourceAt = sourceAt;
  entry.sinkAt = sinkAt;
  return Result::kOk;
}

/**
 * Removes a connection.
 * @param connection The connection: the same source and sink (nodes and groups).
 * @return Result::kOk, or Result::kInvalidArgument if the table hasn't got it.
 */
Result ConnectionTable::Remove(const Connection& connection) {
  for (size_t i = 0; i < m_Count; ++i) {
    const Connection& kept = m_Entries[i].connection;
    if (SameEnd(kept.source, connection.source) && SameEnd(kept.sink, connection.sink)) {
      for (size_t j = i + 1; j < m_Count; ++j) {
        m_Entries[j - 1] = m_Entries[j];
      }
      --m_Count;
      return Result::kOk;
    }
  }
  return Result::kInvalidArgument;
}

/**
 * Finds the node of connection @p i's source in a chain.
 * @param i The connection, below Count().
 * @param chain The bus's sub-nodes.
 * @return kMainNode, a sub-node position, or kNoNode if the node isn't in the bus.
 */
NodeAddr ConnectionTable::SourceNode(size_t i, const Chain& chain) const {
  return Find(m_Entries[i].connection.source, m_Entries[i].sourceAt, chain);
}

/**
 * Finds the node of connection @p i's sink in a chain.
 * @param i The connection, below Count().
 * @param chain The bus's sub-nodes.
 * @return kMainNode, a sub-node position, or kNoNode if the node isn't in the bus.
 */
NodeAddr ConnectionTable::SinkNode(size_t i, const Chain& chain) const {
  return Find(m_Entries[i].connection.sink, m_Entries[i].sinkAt, chain);
}

/**
 * Remembers where each connection's ends are now, for ends whose chip ID several nodes
 * share. An end not in the bus keeps its last position.
 * @param chain The bus's sub-nodes.
 */
void ConnectionTable::Remember(const Chain& chain) {
  for (size_t i = 0; i < m_Count; ++i) {
    Entry& entry = m_Entries[i];
    const NodeAddr source = Find(entry.connection.source, entry.sourceAt, chain);
    const NodeAddr sink = Find(entry.connection.sink, entry.sinkAt, chain);
    entry.sourceAt = (source != kNoNode) ? source : entry.sourceAt;
    entry.sinkAt = (sink != kNoNode) ? sink : entry.sinkAt;
  }
}

/**
 * Finds the node an end names: the main node, or the one sub-node with its chip ID, or,
 * when several share it, the one at @p hint.
 * @param end The end.
 * @param hint Where the end was last seen, or kNoNode.
 * @param chain The bus's sub-nodes.
 * @return kMainNode, a sub-node position, or kNoNode if it isn't in the bus (or several
 *         nodes have its chip ID and none is at @p hint).
 */
NodeAddr ConnectionTable::Find(const ConnectionEnd& end, NodeAddr hint, const Chain& chain) {
  if (end.mainNode) {
    return kMainNode;
  }
  NodeAddr found = kNoNode;
  size_t matches = 0;
  for (size_t i = 0; i < chain.count; ++i) {
    if (std::memcmp(chain.nodes[i].identity.chipId, end.chipId, sizeof(end.chipId)) == 0) {
      found = static_cast<NodeAddr>(i);
      ++matches;
    }
  }
  if (matches <= 1) {
    return found;
  }
  const bool hintHolds =
      IsSubNode(hint) && static_cast<size_t>(hint) < chain.count &&
      std::memcmp(chain.nodes[hint].identity.chipId, end.chipId, sizeof(end.chipId)) == 0;
  return hintHolds ? hint : kNoNode;
}

/**
 * Tells whether two ends are the same: the same node (the main node, or the same chip ID)
 * and the same group.
 * @param a One end.
 * @param b The other.
 * @return True if they are the same.
 */
bool ConnectionTable::SameEnd(const ConnectionEnd& a, const ConnectionEnd& b) {
  if (a.mainNode != b.mainNode || a.group != b.group) {
    return false;
  }
  return a.mainNode || std::memcmp(a.chipId, b.chipId, sizeof(a.chipId)) == 0;
}

}  // namespace a2b
