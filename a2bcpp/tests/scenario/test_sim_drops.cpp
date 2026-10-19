/**
 * @file test_sim_drops.cpp
 * @brief Simulator scenarios for the running chain: 4 (unplug the last node, a middle
 *        node, the first), re-plug, a node that resets, the drop interrupts, a main node
 *        that resets, 12 (transient I2C errors), the BECNT writes and RejectNode().
 *
 * The drops come from the simulator's TRM model (sim/simulator.cpp), checked against the
 * bench's cable pulls (goldens G4 and G5 were not captured).
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <vector>

#include "sim_bench.h"
#include "test_harness.h"

namespace a2b {
namespace {

using test::Access;
using test::Bench;
using test::Count;
using test::CountingNode;
using test::Find;
using test::kBroadcast;
using test::kBusAddr;
using test::kMainAddr;

// Scenario 4: unplug the last node and a middle node.

A2B_TEST(Scenario4UnplugLast) {
  Bench bench;
  bench.PlugChain(3);
  A2B_CHECK(bench.StartWith(3));
  bench.listener.events.clear();
  bench.bus.Unplug(2);
  bench.Run(bench.board.timings.chainCheckMs + 5);  // Found by the next chain check.
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
  A2B_CHECK(bench.network.State() == NetworkState::kRunning);
  const std::vector<Event> removed = bench.listener.Of(EventType::kNodeRemoved);
  A2B_CHECK_EQ(removed.size(), 1);
  A2B_CHECK(removed.size() == 1 && removed[0].node == 2 &&
            removed[0].removal == RemovalReason::kDropped && removed[0].chipId[5] == 0x12);
  A2B_CHECK_EQ(CountingNode::removed, 1);
  A2B_CHECK(bench.Logged("W: a2b: sub-node 2 dropped"));
  // Sub-node 1 opened its switch, and its B side is probed again, no sooner than 250 ms
  // after (the AD2437 TRM's wait), through sub-node 1.
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kSwctl), 0x10);
  bench.sim.ClearTrace();
  bench.Run(bench.board.timings.probeIntervalMs + 100);
  const std::vector<Access> a = bench.Accesses();
  const int probe = Find(a, 0, kMainNode, true, reg::kDiscvry);
  A2B_CHECK(probe >= 0 && a[probe].nodeadr == 1);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
  A2B_CHECK(bench.listener.Of(EventType::kStateChanged).empty());  // Running throughout.
}

A2B_TEST(Scenario4UnplugMiddle) {
  // The cable into sub-node 1 (golden G5's case): sub-node 1 and every node after it drop.
  Bench bench;
  bench.PlugChain(4);
  A2B_CHECK(bench.StartWith(4));
  bench.listener.events.clear();
  bench.bus.Unplug(1);
  bench.Run(bench.board.timings.chainCheckMs + 5);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  const std::vector<Event> removed = bench.listener.Of(EventType::kNodeRemoved);
  A2B_CHECK_EQ(removed.size(), 3);
  for (size_t i = 0; i < removed.size(); ++i) {
    A2B_CHECK_EQ(removed[i].node, static_cast<NodeAddr>(1 + i));  // In order.
    A2B_CHECK(removed[i].removal == RemovalReason::kDropped);
  }
  A2B_CHECK_EQ(CountingNode::removed, 3);
  A2B_CHECK(bench.network.SubNodeInfoAt(1) == nullptr && bench.network.NodeObject(1) == nullptr);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kSwctl) & reg::kSwctlEnsw, 0);
  A2B_CHECK(bench.bus.Sub(0).Reg(reg::kGpioien) == 0x7F);  // Sub-node 0 is untouched.
}

A2B_TEST(Scenario4UnplugFirst) {
  Bench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  bench.bus.Unplug(0);
  bench.Run(bench.board.timings.chainCheckMs + 5);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 0);
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kSwctl), 0x10);  // The main node's switch opened.
  A2B_CHECK_EQ(bench.listener.Of(EventType::kNodeRemoved).size(), 2);
  A2B_CHECK(bench.network.State() == NetworkState::kRunning);
}

A2B_TEST(ChainCheckReadsEveryNodesIds) {
  // Every chainCheckMs: the main node's VENDOR and PRODUCT and CONTROL, then each sub-node's
  // VENDOR and PRODUCT (the TRMs' bus drop flow).
  Bench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  bench.sim.ClearTrace();
  bench.Run(2000);
  const std::vector<Access> a = bench.Accesses();
  const int checks = static_cast<int>(2000 / bench.board.timings.chainCheckMs);
  A2B_CHECK_EQ(Count(a, kMainNode, false, reg::kProduct), checks);
  A2B_CHECK_EQ(Count(a, kMainNode, false, reg::kControl), checks);
  A2B_CHECK_EQ(Count(a, 0, false, reg::kProduct), checks);
  A2B_CHECK_EQ(Count(a, 1, false, reg::kProduct), checks);
  // In order: the main node, then sub-node 0, then sub-node 1.
  const int main = Find(a, 0, kMainNode, false, reg::kControl);
  const int s0 = Find(a, static_cast<size_t>(main), 0, false, reg::kVendor);
  const int s1 = Find(a, static_cast<size_t>(s0), 1, false, reg::kVendor);
  A2B_CHECK(main >= 0 && main < s0 && s0 < s1);
  A2B_CHECK_EQ(test::FailedTransfers(bench.sim.Trace()), 0);
}

A2B_TEST(ChecksGoOnWhileDiscoveryIsPaused) {
  // Unlike ADI's stack, which checked only before its probes.
  Bench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  bench.network.PauseDiscovery();
  bench.bus.Unplug(1);
  bench.Run(bench.board.timings.chainCheckMs + 5);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
}

// Re-plug and nodes that reset.

A2B_TEST(RePlugIsFoundAgain) {
  Bench bench;
  bench.PlugChain(3);
  A2B_CHECK(bench.StartWith(3));
  bench.bus.Unplug(1);
  A2B_CHECK(bench.RunUntilCount(1, 1000));
  bench.listener.events.clear();
  bench.PlugChain(2, 0x21);  // Two other modules where the first two were.
  A2B_CHECK(bench.RunUntilCount(3, 2000));
  const std::vector<Event> added = bench.listener.Of(EventType::kNodeAdded);
  A2B_CHECK_EQ(added.size(), 2);
  A2B_CHECK(added.size() == 2 && added[0].node == 1 && added[0].chipId[5] == 0x21 &&
            added[1].node == 2 && added[1].chipId[5] == 0x22);
  const SubNodeInfo* node = bench.network.SubNodeInfoAt(2);
  A2B_CHECK(node != nullptr && node->identity.chipId[5] == 0x22);
  A2B_CHECK_EQ(bench.bus.Sub(2).Reg(reg::kGpioien), 0x7F);  // Configured again.
}

A2B_TEST(NodeThatResetsIsFoundAgain) {
  // A node whose PLL unlocks (e.g. noise) drops out with the nodes after it, but stays
  // plugged in and powered: the upstream switch is opened, so they start from power-up,
  // and the next probes find them again.
  Bench bench;
  bench.PlugChain(3);
  A2B_CHECK(bench.StartWith(3));
  bench.listener.events.clear();
  bench.bus.ResetSub(1);
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 1);
  A2B_CHECK(bench.RunUntilCount(1, 600));
  A2B_CHECK(bench.RunUntilCount(3, 2000));
  A2B_CHECK_EQ(bench.listener.Of(EventType::kNodeRemoved).size(), 2);
  A2B_CHECK_EQ(bench.listener.Of(EventType::kNodeAdded).size(), 2);
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kGpioien), 0x7F);
  A2B_CHECK_EQ(bench.bus.Sub(2).Reg(reg::kGpioien), 0x7F);
}

// Drop interrupts.

A2B_TEST(SrfMissedInterruptChecksAtOnce) {
  // Nodes that enable the SRF-missed interrupt: the new last node raises it when the node
  // after it drops, which has the chain checked at once, well before the next periodic
  // check. (A reset node stays powered, so its switch reports no power fault.)
  BoardConfig board = test::MakeBoard();
  board.timings.chainCheckMs = 60000;
  Bench bench(board);
  CountingNode::srfInterrupt = true;
  bench.PlugChain(3);
  A2B_CHECK(bench.StartWith(3));
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kIntmsk0), reg::kIntmsk0Pwrerr | reg::kIntmsk0Srferr);
  bench.listener.events.clear();
  bench.bus.ResetSub(2);
  bench.Run(2 * bench.board.timings.interruptPollMs + 2);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
  const std::vector<Event> irqs = bench.listener.Of(EventType::kInterrupt);
  A2B_CHECK_EQ(irqs.size(), 1);
  A2B_CHECK(irqs.size() == 1 && irqs[0].node == 1 && irqs[0].interruptType == 5);
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kNode) & reg::kNodeLast, reg::kNodeLast);
}

A2B_TEST(InterruptOfADroppedNodeIsAnIntTypeReadError) {
  // A sub-node's interrupt still waiting when the node drops can't be read: INTTYPE 0xFD
  // (TRMs), which has the chain checked at once.
  BoardConfig board = test::MakeBoard();
  board.timings.chainCheckMs = 60000;
  Bench bench(board);
  bench.PlugChain(3);
  A2B_CHECK(bench.StartWith(3));
  bench.listener.events.clear();
  bench.bus.RaiseInterrupt(2, 16);  // IO0PND.
  bench.bus.ResetSub(2);
  bench.Run(2 * bench.board.timings.interruptPollMs + 2);
  const std::vector<Event> irqs = bench.listener.Of(EventType::kInterrupt);
  A2B_CHECK(irqs.size() == 1 && irqs[0].node == 2 && irqs[0].interruptType == 0xFD);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
}

A2B_TEST(SupplyRemovedBeforeTheLastNodeIsFoundByThePowerFault) {
  // What the Pi bench showed: the 24 V removed at sub-node 2, which keeps
  // running from the A2B pair, so the last of four nodes loses power. Sub-node 2 reports a
  // power fault (INTSRC 0x42, INTTYPE 15) with the default masks, the chain is checked at
  // once, sub-node 3 NAKs three reads, and sub-node 2's switch is opened (SWCTL 0x10). Its
  // position is probed again, finds nothing while the 24 V is off, and finds the node
  // once the 24 V is back.
  BoardConfig board = test::MakeBoard();
  board.timings.chainCheckMs = 60000;
  Bench bench(board);
  bench.PlugChain(4);
  A2B_CHECK(bench.StartWith(4));
  bench.listener.events.clear();
  bench.sim.ClearTrace();
  bench.bus.SetDownstreamSupply(2, false);
  bench.Run(bench.board.timings.interruptPollMs + 2);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 3);
  const std::vector<Event> irqs = bench.listener.Of(EventType::kInterrupt);
  A2B_CHECK(irqs.size() == 1 && irqs[0].node == 2 && irqs[0].interruptType == 15);
  const std::vector<Event> removed = bench.listener.Of(EventType::kNodeRemoved);
  A2B_CHECK(removed.size() == 1 && removed[0].node == 3);
  A2B_CHECK_EQ(test::FailedTransfers(bench.sim.Trace()), 3);
  A2B_CHECK_EQ(bench.bus.Sub(2).Reg(reg::kSwctl), 0x10);
  bench.Run(3000);  // Probes behind sub-node 2 find nothing; one power fault only.
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 3);
  A2B_CHECK_EQ(bench.listener.Of(EventType::kInterrupt).size(), 1);
  bench.bus.SetDownstreamSupply(2, true);
  A2B_CHECK(bench.RunUntilCount(4, 1000));
}

A2B_TEST(PulledCableRaisesNoPowerFault) {
  // A cable pulled out, not a supply removed: no power fault, and with the default masks no
  // interrupt, as on the bench, so the next chain check finds the drop.
  Bench bench;
  bench.PlugChain(4);
  A2B_CHECK(bench.StartWith(4));
  bench.listener.events.clear();
  bench.bus.Unplug(3);
  bench.Run(bench.board.timings.chainCheckMs + 5);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 3);
  A2B_CHECK(bench.listener.Of(EventType::kInterrupt).empty());
  A2B_CHECK_EQ(bench.listener.Of(EventType::kNodeRemoved).size(), 1);
}

A2B_TEST(SubNodeInterruptIsReported) {
  // An interrupt the stack doesn't use is passed on; it doesn't have the chain checked.
  BoardConfig board = test::MakeBoard();
  board.timings.chainCheckMs = 60000;
  Bench bench(board);
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  bench.listener.events.clear();
  bench.sim.ClearTrace();
  bench.bus.RaiseInterrupt(1, 17);  // IO1PND.
  bench.Run(2 * bench.board.timings.interruptPollMs + 2);
  const std::vector<Event> irqs = bench.listener.Of(EventType::kInterrupt);
  A2B_CHECK(irqs.size() == 1 && irqs[0].node == 1 && irqs[0].interruptType == 17);
  A2B_CHECK_EQ(Count(bench.Accesses(), kMainNode, false, reg::kControl), 0);  // No check.
}

A2B_TEST(MainNodeResetStartsAgain) {
  // A main node that lost its settings (power dip or RESETn) reads CONTROL.MSTR = 0: the
  // bus starts again (RESETn first, where the platform has it) and finds the chain again.
  Bench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  bench.listener.events.clear();
  bench.bus.PowerCycle();
  A2B_CHECK(bench.RunUntil(NetworkState::kStarting, 600));
  A2B_CHECK(bench.Logged("W: a2b: main node was reset; starting again"));
  const std::vector<Event> removed = bench.listener.Of(EventType::kNodeRemoved);
  A2B_CHECK(removed.size() == 2 && removed[0].removal == RemovalReason::kRestart);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 3000));
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
}

// Scenario 12: transient I2C errors.

A2B_TEST(Scenario12FailedInterruptReadIsRetried) {
  Bench bench;
  bench.PlugChain(1);
  A2B_CHECK(bench.StartWith(1));
  bench.listener.events.clear();
  bench.sim.FailAddress(kMainAddr, 1);  // The next INTSRC read.
  bench.Run(1000);
  A2B_CHECK(bench.network.State() == NetworkState::kRunning);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK(bench.listener.events.empty());
}

A2B_TEST(Scenario12FailedCheckReadIsRetried) {
  // Two failed reads of a sub-node in a check: retried, no drop. Three: the node counts as
  // dropped, and the next probe finds it again. (Discovery paused and no BECNT writes, so
  // only the checks use the bus address.)
  BoardConfig board = test::MakeBoard();
  board.timings.becntResetMs = 0;
  Bench bench(board);
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  bench.network.PauseDiscovery();
  bench.listener.events.clear();
  bench.sim.FailAddress(kBusAddr, 2);
  bench.Run(bench.board.timings.chainCheckMs + 5);
  A2B_CHECK(bench.listener.Of(EventType::kNodeRemoved).empty());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
  bench.sim.FailAddress(kBusAddr, 3);
  bench.Run(bench.board.timings.chainCheckMs + 5);
  A2B_CHECK_EQ(bench.listener.Of(EventType::kNodeRemoved).size(), 2);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 0);
  bench.network.ResumeDiscovery();
  A2B_CHECK(bench.RunUntilCount(2, 2000));
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 2000));
}

A2B_TEST(ResumeDuringStartUpWaitsOutSustain) {
  // ResumeDiscovery() called while the bus starts: the start-up's waits are kept. A main node
  // left running has SUSTAIN after its soft reset, and INV or MSTR inside it wedges it.
  Bench bench;
  bench.PlugChain(1);
  bench.bus.StartRunning();
  bench.network.PauseDiscovery();
  A2B_CHECK(bench.network.Start() == Result::kOk);
  for (int i = 0; i < 200 && !bench.bus.Wedged(); ++i) {
    bench.Run(1);
    bench.network.ResumeDiscovery();
    bench.network.PauseDiscovery();
  }
  A2B_CHECK(!bench.bus.Wedged());
  bench.network.ResumeDiscovery();
  A2B_CHECK(bench.RunUntilCount(1, 2000));
}

A2B_TEST(ResumeAfterADropWaitsForTheSwitch) {
  // A dropped node's position is probed again no sooner than 250 ms after its switch opened,
  // even if discovery is paused and resumed in between.
  Bench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  bench.network.PauseDiscovery();
  bench.bus.Unplug(1);
  A2B_CHECK(bench.RunUntilCount(1, 2000));
  bench.bus.Plug(test::Ad2437(0x11));
  bench.sim.ClearTrace();
  bench.network.ResumeDiscovery();
  bench.Run(5);
  A2B_CHECK_EQ(test::Count(bench.Accesses(), kMainNode, true, reg::kDiscvry), 0);
  A2B_CHECK(bench.RunUntilCount(2, 2000));
}

A2B_TEST(Scenario12FailedProbeIsAbandonedAndTriedAgain) {
  // A transfer of a probe fails: the probe is given up and tidied up, the chain checked,
  // and the next probe finds the node.
  Bench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntilCount(1, 2000));
  bench.sim.FailAddress(kBusAddr, 1);  // The probe behind sub-node 0 writes it next.
  bench.Run(5);
  A2B_CHECK(bench.Logged("W: a2b: probe for sub-node 1 failed; checking the chain"));
  A2B_CHECK(bench.network.State() != NetworkState::kFailed);
  A2B_CHECK(bench.RunUntilCount(2, 2000));
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 2000));
  A2B_CHECK(bench.listener.Of(EventType::kNodeRemoved).empty());
}

A2B_TEST(Scenario12FailedSetUpIsAbandonedAndTriedAgain) {
  // A register write of a node's set-up fails: the node is switched off, the chain checked,
  // and the next probe finds and configures it; it never counted as in the bus.
  Bench bench;
  bench.PlugChain(1);
  bench.sim.FailRegisterWrite(kBusAddr, reg::kGpioien, 1);
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntilCount(1, 3000));
  A2B_CHECK(bench.Logged("W: a2b: probe for sub-node 0 failed; checking the chain"));
  A2B_CHECK_EQ(bench.listener.Of(EventType::kNodeAdded).size(), 1);
  A2B_CHECK(bench.listener.Of(EventType::kNodeRemoved).empty());
  A2B_CHECK_EQ(CountingNode::removed, 0);  // Its first object never joined.
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kGpioien), 0x7F);
  const std::vector<Access> a = bench.Accesses();
  A2B_CHECK_EQ(Count(a, kMainNode, true, reg::kDiscvry), 2);
}

// Bit error counts.

A2B_TEST(BecntIsClearedEverySecond) {
  Bench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  bench.sim.ClearTrace();
  bench.Run(3500);
  const std::vector<Access> a = bench.Accesses();
  A2B_CHECK_EQ(Count(a, kBroadcast, true, reg::kBecnt, 0), 3);  // Every node, at once.
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kBecnt), 0);
  const int at = Find(a, 0, kBroadcast, true, reg::kBecnt);
  A2B_CHECK(at >= 0 && a[at].nodeadr == reg::kNodeadrBrcst);
}

A2B_TEST(BecntOnTheMainNodeAloneWithoutSubNodes) {
  Bench bench;
  A2B_CHECK(bench.StartWith(0));
  bench.sim.ClearTrace();
  bench.Run(2500);
  const std::vector<Access> a = bench.Accesses();
  A2B_CHECK_EQ(Count(a, kMainNode, true, reg::kBecnt, 0), 2);
  A2B_CHECK_EQ(Count(a, kBroadcast, true, reg::kBecnt), 0);
  A2B_CHECK_EQ(test::FailedTransfers(bench.sim.Trace()), 0);
}

A2B_TEST(BecntWritesCanBeTurnedOff) {
  BoardConfig board = test::MakeBoard();
  board.timings.becntResetMs = 0;
  Bench bench(board);
  bench.PlugChain(1);
  A2B_CHECK(bench.StartWith(1));
  bench.Run(2500);
  const std::vector<Access> a = bench.Accesses();
  A2B_CHECK_EQ(Count(a, kBroadcast, true, reg::kBecnt), 0);
  A2B_CHECK_EQ(Count(a, kMainNode, true, reg::kBecnt), 0);
}

// RejectNode().

A2B_TEST(RejectNodeTakesItAndTheNodesAfterItOut) {
  Bench bench;
  bench.PlugChain(3);
  A2B_CHECK(bench.StartWith(3));
  A2B_CHECK(bench.network.RejectNode(3) == Result::kInvalidArgument);
  A2B_CHECK(bench.network.RejectNode(-1) == Result::kInvalidArgument);
  bench.listener.events.clear();
  A2B_CHECK(bench.network.RejectNode(1) == Result::kOk);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 3);  // At the next Poll().
  bench.Run(1);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kByApp);
  const std::vector<Event> removed = bench.listener.Of(EventType::kNodeRemoved);
  A2B_CHECK(removed.size() == 2 && removed[0].node == 1 && removed[1].node == 2 &&
            removed[0].removal == RemovalReason::kRejected &&
            removed[1].removal == RemovalReason::kRejected);
  A2B_CHECK_EQ(CountingNode::removed, 2);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kSwctl) & reg::kSwctlEnsw, 0);
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 1);
  // Its position is probed again every rejectedProbeMs: the same node is switched off
  // again, without an event and without its module info being read.
  const int calls = bench.source.calls;
  bench.listener.events.clear();
  bench.Run(2 * bench.board.timings.rejectedProbeMs + 100);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK(bench.listener.events.empty());
  A2B_CHECK_EQ(bench.source.calls, calls);
  // A different module plugged in there is taken in.
  bench.bus.Unplug(1);
  bench.PlugChain(1, 0x31);
  A2B_CHECK(bench.RunUntilCount(2, bench.board.timings.rejectedProbeMs + 200));
  const SubNodeInfo* node = bench.network.SubNodeInfoAt(1);
  A2B_CHECK(node != nullptr && node->identity.chipId[5] == 0x31);
}

A2B_TEST(RejectNodeDuringAProbe) {
  // A probe behind the last node is under way: it is abandoned, and the switches are put
  // back before the node is taken out.
  Bench bench;
  bench.PlugChain(1);
  A2B_CHECK(bench.StartWith(1));
  for (int i = 0; i < 1000 && !bench.bus.DiscoveryActive(); ++i) {
    bench.Run(1);
  }
  A2B_CHECK(bench.bus.DiscoveryActive());
  A2B_CHECK(bench.network.RejectNode(0) == Result::kOk);
  bench.Run(1);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 0);
  A2B_CHECK(!bench.bus.DiscoveryActive());                // ENDDSC.
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kSwctl), 0x10);  // Sub-node 0 switched off.
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 0);
  bench.Run(1000);
  A2B_CHECK(bench.network.State() == NetworkState::kRunning);
}

A2B_TEST(RejectedNodeOnItsOwnSupplyIsNotProbedAgain) {
  // A Custom link: the rejected node keeps its power, so it stays discovered, and its
  // position is not probed again until Start() (a known limit; see the roadmap).
  Bench bench;
  bench.PlugChain(1);
  sim::SubNodeSpec own = test::Ad2437(0x77);
  own.chip.productId = 0x10;  // Not supported.
  own.link.power = sim::LinkPower::kSeparateWires;
  bench.bus.Plug(own);
  A2B_CHECK(bench.network.SetLinkProfile(1, LinkProfile::Custom()) == Result::kOk);
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kUnsupportedChip);
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 2);
  bench.sim.ClearTrace();
  bench.Run(2 * bench.board.timings.rejectedProbeMs + 100);
  A2B_CHECK_EQ(Count(bench.Accesses(), kMainNode, true, reg::kDiscvry), 0);
  A2B_CHECK(bench.network.State() == NetworkState::kRunning);
}

A2B_TEST(RejectNodeOnItsOwnSupplyCutsTheNodesAfterIt) {
  Bench bench;
  bench.PlugChain(1);
  sim::SubNodeSpec own = test::Ad2437(0x21);
  own.link.power = sim::LinkPower::kSeparateWires;
  bench.bus.Plug(own);
  bench.PlugChain(1, 0x22);
  A2B_CHECK(bench.network.SetLinkProfile(1, LinkProfile::Custom()) == Result::kOk);
  A2B_CHECK(bench.StartWith(3));
  A2B_CHECK(bench.network.RejectNode(1) == Result::kOk);
  bench.Run(1);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kSwctl) & reg::kSwctlEnsw, 0);  // Its own switch.
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 2);  // It keeps its power; sub-node 2 lost it.
  bench.sim.ClearTrace();
  bench.Run(2 * bench.board.timings.rejectedProbeMs + 100);
  A2B_CHECK_EQ(Count(bench.Accesses(), kMainNode, true, reg::kDiscvry), 0);
}

A2B_TEST(StartForgetsARejectedNode) {
  Bench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  A2B_CHECK(bench.network.RejectNode(1) == Result::kOk);
  bench.Run(1);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kNone);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 3000));
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
