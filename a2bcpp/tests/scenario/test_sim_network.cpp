/**
 * @file test_sim_network.cpp
 * @brief Simulator scenarios for Network: 1 (main node idle), 2 (single hot-plug) and 9 (no
 *        MSTR_RUNNING), with the start-up sequence, the image write, the host script,
 *        module info, rejections and restarts. Chains are in test_sim_chain.cpp.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "a2b/network.h"
#include "detail/regdefs.h"
#include "sim/module_info.h"
#include "sim/simulator.h"
#include "sim_steps.h"
#include "test_harness.h"

namespace a2b {
namespace {

using sim::Simulator;

constexpr uint8_t kMainAddr = 0x68;
constexpr uint8_t kBusAddr = 0x69;
constexpr uint8_t kCodecAddr = 0x39;

/** A module-info source with one image for every node, or none. */
class FixedSource : public ModuleInfoSource {
 public:
  explicit FixedSource(std::vector<uint8_t> image) : m_Image(std::move(image)) {}

  size_t GetModuleInfo(const SubNodeIdentity& node, uint8_t* buf, size_t bufLen) override {
    last = node;
    ++calls;
    const size_t len = (m_Image.size() < bufLen) ? m_Image.size() : bufLen;
    for (size_t i = 0; i < len; ++i) {
      buf[i] = m_Image[i];
    }
    return len;
  }

  SubNodeIdentity last;
  int calls = 0;

 private:
  std::vector<uint8_t> m_Image;
};

/** A host peripheral that records when it was written. */
class Codec : public sim::I2cDevice {
 public:
  Result Write(const uint8_t* data, size_t len, uint32_t nowMs) override {
    (void)data;
    (void)len;
    writeTimes.push_back(nowMs);
    return Result::kOk;
  }
  Result Read(uint8_t* data, size_t len, uint32_t nowMs) override {
    (void)nowMs;
    for (size_t i = 0; i < len; ++i) {
      data[i] = 0;
    }
    return Result::kOk;
  }
  std::vector<uint32_t> writeTimes;
};

/** The settings of an AD2437 main node, as a small hand-made image. */
BoardConfig MakeBoard(uint8_t mainProduct = 0x37) {
  BoardConfig board;
  board.mainProduct = mainProduct;
  RegisterImage& image = board.mainNode;
  image.Set(reg::kI2sgcfg, 0xE2);  // INV set.
  image.Set(reg::kI2scfg, 0x21);
  image.Set(reg::kPincfg, 0x00);
  image.Set(reg::kClk2cfg, 0x81);
  image.Set(reg::kIntmsk0, 0x10);
  image.Set(reg::kIntmsk1, 0x00);
  image.Set(reg::kIntmsk2, 0x08);  // No DSCDONE: the stack adds it.
  image.Set(reg::kBecctl, 0xEF);
  image.Set(reg::kSlotfmt, 0x44);
  image.Set(reg::kDatctl, 0x03);
  image.Set(reg::kRespcycs, 0xB9);
  image.Set(reg::kControl, 0x00);
  image.Set(reg::kSwctl, 0x11);
  return board;
}

/** The module info of a plain AD2437 module with a few settings. */
std::vector<uint8_t> MakeImage() {
  sim::ModuleInfoRegs regs;
  regs.i2scfg = 0x19;
  regs.i2sgcfg = 0xE0;
  regs.gpioien = 0x7F;
  regs.pincfg = 0x00;
  regs.spicfg = 0x19;
  regs.clk2cfg = 0x81;
  sim::ModuleInfoHeader header;
  header.powerConfig = sim::kPowerConfigHighPower;
  return sim::MakeModuleInfoImage(header, regs);
}

/** A bus with a Network on it, polled every millisecond. */
struct Bench {
  Simulator sim;
  sim::Bus& bus;
  BoardConfig board;
  FixedSource source;
  Network network;

