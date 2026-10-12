/**
 * @file test_sim_mixed_routing.cpp
 * @brief Simulator scenario 11 with routing: chains that mix AD243x nodes,
 *        routed through their crossbars, with AD242x nodes, which play and send their
 *        channels in TDM order, under each main node that can be one (AD2428, AD2433,
 *        AD2437; the AD2427 is not main-node capable, scenario 11). Crossbar nodes before and
 *        after a TDM node, the TDM-order rules with a crossbar node in the middle, the local
 *        and whole-bus mutes, and the chain changing while routed.
 *
 * Chip facts, from docs/ADI_chips (pdftotext):
 * - The AD242x has the AD243x's slot registers and passing on, without RXMASK and TXXBAR
 *   (AD242x TRM, "Managing A2B System Data Flow").
 * - A node passes slots on in the order it receives them, before its own (AD2437 TRM, "Slot
 *   Register Configuration"). A crossbar picks the node's own channels (RXMASK, in channel
 *   order) and places what the node plays (TXXBAR), so it doesn't reorder the slots that
 *   another node plays: the TDM order of an AD242x sink holds with a crossbar node anywhere.
 * - RXMASK and TXXBAR take effect at once, the slot registers at NEWSTRCT (AD2437 TRM,
 *   "I2S/TDM Flexible Mapping"), so a new RXMASK can reach an AD242x sink, which has nothing
 *   to mute locally, before the new slots do (the local mute's rule, network.cpp).
 * - SLOTFMT means the same on both families (parity; ECC or floating point with the format
 *   bit), and the response cycles' Formula B covers "AD2426/27/28/3x or mixed" systems
 *   (AD2431-35 TRM, Table 6-10), so neither depends on the mix.
 *
 * On the bench: the AB0020 boards (AD2428 and AD2437 modules) and the AB0204 are routed as in
 * docs/bench_console.md ("A mixed chain"). TODO(test on hardware): an AD2433 in a routed chain.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <cstdio>
#include <vector>

#include "a2b/network.h"
#include "routing_bench.h"
#include "sim/simulator.h"
#include "sim_bench.h"
#include "test_harness.h"

namespace a2b {
namespace test {
namespace {

/** The main-node capable chips. */
constexpr uint8_t kMainChips[] = {0x28, 0x33, 0x37};

/** A routed bench with an @p mainProduct main node (PairBoard()) and sub-nodes of
    @p products, plugged in with chip IDs ending 0x10, 0x11, ...; each module has PairImage(),
    or MicPairImage() on an AD2427, and @p more gives the images of positions after them. */
struct MixedBench : RoutingBench {
  MixedBench(uint8_t mainProduct, const std::vector<uint8_t>& products,
             const std::vector<uint8_t>& more = {})
      : RoutingBench(PairBoard(mainProduct), Images(products, more), ChipOf(mainProduct)) {
    for (size_t i = 0; i < products.size(); ++i) {
      bus.Plug(SubOf(products[i], static_cast<uint8_t>(0x10 + i)));
    }
  }

  /** The module images of @p products and then @p more, by position. */
  static std::vector<std::vector<uint8_t>> Images(const std::vector<uint8_t>& products,
                                                  const std::vector<uint8_t>& more) {
    std::vector<std::vector<uint8_t>> images;
    for (const std::vector<uint8_t>* list : {&products, &more}) {
      for (uint8_t product : *list) {
        images.push_back((product == 0x27) ? MicPairImage() : PairImage(product));
      }
    }
    return images;
  }

  /** Adds a connection from @p from's RX group to @p to's TX group, by position; true if it
      was taken. */
  bool Connect(NodeAddr from, uint8_t fromGroup, NodeAddr to, uint8_t toGroup) {
    return network.AddConnection(from, fromGroup, to, toGroup) == Result::kOk;
  }

