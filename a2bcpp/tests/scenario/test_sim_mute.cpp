/**
 * @file test_sim_mute.cpp
 * @brief Muting on the simulator: Network::MuteNetwork() (DATCTL and NEWSTRCT),
 *        the mute while a routing plan is written (BoardRouting::mute: a node's TX crossbar
 *        pointed at location 31, or the whole bus), and the NetworkListener's
 *        OnAudioMute() and OnAudioUnmute(). The audio at the instant a new structure takes
 *        effect is read through Bus::SetNewStructObserver(), since that is what a mute is
 *        for; the rest follows the audio the registers route (Bus::TraceAudio()).
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <vector>

#include "a2b/network.h"
#include "detail/regdefs.h"
#include "routing_bench.h"
#include "sim/simulator.h"
#include "sim_bench.h"
#include "test_harness.h"

namespace a2b {
namespace {

using test::Ad2428Board;
using test::CodecImage;
using test::GroupBoard;
using test::MicrophoneImage;
using test::MuteRecorder;
using test::NewStructAudio;
using test::NoWrongChannel;
using test::PairImage;
using test::Plays;
using test::RoutingBench;
using test::Silent;

/** A codec behind a sub-node that notes the bus's NEWSTRCT count at each write. */
class Codec : public sim::I2cDevice {
 public:
  explicit Codec(const sim::Bus& bus) : m_Bus(bus) {}

  /** Keeps the bytes and the count. */
  Result Write(const uint8_t* data, size_t len, uint32_t nowMs) override {
    (void)nowMs;
    written.assign(data, data + len);
    newStructsAtWrite = m_Bus.NewStructCount();
    ++writes;
    return Result::kOk;
  }

  /** Nothing to read. */
  Result Read(uint8_t* data, size_t len, uint32_t nowMs) override {
    (void)data;
    (void)len;
    (void)nowMs;
    return Result::kOk;
  }

  std::vector<uint8_t> written;  ///< The last write.
  size_t newStructsAtWrite = 0;  ///< NEWSTRCTs the bus had written then.
  int writes = 0;                ///< Writes.

 private:
  const sim::Bus& m_Bus;
};

/** True if nodes -1 .. @p count - 1 all play nothing. */
bool AllSilent(const sim::AudioTrace& audio, int count) {
  bool silent = true;
  for (int node = -1; node < count; ++node) {
    silent = Silent(audio, node, {}) && silent;
  }
  return silent;
}

/** Connects the main node's stereo pair to sub-node @p sink's TX group 0. */
void Feed(RoutingBench& bench, NodeAddr sink) {
  A2B_CHECK(bench.network.AddConnection(kMainNode, 0, sink, 0) == Result::kOk);
}

A2B_TEST(MuteNetworkSilencesTheBusAndUnmutesIt) {
  RoutingBench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  Feed(bench, 0);
  Feed(bench, 1);
  A2B_CHECK(bench.network.AddConnection(0, 0, kMainNode, 0) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied());
  A2B_CHECK(Plays(bench.Audio(), 0, 0, -1, 0) && Plays(bench.Audio(), 1, 1, -1, 1));
  A2B_CHECK(!bench.network.NetworkMuted());
  // Muted at the next poll: DATCTL clear everywhere, the plan kept.
  const size_t applied = bench.listener.Of(EventType::kRoutingApplied).size();
  bench.network.MuteNetwork(true);
  A2B_CHECK(bench.network.NetworkMuted());
  bench.Run(5);
  A2B_CHECK(AllSilent(bench.Audio(), 2));
  A2B_CHECK(bench.Audio().error.empty());
  for (int node = -1; node < 2; ++node) {
    const sim::Node& n = (node < 0) ? bench.bus.Main() : bench.bus.Sub(static_cast<size_t>(node));
    A2B_CHECK_EQ(n.AppliedReg(reg::kDatctl), 0x00);
  }
  A2B_CHECK(bench.network.Routing().valid);
  A2B_CHECK_EQ(bench.listener.Of(EventType::kRoutingApplied).size(), applied);
  // Unmuted again.
  bench.network.MuteNetwork(false);
  bench.Run(5);
  A2B_CHECK(!bench.network.NetworkMuted());
  const sim::AudioTrace audio = bench.Audio();
  A2B_CHECK(audio.error.empty());
  A2B_CHECK(Plays(audio, 0, 0, -1, 0) && Plays(audio, 1, 1, -1, 1) && Plays(audio, -1, 0, 0, 0));
  A2B_CHECK_EQ(bench.bus.Sub(1).AppliedReg(reg::kDatctl), reg::kDatctlDns | reg::kDatctlUps);
  A2B_CHECK_EQ(bench.bus.AccessesInNewStructSettle(), 0u);
}

