/**
 * @file test_connection_table.cpp
 * @brief Unit tests for ConnectionTable: adding, replacing, removing, and finding each
 *        end's node by chip ID, with duplicate and zero chip IDs.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstring>
#include <initializer_list>

#include "a2b/module_info.h"
#include "connection_table.h"
#include "test_harness.h"

namespace a2b {
namespace {

/** A sub-node end with the last chip ID byte @p id. */
ConnectionEnd Sub(uint8_t id, uint8_t group) {
  ConnectionEnd end;
  end.chipId[5] = id;
  end.group = group;
  return end;
}

/** The main node's end. */
ConnectionEnd Main(uint8_t group) {
  ConnectionEnd end;
  end.mainNode = true;
  end.group = group;
  return end;
}

/** A connection from @p source to @p sink. */
Connection Link(const ConnectionEnd& source, const ConnectionEnd& sink) {
  Connection connection;
  connection.source = source;
  connection.sink = sink;
  return connection;
}

/** A chain of sub-nodes whose chip IDs end in @p ids. */
struct TestChain {
  SubNodeInfo nodes[kMaxSubNodes];
  ConnectionTable::Chain chain;

  explicit TestChain(std::initializer_list<uint8_t> ids) {
    size_t n = 0;
    for (uint8_t id : ids) {
      nodes[n].identity.position = static_cast<NodeAddr>(n);
      nodes[n].identity.chipId[5] = id;
      ++n;
    }
    chain.nodes = nodes;
    chain.count = n;
  }
};

A2B_TEST(AddsAndFindsByChipId) {
  ConnectionTable table;
  A2B_CHECK(table.Add(Link(Main(0), Sub(0x22, 0))) == Result::kOk);
  A2B_CHECK(table.Add(Link(Sub(0x22, 1), Main(0))) == Result::kOk);
  A2B_CHECK_EQ(table.Count(), 2);
  const TestChain chain({0x11, 0x22});
  A2B_CHECK_EQ(table.SourceNode(0, chain.chain), kMainNode);
  A2B_CHECK_EQ(table.SinkNode(0, chain.chain), 1);
  A2B_CHECK_EQ(table.SourceNode(1, chain.chain), 1);
  // The module moved to position 0: the connection follows it.
  const TestChain moved({0x22, 0x11});
  A2B_CHECK_EQ(table.SinkNode(0, moved.chain), 0);
  // Not in the bus: it waits.
  const TestChain gone({0x11});
  A2B_CHECK_EQ(table.SinkNode(0, gone.chain), kNoNode);
}

A2B_TEST(SameConnectionTwiceChangesNothing) {
  ConnectionTable table;
  A2B_CHECK(table.Add(Link(Main(0), Sub(0x22, 0))) == Result::kOk);
  A2B_CHECK(table.Add(Link(Main(0), Sub(0x22, 0))) == Result::kOk);
  A2B_CHECK_EQ(table.Count(), 1);
}

A2B_TEST(SinkTakesOneSource) {
  // ADI's addConnection: a connection to a sink (node and group) another connection has
  // replaces it.
  ConnectionTable table;
  A2B_CHECK(table.Add(Link(Main(0), Sub(0x22, 0))) == Result::kOk);
  A2B_CHECK(table.Add(Link(Sub(0x11, 3), Sub(0x22, 0))) == Result::kOk);
  A2B_CHECK_EQ(table.Count(), 1);
  A2B_CHECK(table.At(0).source.chipId[5] == 0x11 && table.At(0).source.group == 3);
  // Another group of the same node is another sink.
  A2B_CHECK(table.Add(Link(Main(0), Sub(0x22, 1))) == Result::kOk);
  A2B_CHECK_EQ(table.Count(), 2);
}

A2B_TEST(RemovesAndClears) {
  ConnectionTable table;
  A2B_CHECK(table.Add(Link(Main(0), Sub(0x22, 0))) == Result::kOk);
  A2B_CHECK(table.Add(Link(Main(1), Sub(0x33, 0))) == Result::kOk);
  A2B_CHECK(table.Remove(Link(Main(1), Sub(0x22, 0))) == Result::kInvalidArgument);  // No such.
  A2B_CHECK(table.Remove(Link(Main(0), Sub(0x22, 0))) == Result::kOk);
  A2B_CHECK_EQ(table.Count(), 1);
  A2B_CHECK_EQ(table.At(0).sink.chipId[5], 0x33);  // The rest keep their order.
  table.Clear();
  A2B_CHECK_EQ(table.Count(), 0);
}

A2B_TEST(FullTable) {
  ConnectionTable table;
  for (size_t i = 0; i < kMaxConnections; ++i) {
    A2B_CHECK(table.Add(Link(Main(0), Sub(static_cast<uint8_t>(i), 0))) == Result::kOk);
  }
  A2B_CHECK(table.Add(Link(Main(0), Sub(0xEE, 0))) == Result::kNoSpace);
  A2B_CHECK(table.Add(Link(Main(1), Sub(0x00, 0))) == Result::kOk);  // A replacement fits.
}

A2B_TEST(SharedChipIdsUseTheLastPosition) {
  // Two modules with no chip ID (all zero): an end is the node where it was last seen, if
  // that node still has the chip ID; ADI's table took the first node with it.
  ConnectionTable table;
  ConnectionEnd zero = Sub(0x00, 0);
  A2B_CHECK(table.Add(Link(Main(0), zero), kMainNode, 1) == Result::kOk);
  const TestChain chain({0x00, 0x00});
  A2B_CHECK_EQ(table.SinkNode(0, chain.chain), 1);
  A2B_CHECK_EQ(ConnectionTable::Find(zero, kNoNode, chain.chain), kNoNode);  // Ambiguous.
  // The node at the remembered position isn't that chip any more: not found.
  const TestChain changed({0x00, 0x55, 0x00});
  A2B_CHECK_EQ(table.SinkNode(0, changed.chain), kNoNode);
  // Only one node with it: that one, wherever it is.
  const TestChain single({0x55, 0x66, 0x00});
  A2B_CHECK_EQ(table.SinkNode(0, single.chain), 2);
  // Remember() keeps the position it is found at.
  table.Remember(single.chain);
  const TestChain both({0x00, 0x66, 0x00});
  A2B_CHECK_EQ(table.SinkNode(0, both.chain), 2);
}

A2B_TEST(SameEnd) {
  A2B_CHECK(ConnectionTable::SameEnd(Main(0), Main(0)));
  A2B_CHECK(!ConnectionTable::SameEnd(Main(0), Main(1)));
  A2B_CHECK(!ConnectionTable::SameEnd(Main(0), Sub(0, 0)));
  A2B_CHECK(ConnectionTable::SameEnd(Sub(7, 2), Sub(7, 2)));
  A2B_CHECK(!ConnectionTable::SameEnd(Sub(7, 2), Sub(8, 2)));
  ConnectionEnd main = Main(0);
  main.chipId[0] = 0x12;  // Not used for the main node.
  A2B_CHECK(ConnectionTable::SameEnd(main, Main(0)));
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