  /** Removes one; true if it was there. */
  bool Disconnect(NodeAddr from, uint8_t fromGroup, NodeAddr to, uint8_t toGroup) {
    return network.RemoveConnection(from, fromGroup, to, toGroup) == Result::kOk;
  }

  /** The audio now, its model's errors printed. */
  sim::AudioTrace Checked() const {
    const sim::AudioTrace audio = Audio();
    if (!audio.error.empty()) {
      std::printf("  %s\n", audio.error.c_str());
    }
    return audio;
  }
};

/** True if node @p sink plays node @p source's pair @p rx, @p rx + 1 on TX @p tx, @p tx + 1. */
bool PlaysPair(const sim::AudioTrace& audio, int sink, unsigned tx, int source, unsigned rx) {
  return Plays(audio, sink, tx, source, rx) && Plays(audio, sink, tx + 1, source, rx + 1);
}

/** True if nodes -1 .. @p count - 1 all play nothing. */
bool AllSilent(const sim::AudioTrace& audio, int count) {
  bool silent = true;
  for (int node = -1; node < count; ++node) {
    silent = Silent(audio, node, {}) && silent;
  }
  return silent;
}

A2B_TEST(EachMainNodeRoutesAMixedChain) {
  // Under each main node: an AD2437 (crossbar), an AD2428 (TDM), an AD2433 (crossbar) and an
  // AD2427 with four microphones (TDM). Down: the main node's pair to the first three; the
  // AD2437's pair to the AD2428 (crossbar to TDM), the AD2428's to the AD2433 (TDM to
  // crossbar). Up: the AD2433's pair to the AD2437, across the AD2428, which passes it on;
  // the microphones to the AD2428 after what it plays downstream; and every node to the main
  // node, last sub-node first, which an AD2428 main node needs and a crossbar main node
  // takes as well.
  for (uint8_t main : kMainChips) {
    std::printf(" main node AD24%02X\n", main);
    MixedBench bench(main, {0x37, 0x28, 0x33, 0x27});
    A2B_CHECK(bench.StartWith(4));
    A2B_CHECK(bench.Connect(kMainNode, 0, 0, 0));
    A2B_CHECK(bench.Connect(kMainNode, 0, 1, 0));
    A2B_CHECK(bench.Connect(kMainNode, 0, 2, 0));
    A2B_CHECK(bench.Connect(0, 0, 1, 1));
    A2B_CHECK(bench.Connect(1, 0, 2, 1));
    A2B_CHECK(bench.Connect(2, 0, 0, 1));
    A2B_CHECK(bench.Connect(3, 1, 1, 2));
    A2B_CHECK(bench.Connect(3, 2, 1, 3));
    A2B_CHECK(bench.Connect(3, 1, kMainNode, 0));
    A2B_CHECK(bench.Connect(3, 2, kMainNode, 1));
    A2B_CHECK(bench.Connect(2, 0, kMainNode, 2));
    A2B_CHECK(bench.Connect(1, 0, kMainNode, 3));
    A2B_CHECK(bench.Connect(0, 0, kMainNode, 4));
    A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
    A2B_CHECK(bench.RunUntilApplied());
    const sim::AudioTrace audio = bench.Checked();
    A2B_CHECK(audio.error.empty());  // Also: no crossbar register written on an AD242x.
    A2B_CHECK(PlaysPair(audio, 0, 0, -1, 0) && PlaysPair(audio, 0, 2, 2, 0));
    A2B_CHECK(Silent(audio, 0, {0, 1, 2, 3}));
    A2B_CHECK(PlaysPair(audio, 1, 0, -1, 0) && PlaysPair(audio, 1, 2, 0, 0));
    A2B_CHECK(PlaysPair(audio, 1, 4, 3, 0) && PlaysPair(audio, 1, 6, 3, 2));
    A2B_CHECK(PlaysPair(audio, 2, 0, -1, 0) && PlaysPair(audio, 2, 2, 1, 0));
    A2B_CHECK(Silent(audio, 2, {0, 1, 2, 3}));
    A2B_CHECK(PlaysPair(audio, -1, 0, 3, 0) && PlaysPair(audio, -1, 2, 3, 2));
    A2B_CHECK(PlaysPair(audio, -1, 4, 2, 0) && PlaysPair(audio, -1, 6, 1, 0));
    A2B_CHECK(PlaysPair(audio, -1, 8, 0, 0));
    A2B_CHECK(Silent(audio, -1, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}));
    // The slots: down, the main node's pair, then the AD2437's for the AD2428, then (the
    // AD2437's dropped) the AD2428's; up, every node's for the main node.
    A2B_CHECK_EQ(audio.dnSlots[1], 4u);
    A2B_CHECK_EQ(audio.dnSlots[2], 4u);
    A2B_CHECK_EQ(audio.upSlots[0], 10u);
    A2B_CHECK_EQ(bench.network.Routing().main.upSlots, 10);
    A2B_CHECK_EQ(bench.bus.AccessesInNewStructSettle(), 0u);
  }
}