A2B_TEST(AMutedBusStaysMutedThroughPlansAndRestarts) {
  RoutingBench bench;
  bench.PlugChain(1);
  A2B_CHECK(bench.StartWith(1));
  bench.network.MuteNetwork(true);
  bench.Run(5);
  Feed(bench, 0);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied(1));
  A2B_CHECK(AllSilent(bench.Audio(), 1));  // The plan was written muted.
  // A plan for a longer chain, then a restart (the main node relocks): still muted.
  bench.bus.Plug(test::Ad2437(0x11));
  A2B_CHECK(bench.RunUntilApplied(2));
  A2B_CHECK(AllSilent(bench.Audio(), 2));
  bench.bus.SetSync(false);
  bench.Run(5);
  bench.bus.SetSync(true);
  A2B_CHECK(bench.RunUntilApplied(3, 5000));
  A2B_CHECK(bench.RunUntilCount(2, 5000));  // The nodes come back one at a time.
  bench.Run(1000);
  A2B_CHECK(AllSilent(bench.Audio(), 2));
  A2B_CHECK(bench.network.NetworkMuted());
  bench.network.MuteNetwork(false);
  bench.Run(5);
  A2B_CHECK(Plays(bench.Audio(), 0, 0, -1, 0) && Plays(bench.Audio(), 0, 1, -1, 1));
}

A2B_TEST(MuteBeforeStartIsWrittenWithTheImage) {
  // MuteNetwork() on a bus that isn't started: the image's DATCTL is written without the
  // enables, and the bus comes up muted.
  BoardConfig board = GroupBoard();
  board.mainNode.Set(reg::kDatctl, reg::kDatctlDns | reg::kDatctlUps);
  RoutingBench bench(board);
  bench.PlugChain(1);
  bench.network.MuteNetwork(true);
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kDatctl), 0x00);
  Feed(bench, 0);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied());
  A2B_CHECK(AllSilent(bench.Audio(), 1));
}