  explicit Bench(const BoardConfig& config = MakeBoard(),
                 const sim::ChipSpec& mainChip = sim::ChipSpec())
      : bus(sim.AddBus(kMainAddr, kBusAddr, mainChip)),
        board(config),
        source(MakeImage()),
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

  /** Counts host writes of @p value to main-node register @p reg in the trace. */
  int CountMainWrites(uint8_t reg, int value = -1) const {
    // "I2CT,<seq>,<us>,0,68,W,0,2,<reg><value>,..." for one-register writes.
    char want[16];
    std::snprintf(want, sizeof(want), ",68,W,0,2,%02X", reg);
    int count = 0;
    const std::string trace = sim.Trace();
    for (size_t at = trace.find(want); at != std::string::npos; at = trace.find(want, at + 1)) {
      if (value < 0 || std::stoi(trace.substr(at + 12, 2), nullptr, 16) == value) {
        ++count;
      }
    }
    return count;
  }
};

/** A sub-node with @p productId that detects CFG-4, as MakeImage() says. */
sim::SubNodeSpec Ad(uint8_t productId) {
  sim::SubNodeSpec spec;
  spec.chip.productId = productId;
  spec.chip.powerConfig = sim::kPowerConfigHighPower;
  return spec;
}

// Scenario 1: main node idle.

A2B_TEST(Scenario1MainNodeIdle) {
  Bench bench;
  A2B_CHECK(bench.network.State() == NetworkState::kStopped);
  bench.Run(10);
  A2B_CHECK_EQ(bench.sim.TransferCount(), 0);  // Nothing before Start().
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kDiscovering));
  A2B_CHECK(bench.bus.Running());
  A2B_CHECK(!bench.bus.Wedged());
  const sim::Node& main = bench.bus.Main();
  A2B_CHECK_EQ(main.Reg(reg::kI2sgcfg), 0xE2);
  A2B_CHECK_EQ(main.Reg(reg::kI2scfg), 0x21);
  A2B_CHECK_EQ(main.Reg(reg::kPincfg), 0x00);
  A2B_CHECK_EQ(main.Reg(reg::kSlotfmt), 0x44);
  A2B_CHECK_EQ(main.Reg(reg::kDatctl), 0x03);
  A2B_CHECK_EQ(main.Reg(reg::kIntmsk2), 0x08 | reg::kIntmsk2Dscdone);

  // The first probe finds nothing: the chain is up, with no sub-nodes.
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 100));
  A2B_CHECK_EQ(bench.CountMainWrites(reg::kDiscvry, 0xB9), 1);
  // Probes every probeIntervalMs + discoveryTimeoutMs; nothing answers.
  bench.Run(2000);
  A2B_CHECK(bench.network.State() == NetworkState::kRunning);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 0);
  const int probes = bench.CountMainWrites(reg::kDiscvry, 0xB9);
  A2B_CHECK(probes >= 4 && probes <= 6);
  // Each probe ends with ENDDSC and the switch opened.
  A2B_CHECK_EQ(main.Reg(reg::kSwctl), 0x10);
  A2B_CHECK(!bench.bus.DiscoveryActive());
  A2B_CHECK_EQ(bench.sim.UnlockedTransfers(), 0);
}

A2B_TEST(StartUpWaitsOutSustainOnARunningMainNode) {
  // The main node is still running from an earlier session (no RESETn at start-up), and
  // the board asks for less than the 25 ms minimum.
  BoardConfig board = MakeBoard();
  board.timings.rediscoveryWaitMs = 5;
  Bench bench(board);
  bench.bus.StartRunning();
  A2B_CHECK(bench.network.Start() == Result::kOk);
  bench.Run(1);
  A2B_CHECK(bench.bus.InSustain());
  bench.Run(23);
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kControl), 0);  // No MSTR yet.
  A2B_CHECK(bench.RunUntil(NetworkState::kDiscovering));
  A2B_CHECK(!bench.bus.Wedged());
  A2B_CHECK(bench.bus.Running());
}

