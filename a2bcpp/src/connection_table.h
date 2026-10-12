/**
 * @file connection_table.h
 * @brief The connections a Network keeps, by chip ID, and how they find their nodes in the
 *        chain. Internal; Network owns one.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_CONNECTION_TABLE_H_
#define A2BCPP_CONNECTION_TABLE_H_

#include <cstddef>
#include <cstdint>

#include "a2b/module_info.h"
#include "a2b/routing.h"
#include "a2b/types.h"

namespace a2b {

/// Up to kMaxConnections connections in fixed storage. One thread per table.
class ConnectionTable {
 public:
  /** @brief The chain the connections are matched against: each sub-node's chip ID. */
  struct Chain {
    const SubNodeInfo* nodes = nullptr;  ///< Sub-nodes 0 to count - 1.
    size_t count = 0;                    ///< Sub-nodes in the bus.
  };

  ConnectionTable() = default;  ///< Empty.

  Result Add(const Connection& connection, NodeAddr sourceAt = kNoNode,
             NodeAddr sinkAt = kNoNode);        ///< Adds a connection.
  Result Remove(const Connection& connection);  ///< Removes a connection.
  void Clear() { m_Count = 0; }                 ///< Removes them all.

  size_t Count() const { return m_Count; }  ///< Connections kept.
  /// The i-th connection, oldest first.
  const Connection& At(size_t i) const { return m_Entries[i].connection; }
  NodeAddr SourceNode(size_t i, const Chain& chain) const;  ///< Where its source is now.
  NodeAddr SinkNode(size_t i, const Chain& chain) const;    ///< Where its sink is now.
  void Remember(const Chain& chain);                        ///< Keeps where each end is now.

  static NodeAddr Find(const ConnectionEnd& end, NodeAddr hint,
                       const Chain& chain);  ///< The node an end names, or kNoNode.
  static bool SameEnd(const ConnectionEnd& a, const ConnectionEnd& b);  ///< Same node, group.

 private:
  /** @brief A connection and where its ends were last seen. */
  struct Entry {
    Connection connection;
    NodeAddr sourceAt = kNoNode;  ///< The source's position when last seen.
    NodeAddr sinkAt = kNoNode;    ///< The sink's position when last seen.
  };

  Entry m_Entries[kMaxConnections];
  size_t m_Count = 0;
};

}  // namespace a2b

#endif  // A2BCPP_CONNECTION_TABLE_H_
