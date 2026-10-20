/**
 * @file test_sim_node_hooks.cpp
 * @brief NodeFactory and the SubNode hooks on the simulator, and per-link profiles for
 *        single-step links: Network::SetLinkProfile(), SubNode::DownstreamLink() and
 *        BoardConfig::defaultLink, in that order of precedence.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "a2b/network.h"
#include "a2b/sub_node.h"
#include "detail/regdefs.h"
#include "sim/module_info.h"
#include "sim/simulator.h"
#include "sim_trace.h"
#include "test_harness.h"

namespace a2b {
namespace {

using test::Access;
using test::Count;
using test::Decode;
using test::Find;

constexpr uint8_t kMainAddr = 0x68;
constexpr uint8_t kBusAddr = 0x69;
constexpr uint16_t kAmpProduct = 0x0101;  ///< A module with its own class.
constexpr uint16_t kMicProduct = 0x0102;  ///< Another.
constexpr uint32_t kAmpTag = 0xA3;
constexpr uint32_t kMicTag = 0x31C;
constexpr uint32_t kChosenTag = 0xC0;

/** Counts of hook calls across the test's node objects. */
struct Calls {
  int made = 0;
  int destroyed = 0;
  int removed = 0;
  int configured = 0;
};
Calls g_Calls;

/** An amplifier module: edits its settings, adds operations, has its own masks. */
class AmpModule : public SubNode {
 public:
  /** Counts the object. */
  explicit AmpModule(const Context& context) : SubNode(context) { ++g_Calls.made; }
  /** Counts the object's end. */
  ~AmpModule() override { ++g_Calls.destroyed; }
  AmpModule(const AmpModule&) = delete;
  AmpModule& operator=(const AmpModule&) = delete;

  /** GPIOIEN changed, I2CCFG added, and SWCTL (the stack's) tried. */
  void TweakRegisters(RegisterImage& image) override {
    image.Set(reg::kGpioien, 0x3F);
    image.Set(reg::kI2ccfg, 0x01);
    image.Set(reg::kSwctl, 0x55);
  }
  /** A write to its amplifier at 0x2C, a wait, and a GPIO register. */
  void OnConfigured(OpList& ops) override {
    ++g_Calls.configured;
    ops.Add(Op::PeripheralWrite(Position(), 0x2C, 1, 0x10, m_Unmute, sizeof(m_Unmute)));
    ops.Add(Op::Wait(30));
    ops.Add(Op::RegWrite(Position(), reg::kGpiodat, 0x01));
  }
  /** INTMSK0 0x12, INTMSK1 0x01. */
  InterruptMasks InterruptMask() const override {
    InterruptMasks masks;
    masks.intmsk0 = 0x12;
    masks.intmsk1 = 0x01;
    return masks;
  }
  /** Counts the call. */
  void OnRemoved() override { ++g_Calls.removed; }
  /** kAmpTag. */
  uint32_t TypeTag() const override { return kAmpTag; }

 private:
  const uint8_t m_Unmute[2] = {0x00, 0x01};
};

/** A microphone module whose downstream connector is wired inverted (XCVRBINV). */
class MicModule : public SubNode {
 public:
  using SubNode::SubNode;
  /** The board's link, B side inverted. */
  LinkProfile DownstreamLink() const override {
    LinkProfile link = SubNode::DownstreamLink();
    link.bSideInverted = true;
    return link;
  }
  /** kMicTag. */
  uint32_t TypeTag() const override { return kMicTag; }
};

/** A module class the app's chooser picks by chip ID. */
class ChosenModule : public SubNode {
 public:
  using SubNode::SubNode;
  /** kChosenTag. */
  uint32_t TypeTag() const override { return kChosenTag; }
};

/** A module whose downstream link is two-step (RJ45). */
class Rj45Module : public SubNode {
 public:
  using SubNode::SubNode;
  /** RJ45. */
  LinkProfile DownstreamLink() const override { return LinkProfile::Rj45(); }
};

/** Picks ChosenModule for chip IDs ending in 0xC5, else leaves it to the table. */
MakeNodeFn ChooseByChipId(const SubNodeInfo& info, void* context) {
  ++*static_cast<int*>(context);
  return (info.identity.chipId[5] == 0xC5) ? Make<ChosenModule> : nullptr;
}

