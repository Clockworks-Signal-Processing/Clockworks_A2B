/**
 * @file test_sim_chain.cpp
 * @brief Simulator scenarios for chains: 3 (chain of 3) and 7 (max sub-nodes, the app
 *        pausing discovery), with the link set-up before each probe (interrupt masks,
 *        polarity, the power-configuration check and its settle), the switch modes around
 *        each probe, the last node's switch mode, rejections in a chain, hot-plug at the
 *        end of the chain and the RESETn pulse at start-up.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "a2b/network.h"
#include "detail/regdefs.h"
#include "sim/module_info.h"
#include "sim/simulator.h"
#include "sim_steps.h"
#include "sim_trace.h"
#include "test_harness.h"

namespace a2b {
namespace {

using test::Access;
using test::Count;
using test::Decode;
using test::FailedTransfers;
using test::Find;
using test::kBroadcast;

constexpr uint8_t kMainAddr = 0x68;
constexpr uint8_t kBusAddr = 0x69;
/** A module-info source with one image for every node. */
class FixedSource : public ModuleInfoSource {
 public:
  explicit FixedSource(std::vector<uint8_t> image) : m_Image(std::move(image)) {}

  /** Copies the image. */
  size_t GetModuleInfo(const SubNodeIdentity& node, uint8_t* buf, size_t bufLen) override {
    (void)node;
    const size_t len = (m_Image.size() < bufLen) ? m_Image.size() : bufLen;
    for (size_t i = 0; i < len; ++i) {
      buf[i] = m_Image[i];
    }
    return len;
  }

 private:
  std::vector<uint8_t> m_Image;
};

/** A module with GPIO0-6 as inputs and PINCFG 0, in power configuration @p powerConfig. */
std::vector<uint8_t> MakeImage(uint8_t powerConfig = sim::kPowerConfigHighPower) {
  sim::ModuleInfoHeader header;
  header.powerConfig = powerConfig;
  sim::ModuleInfoRegs regs;
  regs.gpioien = 0x7F;
  regs.pincfg = 0x00;
  return sim::MakeModuleInfoImage(header, regs);
}

/** An AD2437 main node in CFG-4 with a small image (SWCTL 0x11, RESPCYCS 0xB9). */
BoardConfig MakeBoard() {
  BoardConfig board;
  board.mainProduct = 0x37;
  board.highPowerSwitchCfg = 4;
  RegisterImage& image = board.mainNode;
  image.Set(reg::kIntmsk0, 0x10);
  image.Set(reg::kIntmsk2, 0x08);
  image.Set(reg::kRespcycs, 0xB9);
  image.Set(reg::kControl, 0x00);
  image.Set(reg::kSwctl, 0x11);
  return board;
}

/** A chip that detects CFG-4, as the bench's AD2437 nodes do. */
sim::ChipSpec Chip(uint8_t productId = 0x37) {
  sim::ChipSpec chip;
  chip.productId = productId;
  chip.powerConfig = sim::kPowerConfigHighPower;
  chip.lowVoltageInput = true;
  return chip;
}

/** A sub-node of chip @p productId behind a plain UTP link. */
sim::SubNodeSpec Ad(uint8_t productId, uint8_t chipIdByte = 0) {
  sim::SubNodeSpec spec;
  spec.chip = Chip(productId);
  spec.chip.chipId[5] = chipIdByte;
  return spec;
}

/** A Platform that counts the reset-line calls and can refuse them. */
class ResetCountingSim : public sim::Simulator {
 public:
  /** Counts the call, then fails it or passes it on. */
  Result SetMainNodeReset(uint8_t bus, bool asserted) override {
    (asserted ? asserts : releases)++;
    if (fail) {
      return Result::kI2cError;
    }
    return Simulator::SetMainNodeReset(bus, asserted);
  }
  int asserts = 0;
  int releases = 0;
  bool fail = false;
};

/** A bus with a Network on it, polled every millisecond. */
struct Bench {
  ResetCountingSim sim;
  sim::Bus& bus;
  BoardConfig board;
  FixedSource source;
  Network network;

  explicit Bench(const BoardConfig& config = MakeBoard(),
                 const std::vector<uint8_t>& image = MakeImage())
      : bus(sim.AddBus(kMainAddr, kBusAddr, Chip())),
        board(config),
        source(image),
        network(sim, board) {
    network.SetModuleInfoSource(&source);
  }