A2B_TEST(HooksCallAroundAPlanAndTheLocalMuteCoversTheNewStructure) {
  RoutingBench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  MuteRecorder recorder(bench.listener);
  bench.network.SetListener(&recorder);
  Codec codec(bench.bus);
  bench.bus.Sub(0).AttachDevice(0x2C, codec);
  const std::vector<uint8_t> data = {0xA5, 0x5A};
  size_t newStructsInHook = 0;
  sim::AudioTrace audioInHook;
  recorder.onMute = [&](Network& network, const MuteScope&) {
    // The app mutes its codec through the Network, before the bus changes.
    A2B_CHECK(network.PeripheralWrite(0, PeripheralAddress::I2c(0x2C), data.data(), data.size()) ==
              Result::kOk);
    newStructsInHook = bench.bus.NewStructCount();
    audioInHook = bench.bus.TraceAudio();
  };
  // Plan 1: the main node's pair to TX group 0 of both sub-nodes. Every crossbar changes from
  // its power-on one.
  Feed(bench, 0);
  Feed(bench, 1);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  const size_t before = bench.bus.NewStructCount();
  A2B_CHECK(bench.RunUntilApplied(1));
  A2B_CHECK_EQ(recorder.mutes, 1);
  A2B_CHECK_EQ(recorder.unmutes, 1);
  A2B_CHECK_EQ(recorder.unmutesAtApplied, 1);  // The unmute hook came before the event.
  A2B_CHECK(recorder.order == "MU");
  A2B_CHECK_EQ(newStructsInHook, before);                // Called before the plan's NEWSTRCT ...
  A2B_CHECK_EQ(bench.bus.NewStructCount(), before + 1);  // ... which local mute does not double.
  A2B_CHECK(AllSilent(audioInHook, 2));                  // Nothing was routed yet.
  A2B_CHECK(!recorder.muteScope.network);
  A2B_CHECK(recorder.muteScope.Includes(kMainNode) && recorder.muteScope.Includes(0) &&
            recorder.muteScope.Includes(1));
  A2B_CHECK(recorder.unmuteScope.Includes(0));
  A2B_CHECK_EQ(codec.writes, 1);  // The app's mute reached its codec ...
  A2B_CHECK(codec.written == data);
  A2B_CHECK_EQ(codec.newStructsAtWrite, before);  // ... before the plan's NEWSTRCT.
  A2B_CHECK(Plays(bench.Audio(), 0, 0, -1, 0) && Plays(bench.Audio(), 1, 1, -1, 1));
  // Plan 2 moves sub-node 1's feed to its TX group 1 (channels 2-3): only its crossbar
  // changes, so only it is muted. At the instant the new structure takes effect it plays
  // nothing (its old crossbar would still play the pair on channels 0-1), and sub-node 0 plays
  // on.
  NewStructAudio atNewStruct(bench);
  A2B_CHECK(bench.network.RemoveConnection(kMainNode, 0, 1, 0) == Result::kOk);
  A2B_CHECK(bench.network.AddConnection(kMainNode, 0, 1, 1) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied(2));
  A2B_CHECK_EQ(recorder.mutes, 2);
  A2B_CHECK_EQ(recorder.unmutes, 2);
  A2B_CHECK(recorder.order == "MUMU");
  A2B_CHECK(recorder.muteScope.Includes(1));
  A2B_CHECK(!recorder.muteScope.Includes(0) && !recorder.muteScope.Includes(kMainNode));
  A2B_CHECK_EQ(atNewStruct.audio.size(), 1u);
  if (atNewStruct.audio.size() == 1) {
    const sim::AudioTrace& mid = atNewStruct.audio[0];
    A2B_CHECK(Silent(mid, 1, {}));
    A2B_CHECK(Plays(mid, 0, 0, -1, 0) && Plays(mid, 0, 1, -1, 1));
  }
  const sim::AudioTrace audio = bench.Audio();
  A2B_CHECK(audio.error.empty());
  A2B_CHECK(Plays(audio, 1, 2, -1, 0) && Plays(audio, 1, 3, -1, 1));
  A2B_CHECK(Silent(audio, 1, {2, 3}));
  A2B_CHECK(Plays(audio, 0, 0, -1, 0));
  A2B_CHECK_EQ(bench.bus.AccessesInNewStructSettle(), 0u);
  // The same plan again changes no crossbar: the hooks are called, and nothing is muted.
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied(3));
  A2B_CHECK_EQ(recorder.mutes, 3);
  for (int node = -1; node < 2; ++node) {
    A2B_CHECK(!recorder.muteScope.Includes(node));
  }
}

A2B_TEST(ARemovedModuleComesBackWithItsOwnCrossbar) {
  // A module unplugged and a new one plugged in its place starts with the power-on crossbar:
  // it is muted for the plan, whatever the plan before it held for that position.
  RoutingBench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  MuteRecorder recorder(bench.listener);
  bench.network.SetListener(&recorder);
  Feed(bench, 0);
  Feed(bench, 1);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied(1));
  bench.bus.Unplug(1);
  A2B_CHECK(bench.RunUntilApplied(2));
  A2B_CHECK(!recorder.muteScope.Includes(0));
  bench.bus.Plug(test::Ad2437(0x12));  // A different module at position 1.
  A2B_CHECK(bench.RunUntilApplied(3));
  A2B_CHECK(recorder.muteScope.Includes(1));  // In no connection, but its crossbar is new.
  A2B_CHECK(!recorder.muteScope.Includes(0));
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
}

