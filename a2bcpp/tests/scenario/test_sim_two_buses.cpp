/**
 * @file test_sim_two_buses.cpp
 * @brief Simulator scenario 15: two Networks on one Platform, their main nodes on one host
 *        I2C bus (0x68/0x69 and 0x6A/0x6B, the TRMs' I2C address table). Each bus's
 *        start-up, probes, chain checks, broadcasts, drops, restarts, reset and IRQ lines,
 *        events and rejections stay its own; a listener, source or factory may serve both.
 *
 * Two buses have not run on hardware: this scenario is their only test.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <vector>

#include "a2b/network.h"
#include "detail/regdefs.h"
#include "sim/eeprom.h"
#include "sim/simulator.h"
#include "sim_bench.h"
#include "sim_trace.h"
#include "test_harness.h"

namespace a2b {
namespace test {
namespace {

constexpr uint8_t kMainAddr1 = 0x6A;  ///< Bus 1's main node: ADR1 high (TRMs, "I2C Interface").
constexpr uint8_t kBusAddr1 = 0x6B;   ///< Bus 1's bus address.

/** Bus 1's board: as bus 0's, at the other addresses, with bus index 1. */
BoardConfig MakeBoard1(uint8_t product = 0x37) {
  BoardConfig board = MakeBoardFor(product);
  board.busIndex = 1;
  board.mainAddr = kMainAddr1;
  board.busAddr = kBusAddr1;
  return board;
}

/** Two buses on one simulator, each with its Network, source and listener; one factory. */
struct TwoBuses {
  sim::Simulator sim;
  sim::Bus& bus0;
  sim::Bus& bus1;
  BoardConfig board0;
  BoardConfig board1;
  PositionSource source0;
  PositionSource source1;
  NodeFactory factory;
  Recorder listener0;
  Recorder listener1;
  Network network0;
  Network network1;

  /** Bus 0 and bus 1 with AD2437 main nodes, or bus 1 with @p product1. */
  explicit TwoBuses(uint8_t product1 = 0x37)
      : bus0(sim.AddBus(kMainAddr, kBusAddr, Bench::MainChip())),
        bus1(sim.AddBus(kMainAddr1, kBusAddr1, ChipOf(product1))),
        board0(MakeBoard()),
        board1(MakeBoard1(product1)),
        source0({MakeImage()}),
        source1({MakeImage()}),
        network0(sim, board0),
        network1(sim, board1) {
    CountingNode::removed = 0;
    CountingNode::srfInterrupt = false;
    factory.ByModule(0x00AD, 0x0037, Make<CountingNode>);
    network0.SetModuleInfoSource(&source0);
    network1.SetModuleInfoSource(&source1);
    network0.SetNodeFactory(&factory);
    network1.SetNodeFactory(&factory);
    network0.SetListener(&listener0);
    network1.SetListener(&listener1);
  }

  /** Polls both buses, in turn, every millisecond for @p ms. */
  void Run(uint32_t ms) {
    for (uint32_t i = 0; i < ms; ++i) {
      sim.Advance(1);
      network0.Poll(sim.NowMs());
      network1.Poll(sim.NowMs());
    }
  }

  /** Polls until both buses run, for at most @p ms. */
  bool RunUntilBothRun(uint32_t ms = 5000) {
    for (uint32_t i = 0; i < ms && !BothRun(); ++i) {
      Run(1);
    }
    return BothRun();
  }

  /** Both buses run. */
  bool BothRun() const {
    return network0.State() == NetworkState::kRunning && network1.State() == NetworkState::kRunning;
  }

  /** Starts both buses and polls until they run with @p count0 and @p count1 sub-nodes. */
  bool StartWith(size_t count0, size_t count1) {
    return network0.Start() == Result::kOk && network1.Start() == Result::kOk &&
           RunUntilBothRun() && network0.SubNodeCount() == count0 &&
           network1.SubNodeCount() == count1;
  }