A2B_TEST(StartUpSequence) {
  // VENDOR/PRODUCT/VERSION, soft reset (MSTR = 0), rediscoveryWaitMs, INV alone, MSTR.
  Bench bench;
  bench.bus.StartRunning();
  bench.sim.SetNowMs(1000);
  A2B_CHECK(bench.network.Start() == Result::kOk);
  bench.Run(260);
  const std::string trace = bench.sim.Trace();
  const char* const kExpected[] = {
      "I2CT,0,1001000,0,68,WR,0,1,02,3,AD3721",
      "I2CT,1,1001000,0,68,W,0,2,1204,0,",
      "I2CT,2,1251000,0,68,W,0,2,4180,0,",
      "I2CT,3,1251000,0,68,W,0,2,1280,0,",
  };
  for (const char* line : kExpected) {
    if (trace.find(line) == std::string::npos) {
      std::printf("missing: %s\n%s", line, trace.substr(0, 600).c_str());
      A2B_CHECK(false);
    }
  }
}

A2B_TEST(NoInvWhenTheImageHasNone) {
  BoardConfig board = MakeBoard();
  board.mainNode.Set(reg::kI2sgcfg, 0x62);
  Bench bench(board);
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kDiscovering));
  A2B_CHECK_EQ(bench.CountMainWrites(reg::kI2sgcfg), 0);  // Only in a burst with I2SCFG.
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kI2sgcfg), 0x62);
}

A2B_TEST(ImageSkipsStackRegistersAndUsesBursts) {
  BoardConfig board = MakeBoard();
  board.mainNode.Set(reg::kNodeadr, 0x05);
  board.mainNode.Set(reg::kDiscvry, 0x33);
  board.mainNode.Set(reg::kIntpnd2, 0xFF);
  // The routing registers are the routing plan's: the image's are not written.
  board.mainNode.Set(reg::kDnslots, 0x0C);
  board.mainNode.Set(reg::kTxxbar0 + 1, 0x07);
  board.mainNode.Set(reg::kRxmask0, 0x03);
  for (int i = 0; i < 20; ++i) {  // GPIODEN to MBOX0B1, 0x80-0x93.
    board.mainNode.Set(static_cast<uint16_t>(reg::kGpioden + i), static_cast<uint8_t>(31 - i));
  }
  Bench bench(board);
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kDiscovering));
  A2B_CHECK_EQ(bench.CountMainWrites(reg::kNodeadr, 0x05), 0);
  A2B_CHECK_EQ(bench.CountMainWrites(reg::kDiscvry, 0x33), 0);
  A2B_CHECK_EQ(bench.CountMainWrites(reg::kDnslots), 0);
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kTxxbar0 + 1), 0x01);  // Its power-on value.
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kRxmask0), 0xFF);
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kGpioden + 19), 12);
  // 0x80-0x93 in two transfers (16 + 4), not twenty.
  A2B_CHECK_EQ(bench.CountMainWrites(reg::kGpioden + 1), 0);
  A2B_CHECK(bench.sim.Trace().find(",68,W,0,17,80") != std::string::npos);
  A2B_CHECK(bench.sim.Trace().find(",68,W,0,5,90") != std::string::npos);
}

A2B_TEST(HookEditsThisStartUpsImage) {
  struct Hooks : BoardHooks {
    void BeforeMainNodeInit(RegisterImage& image) override {
      image.Set(reg::kClk2cfg, 0x00);
      ++calls;
    }
    int calls = 0;
  } hooks;
  BoardConfig board = MakeBoard();
  board.hooks = &hooks;
  Bench bench(board);
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK_EQ(hooks.calls, 1);
  A2B_CHECK(bench.RunUntil(NetworkState::kDiscovering));
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kClk2cfg), 0x00);
  uint8_t value = 0;
  A2B_CHECK(bench.board.mainNode.Get(reg::kClk2cfg, value) && value == 0x81);  // Unchanged.
}