  /** Polls every millisecond for @p ms. */
  void Run(uint32_t ms) {
    for (uint32_t i = 0; i < ms; ++i) {
      sim.Advance(1);
      network.Poll(sim.NowMs());
    }
  }

  /** Polls until the state is @p state, for at most @p ms. */
  bool RunUntil(NetworkState state, uint32_t ms = 5000) {
    for (uint32_t i = 0; i < ms && network.State() != state; ++i) {
      Run(1);
    }
    return network.State() == state;
  }

  /** Polls until @p count sub-nodes are in the bus, for at most @p ms. */
  bool RunUntilCount(size_t count, uint32_t ms = 5000) {
    for (uint32_t i = 0; i < ms && network.SubNodeCount() != count; ++i) {
      Run(1);
    }
    return network.SubNodeCount() == count;
  }

  /** The register accesses so far. */
  std::vector<Access> Accesses() const { return Decode(sim.Trace()); }
};

// Scenario 3: chain of 3.

A2B_TEST(Scenario3ChainOfThree) {
  Bench bench;
  for (uint8_t i = 0; i < 3; ++i) {
    bench.bus.Plug(Ad(0x37, static_cast<uint8_t>(0x10 + i)));
  }
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kDiscovering));
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 2000));
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 3);
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 3);
  for (NodeAddr n = 0; n < 3; ++n) {
    const SubNodeInfo* node = bench.network.SubNode(n);
    A2B_CHECK(node != nullptr && node->identity.position == n);
    A2B_CHECK(node != nullptr && node->identity.chipId[5] == 0x10 + n);
    A2B_CHECK_EQ(bench.bus.Sub(n).Reg(reg::kGpioien), 0x7F);
    A2B_CHECK_EQ(bench.bus.Sub(n).Reg(reg::kPincfg), 0x00);
    // Each has its masks, once it is the upstream node of a probe.
    A2B_CHECK_EQ(bench.bus.Sub(n).Reg(reg::kIntmsk0), reg::kIntmsk0Pwrerr);
    A2B_CHECK_EQ(bench.bus.Sub(n).Reg(reg::kIntmsk1), 0x00);
  }
  A2B_CHECK(bench.network.SubNode(3) == nullptr);
  // The switches: the main node and the first two sub-nodes power the next node; the last
  // keeps its mode with ENSW clear.
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kSwctl), 0x11);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kSwctl), 0x11);
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kSwctl), 0x11);
  A2B_CHECK_EQ(bench.bus.Sub(2).Reg(reg::kSwctl), 0x10);
  // DISCVRY: the TRMs' response cycles of each position from the main node's RESPCYCS;
  // each for n > 0 written with NODEADR on sub-node n - 1 (the TRMs' rule).
  const std::vector<Access> accesses = bench.Accesses();
  const uint8_t* expected = test::kBenchDiscvry;
  size_t from = 0;
  for (int n = 0; n < 4; ++n) {
    const int at = Find(accesses, from, kMainNode, true, reg::kDiscvry);
    A2B_CHECK(at >= 0);
    if (at < 0) {
      break;
    }
    A2B_CHECK_EQ(accesses[at].value, expected[n]);
    if (n > 0) {
      A2B_CHECK_EQ(accesses[at].nodeadr, n - 1);
    }
    from = static_cast<size_t>(at) + 1;
  }
  bool logged = false;
  for (const std::string& line : bench.sim.LogLines()) {
    logged = logged || line == "I: a2b: chain up with 3 sub-node(s)";
  }
  A2B_CHECK(logged);
  A2B_CHECK_EQ(bench.sim.UnlockedTransfers(), 0);
}

A2B_TEST(MixedChainUsesEachChipsSwitchMode) {
  // AD2437 main node, then AD2428, AD2433 and AD2427: the AD242x TRM's discovery flow
  // uses SWCTL 0x01, the AD243x TRMs' 0x11; only the AD243x have SWSTAT2.
  Bench bench;
  bench.bus.Plug(Ad(0x28));
  bench.bus.Plug(Ad(0x33));
  bench.bus.Plug(Ad(0x27));
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 2000));
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 3);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kSwctl), 0x01);
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kSwctl), 0x11);
  A2B_CHECK_EQ(bench.bus.Sub(2).Reg(reg::kSwctl), 0x00);  // The last: mode 0, ENSW clear.
  const std::vector<Access> accesses = bench.Accesses();
  A2B_CHECK_EQ(Count(accesses, 0, false, reg::kSwstat2), 0);
  A2B_CHECK_EQ(Count(accesses, 1, false, reg::kSwstat2), 1);
  A2B_CHECK_EQ(Count(accesses, 2, false, reg::kSwstat2), 0);
  // The inrush mode is mode 2 on both families.
  A2B_CHECK(Count(accesses, 0, true, reg::kSwctl, 0x21) >= 1);
}