  /** Bus 0's register accesses in the trace. */
  std::vector<Access> Accesses0() const { return Decode(sim.Trace(), kMainAddr, kBusAddr); }
  /** Bus 1's register accesses in the trace. */
  std::vector<Access> Accesses1() const { return Decode(sim.Trace(), kMainAddr1, kBusAddr1); }
};

/** The events of @p type in @p events from index @p from on. */
std::vector<Event> Since(const std::vector<Event>& events, size_t from, EventType type) {
  std::vector<Event> out;
  for (size_t i = from; i < events.size(); ++i) {
    if (events[i].type == type) {
      out.push_back(events[i]);
    }
  }
  return out;
}

A2B_TEST(Scenario15BothBusesFindTheirOwnChains) {
  TwoBuses buses;
  buses.bus0.Plug(Ad2437(0x10));
  buses.bus0.Plug(Ad2437(0x11));
  buses.bus0.Plug(Ad2437(0x12));
  buses.bus1.Plug(Ad2437(0x20));
  buses.bus1.Plug(Ad2437(0x21));
  A2B_CHECK(buses.StartWith(3, 2));
  A2B_CHECK_EQ(buses.bus0.DiscoveredCount(), 3);
  A2B_CHECK_EQ(buses.bus1.DiscoveredCount(), 2);
  A2B_CHECK(!buses.bus0.Wedged() && !buses.bus1.Wedged());

  // Each Network reports its own nodes, with its bus index, and each listener gets only
  // its own bus's events.
  for (NodeAddr n = 0; n < 3; ++n) {
    const SubNodeInfo* info = buses.network0.SubNodeInfoAt(n);
    A2B_CHECK(info != nullptr && info->identity.chipId[5] == 0x10 + n && info->identity.bus == 0);
  }
  for (NodeAddr n = 0; n < 2; ++n) {
    const SubNodeInfo* info = buses.network1.SubNodeInfoAt(n);
    A2B_CHECK(info != nullptr && info->identity.chipId[5] == 0x20 + n && info->identity.bus == 1);
  }
  const std::vector<Event> added0 = buses.listener0.Of(EventType::kNodeAdded);
  const std::vector<Event> added1 = buses.listener1.Of(EventType::kNodeAdded);
  A2B_CHECK_EQ(added0.size(), 3);
  A2B_CHECK_EQ(added1.size(), 2);
  for (const Event& event : added0) {
    A2B_CHECK_EQ(event.chipId[5] & 0xF0, 0x10);
  }
  for (const Event& event : added1) {
    A2B_CHECK_EQ(event.chipId[5] & 0xF0, 0x20);
  }
  for (const NodeReview& review : buses.listener1.reviews) {
    A2B_CHECK_EQ(review.node->identity.bus, 1);
  }

  // Each bus gets its own chain checks and BECNT broadcasts, each on its own addresses.
  buses.sim.ClearTrace();
  buses.Run(2100);
  const std::vector<Access> accesses0 = buses.Accesses0();
  const std::vector<Access> accesses1 = buses.Accesses1();
  A2B_CHECK(Count(accesses0, kBroadcast, true, reg::kBecnt, 0) >= 2);
  A2B_CHECK(Count(accesses1, kBroadcast, true, reg::kBecnt, 0) >= 2);
  A2B_CHECK(Count(accesses0, 2, false, reg::kVendor) >= 4);
  A2B_CHECK(Count(accesses1, 1, false, reg::kVendor) >= 4);
  A2B_CHECK_EQ(Count(accesses1, 2, false, reg::kVendor), 0);  // Bus 1 has two sub-nodes.
  // Every transfer held the Platform's lock.
  A2B_CHECK_EQ(buses.sim.UnlockedTransfers(), 0);
}

