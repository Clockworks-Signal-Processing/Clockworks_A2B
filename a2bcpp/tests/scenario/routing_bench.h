/**
 * @file routing_bench.h
 * @brief What the routing scenario tests share routing: module images with channel groups and
 *        ports, boards with the main node's groups, a bench that waits for routing plans, the
 *        audio checks (what a node plays, which TX channels are silent, nothing wrong when a
 *        new structure takes effect), and a listener that records the mute hooks.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_TESTS_SCENARIO_ROUTING_BENCH_H_
#define A2BCPP_TESTS_SCENARIO_ROUTING_BENCH_H_

#include <cstdio>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "a2b/network.h"
#include "detail/regdefs.h"
#include "sim/module_info.h"
#include "sim/simulator.h"
#include "sim_bench.h"

namespace a2b {
namespace test {

using sim::Signal;

/** A module image with the bench modules' settings, the port settings @p regs gives and
    channel groups @p groups. */
inline std::vector<uint8_t> PortImage(const sim::ModuleInfoRegs& regs, const ChannelGroups& groups,
                                      uint8_t powerConfig = sim::kPowerConfigHighPower) {
  sim::ModuleInfoHeader header;
  header.powerConfig = powerConfig;
  sim::ModuleInfoRegs settings = regs;
  settings.gpioien = 0x7F;
  settings.pincfg = 0x00;
  std::vector<uint8_t> image = sim::MakeModuleInfoImage(header, settings);
  sim::AddChannelGroups(image, groups);
  return image;
}

/** A bench module's image with channel groups: RX 0 and TX 0 on channels 0-1 (a stereo
    codec, as the AB0020's), and TX 1 on channels 2-3. Its port is TDM32 on DTX0 and DRX0,
    32 channels each way, so the routing tests are about the slots, not the port. */
inline std::vector<uint8_t> GroupImage(uint32_t rx0 = 0x3, uint32_t tx0 = 0x3, uint32_t tx1 = 0xC) {
  sim::ModuleInfoRegs regs;
  regs.i2sgcfg = 0x07;  // TDM32.
  regs.i2scfg = 0x11;   // DTX0 and DRX0.
  ChannelGroups groups;
  groups.AddRx(0, rx0);
  groups.AddTx(0, tx0);
  groups.AddTx(1, tx1);
  return PortImage(regs, groups);
}

/** The bench board with the main node's groups: RX 0 and TX 0 on channels 0-1, on its I2S
    port's DTX0 and DRX0. */
inline BoardConfig GroupBoard() {
  BoardConfig board = test::MakeBoard();
  board.mainNode.Set(reg::kSlotfmt, 0x44);  // 24-bit slots both ways.
  board.mainNode.Set(reg::kI2sgcfg, 0xE0);  // I2S, 32-bit: RESPOFFS 245.
  board.mainNode.Set(reg::kI2scfg, 0x11);   // DTX0 and DRX0: two channels each way.
  board.routing.groups.AddRx(0, 0x3);
  board.routing.groups.AddTx(0, 0x3);
  return board;
}

/** GroupBoard() with the main node's port at TDM32 (32 channels each way), for groups of
    32 channels. */
inline BoardConfig WideBoard() {
  BoardConfig board = GroupBoard();
  board.mainNode.Set(reg::kI2sgcfg, 0xE7);
  return board;
}

/** A bench whose modules all have GroupImage(). */
struct RoutingBench : Bench {
  explicit RoutingBench(const BoardConfig& config = GroupBoard(),
                        std::vector<std::vector<uint8_t>> images = {GroupImage()},
                        const sim::ChipSpec& mainChip = MainChip())
      : Bench(config, std::move(images), mainChip) {}

  /** Polls until @p count EventType::kRoutingApplied events have come, for at most @p ms. */
  bool RunUntilApplied(size_t count = 1, uint32_t ms = 3000) {
    for (uint32_t i = 0; i < ms && listener.Of(EventType::kRoutingApplied).size() < count; ++i) {
      Run(1);
    }
    return listener.Of(EventType::kRoutingApplied).size() >= count;
  }

  /** The audio the simulated bus carries now. */
  sim::AudioTrace Audio() const { return bus.TraceAudio(); }
};