A2B_TEST(ARxMaskChangeMutesTheNodesThatPlayItsSlots) {
  // RXMASK takes effect at once, so from the plan's NEWSTRCT until the crossbars are written
  // the new slots carry the frame buffer the old RXMASK filled. Plan 1: sub-node
  // 0's channels 0-1 to sub-node 2 and 4-5 to sub-node 1 (RXMASK 0x33; sub-node 1 plays the
  // third and fourth slots). Plan 2 drops the first: RXMASK 0x30, and sub-node 1 plays the
  // first two slots into the same locations, so its crossbar doesn't change; but until
  // sub-node 0's RXMASK is written those slots carry its channels 0-1. Sub-node 1 is muted
  // too, and no node plays a channel it shouldn't.
  RoutingBench bench(GroupBoard(), {PairImage(0x37)});
  bench.PlugChain(3);
  A2B_CHECK(bench.StartWith(3));
  MuteRecorder recorder(bench.listener);
  bench.network.SetListener(&recorder);
  A2B_CHECK(bench.network.AddConnection(0, 0, 2, 0) == Result::kOk);
  A2B_CHECK(bench.network.AddConnection(0, 2, 1, 0) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied(1));
  A2B_CHECK(Plays(bench.Audio(), 1, 0, 0, 4) && Plays(bench.Audio(), 2, 0, 0, 0));
  NewStructAudio atNewStruct(bench);
  A2B_CHECK(bench.network.RemoveConnection(0, 0, 2, 0) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied(2));
  const sim::AudioTrace settled = bench.Audio();
  A2B_CHECK(settled.error.empty());
  A2B_CHECK(Plays(settled, 1, 0, 0, 4) && Plays(settled, 1, 1, 0, 5));
  A2B_CHECK(Silent(settled, 1, {0, 1}) && Silent(settled, 2, {}));
  A2B_CHECK(!recorder.muteScope.network);
  A2B_CHECK(recorder.muteScope.Includes(1) && recorder.muteScope.Includes(2));
  A2B_CHECK(!recorder.muteScope.Includes(kMainNode) && !recorder.muteScope.Includes(0));
  A2B_CHECK_EQ(atNewStruct.audio.size(), 1u);
  if (atNewStruct.audio.size() == 1) {
    A2B_CHECK(NoWrongChannel(atNewStruct.audio[0], settled, 3));
  }
}

A2B_TEST(WholeBusMuteCoversTheNewStructureAndIsLifted) {
  BoardConfig board = GroupBoard();
  board.routing.mute = RoutingMute::kGlobal;
  RoutingBench bench(board);
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  MuteRecorder recorder(bench.listener);
  bench.network.SetListener(&recorder);
  Feed(bench, 0);
  Feed(bench, 1);
  A2B_CHECK(bench.network.AddConnection(1, 0, kMainNode, 0) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  NewStructAudio atNewStruct(bench);
  const size_t before = bench.bus.NewStructCount();
  A2B_CHECK(bench.RunUntilApplied());
  // Two NEWSTRCT: the slots with DATCTL clear, and DATCTL again.
  A2B_CHECK_EQ(bench.bus.NewStructCount(), before + 2);
  A2B_CHECK_EQ(atNewStruct.audio.size(), 2u);
  if (atNewStruct.audio.size() == 2) {
    A2B_CHECK(AllSilent(atNewStruct.audio[0], 2));
    A2B_CHECK(Plays(atNewStruct.audio[1], 1, 0, -1, 0) && Plays(atNewStruct.audio[1], -1, 0, 1, 0));
  }
  A2B_CHECK(recorder.muteScope.network);
  A2B_CHECK(recorder.unmuteScope.network);
  A2B_CHECK(recorder.muteScope.Includes(0) && recorder.muteScope.Includes(kMainNode));
  A2B_CHECK_EQ(recorder.unmutes, 1);
  for (int node = -1; node < 2; ++node) {
    const sim::Node& n = (node < 0) ? bench.bus.Main() : bench.bus.Sub(static_cast<size_t>(node));
    A2B_CHECK_EQ(n.AppliedReg(reg::kDatctl), reg::kDatctlDns | reg::kDatctlUps);
  }
  A2B_CHECK(!bench.network.NetworkMuted());
  A2B_CHECK_EQ(bench.bus.AccessesInNewStructSettle(), 0u);
  // The app's own mute holds through the next plan: the hooks are called, the bus stays quiet.
  bench.network.MuteNetwork(true);
  bench.Run(5);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied(2));
  A2B_CHECK_EQ(recorder.unmutes, 2);
  A2B_CHECK(AllSilent(bench.Audio(), 2));
  bench.network.MuteNetwork(false);
  bench.Run(5);
  A2B_CHECK(Plays(bench.Audio(), 1, 0, -1, 0));
}