A2B_TEST(OneBusStartedLeavesTheOtherUntouched) {
  TwoBuses buses;
  buses.bus0.Plug(Ad2437(0x10));
  buses.bus1.Plug(Ad2437(0x20));
  buses.bus1.StartRunning();  // Left running by an earlier session.
  A2B_CHECK(buses.network0.Start() == Result::kOk);
  buses.Run(3000);
  A2B_CHECK(buses.network0.State() == NetworkState::kRunning);
  A2B_CHECK_EQ(buses.network0.SubNodeCount(), 1);
  // Nothing went to bus 1's addresses, and its Network said nothing.
  A2B_CHECK(buses.Accesses1().empty());
  A2B_CHECK(buses.listener1.events.empty());
  A2B_CHECK(buses.network1.State() == NetworkState::kStopped);
  A2B_CHECK(buses.bus1.Running());
  A2B_CHECK_EQ(buses.bus1.DiscoveredCount(), 0);

  // Started later, bus 1 comes up; bus 0 carries on.
  const size_t mark0 = buses.listener0.events.size();
  A2B_CHECK(buses.network1.Start() == Result::kOk);
  A2B_CHECK(buses.RunUntilBothRun());
  buses.Run(1000);
  A2B_CHECK_EQ(buses.network1.SubNodeCount(), 1);
  A2B_CHECK_EQ(buses.listener0.events.size(), mark0);
}

A2B_TEST(ResetAndIrqLinesFollowTheBusIndex) {
  TwoBuses buses;
  buses.bus0.SetHasResetLine(true);
  buses.bus1.SetHasResetLine(true);
  buses.bus0.SetHasIrqLine(true);
  buses.bus1.SetHasIrqLine(true);
  buses.bus0.Plug(Ad2437(0x10));
  buses.bus1.Plug(Ad2437(0x20));
  buses.bus1.StartRunning();
  // Bus 0's RESETn pulse doesn't reach bus 1's main node.
  A2B_CHECK(buses.network0.Start() == Result::kOk);
  buses.Run(1000);
  A2B_CHECK(buses.network0.State() == NetworkState::kRunning);
  A2B_CHECK(buses.bus1.Running());
  A2B_CHECK(buses.network1.Start() == Result::kOk);
  A2B_CHECK(buses.RunUntilBothRun());
  buses.Run(600);

  // With the IRQ lines quiet, neither bus reads its interrupt registers.
  buses.sim.ClearTrace();
  buses.Run(300);
  A2B_CHECK_EQ(Count(buses.Accesses0(), kMainNode, false, reg::kIntsrc), 0);
  A2B_CHECK_EQ(Count(buses.Accesses1(), kMainNode, false, reg::kIntsrc), 0);

  // An interrupt on bus 1 is read by bus 1 alone, and reported to its listener alone.
  const size_t mark0 = buses.listener0.events.size();
  buses.bus1.RaiseInterrupt(0, 16);  // IO0PND.
  buses.Run(50);
  A2B_CHECK_EQ(Count(buses.Accesses0(), kMainNode, false, reg::kIntsrc), 0);
  A2B_CHECK(Count(buses.Accesses1(), kMainNode, false, reg::kIntsrc) >= 1);
  const std::vector<Event> irqs = buses.listener1.Of(EventType::kInterrupt);
  A2B_CHECK(irqs.size() == 1 && irqs[0].node == 0 && irqs[0].interruptType == 16);
  A2B_CHECK_EQ(buses.listener0.events.size(), mark0);
}