A2B_TEST(HostScriptRunsBeforeDiscovery) {
  static const uint8_t kPll[] = {0x00, 0x01};
  FixedOpList<4> script;
  script.Add(Op::PeripheralWrite(kMainNode, kCodecAddr, 2, 0x4002, kPll, sizeof(kPll)));
  script.Add(Op::Wait(100));
  script.Add(Op::PeripheralWrite(kMainNode, kCodecAddr, 2, 0x4000, kPll, sizeof(kPll)));
  BoardConfig board = MakeBoard();
  board.hostScript = &script;
  Bench bench(board);
  Codec codec;
  bench.sim.AttachHostDevice(kCodecAddr, codec);
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kConfiguring));
  A2B_CHECK(bench.RunUntil(NetworkState::kDiscovering));
  A2B_CHECK_EQ(codec.writeTimes.size(), 2u);
  A2B_CHECK(codec.writeTimes.size() == 2 && codec.writeTimes[1] - codec.writeTimes[0] >= 100);
  A2B_CHECK_EQ(bench.CountMainWrites(reg::kDiscvry), 0);  // The probe comes at the next poll,
  bench.Run(4);  // and its DISCVRY after the NEWSTRCT settle.
  A2B_CHECK_EQ(bench.CountMainWrites(reg::kDiscvry), 1);
}

A2B_TEST(HostScriptFailureStopsTheBus) {
  static const uint8_t kData[] = {0x00};
  FixedOpList<2> script;
  script.Add(Op::PeripheralWrite(kMainNode, kCodecAddr, 1, 0, kData, 1));  // Nothing there.
  BoardConfig board = MakeBoard();
  board.hostScript = &script;
  Bench bench(board);
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kFailed));
  A2B_CHECK(bench.network.Error() == NetworkError::kHostScriptFailed);
}

A2B_TEST(InterruptChecksFollowTheInterval) {
  Bench bench;
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kDiscovering));
  bench.Run(5);
  bench.sim.ClearTrace();
  bench.Run(1000);
  // One INTSRC read every 20 ms, and one at the end of each probe.
  const std::string trace = bench.sim.Trace();
  int reads = 0;
  for (size_t at = trace.find(",68,WR,0,1,16,"); at != std::string::npos;
       at = trace.find(",68,WR,0,1,16,", at + 1)) {
    ++reads;
  }
  A2B_CHECK(reads >= 50 && reads <= 55);
}

A2B_TEST(IrqLineGatesTheChecks) {
  Bench bench;
  bench.bus.SetHasIrqLine(true);
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kDiscovering));
  bench.Run(5);
  bench.sim.ClearTrace();
  bench.Run(1000);
  A2B_CHECK(bench.sim.Trace().find(",68,WR,0,1,16,") == std::string::npos);
}

// Scenario 2: single hot-plug.