A2B_TEST(TdmOrderAroundACrossbarNodeInTheMiddle) {
  // An AD2428 main node; an AD2428, an AD2437 and an AD2428. Down to the last AD2428, the
  // slots arrive as the main node's, the first AD2428's, then the AD2437's: its TX groups must
  // take them in that order, which the AD2437's crossbar doesn't change. The AD2437 as a sink
  // takes them in any order. Up to the main node, the last sub-node's first.
  MixedBench bench(0x28, {0x28, 0x37, 0x28});
  A2B_CHECK(bench.StartWith(3));
  A2B_CHECK(bench.Connect(kMainNode, 0, 2, 0));
  A2B_CHECK(bench.Connect(0, 0, 2, 1));
  A2B_CHECK(bench.Connect(1, 0, 2, 2));
  A2B_CHECK(bench.Connect(0, 0, 1, 0));          // Against the bus order: ...
  A2B_CHECK(bench.Connect(kMainNode, 0, 1, 1));  // ... the crossbar places them.
  A2B_CHECK(bench.Connect(2, 0, kMainNode, 0));
  A2B_CHECK(bench.Connect(1, 0, kMainNode, 1));
  A2B_CHECK(bench.Connect(0, 0, kMainNode, 2));
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied());
  sim::AudioTrace audio = bench.Checked();
  A2B_CHECK(audio.error.empty());
  A2B_CHECK(PlaysPair(audio, 2, 0, -1, 0) && PlaysPair(audio, 2, 2, 0, 0) &&
            PlaysPair(audio, 2, 4, 1, 0));
  A2B_CHECK(PlaysPair(audio, 1, 0, 0, 0) && PlaysPair(audio, 1, 2, -1, 0));
  A2B_CHECK(PlaysPair(audio, -1, 0, 2, 0) && PlaysPair(audio, -1, 2, 1, 0) &&
            PlaysPair(audio, -1, 4, 0, 0));

  // The AD2437's pair before the first AD2428's at the last node: refused, and the plan
  // written last plays on.
  A2B_CHECK(bench.Disconnect(0, 0, 2, 1) && bench.Disconnect(1, 0, 2, 2));
  A2B_CHECK(bench.Connect(1, 0, 2, 1) && bench.Connect(0, 0, 2, 2));
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kChannelOrder);
  bench.Run(50);
  A2B_CHECK(PlaysPair(bench.Audio(), 2, 2, 0, 0));
  A2B_CHECK(bench.Disconnect(1, 0, 2, 1) && bench.Disconnect(0, 0, 2, 2));
  A2B_CHECK(bench.Connect(0, 0, 2, 1) && bench.Connect(1, 0, 2, 2));

  // Up to the main node in chain order rather than bus order: refused.
  A2B_CHECK(bench.Disconnect(2, 0, kMainNode, 0) && bench.Disconnect(0, 0, kMainNode, 2));
  A2B_CHECK(bench.Connect(0, 0, kMainNode, 0) && bench.Connect(2, 0, kMainNode, 2));
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kChannelOrder);

  // A slot no one further up wants can't be dropped ahead of one they do, and an AD242x main
  // node plays every slot it receives: with the last AD2428's pair up to the first and the
  // AD2437's to the main node, the main node receives both, the AD2437's second. Its TX 0
  // is refused; its TX 1 is routed, and TX 0 then plays the pair meant for the first AD2428.
  A2B_CHECK(bench.Disconnect(0, 0, kMainNode, 0) && bench.Disconnect(1, 0, kMainNode, 1) &&
            bench.Disconnect(2, 0, kMainNode, 2));
  A2B_CHECK(bench.Connect(2, 0, 0, 0) && bench.Connect(1, 0, kMainNode, 0));
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kChannelOrder);
  A2B_CHECK(bench.Disconnect(1, 0, kMainNode, 0) && bench.Connect(1, 0, kMainNode, 1));
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied(2));
  audio = bench.Checked();
  A2B_CHECK(audio.error.empty());
  A2B_CHECK(PlaysPair(audio, 0, 0, 2, 0));
  A2B_CHECK(PlaysPair(audio, -1, 2, 1, 0) && PlaysPair(audio, -1, 0, 2, 0));
  A2B_CHECK_EQ(bench.network.Routing().main.upSlots, 4);
}