A2B_TEST(DropOnOneBusLeavesTheOtherAlone) {
  TwoBuses buses;
  for (uint8_t id = 0x10; id < 0x13; ++id) {
    buses.bus0.Plug(Ad2437(id));
  }
  for (uint8_t id = 0x20; id < 0x23; ++id) {
    buses.bus1.Plug(Ad2437(id));
  }
  A2B_CHECK(buses.StartWith(3, 3));
  // Bus 1 stops probing, so its switches stay as they are (its chain is still checked).
  buses.network1.PauseDiscovery();
  buses.Run(1000);
  buses.sim.ClearTrace();
  const size_t mark0 = buses.listener0.events.size();
  const size_t mark1 = buses.listener1.events.size();
  uint8_t swctl1[3];
  for (size_t n = 0; n < 3; ++n) {
    swctl1[n] = buses.bus1.Sub(n).Reg(reg::kSwctl);
  }

  // Sub-node 1 of bus 0 unplugged: bus 0 takes it and sub-node 2 out.
  buses.bus0.Unplug(1);
  buses.Run(2000);
  A2B_CHECK_EQ(buses.network0.SubNodeCount(), 1);
  const std::vector<Event> removed = Since(buses.listener0.events, mark0, EventType::kNodeRemoved);
  A2B_CHECK(removed.size() == 2 && removed[0].node == 1 && removed[1].node == 2 &&
            removed[0].removal == RemovalReason::kDropped);
  // Bus 1 saw nothing.
  A2B_CHECK_EQ(buses.network1.SubNodeCount(), 3);
  A2B_CHECK_EQ(buses.bus1.DiscoveredCount(), 3);
  A2B_CHECK_EQ(buses.listener1.events.size(), mark1);
  for (size_t n = 0; n < 3; ++n) {
    A2B_CHECK_EQ(buses.bus1.Sub(n).Reg(reg::kSwctl), swctl1[n]);
  }
  const std::vector<Access> accesses1 = buses.Accesses1();
  for (NodeAddr n = 0; n < 3; ++n) {
    A2B_CHECK_EQ(Count(accesses1, n, true, reg::kSwctl), 0);
    A2B_CHECK(Count(accesses1, n, false, reg::kVendor) >= 3);  // Its own chain checks.
  }

  // Plugged back in, bus 0 finds them again; bus 1 still sees nothing.
  buses.bus0.Plug(Ad2437(0x11));
  buses.bus0.Plug(Ad2437(0x12));
  for (int i = 0; i < 3000 && buses.network0.SubNodeCount() != 3; ++i) {
    buses.Run(1);
  }
  A2B_CHECK_EQ(buses.network0.SubNodeCount(), 3);
  A2B_CHECK_EQ(buses.listener1.events.size(), mark1);
}

A2B_TEST(MainNodeResetRestartsOnlyItsBus) {
  TwoBuses buses;
  buses.bus0.SetHasResetLine(true);
  buses.bus1.SetHasResetLine(true);
  buses.bus0.Plug(Ad2437(0x10));
  buses.bus0.Plug(Ad2437(0x11));
  buses.bus1.Plug(Ad2437(0x20));
  buses.bus1.Plug(Ad2437(0x21));
  A2B_CHECK(buses.StartWith(2, 2));
  const size_t mark0 = buses.listener0.events.size();
  const size_t mark1 = buses.listener1.events.size();

  // Bus 0's main node is reset (its supply dipped): its chain check sees CONTROL.MSTR clear
  // and starts bus 0 again from RESETn.
  buses.bus0.PowerCycle();
  bool restarted = false;
  for (int i = 0; i < 2000; ++i) {
    buses.Run(1);
    restarted = restarted || buses.network0.State() == NetworkState::kStarting;
    A2B_CHECK(buses.bus1.Running());  // Bus 1's main node is never reset.
  }
  A2B_CHECK(restarted);
  A2B_CHECK(buses.RunUntilBothRun());
  A2B_CHECK_EQ(buses.network0.SubNodeCount(), 2);
  const std::vector<Event> removed = Since(buses.listener0.events, mark0, EventType::kNodeRemoved);
  A2B_CHECK(removed.size() == 2 && removed[0].removal == RemovalReason::kRestart);
  A2B_CHECK_EQ(buses.network1.SubNodeCount(), 2);
  A2B_CHECK_EQ(buses.listener1.events.size(), mark1);
}