A2B_TEST(Scenario2SingleHotPlug) {
  Bench bench;
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning));
  bench.Run(1200);  // A few empty probes.
  sim::SubNodeSpec spec = Ad(0x37);
  const uint8_t chipId[6] = {0x18, 0xAC, 0xEE, 0x04, 0x23, 0x16};
  for (int i = 0; i < 6; ++i) {
    spec.chip.chipId[i] = chipId[i];
  }
  bench.bus.Plug(spec);
  A2B_CHECK(bench.RunUntilCount(1, 1000));
  A2B_CHECK(bench.network.State() == NetworkState::kRunning);
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kNone);
  const SubNodeInfo* node = bench.network.SubNodeInfoAt(0);
  A2B_CHECK(node != nullptr);
  A2B_CHECK(bench.network.SubNodeInfoAt(1) == nullptr);
  A2B_CHECK(bench.network.SubNodeInfoAt(kMainNode) == nullptr);
  if (node != nullptr) {
    A2B_CHECK_EQ(node->identity.position, 0);
    A2B_CHECK_EQ(node->identity.vendorId, 0xAD);
    A2B_CHECK_EQ(node->identity.productId, 0x37);
    A2B_CHECK_EQ(node->identity.version, 0x21);
    A2B_CHECK_EQ(node->identity.capability, 0x03);
    A2B_CHECK_EQ(node->identity.chipId[5], 0x16);
    A2B_CHECK_EQ(node->moduleInfo.productId, 0x0037);
    A2B_CHECK_EQ(node->moduleInfo.powerConfig, 4);
  }
  // The source was asked with the node's identity.
  A2B_CHECK_EQ(bench.source.calls, 1);
  A2B_CHECK_EQ(bench.source.last.chipId[0], 0x18);
  // The module info's settings, SPI included on an AD2437.
  const sim::Node& sub = bench.bus.Sub(0);
  A2B_CHECK_EQ(sub.Reg(reg::kI2scfg), 0x19);
  A2B_CHECK_EQ(sub.Reg(reg::kI2sgcfg), 0xE0);
  A2B_CHECK_EQ(sub.Reg(reg::kGpioien), 0x7F);
  A2B_CHECK_EQ(sub.Reg(reg::kPincfg), 0x00);
  A2B_CHECK_EQ(sub.Reg(reg::kClk2cfg), 0x81);
  A2B_CHECK_EQ(sub.Reg(reg::kSpicfg), 0x19);
  // The main node's switch was in mode 2 while the node was identified (ADI's
  // 0x21), then back in its mode with ENSW (0x11).
  A2B_CHECK_EQ(bench.CountMainWrites(reg::kSwctl, 0x21), 1);
  bench.Run(80);
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kSwctl), 0x11);
  // Probing goes on behind sub-node 0, with position 1's DISCVRY; sub-node 0, the last node,
  // keeps its switch mode with ENSW clear (0x10 in ADI's stack).
  const int probes = bench.CountMainWrites(reg::kDiscvry, test::kBenchDiscvry[1]);
  A2B_CHECK(probes >= 1);
  bench.Run(1200);  // probeIntervalMs + discoveryTimeoutMs apart.
  A2B_CHECK(bench.CountMainWrites(reg::kDiscvry, test::kBenchDiscvry[1]) >= probes + 2);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kSwctl), 0x10);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK(bench.network.State() == NetworkState::kRunning);
}

A2B_TEST(NodePresentAtStartIsFoundByTheFirstProbe) {
  Bench bench;
  bench.bus.Plug(Ad(0x37));
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 1000));
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK_EQ(bench.CountMainWrites(reg::kDiscvry, 0xB9), 1);
  A2B_CHECK_EQ(bench.CountMainWrites(reg::kDiscvry, test::kBenchDiscvry[1]),
               1);  // The probe behind it.
}

A2B_TEST(Ad242xSubNodeGetsNoSpiSettings) {
  Bench bench;
  bench.bus.Plug(Ad(0x28));
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 1000));
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kI2scfg), 0x19);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kSpicfg), 0x00);
}

A2B_TEST(EveryChipAsMainNode) {
  // Every main-node capable chip (the AD2427 is not: StartChecksTheBoard).
  for (uint8_t productId : {0x28, 0x33, 0x37}) {
    sim::ChipSpec chip;
    chip.productId = productId;
    Bench bench(MakeBoard(productId), chip);
    bench.bus.StartRunning();
    bench.bus.Plug(Ad(0x37));
    A2B_CHECK(bench.network.Start() == Result::kOk);
    A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 2000));
    A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
    A2B_CHECK(!bench.bus.Wedged());
  }
}

