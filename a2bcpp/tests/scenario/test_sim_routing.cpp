/**
 * @file test_sim_routing.cpp
 * @brief Audio routing on the simulator: connections between channel groups,
 *        ApplyRouting(), the plan written once the chain is up and again as it changes, the
 *        response cycles (with each link's cable length), the NEWSTRCT settle, and the
 *        limits. Each routing is checked by following the audio through the simulator's
 *        model of the registers (sim::Bus::TraceAudio(), written apart from the stack's
 *        planner): every sink channel plays its source channel. Scenarios 5 (re-plug
 *        restores routing) and 7 (bandwidth). AD242x nodes (the second customer
 *        system: an AD2428 main node, an AD2428 codec and AD2427 microphone modules; the TDM
 *        order), channels on each node's port, and the policy's channel maps.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdio>
#include <cstring>
#include <utility>
#include <vector>

#include "a2b/network.h"
#include "detail/regdefs.h"
#include "routing_bench.h"
#include "sim/module_info.h"
#include "sim/simulator.h"
#include "sim_bench.h"
#include "test_harness.h"

namespace a2b {
namespace {

using test::Ad2428Board;
using test::CodecImage;
using test::GroupBoard;
using test::GroupImage;
using test::MicrophoneImage;
using test::Plays;
using test::PortImage;
using test::RoutingBench;
using test::Silent;
using test::WideBoard;

A2B_TEST(TwoNodesBothWays) {
  // The bench's first check: the main node's stereo pair down to sub-node 0 and 1, sub-node
  // 0's up to the main node, and sub-node 1's to sub-node 0's second group.
  RoutingBench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  A2B_CHECK(bench.network.AddConnection(kMainNode, 0, 0, 0) == Result::kOk);
  A2B_CHECK(bench.network.AddConnection(0, 0, kMainNode, 0) == Result::kOk);
  A2B_CHECK(bench.network.AddConnection(kMainNode, 0, 1, 0) == Result::kOk);
  A2B_CHECK(bench.network.AddConnection(1, 0, 0, 1) == Result::kOk);
  A2B_CHECK_EQ(bench.network.ConnectionCount(), 4);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(!bench.network.Routing().valid);  // Written at the next poll.
  A2B_CHECK(bench.RunUntilApplied());
  const RoutingPlan& plan = bench.network.Routing();
  A2B_CHECK(plan.valid);
  A2B_CHECK_EQ(plan.routed, 4);
  A2B_CHECK_EQ(plan.subNodeCount, 2);
  const sim::AudioTrace audio = bench.Audio();
  A2B_CHECK(audio.error.empty());
  A2B_CHECK(Plays(audio, 0, 0, -1, 0) && Plays(audio, 0, 1, -1, 1));
  A2B_CHECK(Plays(audio, 1, 0, -1, 0) && Plays(audio, 1, 1, -1, 1));
  A2B_CHECK(Plays(audio, -1, 0, 0, 0) && Plays(audio, -1, 1, 0, 1));
  A2B_CHECK(Plays(audio, 0, 2, 1, 0) && Plays(audio, 0, 3, 1, 1));
  A2B_CHECK(Silent(audio, 0, {0, 1, 2, 3}) && Silent(audio, 1, {0, 1}) &&
            Silent(audio, -1, {0, 1}));
  // Two slots down to both, two up from each.
  A2B_CHECK_EQ(audio.dnSlots[0], 2u);
  A2B_CHECK_EQ(audio.dnSlots[1], 2u);
  A2B_CHECK_EQ(audio.upSlots[1], 2u);
  A2B_CHECK_EQ(audio.upSlots[0], 2u);  // Sub-node 1's are played and dropped there.
  // The response cycles are on every node, and nothing was sent in a NEWSTRCT settle.
  A2B_CHECK_EQ(bench.bus.Main().AppliedReg(reg::kRespcycs), plan.main.respCycles);
  A2B_CHECK_EQ(bench.bus.Sub(0).AppliedReg(reg::kRespcycs), plan.sub[0].respCycles);
  A2B_CHECK_EQ(bench.bus.Sub(1).AppliedReg(reg::kRespcycs), plan.sub[1].respCycles);
  A2B_CHECK(plan.respCycles.fits && plan.main.respCycles >= plan.respCycles.minMain &&
            plan.main.respCycles <= plan.respCycles.maxMain);
  A2B_CHECK_EQ(bench.bus.AccessesInNewStructSettle(), 0u);
  A2B_CHECK(bench.Logged("I: a2b: routing applied, 4 connection(s)"));
}

A2B_TEST(PlanIsWrittenOnceTheChainIsUp) {
  // ApplyRouting() before Start(): the plan comes once the chain is up (kRunning), not
  // while it is still being discovered.
  RoutingBench bench;
  bench.PlugChain(2);
  Connection connection;
  connection.source.mainNode = true;
  connection.sink.chipId[5] = 0x11;  // Sub-node 1, by chip ID.
  A2B_CHECK(bench.network.AddConnection(connection) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntilApplied());
  const std::vector<Event>& events = bench.listener.events;
  size_t running = events.size();
  size_t applied = events.size();
  for (size_t i = 0; i < events.size(); ++i) {
    if (events[i].type == EventType::kStateChanged && events[i].state == NetworkState::kRunning &&
        running == events.size()) {
      running = i;
    }
    if (events[i].type == EventType::kRoutingApplied && applied == events.size()) {
      applied = i;
    }
  }
  A2B_CHECK(running < applied);
  A2B_CHECK_EQ(bench.listener.Of(EventType::kRoutingApplied).size(), 1u);
  const sim::AudioTrace audio = bench.Audio();
  A2B_CHECK(audio.error.empty());
  A2B_CHECK(Plays(audio, 1, 0, -1, 0) && Plays(audio, 1, 1, -1, 1));
  A2B_CHECK(Silent(audio, 0, {}));
  A2B_CHECK_EQ(bench.network.ConnectionNode(connection.sink), 1);
  A2B_CHECK_EQ(bench.bus.AccessesInNewStructSettle(), 0u);
}

A2B_TEST(DataControlAndSlotFormatReachEveryNodeWithEachPlan) {
  // SLOTFMT and DATCTL are auto-broadcast: a sub-node found after the start-up writes has
  // neither, so each plan writes them again (a bench finding). Sub-node 0 is in, routed;
  // sub-node 1 comes later, with the connections to it waiting.
  RoutingBench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  A2B_CHECK(bench.network.AddConnection(kMainNode, 0, 0, 0) == Result::kOk);
  A2B_CHECK(bench.network.AddConnection(kMainNode, 0, 1, 0) == Result::kOk);
  A2B_CHECK(bench.network.AddConnection(1, 0, kMainNode, 0) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied(1));
  for (int node = -1; node < 2; ++node) {
    const sim::Node& n = (node < 0) ? bench.bus.Main() : bench.bus.Sub(static_cast<size_t>(node));
    A2B_CHECK_EQ(n.AppliedReg(reg::kSlotfmt), 0x44);
    A2B_CHECK_EQ(n.AppliedReg(reg::kDatctl), reg::kDatctlDns | reg::kDatctlUps);
  }
  bench.bus.Unplug(1);
  A2B_CHECK(bench.RunUntilApplied(2));
  bench.bus.Plug(test::Ad2437(0x11));
  A2B_CHECK(bench.RunUntilApplied(3));
  A2B_CHECK_EQ(bench.bus.Sub(1).AppliedReg(reg::kSlotfmt), 0x44);
  A2B_CHECK_EQ(bench.bus.Sub(1).AppliedReg(reg::kDatctl), reg::kDatctlDns | reg::kDatctlUps);
  const sim::AudioTrace audio = bench.Audio();
  A2B_CHECK(audio.error.empty());
  A2B_CHECK(Plays(audio, 1, 0, -1, 0) && Plays(audio, 1, 1, -1, 1));
  A2B_CHECK(Plays(audio, -1, 0, 1, 0) && Plays(audio, -1, 1, 1, 1));
}

A2B_TEST(DataControlFromTheImageDisablesADirection) {
  // The image's DATCTL (here downstream only) is what every node gets: nothing comes up.
  BoardConfig board = GroupBoard();
  board.mainNode.Set(reg::kDatctl, reg::kDatctlDns);
  RoutingBench bench(board);
  bench.PlugChain(1);
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK(bench.network.AddConnection(kMainNode, 0, 0, 0) == Result::kOk);
  A2B_CHECK(bench.network.AddConnection(0, 0, kMainNode, 0) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied());
  const sim::AudioTrace audio = bench.Audio();
  A2B_CHECK(audio.error.empty());
  A2B_CHECK(Plays(audio, 0, 0, -1, 0) && Plays(audio, 0, 1, -1, 1));  // Downstream works.
  A2B_CHECK(Silent(audio, -1, {}));                                   // Upstream is off.
}

A2B_TEST(Scenario5ReplugRestoresRouting) {
  RoutingBench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  A2B_CHECK(bench.network.AddConnection(kMainNode, 0, 1, 0) == Result::kOk);
  A2B_CHECK(bench.network.AddConnection(0, 0, kMainNode, 0) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied(1));
  // Sub-node 1 unplugged: the plan is built again for the shorter chain; its connection
  // waits, and the other carries on.
  bench.bus.Unplug(1);
  A2B_CHECK(bench.RunUntilApplied(2));
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK_EQ(bench.network.Routing().routed, 1);
  A2B_CHECK_EQ(bench.network.ConnectionCount(), 2);
  sim::AudioTrace audio = bench.Audio();
  A2B_CHECK(audio.error.empty());
  A2B_CHECK(Plays(audio, -1, 0, 0, 0) && Plays(audio, -1, 1, 0, 1));
  A2B_CHECK_EQ(bench.bus.Main().AppliedReg(reg::kDnslots), 0);  // Nothing goes down now.
  // Plugged back in: found again and routed again.
  bench.bus.Plug(test::Ad2437(0x11));
  A2B_CHECK(bench.RunUntilApplied(3));
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
  A2B_CHECK_EQ(bench.network.Routing().routed, 2);
  audio = bench.Audio();
  A2B_CHECK(audio.error.empty());
  A2B_CHECK(Plays(audio, 1, 0, -1, 0) && Plays(audio, 1, 1, -1, 1));
  A2B_CHECK(Plays(audio, -1, 0, 0, 0) && Plays(audio, -1, 1, 0, 1));
  A2B_CHECK_EQ(bench.bus.AccessesInNewStructSettle(), 0u);
}

A2B_TEST(ConnectionsFollowTheModules) {
  // A connection to the module with chip ID ...11 at position 1; the modules are plugged
  // back the other way round: it goes to position 0.
  RoutingBench bench;
  bench.PlugChain(2);  // ...10, ...11.
  A2B_CHECK(bench.StartWith(2));
  A2B_CHECK(bench.network.AddConnection(kMainNode, 0, 1, 0) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied(1));
  bench.bus.Unplug(0);
  A2B_CHECK(bench.RunUntilCount(0, 2000));
  bench.bus.Plug(test::Ad2437(0x11));
  bench.bus.Plug(test::Ad2437(0x10));
  A2B_CHECK(bench.RunUntilCount(2, 3000));
  bench.Run(1000);
  const sim::AudioTrace audio = bench.Audio();
  A2B_CHECK(audio.error.empty());
  A2B_CHECK(Plays(audio, 0, 0, -1, 0) && Plays(audio, 0, 1, -1, 1));
  A2B_CHECK(Silent(audio, 1, {}));
  A2B_CHECK_EQ(bench.network.ConnectionNode(bench.network.GetConnection(0)->sink), 0);
}

A2B_TEST(Scenario7BandwidthRefused) {
  // 32 channels from the main node to sub-node 1 and 8 from sub-node 0 to it: 40 slots on
  // the link into sub-node 1. ApplyRouting() refuses; nothing is written.
  BoardConfig board = WideBoard();
  board.routing.groups.AddRx(1, 0xFFFFFFFFu);
  RoutingBench bench(board, {GroupImage(0xFF, 0x3, 0xC), GroupImage(0x3, 0xFFFFFFFFu, 0xFF00)});
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  A2B_CHECK(bench.network.AddConnection(kMainNode, 1, 1, 0) == Result::kOk);
  A2B_CHECK(bench.network.AddConnection(0, 0, 1, 1) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kTooManySlots);
  bench.Run(200);
  A2B_CHECK(!bench.network.Routing().valid);
  A2B_CHECK_EQ(bench.bus.NewStructCount(), 1u);  // Sub-node 0's probe only.
  // Without the second, it fits.
  A2B_CHECK(bench.network.RemoveConnection(0, 0, 1, 1) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied());
  const sim::AudioTrace audio = bench.Audio();
  A2B_CHECK(audio.error.empty());
  A2B_CHECK_EQ(audio.dnSlots[1], 32u);
  A2B_CHECK(Plays(audio, 1, 31, -1, 31));
}

A2B_TEST(Scenario7NodeThatBreaksTheBudget) {
  // The connections wait for sub-node 1; when it is plugged in, the plan is over 32 slots:
  // kRoutingFailed, and the plan written before stays.
  BoardConfig board = WideBoard();
  board.routing.groups.AddRx(1, 0xFFFFFFFFu);
  RoutingBench bench(board, {GroupImage(0xFF, 0x3, 0xC), GroupImage(0x3, 0xFFFFFFFFu, 0xFF00)});
  bench.PlugChain(1);
  A2B_CHECK(bench.StartWith(1));
  Connection big;
  big.source.mainNode = true;
  big.source.group = 1;
  big.sink.chipId[5] = 0x11;
  Connection eight;
  eight.source.chipId[5] = 0x10;
  eight.sink.chipId[5] = 0x11;
  eight.sink.group = 1;
  A2B_CHECK(bench.network.AddConnection(big) == Result::kOk);
  A2B_CHECK(bench.network.AddConnection(eight) == Result::kOk);
  A2B_CHECK(bench.network.AddConnection(kMainNode, 0, 0, 0) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied());
  A2B_CHECK_EQ(bench.network.Routing().routed, 1);
  bench.bus.Plug(test::Ad2437(0x11));
  A2B_CHECK(bench.RunUntilCount(2, 2000));
  bench.Run(1000);
  const std::vector<Event> failed = bench.listener.Of(EventType::kRoutingFailed);
  A2B_CHECK_EQ(failed.size(), 1u);
  A2B_CHECK(!failed.empty() && failed[0].routingError == RoutingError::kTooManySlots &&
            failed[0].node == 0);
  A2B_CHECK_EQ(bench.network.Routing().subNodeCount, 1u);  // The last plan written.
  const sim::AudioTrace audio = bench.Audio();
  A2B_CHECK(Plays(audio, 0, 0, -1, 0) && Plays(audio, 0, 1, -1, 1));
}

A2B_TEST(Scenario7NoResponseCycleWindow) {
  // 32 slots of 32 bits each way on one link don't fit a superframe.
  BoardConfig board = WideBoard();
  board.mainNode.Set(reg::kSlotfmt, 0x66);  // 32-bit slots.
  board.routing.groups.AddRx(1, 0xFFFFFFFFu);
  board.routing.groups.AddTx(1, 0xFFFFFFFFu);
  RoutingBench bench(board, {GroupImage(0xFFFFFFFFu, 0xFFFFFFFFu, 0x0)});
  bench.PlugChain(1);
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK(bench.network.AddConnection(kMainNode, 1, 0, 0) == Result::kOk);
  A2B_CHECK(bench.network.AddConnection(0, 0, kMainNode, 1) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNoResponseCycles);
  bench.network.ClearConnections();
  A2B_CHECK_EQ(bench.network.ConnectionCount(), 0);
}

A2B_TEST(LaterProbesStartFromThePlansResponseCycles) {
  // Once a plan is written, a new node's DISCVRY is the TRMs' value for its position from
  // the plan's main-node RESPCYCS, not from the image's.
  RoutingBench bench;
  bench.PlugChain(1);
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK(bench.network.AddConnection(kMainNode, 0, 0, 0) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied());
  const uint8_t main = bench.network.Routing().main.respCycles;
  A2B_CHECK(main != 0xB9);
  RespCycleInput input;
  input.subNodeCount = 2;
  input.mainRespCycles = main;
  input.cableDm[0] = input.cableDm[1] = 40;
  const uint8_t expected = ComputeResponseCycles(input).sub[1];
  bench.sim.ClearTrace();
  bench.bus.Plug(test::Ad2437(0x11));
  A2B_CHECK(bench.RunUntilApplied(2));
  const std::vector<test::Access> a = bench.Accesses();
  A2B_CHECK_EQ(test::Count(a, kMainNode, true, reg::kDiscvry, expected), 1);
  A2B_CHECK_EQ(bench.bus.Sub(1).AppliedReg(reg::kRespcycs),
               bench.network.Routing().sub[1].respCycles);
}

A2B_TEST(CableLengthPerLink) {
  // The board's 4 m on every link unless the app gives a link its own: the plan's response
  // cycles and a later probe's DISCVRY take it, and a routed bus is planned again at once.
  RoutingBench bench;
  bench.PlugChain(1);
  A2B_CHECK(bench.StartWith(1));
  Network& network = bench.network;
  A2B_CHECK_EQ(network.CableLength(0), 40);
  A2B_CHECK_EQ(network.CableLength(15), 40);
  A2B_CHECK_EQ(network.CableLength(16), 0);
  A2B_CHECK(network.SetCableLength(16, 40) == Result::kInvalidArgument);
  A2B_CHECK(network.SetCableLength(kMainNode, 40) == Result::kInvalidArgument);
  A2B_CHECK(network.SetCableLength(0, 0) == Result::kInvalidArgument);
  A2B_CHECK(network.SetCableLength(0, kMaxCableDm + 1) == Result::kInvalidArgument);
  A2B_CHECK(network.ClearCableLength(16) == Result::kInvalidArgument);
  A2B_CHECK(network.AddConnection(kMainNode, 0, 0, 0) == Result::kOk);
  A2B_CHECK(network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied(1));
  // Two 24-bit slots down to sub-node 0, none up (RESPOFFS 254), over a link of
  // @p cableDm: the plan's response cycles.
  auto expected = [](uint16_t cableDm) {
    RespCycleInput input;
    input.subNodeCount = 1;
    input.dnSlots[0] = 2;
    input.respOffset = 254;
    input.cableDm[0] = cableDm;
    return ComputeResponseCycles(input);
  };
  A2B_CHECK_EQ(network.Routing().respCycles.maxMain, expected(40).maxMain);
  // 15 m: the upstream limit comes down (2 x 6 ns x 15 m is 8.8 bits, against 2.4 for 4 m),
  // and the plan is written again with it.
  A2B_CHECK(network.SetCableLength(0, 150) == Result::kOk);
  A2B_CHECK_EQ(network.CableLength(0), 150);
  A2B_CHECK(bench.RunUntilApplied(2));
  const ResponseCycles longer = expected(150);
  A2B_CHECK(longer.maxMain < expected(40).maxMain);
  A2B_CHECK_EQ(network.Routing().respCycles.maxMain, longer.maxMain);
  A2B_CHECK_EQ(network.Routing().main.respCycles, longer.main);
  A2B_CHECK_EQ(bench.bus.Main().AppliedReg(reg::kRespcycs), longer.main);
  // A 30 m link into position 1: the next probe's DISCVRY takes both cables.
  A2B_CHECK(network.SetCableLength(1, kMaxCableDm) == Result::kOk);
  RespCycleInput input;
  input.subNodeCount = 2;
  input.mainRespCycles = longer.main;
  input.cableDm[0] = 150;
  input.cableDm[1] = kMaxCableDm;
  const uint8_t discvry = ComputeResponseCycles(input).sub[1];
  input.cableDm[1] = 40;
  A2B_CHECK(discvry != ComputeResponseCycles(input).sub[1]);
  bench.sim.ClearTrace();
  bench.bus.Plug(test::Ad2437(0x11));
  A2B_CHECK(bench.RunUntilCount(2, 3000));
  A2B_CHECK_EQ(test::Count(bench.Accesses(), kMainNode, true, reg::kDiscvry, discvry), 1);
  // Back to the board's: written again.
  const size_t applied = bench.listener.Of(EventType::kRoutingApplied).size();
  A2B_CHECK(network.ClearCableLength(0) == Result::kOk);
  A2B_CHECK_EQ(network.CableLength(0), 40);
  A2B_CHECK(bench.RunUntilApplied(applied + 1));
  A2B_CHECK_EQ(bench.bus.AccessesInNewStructSettle(), 0u);
}

/** A policy with its own response cycles: the main node at 0x90, 5 less a position. */
class FixedPolicy : public RoutingPolicy {
 public:
  ResponseCycles RespCycles(const RespCycleInput& input) const override {
    ResponseCycles cycles;
    cycles.fits = true;
    cycles.main = (input.mainRespCycles != 0) ? input.mainRespCycles : 0x90;
    for (size_t n = 0; n < input.subNodeCount; ++n) {
      cycles.sub[n] = static_cast<uint8_t>(cycles.main - 5 * n);
    }
    ++calls;
    return cycles;
  }
  mutable int calls = 0;
};