/** True if @p trace has node @p sink play node @p source's RX channel @p rx on TX @p tx. */
inline bool Plays(const sim::AudioTrace& trace, int sink, unsigned tx, int source, unsigned rx) {
  const Signal played = trace.Played(sink, tx);
  const Signal expected = Signal::From(static_cast<int8_t>(source), static_cast<uint8_t>(rx));
  if (played != expected) {
    std::printf("  node %d TX %u plays node %d RX %u, not node %d RX %u\n", sink, tx, played.node,
                played.channel, source, rx);
    return false;
  }
  return true;
}

/** True if node @p node plays nothing on any TX channel but those in @p used. */
inline bool Silent(const sim::AudioTrace& trace, int node, std::vector<unsigned> used) {
  for (unsigned tx = 0; tx < 32; ++tx) {
    bool listed = false;
    for (unsigned u : used) {
      listed = listed || (u == tx);
    }
    if (!listed && trace.Played(node, tx) != Signal()) {
      std::printf("  node %d TX %u plays node %d RX %u\n", node, tx, trace.Played(node, tx).node,
                  trace.Played(node, tx).channel);
      return false;
    }
  }
  return true;
}

/** The second customer system's modules: an AD2428 with a stereo codec (I2S on DTX0 and
    DRX0, RX 0 and TX 0 on channels 0-1), and an AD2427 with four PDM microphones (both PDM
    pins, two microphones each, I2S off; RX 0 on channels 0-3). */
inline std::vector<uint8_t> CodecImage() {
  sim::ModuleInfoRegs regs;
  regs.i2sgcfg = 0xE0;  // TDM2.
  regs.i2scfg = 0x11;   // TX0EN, RX0EN.
  ChannelGroups groups;
  groups.AddRx(0, 0x3);
  groups.AddTx(0, 0x3);
  return PortImage(regs, groups, 0);
}

/** See CodecImage(). */
inline std::vector<uint8_t> MicrophoneImage() {
  sim::ModuleInfoRegs regs;
  regs.pdmctl = 0x0F;  // PDM0EN, PDM0SLOTS, PDM1EN, PDM1SLOTS.
  ChannelGroups groups;
  groups.AddRx(0, 0xF);
  return PortImage(regs, groups, 0);
}

/** An AD2428 main board for the second customer system: TDM16 on DTX0 and DRX0; RX 0 on
    channels 0-1 (to the codec); TX 0, 1 and 2 on channels 0-3, 4-7 and 8-9, for what comes
    up from the last sub-node first, as an AD242x plays it. */
inline BoardConfig Ad2428Board() {
  BoardConfig board = test::MakeBoardFor(0x28);
  board.mainNode.Set(reg::kSlotfmt, 0x44);
  board.mainNode.Set(reg::kI2sgcfg, 0xE4);  // TDM16.
  board.mainNode.Set(reg::kI2scfg, 0x11);
  board.routing.groups.AddRx(0, 0x003);
  board.routing.groups.AddTx(0, 0x00F);
  board.routing.groups.AddTx(1, 0x0F0);
  board.routing.groups.AddTx(2, 0x300);
  return board;
}

/** The mixed chains' module with an @p product transceiver, any of the I2S
    ones: TDM8 on DTX0 and DRX0 (I2SCFG 0x11 on both families), 8 channels each way, in
    pairs: RX 0, 1 and 2 on channels 0-1, 2-3 and 4-5; TX 0 to 3 on 0-1, 2-3, 4-5 and 6-7.
    Its chip's power configuration. */
inline std::vector<uint8_t> PairImage(uint8_t product) {
  sim::ModuleInfoRegs regs;
  regs.i2sgcfg = 0x02;  // TDM8, 32-bit channels.
  regs.i2scfg = 0x11;
  ChannelGroups groups;
  for (uint8_t g = 0; g < 4; ++g) {
    if (g < 3) {
      groups.AddRx(g, 0x3u << (2 * g));
    }
    groups.AddTx(g, 0x3u << (2 * g));
  }
  return PortImage(regs, groups, PowerConfigOf(product));
}

/** An AD2427 with four PDM microphones (as MicrophoneImage()), in pairs too: RX 0 on
    channels 0-3, RX 1 on 0-1, RX 2 on 2-3 (as the AB0204). */
inline std::vector<uint8_t> MicPairImage() {
  sim::ModuleInfoRegs regs;
  regs.pdmctl = 0x0F;
  ChannelGroups groups;
  groups.AddRx(0, 0xF);
  groups.AddRx(1, 0x3);
  groups.AddRx(2, 0xC);
  return PortImage(regs, groups, 0);
}