A2B_TEST(NoMuteModeMutesNothingButStillCallsTheHooks) {
  BoardConfig board = GroupBoard();
  board.routing.mute = RoutingMute::kNone;
  RoutingBench bench(board);
  bench.PlugChain(1);
  A2B_CHECK(bench.StartWith(1));
  MuteRecorder recorder(bench.listener);
  bench.network.SetListener(&recorder);
  Feed(bench, 0);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  const size_t before = bench.bus.NewStructCount();
  A2B_CHECK(bench.RunUntilApplied());
  A2B_CHECK_EQ(recorder.mutes, 1);
  A2B_CHECK_EQ(recorder.unmutes, 1);
  A2B_CHECK(!recorder.muteScope.network);
  for (int node = -1; node < 1; ++node) {
    A2B_CHECK(!recorder.muteScope.Includes(node));
  }
  A2B_CHECK_EQ(bench.bus.NewStructCount(), before + 1);
  A2B_CHECK(Plays(bench.Audio(), 0, 0, -1, 0));
}

A2B_TEST(AnAd242xHasNoLocalMuteButTheWholeBusMutes) {
  // The second customer system: no node has a crossbar, so the local mute has nothing to
  // do (the slot registers change at NEWSTRCT, all at once); MuteNetwork() reaches them all.
  RoutingBench bench(Ad2428Board(), {CodecImage(), MicrophoneImage()}, test::ChipOf(0x28));
  bench.bus.Plug(test::SubOf(0x28, 0x10));
  bench.bus.Plug(test::SubOf(0x27, 0x11));
  A2B_CHECK(bench.StartWith(2));
  MuteRecorder recorder(bench.listener);
  bench.network.SetListener(&recorder);
  A2B_CHECK(bench.network.AddConnection(kMainNode, 0, 0, 0) == Result::kOk);
  A2B_CHECK(bench.network.AddConnection(1, 0, kMainNode, 0) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied());
  A2B_CHECK_EQ(recorder.mutes, 1);
  A2B_CHECK(!recorder.muteScope.network);
  for (int node = -1; node < 2; ++node) {
    A2B_CHECK(!recorder.muteScope.Includes(node));
  }
  sim::AudioTrace audio = bench.Audio();
  A2B_CHECK(audio.error.empty());
  A2B_CHECK(Plays(audio, 0, 0, -1, 0) && Plays(audio, -1, 0, 1, 0));
  bench.network.MuteNetwork(true);
  bench.Run(5);
  audio = bench.Audio();
  A2B_CHECK(audio.error.empty());
  A2B_CHECK(AllSilent(audio, 2));
  bench.network.MuteNetwork(false);
  bench.Run(5);
  A2B_CHECK(Plays(bench.Audio(), 0, 0, -1, 0) && Plays(bench.Audio(), -1, 0, 1, 0));
}