A2B_TEST(PolicyGivesTheResponseCycles) {
  FixedPolicy policy;
  BoardConfig board = GroupBoard();
  board.routing.policy = &policy;
  RoutingBench bench(board);
  bench.PlugChain(1);
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK(policy.calls >= 2);  // The probes of positions 0 and 1.
  A2B_CHECK(bench.network.AddConnection(kMainNode, 0, 0, 0) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied());
  A2B_CHECK_EQ(bench.bus.Main().AppliedReg(reg::kRespcycs), 0x90);
  bench.sim.ClearTrace();
  bench.bus.Plug(test::Ad2437(0x11));
  A2B_CHECK(bench.RunUntilApplied(2));
  A2B_CHECK_EQ(test::Count(bench.Accesses(), kMainNode, true, reg::kDiscvry, 0x90 - 5), 1);
  A2B_CHECK_EQ(bench.bus.Sub(1).AppliedReg(reg::kRespcycs), 0x90 - 5);
}

A2B_TEST(SecondCustomerSystem) {
  // An AD2428 main node, an AD2428 codec module at sub-node 0 and two AD2427 microphone
  // modules after it: stereo down to the codec; the codec and the microphones up to the main
  // node. No crossbar is written (the audio model counts a crossbar register written on an
  // AD242x as an error).
  RoutingBench bench(Ad2428Board(), {CodecImage(), MicrophoneImage()}, test::ChipOf(0x28));
  bench.bus.Plug(test::SubOf(0x28, 0x10));
  bench.bus.Plug(test::SubOf(0x27, 0x11));
  bench.bus.Plug(test::SubOf(0x27, 0x12));
  A2B_CHECK(bench.StartWith(3));
  A2B_CHECK(bench.network.AddConnection(kMainNode, 0, 0, 0) == Result::kOk);
  A2B_CHECK(bench.network.AddConnection(2, 0, kMainNode, 0) == Result::kOk);
  A2B_CHECK(bench.network.AddConnection(1, 0, kMainNode, 1) == Result::kOk);
  A2B_CHECK(bench.network.AddConnection(0, 0, kMainNode, 2) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied());
  const sim::AudioTrace audio = bench.Audio();
  A2B_CHECK(audio.error.empty());
  if (!audio.error.empty()) {
    std::printf("  %s\n", audio.error.c_str());
  }
  A2B_CHECK(Plays(audio, 0, 0, -1, 0) && Plays(audio, 0, 1, -1, 1));
  for (unsigned c = 0; c < 4; ++c) {
    A2B_CHECK(Plays(audio, -1, c, 2, c));
    A2B_CHECK(Plays(audio, -1, 4 + c, 1, c));
  }
  A2B_CHECK(Plays(audio, -1, 8, 0, 0) && Plays(audio, -1, 9, 0, 1));
  A2B_CHECK(Silent(audio, 0, {0, 1}) && Silent(audio, -1, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}));
  A2B_CHECK_EQ(audio.upSlots[0], 10u);
  A2B_CHECK_EQ(bench.network.Routing().main.upSlots, 10);
  A2B_CHECK_EQ(bench.bus.AccessesInNewStructSettle(), 0u);
}