// The link set-up and the switches around each probe.

A2B_TEST(UpstreamNodeIsSetUpInTheTrmOrderOnce) {
  // Before sub-node 0 first powers the next link: INTMSK0/1, CONTROL (XCVRBINV), SWSTAT2,
  // then SWCTL with ENSW (TRMs, "Discovery Flow"; XCVRBINV before ENSW).
  BoardConfig board = MakeBoard();
  board.defaultLink.bSideInverted = true;
  Bench bench(board);
  sim::SubNodeSpec spec = Ad(0x37);
  spec.link.bSideInverted = true;
  bench.bus.Plug(spec);
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 2000));
  bench.Run(1200);  // Two more probes behind sub-node 0.
  const std::vector<Access> accesses = bench.Accesses();
  const int masks = Find(accesses, 0, 0, true, reg::kIntmsk0, reg::kIntmsk0Pwrerr);
  const int masks1 = Find(accesses, 0, 0, true, reg::kIntmsk1, 0x00);
  const int control = Find(accesses, 0, 0, true, reg::kControl, reg::kControlXcvrbinv);
  const int check = Find(accesses, 0, 0, false, reg::kSwstat2);
  const int ensw = Find(accesses, 0, 0, true, reg::kSwctl, 0x11);
  A2B_CHECK(masks >= 0 && masks1 == masks + 1 && masks1 < control && control < check &&
            check < ensw);
  // Once: later probes only switch and probe.
  A2B_CHECK_EQ(Count(accesses, 0, true, reg::kIntmsk0), 1);
  A2B_CHECK_EQ(Count(accesses, 0, true, reg::kControl), 1);
  A2B_CHECK_EQ(Count(accesses, 0, false, reg::kSwstat2), 1);
  A2B_CHECK(Count(accesses, 0, true, reg::kSwctl, 0x11) >= 3);
  // No override: the chips detect what the board and the module info expect.
  A2B_CHECK_EQ(Count(accesses, kMainNode, true, reg::kSwctl2), 0);
  A2B_CHECK_EQ(Count(accesses, 0, true, reg::kSwctl2), 0);
  // The main node was checked once too, before its first probe.
  const int mainCheck = Find(accesses, 0, kMainNode, false, reg::kSwstat2);
  A2B_CHECK(mainCheck >= 0 && mainCheck < Find(accesses, 0, kMainNode, true, reg::kDiscvry));
  A2B_CHECK_EQ(Count(accesses, kMainNode, false, reg::kSwstat2), 1);
}

A2B_TEST(SwitchModesAroundAProbe) {
  // A probe behind sub-node 1 that finds nothing, as ADI's PnP layer does it: the
  // nodes before sub-node 1 go to mode 2 (0x21), sub-node 1 powers the link (0x11), then
  // after the timeout sub-node 1 opens it (0x10), ENDDSC, and the others go back to 0x11.
  Bench bench;
  bench.bus.Plug(Ad(0x37));
  bench.bus.Plug(Ad(0x37));
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 2000));
  bench.sim.ClearTrace();
  bench.Run(600);
  const std::vector<Access> a = bench.Accesses();
  const int mainInrush = Find(a, 0, kMainNode, true, reg::kSwctl, 0x21);
  const int s0Inrush = Find(a, 0, 0, true, reg::kSwctl, 0x21);
  const int s1On = Find(a, 0, 1, true, reg::kSwctl, 0x11);
  const int probe = Find(a, 0, kMainNode, true, reg::kDiscvry, test::kBenchDiscvry[2]);
  const int s1Off = Find(a, 0, 1, true, reg::kSwctl, 0x10);
  const int end =
      Find(a, 0, kMainNode, true, reg::kControl, reg::kControlMstr | reg::kControlEnddsc);
  const int mainOn = Find(a, 0, kMainNode, true, reg::kSwctl, 0x11);
  const int s0On = Find(a, 0, 0, true, reg::kSwctl, 0x11);
  A2B_CHECK(mainInrush >= 0 && mainInrush < s0Inrush && s0Inrush < s1On && s1On < probe &&
            probe < s1Off && s1Off < end && end < mainOn && mainOn < s0On);
  A2B_CHECK(probe >= 0 && a[probe].nodeadr == 1);
  A2B_CHECK(end >= 0 && a[end].timeMs - a[probe].timeMs >= 70);  // discoveryTimeoutMs.
  A2B_CHECK(!bench.bus.DiscoveryActive());
}