A2B_TEST(FailureOnOneBusLeavesTheOtherRunning) {
  TwoBuses buses;
  buses.bus0.Plug(Ad2437(0x10));
  buses.bus1.Plug(Ad2437(0x20));
  A2B_CHECK(buses.StartWith(1, 1));
  const size_t mark1 = buses.listener1.events.size();

  // Bus 0's main node stops answering: bus 0 tries a restart, then fails.
  buses.sim.FailAddress(kMainAddr, 1000000);
  buses.Run(2000);
  A2B_CHECK(buses.network0.State() == NetworkState::kFailed);
  A2B_CHECK(buses.network0.Error() == NetworkError::kI2cError);
  A2B_CHECK(buses.network1.State() == NetworkState::kRunning);
  A2B_CHECK_EQ(buses.network1.SubNodeCount(), 1);
  A2B_CHECK_EQ(buses.listener1.events.size(), mark1);

  // Once it answers again, Start() brings bus 0 back.
  buses.sim.FailAddress(kMainAddr, 0);
  A2B_CHECK(buses.network0.Start() == Result::kOk);
  A2B_CHECK(buses.RunUntilBothRun());
  A2B_CHECK_EQ(buses.network0.SubNodeCount(), 1);
}

A2B_TEST(RejectNodeStaysOnItsBus) {
  TwoBuses buses;
  for (uint8_t id = 0x10; id < 0x13; ++id) {
    buses.bus0.Plug(Ad2437(id));
  }
  for (uint8_t id = 0x20; id < 0x23; ++id) {
    buses.bus1.Plug(Ad2437(id));
  }
  A2B_CHECK(buses.StartWith(3, 3));
  const size_t mark0 = buses.listener0.events.size();
  const size_t mark1 = buses.listener1.events.size();

  A2B_CHECK(buses.network0.RejectNode(1) == Result::kOk);
  buses.Run(100);
  A2B_CHECK_EQ(buses.network0.SubNodeCount(), 1);
  A2B_CHECK(buses.network0.LastRejection() == RejectReason::kByApp);
  A2B_CHECK_EQ(Since(buses.listener0.events, mark0, EventType::kNodeRemoved).size(), 2);
  A2B_CHECK(buses.network1.LastRejection() == RejectReason::kNone);

  // Bus 0 probes the position again and switches the same node off silently; bus 1 keeps
  // its chain and says nothing.
  const size_t afterReject0 = buses.listener0.events.size();
  buses.Run(5000);
  A2B_CHECK_EQ(buses.network0.SubNodeCount(), 1);
  A2B_CHECK_EQ(buses.listener0.events.size(), afterReject0);
  A2B_CHECK_EQ(buses.network1.SubNodeCount(), 3);
  A2B_CHECK_EQ(buses.bus1.DiscoveredCount(), 3);
  A2B_CHECK_EQ(buses.listener1.events.size(), mark1);
}