A2B_TEST(Ad242xChannelsOutOfBusOrderAreRefused) {
  // As SecondCustomerSystem with the microphone modules' connections the other way round:
  // sub-node 2's slots reach the main node first, so they can't be its TX 4-7. Refused at
  // once, and again, with the node at fault, when another module joins.
  RoutingBench bench(Ad2428Board(), {CodecImage(), MicrophoneImage()}, test::ChipOf(0x28));
  bench.bus.Plug(test::SubOf(0x28, 0x10));
  bench.bus.Plug(test::SubOf(0x27, 0x11));
  bench.bus.Plug(test::SubOf(0x27, 0x12));
  A2B_CHECK(bench.StartWith(3));
  A2B_CHECK(bench.network.AddConnection(1, 0, kMainNode, 0) == Result::kOk);
  A2B_CHECK(bench.network.AddConnection(2, 0, kMainNode, 1) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kChannelOrder);
  bench.bus.Plug(test::SubOf(0x27, 0x13));
  A2B_CHECK(bench.RunUntilCount(4));
  bench.Run(1000);
  const std::vector<Event> failed = bench.listener.Of(EventType::kRoutingFailed);
  A2B_CHECK(!failed.empty() && failed.back().routingError == RoutingError::kChannelOrder &&
            failed.back().node == kMainNode);
  A2B_CHECK(!bench.network.Routing().valid);
}