/** A peripheral that records writes; it can NAK. */
class Recorder : public sim::I2cDevice {
 public:
  /** Records the bytes and the time. */
  Result Write(const uint8_t* data, size_t len, uint32_t nowMs) override {
    if (nak) {
      return Result::kI2cError;
    }
    writes.emplace_back(data, data + len);
    times.push_back(nowMs);
    return Result::kOk;
  }
  /** Reads zeros. */
  Result Read(uint8_t* data, size_t len, uint32_t nowMs) override {
    (void)nowMs;
    for (size_t i = 0; i < len; ++i) {
      data[i] = 0;
    }
    return Result::kOk;
  }
  std::vector<std::vector<uint8_t>> writes;
  std::vector<uint32_t> times;
  bool nak = false;
};

/** A module-info image for module @p productId, GPIO0-6 as inputs, CFG-4. */
std::vector<uint8_t> MakeImage(uint16_t productId) {
  sim::ModuleInfoHeader header;
  header.productId = productId;
  header.powerConfig = sim::kPowerConfigHighPower;
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

/** An AD2437 that detects CFG-4, its link from upstream as given. */
sim::SubNodeSpec Ad2437(uint8_t chipIdByte = 0, bool inverted = false,
                        sim::LinkPower power = sim::LinkPower::kOnPair) {
  sim::SubNodeSpec spec;
  spec.chip.productId = 0x37;
  spec.chip.powerConfig = sim::kPowerConfigHighPower;
  spec.chip.lowVoltageInput = true;
  spec.chip.chipId[5] = chipIdByte;
  spec.link.bSideInverted = inverted;
  spec.link.power = power;
  return spec;
}

/** A bus with a Network on it, the factory installed, polled every millisecond. */
struct Bench {
  sim::Simulator sim;
  sim::Bus& bus;
  BoardConfig board;
  NodeFactory factory;
  Network network;
  std::vector<std::unique_ptr<Recorder>> amps;  ///< Each node's device at 0x2C.

  explicit Bench(const BoardConfig& config = MakeBoard())
      : bus(sim.AddBus(kMainAddr, kBusAddr, Ad2437().chip)), board(config), network(sim, board) {
    g_Calls = Calls();
    factory.ByModule(0x00AD, kAmpProduct, Make<AmpModule>);
    factory.ByModule(0x00AD, kMicProduct, Make<MicModule>);
    network.SetNodeFactory(&factory);
  }

  /** Plugs in a node with module @p productId in its EEPROM and a device at 0x2C. */
  sim::Node& Plug(uint16_t productId, const sim::SubNodeSpec& spec = Ad2437()) {
    sim::Node& node = bus.Plug(spec);
    node.AddEeprom(0x50).Load(MakeImage(productId));
    amps.push_back(std::make_unique<Recorder>());
    node.AttachDevice(0x2C, *amps.back());
    return node;
  }

  /** Polls every millisecond for @p ms. */
  void Run(uint32_t ms) {
    for (uint32_t i = 0; i < ms; ++i) {
      sim.Advance(1);
      network.Poll(sim.NowMs());
    }
  }

  /** Starts, then polls until the chain is up. */
  bool StartAndRun(uint32_t ms = 5000) {
    if (network.Start() != Result::kOk) {
      return false;
    }
    return RunUntil(NetworkState::kRunning, ms);
  }

  /** Polls until the state is @p state, for at most @p ms. */
  bool RunUntil(NetworkState state, uint32_t ms = 5000) {
    for (uint32_t i = 0; i < ms && network.State() != state; ++i) {
      Run(1);
    }
    return network.State() == state;
  }

  /** Polls until @p count sub-nodes are in, for at most @p ms. */
  bool RunUntilCount(size_t count, uint32_t ms = 5000) {
    for (uint32_t i = 0; i < ms && network.SubNodeCount() != count; ++i) {
      Run(1);
    }
    return network.SubNodeCount() == count;
  }

  /** True if a log line holds @p text. */
  bool Logged(const char* text) const {
    for (const std::string& line : sim.LogLines()) {
      if (line.find(text) != std::string::npos) {
        return true;
      }
    }
    return false;
  }
};

// NodeFactory.

A2B_TEST(FactoryPicksTheClassByModule) {
  Bench bench;
  bench.Plug(kAmpProduct);
  bench.Plug(0x0037);
  bench.Plug(kMicProduct);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 3);
  A2B_CHECK(bench.network.NodeObject(0) != nullptr &&
            bench.network.NodeObject(0)->TypeTag() == kAmpTag);
  A2B_CHECK(bench.network.NodeObject(1) != nullptr &&
            bench.network.NodeObject(1)->TypeTag() == 0);  // A plain SubNode.
  A2B_CHECK(bench.network.NodeObject(2) != nullptr &&
            bench.network.NodeObject(2)->TypeTag() == kMicTag);
  A2B_CHECK(bench.network.NodeObject(3) == nullptr);
  SubNode* node = bench.network.NodeObject(0);
  A2B_CHECK(node != nullptr && node->Position() == 0);
  A2B_CHECK(node != nullptr && &node->Info() == bench.network.SubNodeInfoAt(0));
  if (node != nullptr && node->TypeTag() == kAmpTag) {
    AmpModule& amp = static_cast<AmpModule&>(*node);  // The app's use of the tag.
    A2B_CHECK_EQ(amp.Info().moduleInfo.productId, kAmpProduct);
  }
  A2B_CHECK_EQ(g_Calls.made, 1);
}