A2B_TEST(SharedListenerTellsTheBusesApart) {
  TwoBuses buses;
  Recorder shared;
  std::vector<const Network*> from;  // The bus of each event.
  shared.onEvent = [&from](Network& network, const Event&) { from.push_back(&network); };
  // Reject sub-node 1 of bus 1 only.
  shared.decide = [](const NodeReview& review) {
    return (review.node->identity.bus == 1 && review.node->identity.position == 1)
               ? RejectReason::kByApp
               : review.policy;
  };
  buses.network0.SetListener(&shared);
  buses.network1.SetListener(&shared);
  for (uint8_t id = 0x10; id < 0x13; ++id) {
    buses.bus0.Plug(Ad2437(id));
  }
  for (uint8_t id = 0x20; id < 0x23; ++id) {
    buses.bus1.Plug(Ad2437(id));
  }
  A2B_CHECK(buses.StartWith(3, 1));
  A2B_CHECK(buses.network0.LastRejection() == RejectReason::kNone);
  A2B_CHECK(buses.network1.LastRejection() == RejectReason::kByApp);

  // Each event came from the Network it describes.
  A2B_CHECK_EQ(from.size(), shared.events.size());
  int added0 = 0;
  int added1 = 0;
  int rejected1 = 0;
  for (size_t i = 0; i < shared.events.size(); ++i) {
    const Event& event = shared.events[i];
    const bool bus1 = (from[i] == &buses.network1);
    A2B_CHECK(from[i] == &buses.network0 || bus1);
    if (event.type == EventType::kNodeAdded) {
      A2B_CHECK_EQ(event.chipId[5] & 0xF0, bus1 ? 0x20 : 0x10);
      (bus1 ? added1 : added0) += 1;
    }
    if (event.type == EventType::kNodeRejected) {
      A2B_CHECK(bus1 && event.node == 1 && event.chipId[5] == 0x21);
      ++rejected1;
    }
  }
  A2B_CHECK_EQ(added0, 3);
  A2B_CHECK_EQ(added1, 1);
  A2B_CHECK_EQ(rejected1, 1);

  // Bus 1's rejected node is switched off again silently; bus 0 is not disturbed.
  const size_t mark = shared.events.size();
  buses.Run(5000);
  A2B_CHECK_EQ(shared.events.size(), mark);
  A2B_CHECK_EQ(buses.network0.SubNodeCount(), 3);
  A2B_CHECK_EQ(buses.network1.SubNodeCount(), 1);
}

/** A module class per bus, by type tag. */
class BusZeroNode : public SubNode {
 public:
  using SubNode::SubNode;
  uint32_t TypeTag() const override { return 0xB0; }
};

/** The other bus's module class. */
class BusOneNode : public SubNode {
 public:
  using SubNode::SubNode;
  uint32_t TypeTag() const override { return 0xB1; }
};

/** One source for both buses: the module product ID tells the bus (0x0037 or 0x0038). */
class BusSource : public ModuleInfoSource {
 public:
  /** The module of the node's bus. */
  size_t GetModuleInfo(const SubNodeIdentity& node, uint8_t* buf, size_t bufLen) override {
    sim::ModuleInfoHeader header;
    header.productId = (node.bus == 0) ? 0x0037 : 0x0038;
    header.powerConfig = sim::kPowerConfigHighPower;
    const std::vector<uint8_t> image = sim::MakeModuleInfoImage(header);
    ++calls[node.bus & 1];
    const size_t len = (image.size() < bufLen) ? image.size() : bufLen;
    for (size_t i = 0; i < len; ++i) {
      buf[i] = image[i];
    }
    return len;
  }

  int calls[2] = {};  ///< GetModuleInfo() calls per bus.
};

/** The app chooser: bus 1's nodes by the bus index (the table would pick BusZeroNode). */
MakeNodeFn ChooseByBus(const SubNodeInfo& info, void* context) {
  (void)context;
  return (info.identity.bus == 1) ? Make<BusOneNode> : nullptr;
}

A2B_TEST(SharedSourceAndFactory) {
  TwoBuses buses;
  BusSource source;
  NodeFactory factory;
  factory.ByModule(0x00AD, 0x0037, Make<BusZeroNode>);
  factory.ByModule(0x00AD, 0x0038, Make<BusZeroNode>);
  factory.SetChooser(ChooseByBus, nullptr);
  for (Network* network : {&buses.network0, &buses.network1}) {
    network->SetModuleInfoSource(&source);
    network->SetNodeFactory(&factory);
  }
  buses.bus0.Plug(Ad2437(0x10));
  buses.bus0.Plug(Ad2437(0x11));
  buses.bus1.Plug(Ad2437(0x20));
  A2B_CHECK(buses.StartWith(2, 1));
  A2B_CHECK_EQ(source.calls[0], 2);
  A2B_CHECK_EQ(source.calls[1], 1);
  A2B_CHECK_EQ(buses.network0.SubNodeInfoAt(1)->moduleInfo.productId, 0x0037);
  A2B_CHECK_EQ(buses.network1.SubNodeInfoAt(0)->moduleInfo.productId, 0x0038);
  A2B_CHECK(buses.network0.NodeObject(0)->TypeTag() == 0xB0);
  A2B_CHECK(buses.network0.NodeObject(1)->TypeTag() == 0xB0);
  A2B_CHECK(buses.network1.NodeObject(0)->TypeTag() == 0xB1);
}