A2B_TEST(LocalAndWholeBusMutesInAMixedChain) {
  // An AD2437 main node; an AD2437 (the source), an AD2428 and an AD2437 (sinks).
  MixedBench bench(0x37, {0x37, 0x28, 0x37});
  A2B_CHECK(bench.StartWith(3));
  MuteRecorder recorder(bench.listener);
  bench.network.SetListener(&recorder);
  // Plan 1: the main node's pair to both sinks; the source's channels 4-5 up to the main
  // node (RXMASK 0x30).
  A2B_CHECK(bench.Connect(kMainNode, 0, 1, 0) && bench.Connect(kMainNode, 0, 2, 0));
  A2B_CHECK(bench.Connect(0, 2, kMainNode, 0));
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied(1));
  A2B_CHECK(PlaysPair(bench.Audio(), 1, 0, -1, 0) && PlaysPair(bench.Audio(), -1, 0, 0, 4));

  // Plan 2 adds the source's channels 0-1 to the AD2428's TX 1 (RXMASK 0x33). Until the new
  // RXMASK is written the new downstream slots carry channels 4-5, which the AD2428 would
  // play, and it has no crossbar to mute: the local mute mutes the whole bus for this plan.
  NewStructAudio atNewStruct(bench);
  A2B_CHECK(bench.Connect(0, 0, 1, 1));
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  size_t newStructs = bench.bus.NewStructCount();
  A2B_CHECK(bench.RunUntilApplied(2));
  sim::AudioTrace settled = bench.Checked();
  A2B_CHECK(settled.error.empty());
  A2B_CHECK(PlaysPair(settled, 1, 0, -1, 0) && PlaysPair(settled, 1, 2, 0, 0));
  A2B_CHECK(PlaysPair(settled, -1, 0, 0, 4) && PlaysPair(settled, 2, 0, -1, 0));
  A2B_CHECK(recorder.muteScope.network);
  A2B_CHECK_EQ(bench.bus.NewStructCount(), newStructs + 2);
  A2B_CHECK_EQ(atNewStruct.audio.size(), 2u);
  for (const sim::AudioTrace& mid : atNewStruct.audio) {
    A2B_CHECK(NoWrongChannel(mid, settled, 3));
  }
  A2B_CHECK(!atNewStruct.audio.empty() && AllSilent(atNewStruct.audio[0], 3));

  // Plan 3 sends the same pair to the other AD2437's TX 1 too: no RXMASK changes, only that
  // sink's crossbar, so only it is muted, and the AD2428 plays on.
  atNewStruct.audio.clear();
  A2B_CHECK(bench.Connect(0, 0, 2, 1));
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  newStructs = bench.bus.NewStructCount();
  A2B_CHECK(bench.RunUntilApplied(3));
  settled = bench.Checked();
  A2B_CHECK(PlaysPair(settled, 2, 2, 0, 0) && PlaysPair(settled, 1, 2, 0, 0));
  A2B_CHECK(!recorder.muteScope.network);
  A2B_CHECK(recorder.muteScope.Includes(2));
  A2B_CHECK(!recorder.muteScope.Includes(kMainNode) && !recorder.muteScope.Includes(0) &&
            !recorder.muteScope.Includes(1));
  A2B_CHECK_EQ(bench.bus.NewStructCount(), newStructs + 1);
  A2B_CHECK_EQ(atNewStruct.audio.size(), 1u);
  if (atNewStruct.audio.size() == 1) {
    A2B_CHECK(NoWrongChannel(atNewStruct.audio[0], settled, 3));
    A2B_CHECK(Silent(atNewStruct.audio[0], 2, {}));
    A2B_CHECK(PlaysPair(atNewStruct.audio[0], 1, 0, -1, 0) &&
              PlaysPair(atNewStruct.audio[0], 1, 2, 0, 0));
  }

  // RoutingMute::kGlobal: the whole bus, the AD2428 included, for any plan.
  bench.board.routing.mute = RoutingMute::kGlobal;
  atNewStruct.audio.clear();
  A2B_CHECK(bench.Disconnect(0, 0, 2, 1));
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied(4));
  settled = bench.Checked();
  A2B_CHECK(recorder.muteScope.network);
  A2B_CHECK(Silent(settled, 2, {0, 1}) && PlaysPair(settled, 1, 2, 0, 0));
  A2B_CHECK_EQ(atNewStruct.audio.size(), 2u);
  if (atNewStruct.audio.size() == 2) {
    A2B_CHECK(AllSilent(atNewStruct.audio[0], 3));
    A2B_CHECK(NoWrongChannel(atNewStruct.audio[1], settled, 3));
  }
  A2B_CHECK_EQ(recorder.mutes, 4);
  A2B_CHECK_EQ(recorder.unmutes, 4);
  A2B_CHECK_EQ(bench.bus.AccessesInNewStructSettle(), 0u);
}