/** Says every node's port gives 32 channels each way, whatever the stack wrote to it. */
class WidePortPolicy : public RoutingPolicy {
 public:
  ChannelMap MapGroups(const PortSettings& port) const override {
    ChannelMap map = ComputeChannelMap(port);
    map.rxChannels = 32;
    map.txChannels = 32;
    ++calls;
    return map;
  }
  mutable int calls = 0;
};

A2B_TEST(PortOffIsRefusedUnlessThePolicySaysOtherwise) {
  // A module image with groups but its I2S port off (the console's generic image before
  // the connection is refused, at the sink. An app that turns the port on
  // itself (here a raw write) says so through RoutingPolicy::MapGroups().
  ChannelGroups groups;
  groups.AddRx(0, 0x3);
  groups.AddTx(0, 0x3);
  RoutingBench bench(GroupBoard(), {PortImage(sim::ModuleInfoRegs(), groups)});
  bench.PlugChain(1);
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK(bench.network.AddConnection(kMainNode, 0, 0, 0) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNoSuchChannel);
  WidePortPolicy policy;
  bench.board.routing.policy = &policy;
  const uint8_t i2scfg = 0x11;
  A2B_CHECK(bench.network.WriteRegisters(0, reg::kI2scfg, &i2scfg, 1) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(policy.calls >= 2);
  A2B_CHECK(bench.RunUntilApplied());
  A2B_CHECK(Plays(bench.Audio(), 0, 0, -1, 0) && Plays(bench.Audio(), 0, 1, -1, 1));
}

A2B_TEST(NodeInNoConnectionNeedsNoCrossbar) {
  // An AD2428 in an AD2437 chain, in no connection: the chain is routed (it was once refused).
  // Mixed chains are in test_sim_mixed_routing.
  RoutingBench bench;
  bench.bus.Plug(test::Ad2437(0x10));
  bench.bus.Plug(test::SubOf(0x28, 0x11));
  A2B_CHECK(bench.StartWith(2));
  A2B_CHECK(bench.network.AddConnection(kMainNode, 0, 0, 0) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied());
  const sim::AudioTrace audio = bench.Audio();
  A2B_CHECK(audio.error.empty());
  A2B_CHECK(Plays(audio, 0, 0, -1, 0) && Plays(audio, 0, 1, -1, 1));
}

A2B_TEST(FailedWriteIsTriedAgain) {
  RoutingBench bench;
  bench.PlugChain(1);
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK(bench.network.AddConnection(kMainNode, 0, 0, 0) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  bench.sim.FailAddress(test::kBusAddr, 1);  // Sub-node 0's slot registers.
  A2B_CHECK(bench.RunUntilApplied());
  const std::vector<Event> failed = bench.listener.Of(EventType::kRoutingFailed);
  A2B_CHECK_EQ(failed.size(), 1u);
  A2B_CHECK(!failed.empty() && failed[0].routingError == RoutingError::kI2cError);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);  // The chain check found it there.
  A2B_CHECK(Plays(bench.Audio(), 0, 0, -1, 0));
}

A2B_TEST(RestartRoutesAgain) {
  // A SYNC break: the main node relocks and the bus starts again; once the chain is up the
  // plan is written again.
  RoutingBench bench;
  bench.PlugChain(1);
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK(bench.network.AddConnection(kMainNode, 0, 0, 0) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied(1));
  bench.bus.SetSync(false);
  bench.Run(5);
  bench.bus.SetSync(true);
  A2B_CHECK(bench.RunUntilApplied(2, 5000));
  A2B_CHECK(Plays(bench.Audio(), 0, 0, -1, 0));
  A2B_CHECK_EQ(bench.bus.AccessesInNewStructSettle(), 0u);
}

A2B_TEST(ConnectionCalls) {
  RoutingBench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  Network& network = bench.network;
  // By position: nodes in the bus, groups they have, two different nodes.
  A2B_CHECK(network.AddConnection(kMainNode, 0, 2, 0) == Result::kInvalidArgument);
  A2B_CHECK(network.AddConnection(kMainNode, 1, 0, 0) == Result::kInvalidArgument);  // No RX 1.
  A2B_CHECK(network.AddConnection(kMainNode, 0, 0, 7) == Result::kInvalidArgument);  // No TX 7.
  A2B_CHECK(network.AddConnection(0, 0, 0, 1) == Result::kInvalidArgument);
  A2B_CHECK(network.AddConnection(kMainNode, 0, 0, 0) == Result::kOk);
  A2B_CHECK(network.AddConnection(kMainNode, 0, 1, 0) == Result::kOk);
  // A sink takes one source: sub-node 1's TX 0 from sub-node 0 replaces the main node.
  A2B_CHECK(network.AddConnection(0, 0, 1, 0) == Result::kOk);
  A2B_CHECK_EQ(network.ConnectionCount(), 2);
  const Connection* second = network.GetConnection(1);
  A2B_CHECK(second != nullptr && !second->source.mainNode && second->source.chipId[5] == 0x10);
  A2B_CHECK(network.GetConnection(2) == nullptr);
  // By chip ID: anything but the same node twice.
  Connection same;
  same.source.chipId[5] = 0x44;
  same.sink.chipId[5] = 0x44;
  A2B_CHECK(network.AddConnection(same) == Result::kInvalidArgument);
  Connection mains;
  mains.source.mainNode = true;
  mains.sink.mainNode = true;
  A2B_CHECK(network.AddConnection(mains) == Result::kInvalidArgument);
  Connection waiting;
  waiting.source.mainNode = true;
  waiting.sink.chipId[5] = 0x44;  // Not in the bus: kept, waits.
  A2B_CHECK(network.AddConnection(waiting) == Result::kOk);
  A2B_CHECK_EQ(network.ConnectionNode(waiting.sink), kNoNode);
  A2B_CHECK_EQ(network.ConnectionNode(waiting.source), kMainNode);
  // Removing.
  A2B_CHECK(network.RemoveConnection(kMainNode, 0, 1, 0) == Result::kInvalidArgument);  // Gone.
  A2B_CHECK(network.RemoveConnection(0, 0, 1, 0) == Result::kOk);
  A2B_CHECK(network.RemoveConnection(waiting) == Result::kOk);
  A2B_CHECK_EQ(network.ConnectionCount(), 1);
  network.ClearConnections();
  A2B_CHECK_EQ(network.ConnectionCount(), 0);
  // An empty plan: no slots anywhere.
  A2B_CHECK(network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied());
  const sim::AudioTrace audio = bench.Audio();
  A2B_CHECK(audio.error.empty());
  A2B_CHECK(Silent(audio, -1, {}) && Silent(audio, 0, {}) && Silent(audio, 1, {}));
  A2B_CHECK_EQ(bench.network.Routing().routed, 0);
}

/** A stream from @p source's RX group @p group to the sinks' TX group 0. */
AudioStream StreamTo(NodeAddr source, uint8_t group, std::vector<NodeAddr> sinks) {
  AudioStream stream;
  stream.source = source;
  stream.sourceGroup = group;
  for (NodeAddr sink : sinks) {
    stream.sinks[stream.sinkCount].node = sink;
    stream.sinks[stream.sinkCount].group = 0;
    ++stream.sinkCount;
  }
  return stream;
}

A2B_TEST(AudioRoutesAreSetAllOrNothing) {
  // ADI's SetAudioRt: streams from a source group to several sinks; all or nothing.
  RoutingBench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  Network& network = bench.network;
  const AudioStream streams[] = {StreamTo(kMainNode, 0, {0, 1}), StreamTo(0, 0, {kMainNode})};
  A2B_CHECK(network.SetAudioRoutes(streams, 2, false) == Result::kOk);
  A2B_CHECK_EQ(network.ConnectionCount(), 3);
  A2B_CHECK(network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied());
  sim::AudioTrace audio = bench.Audio();
  A2B_CHECK(audio.error.empty());
  A2B_CHECK(Plays(audio, 0, 0, -1, 0) && Plays(audio, 1, 1, -1, 1) && Plays(audio, -1, 0, 0, 0));
  // A wrong stream anywhere leaves the connections as they were.
  AudioStream noSinks = StreamTo(kMainNode, 0, {});
  AudioStream toItself = StreamTo(0, 0, {0});
  AudioStream notInBus = StreamTo(kMainNode, 0, {2});
  AudioStream noGroup = StreamTo(kMainNode, 5, {0});
  AudioStream noSinkGroup = StreamTo(kMainNode, 0, {0});
  noSinkGroup.sinks[0].group = 7;
  const AudioStream good = StreamTo(1, 0, {kMainNode});
  for (const AudioStream& bad : {noSinks, toItself, notInBus, noGroup, noSinkGroup}) {
    const AudioStream list[] = {good, bad};
    A2B_CHECK(network.SetAudioRoutes(list, 2, false) == Result::kInvalidArgument);
    A2B_CHECK(network.SetAudioRoutes(list, 2, true) == Result::kInvalidArgument);
    A2B_CHECK_EQ(network.ConnectionCount(), 3);
  }
  A2B_CHECK(network.SetAudioRoutes(nullptr, 1, false) == Result::kInvalidArgument);
  // Appended: sub-node 1 to the main node's TX group 0 replaces sub-node 0's (a sink plays
  // one source).
  A2B_CHECK(network.SetAudioRoutes(&good, 1, true) == Result::kOk);
  A2B_CHECK_EQ(network.ConnectionCount(), 3);
  A2B_CHECK(network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied(2));
  audio = bench.Audio();
  A2B_CHECK(Plays(audio, -1, 0, 1, 0) && Plays(audio, 0, 0, -1, 0));
  // Replaced: no streams at all is a new, empty routing.
  A2B_CHECK(network.SetAudioRoutes(nullptr, 0, false) == Result::kOk);
  A2B_CHECK_EQ(network.ConnectionCount(), 0);
}

A2B_TEST(AudioRoutesRespectTheConnectionLimit) {
  // The table full (32, by chip ID, most for modules that aren't in the bus): a stream with
  // a new sink doesn't fit and changes nothing; one whose sink is in the table replaces it.
  RoutingBench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  Network& network = bench.network;
  std::vector<Connection> full(kMaxConnections);
  for (size_t i = 0; i < full.size(); ++i) {
    full[i].source.mainNode = true;
    full[i].sink.chipId[4] = 0x01;  // Modules that aren't there ...
    full[i].sink.chipId[5] = static_cast<uint8_t>(i);
  }
  full[0].sink.chipId[4] = 0;
  full[0].sink.chipId[5] = 0x10;  // ... except sub-node 0.
  A2B_CHECK(network.ImportConnections(full.data(), full.size()) == Result::kOk);
  A2B_CHECK_EQ(network.ConnectionCount(), kMaxConnections);
  const AudioStream toNew = StreamTo(kMainNode, 0, {1});
  A2B_CHECK(network.SetAudioRoutes(&toNew, 1, true) == Result::kNoSpace);
  A2B_CHECK_EQ(network.ConnectionCount(), kMaxConnections);
  const AudioStream toKnown = StreamTo(1, 0, {0});  // Sub-node 0's TX group 0 again.
  A2B_CHECK(network.SetAudioRoutes(&toKnown, 1, true) == Result::kOk);
  A2B_CHECK_EQ(network.ConnectionCount(), kMaxConnections);
  const Connection* first = network.GetConnection(0);
  A2B_CHECK(first != nullptr && !first->source.mainNode && first->source.chipId[5] == 0x11);
  // Streams that together make more than the limit, from an empty table.
  const AudioStream both[] = {StreamTo(kMainNode, 0, {0, 1}), StreamTo(0, 0, {kMainNode})};
  A2B_CHECK(network.SetAudioRoutes(both, 2, false) == Result::kOk);
  A2B_CHECK_EQ(network.ConnectionCount(), 3);
}

/** True if two connection ends are the same node and group. */
bool SameEnd(const ConnectionEnd& a, const ConnectionEnd& b) {
  return a.mainNode == b.mainNode && a.group == b.group &&
         (a.mainNode || std::memcmp(a.chipId, b.chipId, sizeof(a.chipId)) == 0);
}

A2B_TEST(ConnectionsAreImportedFromASavedList) {
  RoutingBench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  Network& network = bench.network;
  A2B_CHECK(network.AddConnection(kMainNode, 0, 0, 0) == Result::kOk);
  A2B_CHECK(network.AddConnection(0, 0, kMainNode, 0) == Result::kOk);
  A2B_CHECK(network.AddConnection(kMainNode, 0, 1, 0) == Result::kOk);
  std::vector<Connection> saved;
  for (size_t i = 0; i < network.ConnectionCount(); ++i) {
    saved.push_back(*network.GetConnection(i));
  }
  // Wrong: nothing changes.
  Connection toItself;
  toItself.source.chipId[5] = 0x10;
  toItself.sink.chipId[5] = 0x10;
  std::vector<Connection> bad = saved;
  bad.push_back(toItself);
  A2B_CHECK(network.ImportConnections(bad.data(), bad.size()) == Result::kInvalidArgument);
  A2B_CHECK_EQ(network.ConnectionCount(), 3);
  std::vector<Connection> tooMany(kMaxConnections + 1, saved[0]);
  A2B_CHECK(network.ImportConnections(tooMany.data(), tooMany.size()) == Result::kNoSpace);
  A2B_CHECK_EQ(network.ConnectionCount(), 3);
  A2B_CHECK(network.ImportConnections(nullptr, 1) == Result::kInvalidArgument);
  // Cleared and imported again: the same connections, routed the same.
  network.ClearConnections();
  A2B_CHECK(network.ImportConnections(saved.data(), saved.size()) == Result::kOk);
  A2B_CHECK_EQ(network.ConnectionCount(), 3);
  for (size_t i = 0; i < saved.size(); ++i) {
    const Connection* c = network.GetConnection(i);
    A2B_CHECK(c != nullptr && SameEnd(c->source, saved[i].source) &&
              SameEnd(c->sink, saved[i].sink));
  }
  A2B_CHECK_EQ(network.ConnectionNode(saved[2].sink), 1);
  A2B_CHECK(network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied());
  const sim::AudioTrace audio = bench.Audio();
  A2B_CHECK(audio.error.empty());
  A2B_CHECK(Plays(audio, 0, 0, -1, 0) && Plays(audio, 1, 0, -1, 0) && Plays(audio, -1, 1, 0, 1));
  // An empty list removes every connection.
  A2B_CHECK(network.ImportConnections(nullptr, 0) == Result::kOk);
  A2B_CHECK_EQ(network.ConnectionCount(), 0);
}

A2B_TEST(SharedChipIdsGoByPosition) {
  // Two modules with the same chip ID (zeros): a connection added by position stays with
  // that position.
  RoutingBench bench;
  bench.bus.Plug(test::Ad2437(0x00));
  bench.bus.Plug(test::Ad2437(0x00));
  A2B_CHECK(bench.StartWith(2));
  A2B_CHECK(bench.network.AddConnection(kMainNode, 0, 1, 0) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied());
  const sim::AudioTrace audio = bench.Audio();
  A2B_CHECK(Plays(audio, 1, 0, -1, 0) && Silent(audio, 0, {}));
}

/** A bench module's image with I2SRATE @p i2srate (5: 2x, 6: 4x) and RX 0 and TX 0 on
    @p channels; its port I2S (TDM2) on DTX0 and DRX0, so 2, 4 or 8 channels a superframe
    each way. */
std::vector<uint8_t> RateImage(uint8_t i2srate, uint32_t channels) {
  sim::ModuleInfoRegs regs;
  regs.i2scfg = 0x11;
  regs.i2srate = i2srate;
  ChannelGroups groups;
  groups.AddRx(0, channels);
  groups.AddTx(0, channels);
  return PortImage(regs, groups);
}

A2B_TEST(IncreasedRateComesFromTheImage) {
  // Two sub-nodes whose images ask for 2x (96 kHz) and then 4x (192 kHz): the register is
  // written, and a stereo stream between them is two (four) sample frames, so 4 (8) slots a
  // superframe and as many channels in the group.
  const struct {
    uint8_t i2srate;
    uint32_t channels;
    unsigned rate;
  } kCases[] = {{0x05, 0x0F, 2}, {0x06, 0xFF, 4}};
  for (const auto& testCase : kCases) {
    RoutingBench bench(GroupBoard(), {RateImage(testCase.i2srate, testCase.channels)});
    bench.PlugChain(2);
    A2B_CHECK(bench.StartWith(2));
    A2B_CHECK(bench.network.AddConnection(0, 0, 1, 0) == Result::kOk);
    A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
    A2B_CHECK(bench.RunUntilApplied());
    A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kI2srate), testCase.i2srate);
    A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kI2srate), testCase.i2srate);
    A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kI2srate), 0x00);  // The main node's port: 1x.
    const sim::AudioTrace audio = bench.Audio();
    A2B_CHECK(audio.error.empty());
    A2B_CHECK_EQ(audio.Rate(-1), 1u);
    A2B_CHECK_EQ(audio.Rate(0), testCase.rate);
    A2B_CHECK_EQ(audio.Rate(1), testCase.rate);
    const unsigned channelCount = 2 * testCase.rate;
    for (unsigned c = 0; c < channelCount; ++c) {
      A2B_CHECK(Plays(audio, 1, c, 0, c));
    }
    std::vector<unsigned> used;
    for (unsigned c = 0; c < channelCount; ++c) {
      used.push_back(c);
    }
    A2B_CHECK(Silent(audio, 1, used) && Silent(audio, 0, {}) && Silent(audio, -1, {}));
    A2B_CHECK_EQ(audio.dnSlots[1], static_cast<size_t>(channelCount));
    A2B_CHECK_EQ(audio.dnSlots[0], 0u);
    A2B_CHECK(bench.network.Routing().respCycles.fits);
  }
}