A2B_TEST(InvertedLink) {
  // The link profile sets XCVRBINV, which the node's link needs.
  sim::SubNodeSpec spec = Ad(0x37);
  spec.link.bSideInverted = true;
  {
    Bench bench;
    bench.bus.Plug(spec);
    A2B_CHECK(bench.network.Start() == Result::kOk);
    bench.Run(2000);
    A2B_CHECK_EQ(bench.network.SubNodeCount(), 0);
  }
  {
    BoardConfig board = MakeBoard();
    board.defaultLink.bSideInverted = true;
    Bench bench(board);
    bench.bus.Plug(spec);
    A2B_CHECK(bench.network.Start() == Result::kOk);
    A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 1000));
    A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
    A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kControl), reg::kControlMstr | reg::kControlXcvrbinv);
    // Behind sub-node 0 its image's B port decides: UTP, not inverted.
    A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kControl), 0x00);
  }
  {
    // The app's link wins over the image's.
    BoardConfig board = MakeBoard();
    board.defaultLink.bSideInverted = true;
    Bench bench(board);
    LinkProfile inverted = LinkProfile::Utp();
    inverted.bSideInverted = true;
    A2B_CHECK(bench.network.SetLinkProfile(1, inverted) == Result::kOk);
    bench.bus.Plug(spec);
    A2B_CHECK(bench.network.Start() == Result::kOk);
    A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 1000));
    A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kControl), reg::kControlXcvrbinv);
    bench.Run(1200);  // Idle probes behind sub-node 0 and chain checks.
    // Scenario 10: XCVRBINV was written before ENSW on each node and kept after it.
    A2B_CHECK_EQ(bench.bus.OrderErrors(), 0);
  }
}

A2B_TEST(CustomLinkLeavesTheSwitchOpen) {
  sim::SubNodeSpec spec = Ad(0x37);
  spec.link.power = sim::LinkPower::kSeparateWires;
  BoardConfig board = MakeBoard();
  board.defaultLink = LinkProfile::Custom();
  Bench bench(board);
  bench.bus.Plug(spec);
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 1000));
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kSwctl), 0x10);
  A2B_CHECK_EQ(bench.CountMainWrites(reg::kSwctl, 0x11), 0);
  A2B_CHECK_EQ(bench.CountMainWrites(reg::kSwctl, 0x21), 0);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kSwctl), 0x10);  // Never closed.
}

A2B_TEST(Rejections) {
  {
    Bench bench;
    bench.bus.Plug(Ad(0x10));  // AD2410: not supported.
    A2B_CHECK(bench.network.Start() == Result::kOk);
    A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 1000));
    A2B_CHECK(bench.network.LastRejection() == RejectReason::kUnsupportedChip);
    A2B_CHECK_EQ(bench.network.SubNodeCount(), 0);
    A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kSwctl), 0x10);  // Switched off.
    A2B_CHECK_EQ(bench.source.calls, 0);
  }
  {
    Bench bench;
    bench.network.SetModuleInfoSource(nullptr);
    bench.bus.Plug(Ad(0x37));
    A2B_CHECK(bench.network.Start() == Result::kOk);
    A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 1000));
    A2B_CHECK(bench.network.LastRejection() == RejectReason::kNoModuleInfo);
  }
  {
    Bench bench;
    FixedSource empty{std::vector<uint8_t>()};
    bench.network.SetModuleInfoSource(&empty);
    bench.bus.Plug(Ad(0x37));
    A2B_CHECK(bench.network.Start() == Result::kOk);
    A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 1000));
    A2B_CHECK(bench.network.LastRejection() == RejectReason::kNoModuleInfo);
  }
  {
    Bench bench;
    std::vector<uint8_t> image = MakeImage();
    image[0x03] = 0x38;  // Header changed without a new CRC.
    FixedSource bad(image);
    bench.network.SetModuleInfoSource(&bad);
    bench.bus.Plug(Ad(0x37));
    A2B_CHECK(bench.network.Start() == Result::kOk);
    A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 1000));
    A2B_CHECK(bench.network.LastRejection() == RejectReason::kBadModuleInfo);
    A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kI2scfg), 0x00);  // Nothing written.
  }
}

// Scenario 9: no MSTR_RUNNING.