A2B_TEST(WholeBusMuteReachesAnAd242xNodeDuringAPlan) {
  BoardConfig board = Ad2428Board();
  board.routing.mute = RoutingMute::kGlobal;
  RoutingBench bench(board, {CodecImage(), MicrophoneImage()}, test::ChipOf(0x28));
  bench.bus.Plug(test::SubOf(0x28, 0x10));
  bench.bus.Plug(test::SubOf(0x27, 0x11));
  A2B_CHECK(bench.StartWith(2));
  A2B_CHECK(bench.network.AddConnection(kMainNode, 0, 0, 0) == Result::kOk);
  A2B_CHECK(bench.network.AddConnection(1, 0, kMainNode, 0) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  NewStructAudio atNewStruct(bench);
  A2B_CHECK(bench.RunUntilApplied());
  A2B_CHECK_EQ(atNewStruct.audio.size(), 2u);
  if (atNewStruct.audio.size() == 2) {
    A2B_CHECK(AllSilent(atNewStruct.audio[0], 2));
    A2B_CHECK(Plays(atNewStruct.audio[1], 0, 0, -1, 0));
  }
}

A2B_TEST(AHookThatRoutesAgainRestartsThePlanAndTheHooksStayPaired) {
  RoutingBench bench;
  bench.PlugChain(1);
  A2B_CHECK(bench.StartWith(1));
  MuteRecorder recorder(bench.listener);
  bench.network.SetListener(&recorder);
  Feed(bench, 0);
  bool again = true;
  recorder.onMute = [&](Network& network, const MuteScope&) {
    if (again) {
      again = false;
      A2B_CHECK(network.AddConnection(0, 0, kMainNode, 0) == Result::kOk);
      A2B_CHECK(network.ApplyRouting() == RoutingError::kNone);
    }
  };
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied());
  bench.Run(500);
  A2B_CHECK_EQ(bench.listener.Of(EventType::kRoutingApplied).size(), 1u);
  A2B_CHECK_EQ(recorder.mutes, 1);
  A2B_CHECK_EQ(recorder.unmutes, 1);
  A2B_CHECK_EQ(bench.network.Routing().routed, 2);  // The plan built after the hook's change.
  A2B_CHECK(Plays(bench.Audio(), -1, 0, 0, 0) && Plays(bench.Audio(), 0, 0, -1, 0));
}

A2B_TEST(AFailedWriteKeepsTheHooksPaired) {
  RoutingBench bench;
  bench.PlugChain(1);
  A2B_CHECK(bench.StartWith(1));
  MuteRecorder recorder(bench.listener);
  bench.network.SetListener(&recorder);
  Feed(bench, 0);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  bench.sim.FailAddress(test::kBusAddr, 1);  // The first transfer to a sub-node of the plan.
  A2B_CHECK(bench.RunUntilApplied());
  A2B_CHECK_EQ(bench.listener.Of(EventType::kRoutingFailed).size(), 1u);
  A2B_CHECK_EQ(recorder.mutes, 1);  // Not again for the plan written a second time.
  A2B_CHECK_EQ(recorder.unmutes, 1);
  A2B_CHECK(recorder.order == "MU");
  A2B_CHECK(Plays(bench.Audio(), 0, 0, -1, 0));
}

A2B_TEST(BandwidthIsReportedForTheLastPlanBuilt) {
  RoutingBench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  const BandwidthUsage none = bench.network.Bandwidth();
  A2B_CHECK_EQ(none.percent, 0);
  // Two slots down to sub-node 1 (through sub-node 0), none up.
  Feed(bench, 1);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  const BandwidthUsage one = bench.network.Bandwidth();  // Built, not yet written.
  A2B_CHECK_EQ(one.node, 1);
  A2B_CHECK(one.bits > 0 && one.percent == one.bits * 100u / 1024u);
  A2B_CHECK(bench.RunUntilApplied());
  A2B_CHECK_EQ(bench.network.Routing().bandwidth.bits, one.bits);
  // The same chain's input gives the same number.
  RespCycleInput input;
  input.subNodeCount = 2;
  input.dnSlots[0] = bench.network.Routing().linkDnSlots[0];
  input.dnSlots[1] = bench.network.Routing().linkDnSlots[1];
  input.upSlots[0] = bench.network.Routing().linkUpSlots[0];
  input.upSlots[1] = bench.network.Routing().linkUpSlots[1];
  input.dnSlotBits = 25;  // GroupBoard(): 24-bit slots.
  input.upSlotBits = 25;
  input.cableDm[0] = input.cableDm[1] = 40;
  A2B_CHECK_EQ(ComputeBandwidth(input).bits, one.bits);
  // More audio, more bits.
  A2B_CHECK(bench.network.AddConnection(0, 0, kMainNode, 0) == Result::kOk);
  A2B_CHECK(bench.network.AddConnection(1, 0, 0, 1) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.network.Bandwidth().bits > one.bits);
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