/** A main board for the mixed chains, its main node an @p product (AD2428, AD2433 or
    AD2437): 24-bit slots, TDM16 on DTX0 and DRX0; RX 0 and 1 on channels 0-1 and 2-3, TX 0
    to 7 on the pairs 0-1 to 14-15. */
inline BoardConfig PairBoard(uint8_t product) {
  BoardConfig board = test::MakeBoardFor(product);
  board.mainNode.Set(reg::kSlotfmt, 0x44);
  board.mainNode.Set(reg::kI2sgcfg, 0x04);  // TDM16, 32-bit channels: RESPOFFS 248.
  board.mainNode.Set(reg::kI2scfg, 0x11);
  board.routing.groups.AddRx(0, 0x3);
  board.routing.groups.AddRx(1, 0xC);
  for (uint8_t g = 0; g < 8; ++g) {
    board.routing.groups.AddTx(g, 0x3u << (2 * g));
  }
  return board;
}

/** True if, in @p mid, no node -1 .. @p count - 1 plays anything on a TX channel but silence
    or what it plays in @p settled: what a mute must ensure at the instant a new structure
    takes effect. A node in only one of them (the chain changing) is skipped. */
inline bool NoWrongChannel(const sim::AudioTrace& mid, const sim::AudioTrace& settled, int count) {
  bool right = true;
  const size_t both = (mid.sub.size() < settled.sub.size()) ? mid.sub.size() : settled.sub.size();
  for (int node = -1; node < count && (node < 0 || static_cast<size_t>(node) < both); ++node) {
    for (unsigned tx = 0; tx < 32; ++tx) {
      const Signal now = mid.Played(node, tx);
      const Signal then = settled.Played(node, tx);
      if (now != Signal() && now != then) {
        std::printf("  node %d TX %u plays node %d RX %u, then node %d RX %u\n", node, tx, now.node,
                    now.channel, then.node, then.channel);
        right = false;
      }
    }
  }
  return right;
}

/** The audio when each CONTROL.NEWSTRCT took effect. */
struct NewStructAudio {
  explicit NewStructAudio(RoutingBench& bench) {
    bench.bus.SetNewStructObserver([this, &bench] { audio.push_back(bench.bus.TraceAudio()); });
  }
  std::vector<sim::AudioTrace> audio;
};

/** Passes the events on to the bench's recorder and counts the mute hooks. */
class MuteRecorder : public NetworkListener {
 public:
  explicit MuteRecorder(Recorder& inner) : m_Inner(inner) {}

  /** Records the event; notes the hooks called before a routing event. */
  void OnEvent(Network& network, const Event& event) override {
    if (event.type == EventType::kRoutingApplied) {
      unmutesAtApplied = unmutes;
    }
    m_Inner.OnEvent(network, event);
  }

  /** The recorder's. */
  RejectReason OnNodeIdentified(const NodeReview& review) override {
    return m_Inner.OnNodeIdentified(review);
  }

  /** Counts, keeps the scope and the bus's NEWSTRCT count, runs #onMute. */
  void OnAudioMute(Network& network, const MuteScope& scope) override {
    ++mutes;
    muteScope = scope;
    order += 'M';
    if (onMute) {
      onMute(network, scope);
    }
  }

  /** Counts, keeps the scope, runs #onUnmute. */
  void OnAudioUnmute(Network& network, const MuteScope& scope) override {
    ++unmutes;
    unmuteScope = scope;
    order += 'U';
    if (onUnmute) {
      onUnmute(network, scope);
    }
  }

  int mutes = 0;             ///< OnAudioMute() calls.
  int unmutes = 0;           ///< OnAudioUnmute() calls.
  int unmutesAtApplied = 0;  ///< Unmutes when the last kRoutingApplied was given.
  MuteScope muteScope;       ///< The last OnAudioMute() scope.
  MuteScope unmuteScope;     ///< The last OnAudioUnmute() scope.
  std::string order;         ///< 'M' and 'U' in the order they were called.
  std::function<void(Network&, const MuteScope&)> onMute;    ///< Run in OnAudioMute().
  std::function<void(Network&, const MuteScope&)> onUnmute;  ///< Run in OnAudioUnmute().

 private:
  Recorder& m_Inner;
};

}  // namespace test
}  // namespace a2b

#endif  // A2BCPP_TESTS_SCENARIO_ROUTING_BENCH_H_