A2B_TEST(MixedChainChangesWhileRouted) {
  // An AD2433 main node; an AD2428 (TDM), an AD2437 and an AD2427 with microphones. The
  // AD2428 plays the main node's pair and, after it, the AD2437's, which comes up past it
  // behind the microphones it passes on to the main node.
  MixedBench bench(0x33, {0x28, 0x37, 0x27}, {0x28});
  A2B_CHECK(bench.StartWith(3));
  MuteRecorder recorder(bench.listener);
  bench.network.SetListener(&recorder);
  bool wholeBus = false;
  recorder.onMute = [&wholeBus](Network&, const MuteScope& scope) {
    wholeBus = wholeBus || scope.network;
  };
  A2B_CHECK(bench.Connect(kMainNode, 0, 0, 0) && bench.Connect(kMainNode, 0, 1, 0));
  A2B_CHECK(bench.Connect(1, 0, 0, 1));
  A2B_CHECK(bench.Connect(2, 1, kMainNode, 0) && bench.Connect(2, 2, kMainNode, 1));
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied(1));
  const sim::AudioTrace routed = bench.Checked();
  A2B_CHECK(routed.error.empty());
  A2B_CHECK(PlaysPair(routed, 0, 0, -1, 0) && PlaysPair(routed, 0, 2, 1, 0));
  A2B_CHECK(PlaysPair(routed, 1, 0, -1, 0));
  A2B_CHECK(PlaysPair(routed, -1, 0, 2, 0) && PlaysPair(routed, -1, 2, 2, 2));

  // The AD2437 and the microphones drop: the AD2428 keeps the main node's pair.
  bench.bus.Unplug(1);
  A2B_CHECK(bench.RunUntilCount(1, 3000));
  A2B_CHECK(bench.RunUntilApplied(2));
  sim::AudioTrace audio = bench.Checked();
  A2B_CHECK(audio.error.empty());
  A2B_CHECK(PlaysPair(audio, 0, 0, -1, 0) && Silent(audio, 0, {0, 1}));
  A2B_CHECK(Silent(audio, -1, {}));

  // Plugged back in, the routing comes back. The AD2437 comes back with its power-on
  // crossbar, muted locally; its RX frame buffer's first locations hold the same channels
  // under its power-on RXMASK, so the AD2428 that plays them needs no whole-bus mute. No node
  // plays a wrong channel at any new structure meanwhile.
  NewStructAudio atNewStruct(bench);
  const size_t applied = bench.listener.Of(EventType::kRoutingApplied).size();
  bench.bus.Plug(SubOf(0x37, 0x11));
  bench.bus.Plug(SubOf(0x27, 0x12));
  A2B_CHECK(bench.RunUntilCount(3, 3000));
  A2B_CHECK(bench.RunUntilApplied(applied + 1));
  bench.Run(1000);  // Any plan still due.
  audio = bench.Checked();
  A2B_CHECK(audio.error.empty());
  for (int node = -1; node < 3; ++node) {
    for (unsigned tx = 0; tx < 32; ++tx) {
      A2B_CHECK(audio.Played(node, tx) == routed.Played(node, tx));
    }
  }
  A2B_CHECK(!wholeBus);
  for (const sim::AudioTrace& mid : atNewStruct.audio) {
    A2B_CHECK(NoWrongChannel(mid, routed, 3));
  }

  // An AD2428 joins at the end, and is connected to the first AD2428's TX 2. Its slots come
  // up first, ahead of the AD2437's for TX 1: once it is in the chain (again), the plan is
  // refused at the first AD2428, and the plan written last plays on.
  bench.bus.Plug(SubOf(0x28, 0x13));
  A2B_CHECK(bench.RunUntilCount(4, 3000));
  bench.Run(200);
  A2B_CHECK(bench.Connect(3, 0, 0, 2));
  bench.bus.Unplug(3);
  A2B_CHECK(bench.RunUntilCount(3, 3000));
  bench.Run(200);
  const size_t failed = bench.listener.Of(EventType::kRoutingFailed).size();
  bench.bus.Plug(SubOf(0x28, 0x13));
  A2B_CHECK(bench.RunUntilCount(4, 3000));
  bench.Run(500);
  const std::vector<Event> failures = bench.listener.Of(EventType::kRoutingFailed);
  A2B_CHECK(failures.size() > failed);
  if (failures.size() > failed) {
    A2B_CHECK(failures.back().routingError == RoutingError::kChannelOrder);
    A2B_CHECK_EQ(failures.back().node, 0);
  }
  audio = bench.Checked();
  A2B_CHECK(PlaysPair(audio, 0, 0, -1, 0) && PlaysPair(audio, 0, 2, 1, 0));
  A2B_CHECK(Silent(audio, 0, {0, 1, 2, 3}));
  // Its connection removed, the chain is routed again (the next ApplyRouting()).
  A2B_CHECK(bench.Disconnect(3, 0, 0, 2));
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  bench.Run(200);
  A2B_CHECK(bench.network.Routing().valid && bench.network.Routing().subNodeCount == 4);
  A2B_CHECK_EQ(bench.bus.AccessesInNewStructSettle(), 0u);
}

}  // namespace
}  // namespace test
}  // namespace a2b

A2B_TEST_MAIN()