A2B_TEST(FactoryAsksTheAppFirst) {
  Bench bench;
  int asked = 0;
  bench.factory.SetChooser(ChooseByChipId, &asked);
  bench.Plug(kAmpProduct, Ad2437(0xC5));  // Chosen by chip ID, whatever its module.
  bench.Plug(kAmpProduct, Ad2437(0x01));  // Left to the table.
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(asked, 2);
  A2B_CHECK(bench.network.NodeObject(0) != nullptr &&
            bench.network.NodeObject(0)->TypeTag() == kChosenTag);
  A2B_CHECK(bench.network.NodeObject(1) != nullptr &&
            bench.network.NodeObject(1)->TypeTag() == kAmpTag);
}

A2B_TEST(FactoryTable) {
  NodeFactory factory;
  A2B_CHECK(factory.ByModule(1, 2, nullptr) == Result::kInvalidArgument);
  for (uint16_t i = 0; i < NodeFactory::kMaxModules; ++i) {
    A2B_CHECK(factory.ByModule(0x00AD, i, Make<AmpModule>) == Result::kOk);
  }
  A2B_CHECK(factory.ByModule(0x00AD, 0x7777, Make<AmpModule>) == Result::kNoSpace);
  // A module already in the table gets its new maker.
  A2B_CHECK(factory.ByModule(0x00AD, 3, Make<MicModule>) == Result::kOk);
  SubNodeInfo info;
  info.moduleInfo.vendorId = 0x00AD;
  info.moduleInfo.productId = 3;
  A2B_CHECK(factory.Find(info) == Make<MicModule>);
  info.moduleInfo.productId = 0x7777;
  A2B_CHECK(factory.Find(info) == nullptr);
}

A2B_TEST(NoFactoryMeansPlainNodes) {
  Bench bench;
  bench.network.SetNodeFactory(nullptr);
  bench.Plug(kAmpProduct);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK(bench.network.NodeObject(0) != nullptr && bench.network.NodeObject(0)->TypeTag() == 0);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kGpioien), 0x7F);  // Not tweaked.
}

// The hooks.

A2B_TEST(TweakRegistersEditsTheSettings) {
  Bench bench;
  bench.Plug(kAmpProduct);
  A2B_CHECK(bench.StartAndRun());
  sim::Node& node = bench.bus.Sub(0);
  A2B_CHECK_EQ(node.Reg(reg::kGpioien), 0x3F);  // Changed.
  A2B_CHECK_EQ(node.Reg(reg::kI2ccfg), 0x01);   // Added.
  A2B_CHECK_EQ(node.Reg(reg::kPincfg), 0x00);   // Kept.
  // SWCTL is the stack's: never 0x55.
  A2B_CHECK_EQ(Count(Decode(bench.sim.Trace()), 0, true, reg::kSwctl, 0x55), 0);
}