A2B_TEST(HandlerOnOneBusCallsTheOther) {
  TwoBuses buses;
  buses.bus1.Plug(Ad2437(0x20)).AddEeprom(0x50).Load({0x5A});
  buses.bus1.Plug(Ad2437(0x21));
  A2B_CHECK(buses.network1.Start() == Result::kOk);
  for (int i = 0; i < 3000 && buses.network1.State() != NetworkState::kRunning; ++i) {
    buses.Run(1);
  }
  A2B_CHECK_EQ(buses.network1.SubNodeCount(), 2);

  // When bus 0 finds its node, its handler reads a peripheral on bus 1, pauses bus 1's
  // discovery and rejects bus 1's last node.
  Result read = Result::kNotSupported;
  uint8_t byte = 0;
  buses.listener0.onEvent = [&](Network&, const Event& event) {
    if (event.type == EventType::kNodeAdded) {
      read = buses.network1.PeripheralRead(0, PeripheralAddress::I2c(0x50), &byte, 1);
      buses.network1.PauseDiscovery();
      buses.network1.RejectNode(1);
    }
  };
  buses.bus0.Plug(Ad2437(0x10));
  A2B_CHECK(buses.network0.Start() == Result::kOk);
  A2B_CHECK(buses.RunUntilBothRun());
  buses.Run(100);
  A2B_CHECK(read == Result::kOk);
  A2B_CHECK_EQ(byte, 0x5A);
  A2B_CHECK(buses.network1.DiscoveryPaused());
  A2B_CHECK_EQ(buses.network1.SubNodeCount(), 1);  // Carried out at bus 1's next Poll().
  A2B_CHECK_EQ(buses.network0.SubNodeCount(), 1);
}

A2B_TEST(TwoMainNodeChipsOnOneHost) {
  // An AD2437 bus and an AD2428 bus, each with a chain of mixed chips.
  TwoBuses buses(0x28);
  buses.source0 = PositionSource({MakeImageFor(0x37), MakeImageFor(0x27), MakeImageFor(0x33)});
  buses.source1 = PositionSource({MakeImageFor(0x33), MakeImageFor(0x37), MakeImageFor(0x28)});
  buses.bus0.Plug(SubOf(0x37, 0x10));
  buses.bus0.Plug(SubOf(0x27, 0x11));
  buses.bus0.Plug(SubOf(0x33, 0x12));
  buses.bus1.Plug(SubOf(0x33, 0x20));
  buses.bus1.Plug(SubOf(0x37, 0x21));
  buses.bus1.Plug(SubOf(0x28, 0x22));
  A2B_CHECK(buses.StartWith(3, 3));
  A2B_CHECK_EQ(buses.network0.SubNodeInfoAt(1)->identity.productId, 0x27);
  A2B_CHECK_EQ(buses.network1.SubNodeInfoAt(2)->identity.productId, 0x28);
  // Each main node's switch in its own chip's mode, with ENSW.
  A2B_CHECK_EQ(buses.bus0.Main().Reg(reg::kSwctl), 0x11);
  A2B_CHECK_EQ(buses.bus1.Main().Reg(reg::kSwctl), 0x01);
  A2B_CHECK(!buses.bus0.Wedged() && !buses.bus1.Wedged());
}

}  // namespace
}  // namespace test
}  // namespace a2b

A2B_TEST_MAIN()