A2B_TEST(NoImageRateMeansTheSuperframeRate) {
  // The usual images say nothing about I2SRATE (and a module without an EEPROM has none):
  // 48 kHz, the register at its power-on value.
  RoutingBench bench;
  bench.PlugChain(1);
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK(bench.network.AddConnection(kMainNode, 0, 0, 0) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied());
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kI2srate), 0x00);
  const sim::AudioTrace audio = bench.Audio();
  A2B_CHECK(audio.error.empty());
  A2B_CHECK_EQ(audio.Rate(0), 1u);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kI2ccfg) & reg::kI2ccfgFramerate, 0);
}

A2B_TEST(ReducedRateImageIsRefused) {
  // A reduced rate (I2SRATE.REDUCE here) is out of scope, so the module is rejected.
  RoutingBench bench(GroupBoard(), {RateImage(0x45, 0x3)});
  bench.PlugChain(1);
  A2B_CHECK(bench.network.Start() == Result::kOk);
  bench.Run(2000);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 0);
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kBadModuleInfo);
}

A2B_TEST(FrameRate441SetsTheSubNodesI2cClock) {
  // A 44.1 kHz bus: every sub-node gets I2CCFG.FRAMERATE, and the routing still works.
  BoardConfig board = GroupBoard();
  board.routing.frameRateHz = 44100;
  RoutingBench bench(board);
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kI2ccfg) & reg::kI2ccfgFramerate, reg::kI2ccfgFramerate);
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kI2ccfg) & reg::kI2ccfgFramerate, reg::kI2ccfgFramerate);
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kI2ccfg) & reg::kI2ccfgFramerate, 0);
  A2B_CHECK(bench.network.AddConnection(kMainNode, 0, 1, 0) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied());
  const sim::AudioTrace audio = bench.Audio();
  A2B_CHECK(audio.error.empty());
  A2B_CHECK(Plays(audio, 1, 0, -1, 0) && Plays(audio, 1, 1, -1, 1));
}

A2B_TEST(DiscoverySettlesAfterNewstrct) {
  // Sub-node 0's probe writes NEWSTRCT; nothing goes to the bus for the next millisecond.
  RoutingBench bench;
  bench.PlugChain(3);
  A2B_CHECK(bench.StartWith(3));
  bench.Run(2000);  // More probes at the end of the chain.
  A2B_CHECK(bench.bus.NewStructCount() >= 1u);
  A2B_CHECK_EQ(bench.bus.AccessesInNewStructSettle(), 0u);
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