A2B_TEST(OnConfiguredOperationsRunBeforeTheNodeCounts) {
  Bench bench;
  bench.Plug(kAmpProduct);
  Recorder& amp = *bench.amps[0];
  A2B_CHECK(bench.network.Start() == Result::kOk);
  for (int i = 0; i < 5000 && amp.writes.empty(); ++i) {
    bench.Run(1);
  }
  A2B_CHECK_EQ(amp.writes.size(), 1);
  if (!amp.writes.empty()) {
    A2B_CHECK(amp.writes[0] == std::vector<uint8_t>({0x10, 0x00, 0x01}));
  }
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 0);  // In the 30 ms wait.
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kGpiodat), 0x00);
  bench.Run(35);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kGpiodat), 0x01);
  A2B_CHECK_EQ(g_Calls.configured, 1);
}

A2B_TEST(OnConfiguredFailureRejects) {
  Bench bench;
  bench.Plug(kAmpProduct);
  bench.amps[0]->nak = true;
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 0);
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kConfigFailed);
  // Its object never joined: destroyed without OnRemoved().
  A2B_CHECK_EQ(g_Calls.made, 1);
  A2B_CHECK_EQ(g_Calls.destroyed, 1);
  A2B_CHECK_EQ(g_Calls.removed, 0);
}

A2B_TEST(InterruptMaskIsWrittenBeforeTheNextLink) {
  Bench bench;
  bench.Plug(kAmpProduct);
  bench.Plug(0x0037);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kIntmsk0), 0x12);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kIntmsk1), 0x01);
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kIntmsk0), reg::kIntmsk0Pwrerr);  // The default.
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kIntmsk1), 0x00);
}

A2B_TEST(OnRemovedAtStartAndStop) {
  Bench bench;
  bench.Plug(kAmpProduct);
  bench.Plug(kAmpProduct);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(g_Calls.made, 2);
  A2B_CHECK(bench.network.Start() == Result::kOk);  // Forgets the chain.
  A2B_CHECK_EQ(g_Calls.removed, 2);
  A2B_CHECK_EQ(g_Calls.destroyed, 2);
  A2B_CHECK(bench.network.NodeObject(0) == nullptr);
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning));
  A2B_CHECK_EQ(g_Calls.made, 4);
  bench.network.Stop();
  A2B_CHECK_EQ(g_Calls.removed, 4);
  A2B_CHECK_EQ(g_Calls.destroyed, 4);
}

// Per-link profiles.

A2B_TEST(DownstreamLinkSetsTheNextLink) {
  Bench bench;
  bench.Plug(kMicProduct);               // Its downstream link is inverted...
  bench.Plug(0x0037, Ad2437(0, true));   // ... as this node's link needs.
  bench.Plug(0x0037, Ad2437(0, false));  // The board's link again.
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 3);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kControl) & reg::kControlXcvrbinv, reg::kControlXcvrbinv);
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kControl) & reg::kControlXcvrbinv, 0);
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kControl) & reg::kControlXcvrbinv, 0);
}

A2B_TEST(SetLinkProfileWinsOverDownstreamLink) {
  Bench bench;
  bench.Plug(kMicProduct);               // Asks for an inverted link to sub-node 1.
  bench.Plug(0x0037, Ad2437(0, false));  // But this link is plain.
  A2B_CHECK(bench.network.SetLinkProfile(1, LinkProfile::Utp()) == Result::kOk);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kControl) & reg::kControlXcvrbinv, 0);
}

A2B_TEST(SetLinkProfileForTheMainNodesLink) {
  Bench bench;
  bench.Plug(0x0037, Ad2437(0, true));
  bench.Plug(0x0037, Ad2437(0, false));
  LinkProfile inverted = LinkProfile::Utp();
  inverted.bSideInverted = true;
  A2B_CHECK(bench.network.SetLinkProfile(0, inverted) == Result::kOk);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
  // Every main-node CONTROL write after the first probe keeps XCVRBINV (TRMs).
  const std::vector<Access> accesses = Decode(bench.sim.Trace());
  const int first = Find(accesses, 0, kMainNode, true, reg::kControl,
                         reg::kControlMstr | reg::kControlXcvrbinv | reg::kControlNewstrct);
  A2B_CHECK(first >= 0);
  for (size_t i = static_cast<size_t>(first); first >= 0 && i < accesses.size(); ++i) {
    const Access& a = accesses[i];
    if (a.node == kMainNode && a.write && a.reg == reg::kControl) {
      A2B_CHECK_EQ(a.value & reg::kControlXcvrbinv, reg::kControlXcvrbinv);
    }
  }
  // Sub-node 0's own link is the board's.
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kControl) & reg::kControlXcvrbinv, 0);
}