A2B_TEST(IdentificationUsesTheInrushMode) {
  // A node found behind sub-node 0: sub-node 0 goes to mode 2 at DSCDONE while the new
  // node's IDs are read, then the main node and sub-node 0 go back to 0x11, then CHIPID.
  Bench bench;
  bench.bus.Plug(Ad(0x37));
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 2000));
  bench.sim.ClearTrace();
  bench.bus.Plug(Ad(0x37));
  A2B_CHECK(bench.RunUntilCount(2, 1000));
  const std::vector<Access> a = bench.Accesses();
  const int probe = Find(a, 0, kMainNode, true, reg::kDiscvry, test::kBenchDiscvry[1]);
  const int s0Inrush = Find(a, static_cast<size_t>(probe), 0, true, reg::kSwctl, 0x21);
  const int ids = Find(a, 0, 1, false, reg::kVendor);
  const int mainOn = Find(a, static_cast<size_t>(ids), kMainNode, true, reg::kSwctl, 0x11);
  const int s0On = Find(a, static_cast<size_t>(ids), 0, true, reg::kSwctl, 0x11);
  const int chipId = Find(a, 0, 1, false, reg::kChipid0);
  A2B_CHECK(probe >= 0 && probe < s0Inrush && s0Inrush < ids && ids < mainOn && mainOn < s0On &&
            s0On < chipId);
  A2B_CHECK(bench.network.State() == NetworkState::kRunning);  // Hot-plug at the end.
}

A2B_TEST(PowerConfigurationIsOverriddenAndSettles) {
  // Sub-node 0 detects CFG-0 but its module info says CFG-4: it is overridden (SWCTL2 = 4,
  // SWCTL.CFG_DET_OV), and its switch closes no sooner than 100 ms later (TRMs; ADI's
  // A2B_HIGH_PWR_SWITCH_DELAY).
  Bench bench;
  sim::SubNodeSpec spec = Ad(0x37);
  spec.chip.powerConfig = 0;
  bench.bus.Plug(spec);
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 3000));
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  const std::vector<Access> a = bench.Accesses();
  // Sub-node 0: SWCTL2, then SWCTL 0x12 (override, switch open), then 0x13 >= 100 ms later.
  const int s0Cfg = Find(a, 0, 0, true, reg::kSwctl2, 4);
  const int s0Ov = Find(a, 0, 0, true, reg::kSwctl, 0x12);
  const int s0On = Find(a, 0, 0, true, reg::kSwctl, 0x13);
  A2B_CHECK(s0Cfg >= 0 && s0Cfg < s0Ov && s0Ov < s0On);
  A2B_CHECK(s0On >= 0 && a[s0On].timeMs - a[s0Ov].timeMs >= 100);
  A2B_CHECK_EQ(bench.bus.Sub(0).PowerConfig(), 4);
  // Later writes keep CFG_DET_OV: the last node's switch mode is 0x12, its inrush 0x23.
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kSwctl), 0x12);
  // The main node detected CFG-4 here (Chip()), as the board says: not overridden.
  A2B_CHECK_EQ(Count(a, kMainNode, true, reg::kSwctl2), 0);
}

A2B_TEST(MainNodePowerConfigurationAsDetectedIsNotChecked) {
  // A hand-filled BoardConfig that doesn't name the power configuration keeps the chip's.
  BoardConfig board = MakeBoard();
  board.highPowerSwitchCfg = BoardConfig().highPowerSwitchCfg;
  A2B_CHECK_EQ(board.highPowerSwitchCfg, kPowerConfigAsDetected);
  Bench bench(board);
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 2000));
  const std::vector<Access> a = bench.Accesses();
  A2B_CHECK_EQ(Count(a, kMainNode, false, reg::kSwstat2), 0);
  A2B_CHECK_EQ(Count(a, kMainNode, true, reg::kSwctl2), 0);
  A2B_CHECK_EQ(bench.bus.Main().PowerConfig(), 4);
}