A2B_TEST(Scenario9NoMstrRunning) {
  Bench bench;
  bench.bus.SetSync(false);
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kFailed, 1000));
  A2B_CHECK(bench.network.Error() == NetworkError::kMainNodeNotRunning);
  // It failed at MSTR + discoveryStartDelayMs, and sends nothing more.
  const size_t transfers = bench.sim.TransferCount();
  bench.Run(1000);
  A2B_CHECK_EQ(bench.sim.TransferCount(), transfers);
  bool logged = false;
  for (const std::string& line : bench.sim.LogLines()) {
    logged = logged || line.find("E: a2b: no MSTR_RUNNING") == 0;
  }
  A2B_CHECK(logged);
  // With SYNC back, Start() brings the bus up.
  bench.bus.SetSync(true);
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.network.Error() == NetworkError::kNone);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning));
  A2B_CHECK(!bench.bus.Wedged());
}

A2B_TEST(LastChanceCheckCatchesALateMstrRunning) {
  // With a long interrupt interval the only checks are the first one (before the PLL
  // locks) and the last-chance one at the deadline.
  BoardConfig board = MakeBoard();
  board.timings.interruptPollMs = 1000;
  Bench bench(board);
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kConfiguring, 1000));
}

A2B_TEST(PllRelockStartsAgain) {
  Bench bench;
  bench.bus.Plug(Ad(0x37));
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 1000));
  bench.bus.SetSync(false);  // A SYNC break: the PLL unlocks, the chain is lost.
  bench.Run(10);
  bench.bus.SetSync(true);  // The PLL relocks: MSTR_RUNNING.
  A2B_CHECK(bench.RunUntil(NetworkState::kStarting, 100));
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 0);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 2000));
  A2B_CHECK(!bench.bus.Wedged());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kGpioien), 0x7F);
}

A2B_TEST(Failures) {
  {
    Bench bench(MakeBoard(0x33));  // The board names an AD2433; the chip is an AD2437.
    A2B_CHECK(bench.network.Start() == Result::kOk);
    A2B_CHECK(bench.RunUntil(NetworkState::kFailed, 100));
    A2B_CHECK(bench.network.Error() == NetworkError::kWrongMainNode);
    A2B_CHECK_EQ(bench.sim.TransferCount(), 1);  // Nothing written.
  }
  {
    Bench bench;
    bench.sim.FailAddress(kMainAddr, 1);
    A2B_CHECK(bench.network.Start() == Result::kOk);
    A2B_CHECK(bench.RunUntil(NetworkState::kFailed, 100));
    A2B_CHECK(bench.network.Error() == NetworkError::kI2cError);
    bench.network.Stop();
    A2B_CHECK(bench.network.State() == NetworkState::kStopped);
    A2B_CHECK(bench.network.Error() == NetworkError::kNone);
  }
  {
    // Once the bus is up, a main node that stops answering is started again from RESETn
    // (after a chain check with retries); if it still doesn't answer, the bus stops.
    Bench bench;
    A2B_CHECK(bench.network.Start() == Result::kOk);
    A2B_CHECK(bench.RunUntil(NetworkState::kDiscovering));
    bench.Run(3);
    bench.sim.FailAddress(kMainAddr, 1000000);
    bench.Run(600);
    A2B_CHECK(bench.network.State() == NetworkState::kFailed);
    A2B_CHECK(bench.network.Error() == NetworkError::kI2cError);
  }
}