A2B_TEST(CustomLinkInTheMiddle) {
  Bench bench;
  bench.Plug(0x0037);
  bench.Plug(0x0037, Ad2437(0, false, sim::LinkPower::kSeparateWires));
  bench.Plug(0x0037);
  A2B_CHECK(bench.network.SetLinkProfile(1, LinkProfile::Custom()) == Result::kOk);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 3);
  // Sub-node 0 never switched on the pair; sub-node 1 did, for sub-node 2.
  A2B_CHECK_EQ(Count(Decode(bench.sim.Trace()), 0, true, reg::kSwctl, 0x11), 0);
  A2B_CHECK_EQ(Count(Decode(bench.sim.Trace()), 0, true, reg::kSwctl, 0x21), 0);
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kSwctl), 0x11);
}

A2B_TEST(SetLinkProfileChecks) {
  Bench bench;
  A2B_CHECK(bench.network.SetLinkProfile(0, LinkProfile::Rj45()) == Result::kOk);
  A2B_CHECK(bench.network.SetLinkProfile(0, LinkProfile::Xlr()) == Result::kOk);
  A2B_CHECK(bench.network.SetLinkProfile(0, LinkProfile::ForBPort(5, 0x37)) ==
            Result::kNotSupported);
  A2B_CHECK(bench.network.SetLinkProfile(16, LinkProfile::Utp()) == Result::kInvalidArgument);
  A2B_CHECK(bench.network.SetLinkProfile(-1, LinkProfile::Utp()) == Result::kInvalidArgument);
  A2B_CHECK(bench.network.ClearLinkProfile(16) == Result::kInvalidArgument);
  A2B_CHECK(bench.network.ClearLinkProfile(3) == Result::kOk);
}

A2B_TEST(ClearLinkProfileGoesBackToTheDefault) {
  Bench bench;
  bench.Plug(0x0037, Ad2437(0, false));
  LinkProfile inverted = LinkProfile::Utp();
  inverted.bSideInverted = true;
  A2B_CHECK(bench.network.SetLinkProfile(0, inverted) == Result::kOk);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 0);  // Wrong polarity: nothing answers.
  A2B_CHECK(bench.network.ClearLinkProfile(0) == Result::kOk);
  A2B_CHECK(bench.RunUntilCount(1, 2000));  // The next probe uses the board's link.
}

A2B_TEST(TwoStepDownstreamLink) {
  Bench bench;
  NodeFactory factory;
  factory.ByModule(0x00AD, 0x0037, Make<Rj45Module>);
  bench.network.SetNodeFactory(&factory);
  bench.Plug(0x0037);
  bench.Plug(0x0037, Ad2437(1, true, sim::LinkPower::kTwoStep));
  bench.Plug(0x0037, Ad2437(2, false));
  A2B_CHECK(bench.StartAndRun());
  // Sub-node 1 is found twice (5 V, then 24 V); sub-node 2's link is the RJ45 one too, so
  // the UTP node behind it isn't found at 5 V.
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
  A2B_CHECK(bench.bus.Sub(1).HighVoltage());
  A2B_CHECK_EQ(bench.bus.OrderErrors(), 0);
  // The app's single-step link for that position applies from the next probe.
  A2B_CHECK(bench.network.SetLinkProfile(2, LinkProfile::Utp()) == Result::kOk);
  A2B_CHECK(bench.RunUntilCount(3, 2000));
}

A2B_TEST(LinkChangeAtTheEndOfTheChainAppliesAtTheNextProbe) {
  Bench bench;
  bench.Plug(0x0037);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  bench.Plug(0x0037, Ad2437(0, true));  // Hot-plugged behind an inverted link.
  bench.Run(1200);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);  // Not with the board's link.
  LinkProfile inverted = LinkProfile::Utp();
  inverted.bSideInverted = true;
  A2B_CHECK(bench.network.SetLinkProfile(1, inverted) == Result::kOk);
  A2B_CHECK(bench.RunUntilCount(2, 1200));
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kControl) & reg::kControlXcvrbinv, reg::kControlXcvrbinv);
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