A2B_TEST(MainNodePowerConfigurationIsOverridden) {
  BoardConfig board = MakeBoard();
  board.highPowerSwitchCfg = 0;  // The chip detects CFG-4.
  Bench bench(board);
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 2000));
  const std::vector<Access> a = bench.Accesses();
  const int cfg = Find(a, 0, kMainNode, true, reg::kSwctl2, 0);
  const int ov = Find(a, 0, kMainNode, true, reg::kSwctl, 0x12);
  const int on = Find(a, 0, kMainNode, true, reg::kSwctl, 0x13);
  const int probe = Find(a, 0, kMainNode, true, reg::kDiscvry);
  A2B_CHECK(cfg >= 0 && cfg < ov && ov < on && on < probe);
  A2B_CHECK(probe >= 0 && a[probe].timeMs - a[ov].timeMs >= 100);
  A2B_CHECK_EQ(bench.bus.Main().PowerConfig(), 0);
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kSwctl), 0x12);  // Nothing found: switch open.
}

// Scenario 7: max sub-nodes; the app pauses probing.

A2B_TEST(Scenario7MaxSubNodes) {
  BoardConfig board = MakeBoard();
  board.policy.maxSubNodes = 3;
  Bench bench(board);
  for (int i = 0; i < 5; ++i) {
    bench.bus.Plug(Ad(0x37));
  }
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 2000));
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 3);
  bench.Run(2000);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 3);
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 3);
  // No probe behind the third node: its switch was never closed.
  const std::vector<Access> a = bench.Accesses();
  A2B_CHECK_EQ(Count(a, kMainNode, true, reg::kDiscvry), 3);
  A2B_CHECK_EQ(bench.bus.Sub(2).Reg(reg::kSwctl), 0x00);
}

A2B_TEST(Scenario7SixteenSubNodes) {
  // The hardware limit, NODEADR.NODE's 4 bits: the 16th node is the last probed for.
  BoardConfig board = MakeBoard();
  board.policy.maxSubNodes = kMaxSubNodes;
  Bench bench(board);
  for (int i = 0; i < 17; ++i) {
    bench.bus.Plug(Ad(0x37, static_cast<uint8_t>(i)));
  }
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 5000));
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 16);
  const SubNodeInfo* last = bench.network.SubNode(15);
  A2B_CHECK(last != nullptr && last->identity.chipId[5] == 15);
  const std::vector<Access> a = bench.Accesses();
  A2B_CHECK_EQ(Count(a, kMainNode, true, reg::kDiscvry), 16);
  A2B_CHECK_EQ(Count(a, kMainNode, true, reg::kDiscvry, test::kBenchDiscvry[15]), 1);
}

A2B_TEST(Scenario7AppPausesProbing) {
  Bench bench;
  bench.bus.Plug(Ad(0x37));
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 2000));
  A2B_CHECK(!bench.network.DiscoveryPaused());
  bench.network.PauseDiscovery();
  A2B_CHECK(bench.network.DiscoveryPaused());
  bench.Run(100);  // A probe under way finishes.
  const int probes = Count(bench.Accesses(), kMainNode, true, reg::kDiscvry);
  bench.bus.Plug(Ad(0x37));
  bench.Run(2000);
  A2B_CHECK_EQ(Count(bench.Accesses(), kMainNode, true, reg::kDiscvry), probes);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK(bench.network.State() == NetworkState::kRunning);
  bench.network.ResumeDiscovery();
  A2B_CHECK(!bench.network.DiscoveryPaused());
  A2B_CHECK(bench.RunUntilCount(2, 200));
}

A2B_TEST(PausedBeforeStartBringsUpTheMainNodeOnly) {
  Bench bench;
  bench.bus.Plug(Ad(0x37));
  bench.network.PauseDiscovery();
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 2000));
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 0);
  A2B_CHECK_EQ(Count(bench.Accesses(), kMainNode, true, reg::kDiscvry), 0);
  bench.network.ResumeDiscovery();
  A2B_CHECK(bench.RunUntilCount(1, 200));
}

// Rejections and hot-plug in a chain.