A2B_TEST(StartChecksTheBoard) {
  Simulator sim;
  sim.AddBus(kMainAddr, kBusAddr);
  BoardConfig board = MakeBoard(0x00);
  Network none(sim, board);
  A2B_CHECK(none.Start() == Result::kInvalidArgument);
  BoardConfig ad2438 = MakeBoard(0x38);
  Network unsupported(sim, ad2438);
  A2B_CHECK(unsupported.Start() == Result::kInvalidArgument);
  BoardConfig ad2427 = MakeBoard(0x27);  // Not main-node capable (its data sheet).
  Network subOnly(sim, ad2427);
  A2B_CHECK(subOnly.Start() == Result::kInvalidArgument);
  BoardConfig zeroNodes = MakeBoard();
  zeroNodes.policy.maxSubNodes = 0;
  Network empty(sim, zeroNodes);
  A2B_CHECK(empty.Start() == Result::kInvalidArgument);
  BoardConfig custom = MakeBoard();
  custom.defaultLink = LinkProfile::ForBPort(3, 0x37);  // A B port with no handler.
  Network unsupportedLink(sim, custom);
  A2B_CHECK(unsupportedLink.Start() == Result::kNotSupported);
  A2B_CHECK(unsupportedLink.State() == NetworkState::kStopped);
  BoardConfig rj45 = MakeBoard();
  rj45.defaultLink = LinkProfile::Rj45();
  Network twoStep(sim, rj45);
  A2B_CHECK(twoStep.Start() == Result::kOk);  // A two-step default link (test_sim_two_step).
  A2B_CHECK_EQ(sim.TransferCount(), 0);
}

A2B_TEST(BPortLinks) {
  // The link a module's B port gives (its image's 0x28), by its chip: ADI's XLR and RJ45 are
  // two-step on an AD2437 only, UTP on any other; 3-5 and reserved values have no handler.
  const uint8_t chips[] = {0x37, 0x33, 0x28, 0x27};
  for (uint8_t chip : chips) {
    const bool ad2437 = (chip == 0x37);
    const LinkProfile xlr = LinkProfile::ForBPort(0, chip);
    const LinkProfile rj45 = LinkProfile::ForBPort(1, chip);
    const LinkProfile utp = LinkProfile::ForBPort(2, chip);
    A2B_CHECK((xlr.discovery == LinkProfile::Discovery::kTwoStep) == ad2437);
    A2B_CHECK(!xlr.bSideInverted);
    A2B_CHECK((rj45.discovery == LinkProfile::Discovery::kTwoStep) == ad2437);
    A2B_CHECK_EQ(rj45.bSideInverted, ad2437);
    A2B_CHECK(utp.discovery == LinkProfile::Discovery::kSingleStep && !utp.bSideInverted);
    A2B_CHECK(utp.power == LinkProfile::Power::kOnA2bPair);
    for (uint8_t type : {3, 4, 5, 6, 255}) {
      A2B_CHECK(LinkProfile::ForBPort(type, chip).discovery ==
                LinkProfile::Discovery::kUnsupported);
    }
  }
  LinkProfile profile;
  A2B_CHECK(CustomBPortLink(4, 0x37, profile) == Result::kNotSupported);  // A placeholder.
  // The app can't set an unsupported link.
  sim::Simulator sim;
  Network network(sim, MakeBoard());
  A2B_CHECK(network.SetLinkProfile(1, LinkProfile::ForBPort(3, 0x37)) == Result::kNotSupported);
}

A2B_TEST(LinkPresets) {
  const LinkProfile utp = LinkProfile::Utp();
  const LinkProfile rj45 = LinkProfile::Rj45();
  const LinkProfile xlr = LinkProfile::Xlr();
  const LinkProfile custom = LinkProfile::Custom();
  A2B_CHECK(utp.discovery == LinkProfile::Discovery::kSingleStep && !utp.bSideInverted);
  A2B_CHECK(rj45.discovery == LinkProfile::Discovery::kTwoStep && rj45.bSideInverted);
  A2B_CHECK(xlr.discovery == LinkProfile::Discovery::kTwoStep && !xlr.bSideInverted);
  A2B_CHECK(custom.discovery == LinkProfile::Discovery::kSingleStep && !custom.bSideInverted);
  A2B_CHECK(custom.power == LinkProfile::Power::kSeparateWires);
  A2B_CHECK(utp.power == LinkProfile::Power::kOnA2bPair);
  A2B_CHECK_EQ(rj45.powerGoodGpio, 7);
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