A2B_TEST(RejectionEndsTheChain) {
  Bench bench;
  bench.bus.Plug(Ad(0x37));
  bench.bus.Plug(Ad(0x10));  // AD2410: not supported.
  bench.bus.Plug(Ad(0x37));
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 2000));
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kUnsupportedChip);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kSwctl), 0x10);  // Switched off.
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 1);           // So it dropped out.
  // Its position is probed again every rejectedProbeMs; the same node is switched off
  // again straight after its CHIPID, and nothing after it is probed.
  const int probes = Count(bench.Accesses(), kMainNode, true, reg::kDiscvry);
  bench.Run(bench.board.timings.rejectedProbeMs - 10);
  A2B_CHECK_EQ(Count(bench.Accesses(), kMainNode, true, reg::kDiscvry), probes);
  bench.sim.ClearTrace();
  bench.Run(100);
  const std::vector<Access> a = bench.Accesses();
  A2B_CHECK_EQ(Count(a, kMainNode, true, reg::kDiscvry), 1);
  A2B_CHECK_EQ(Count(a, 1, false, reg::kChipid0), 1);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kSwctl), 0x10);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  // The next Start() tries again, and meets the same node.
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kNone);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 2000));
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kUnsupportedChip);
}

A2B_TEST(HotPlugAtTheEndOfTheChain) {
  Bench bench;
  bench.bus.Plug(Ad(0x37));
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 2000));
  bench.Run(1000);
  bench.bus.Plug(Ad(0x28));
  bench.bus.Plug(Ad(0x37));
  A2B_CHECK(bench.RunUntilCount(3, 1000));  // Found by one idle probe, then at once.
  A2B_CHECK(bench.network.State() == NetworkState::kRunning);
  const SubNodeInfo* node = bench.network.SubNode(1);
  A2B_CHECK(node != nullptr && node->identity.productId == 0x28);
}

// The RESETn pulse at start-up.

A2B_TEST(ResetPulseAtStart) {
  Bench bench;
  bench.bus.SetHasResetLine(true);
  bench.bus.StartRunning();  // Left running: RESETn puts it back in POWER-UP.
  bench.bus.Plug(Ad(0x37));
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK_EQ(bench.sim.asserts, 0);  // Nothing happens in Start().
  bench.Run(1);
  A2B_CHECK_EQ(bench.sim.asserts, 1);
  A2B_CHECK(!bench.bus.Running());
  bench.Run(bench.board.timings.resetPulseMs - 1);
  A2B_CHECK_EQ(bench.sim.releases, 0);
  A2B_CHECK_EQ(bench.sim.TransferCount(), 0);
  bench.Run(1);
  A2B_CHECK_EQ(bench.sim.releases, 1);
  // The first access waits tACCESS; the simulator NAKs any earlier one.
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 2000));
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  const std::vector<Access> a = bench.Accesses();
  A2B_CHECK(!a.empty() && a[0].timeMs >= bench.board.timings.resetPulseMs + 2);
  A2B_CHECK_EQ(FailedTransfers(bench.sim.Trace()), 0);
  // No SUSTAIN: the soft reset came in POWER-UP, where it has no effect.
  A2B_CHECK(!bench.bus.Wedged());
}

A2B_TEST(NoResetPulseOnAPllRelock) {
  Bench bench;
  bench.bus.SetHasResetLine(true);
  bench.bus.Plug(Ad(0x37));
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 2000));
  bench.bus.SetSync(false);
  bench.Run(10);
  bench.bus.SetSync(true);
  A2B_CHECK(bench.RunUntil(NetworkState::kStarting, 100));
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 2000));
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK_EQ(bench.sim.asserts, 1);  // Only the one at Start().
}

A2B_TEST(ResetLineFailureIsLoggedAndSkipped) {
  Bench bench;
  bench.sim.fail = true;
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 2000));
  bool logged = false;
  for (const std::string& line : bench.sim.LogLines()) {
    logged = logged || line == "W: a2b: RESETn failed; starting without it";
  }
  A2B_CHECK(logged);
  A2B_CHECK_EQ(bench.sim.releases, 0);
}

A2B_TEST(ResetPulseLengthIsTheBoards) {
  BoardConfig board = MakeBoard();
  board.timings.resetPulseMs = 5;
  Bench bench(board);
  bench.bus.SetHasResetLine(true);
  A2B_CHECK(bench.network.Start() == Result::kOk);
  bench.Run(5);
  A2B_CHECK_EQ(bench.sim.releases, 0);
  bench.Run(1);
  A2B_CHECK_EQ(bench.sim.releases, 1);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 2000));
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
