/**
 * @file test_sim_discovery.cpp
 * @brief Simulator tests: DSCDONE only when power, the upstream B side (switch or SWBYP),
 *        polarity, response cycles and NODEADR are right; chains, mixed chips, unplugging;
 * power-configuration detection.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sim_steps.h"
#include "test_harness.h"

namespace a2b {
namespace {

using sim::Simulator;
using test::DiscoverNext;
using test::ReadInterrupt;
using test::StartMainNode;

constexpr uint8_t kMainAddr = 0x68;
constexpr uint8_t kBusAddr = 0x69;

/** A simulator with one bus whose main node is running. */
struct Bench {
  Simulator sim;
  sim::Bus& bus;
  Transport transport;

  explicit Bench(const sim::ChipSpec& mainChip = sim::ChipSpec(), uint8_t controlBits = 0)
      : bus(sim.AddBus(kMainAddr, kBusAddr, mainChip)), transport(sim, kMainAddr, kBusAddr) {
    A2B_CHECK(StartMainNode(sim, transport, controlBits));
  }
};

/** A sub-node of chip @p productId (PRODUCT, e.g. 0x37), the rest as the spec's defaults. */
sim::SubNodeSpec Ad(uint8_t productId) {
  sim::SubNodeSpec spec;
  spec.chip.productId = productId;
  return spec;
}

A2B_TEST(DiscoversOneSubNode) {
  Bench bench;
  bench.bus.Plug(Ad(0x37));
  Transport& transport = bench.transport;
  // The sub-node does not answer before it is discovered.
  uint8_t value = 0;
  A2B_CHECK(transport.ReadReg(0, reg::kVendor, value) == Result::kI2cError);

  transport.WriteReg(kMainNode, reg::kIntmsk2, reg::kIntmsk2Dscdone);
  transport.WriteReg(kMainNode, reg::kSwctl, reg::kSwctlEnsw);
  transport.WriteReg(kMainNode, reg::kDiscvry, test::kBenchRespCycles);
  uint8_t discstat = 0;
  A2B_CHECK(transport.ReadReg(kMainNode, reg::kDiscstat, discstat) == Result::kOk);
  A2B_CHECK_EQ(discstat, reg::kDiscstatDscact);
  bench.sim.Advance(bench.sim.GetTiming().discoveryMs - 1);
  A2B_CHECK_EQ(ReadInterrupt(transport), -1);
  bench.sim.Advance(1);
  A2B_CHECK_EQ(ReadInterrupt(transport), reg::kInttypeDscdone);
  A2B_CHECK(transport.ReadReg(kMainNode, reg::kDiscstat, discstat) == Result::kOk);
  A2B_CHECK_EQ(discstat, 0);
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 1);

  // INTPND2.DSCDONE is set, and writing 1 clears it.
  uint8_t pending = 0;
  A2B_CHECK(transport.ReadReg(kMainNode, reg::kIntpnd2, pending) == Result::kOk);
  A2B_CHECK_EQ(pending, reg::kIntpnd2Dscdone);
  transport.WriteReg(kMainNode, reg::kIntpnd2, reg::kIntpnd2Dscdone);
  A2B_CHECK(transport.ReadReg(kMainNode, reg::kIntpnd2, pending) == Result::kOk);
  A2B_CHECK_EQ(pending, 0);

  // Now it answers, as the bench's first sub-node did.
  uint8_t ids[4] = {};
  A2B_CHECK(transport.ReadRegs(0, reg::kVendor, ids, sizeof(ids)) == Result::kOk);
  A2B_CHECK_EQ(ids[0], 0xAD);
  A2B_CHECK_EQ(ids[1], 0x37);
  A2B_CHECK_EQ(ids[2], 0x21);
  A2B_CHECK_EQ(ids[3], 0x03);
  A2B_CHECK(transport.ReadReg(0, reg::kNode, value) == Result::kOk);
  A2B_CHECK_EQ(value, reg::kNodeLast | reg::kNodeDiscvd | 0);
}

A2B_TEST(ChipIdIsReadable) {
  Bench bench;
  sim::SubNodeSpec spec = Ad(0x37);
  const uint8_t chipId[reg::kCountChipid] = {0x18, 0xAC, 0xEE, 0x04, 0x23, 0x16};
  for (int i = 0; i < reg::kCountChipid; ++i) {
    spec.chip.chipId[i] = chipId[i];
  }
  bench.bus.Plug(spec);
  A2B_CHECK(DiscoverNext(bench.sim, bench.transport, kMainNode));
  uint8_t read[reg::kCountChipid] = {};
  A2B_CHECK(bench.transport.ReadRegs(0, reg::kChipid0, read, sizeof(read)) == Result::kOk);
  for (int i = 0; i < reg::kCountChipid; ++i) {
    A2B_CHECK_EQ(read[i], chipId[i]);
  }
  // CHIPID is read-only.
  bench.transport.WriteReg(0, reg::kChipid0, 0x00);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kChipid0), 0x18);
}

A2B_TEST(NoDscdoneWithoutEnsw) {
  Bench bench;
  bench.bus.Plug(Ad(0x37));
  Transport& transport = bench.transport;
  transport.WriteReg(kMainNode, reg::kIntmsk2, reg::kIntmsk2Dscdone);
  transport.WriteReg(kMainNode, reg::kDiscvry, test::kBenchRespCycles);
  bench.sim.Advance(200);
  A2B_CHECK_EQ(ReadInterrupt(transport), -1);
  A2B_CHECK(bench.bus.DiscoveryActive());
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 0);
}

A2B_TEST(DscdoneNeedsMatchingPolarity) {
  sim::SubNodeSpec spec = Ad(0x37);
  spec.link.bSideInverted = true;
  {
    Bench bench;  // XCVRBINV not set.
    bench.bus.Plug(spec);
    A2B_CHECK(!DiscoverNext(bench.sim, bench.transport, kMainNode));
  }
  {
    Bench bench(sim::ChipSpec(), reg::kControlXcvrbinv);  // As the reference board: 0x91.
    bench.bus.Plug(spec);
    A2B_CHECK(DiscoverNext(bench.sim, bench.transport, kMainNode));
  }
  {
    Bench bench(sim::ChipSpec(), reg::kControlXcvrbinv);  // Set, but the link is straight.
    bench.bus.Plug(Ad(0x37));
    A2B_CHECK(!DiscoverNext(bench.sim, bench.transport, kMainNode));
  }
}

A2B_TEST(EveryChipCanInvertItsBSide) {
  // As the main node (the AD2427 is not main-node capable)...
  for (uint8_t productId : {0x28, 0x33, 0x37}) {
    sim::ChipSpec mainChip;
    mainChip.productId = productId;
    sim::SubNodeSpec spec = Ad(0x37);
    spec.link.bSideInverted = true;
    Bench bench(mainChip, reg::kControlXcvrbinv);
    bench.bus.Plug(spec);
    A2B_CHECK(DiscoverNext(bench.sim, bench.transport, kMainNode));
  }
  // ...and as a sub-node.
  for (uint8_t productId : {0x27, 0x28, 0x33, 0x37}) {
    Bench bench;
    bench.bus.Plug(Ad(productId));
    sim::SubNodeSpec spec = Ad(0x37);
    spec.link.bSideInverted = true;
    bench.bus.Plug(spec);
    A2B_CHECK(DiscoverNext(bench.sim, bench.transport, kMainNode));
    bench.transport.WriteReg(0, reg::kControl, reg::kControlXcvrbinv);
    A2B_CHECK(DiscoverNext(bench.sim, bench.transport, 0));
  }
}

A2B_TEST(XcvrbinvChangedUnderEnswIsCounted) {
  // Scenario 10's check: the TRMs want XCVRBINV set before ENSW, and kept by later CONTROL
  // writes.
  Bench bench;
  Transport& transport = bench.transport;
  const uint8_t mstr = reg::kControlMstr;
  transport.WriteReg(kMainNode, reg::kControl, static_cast<uint8_t>(mstr | reg::kControlXcvrbinv));
  transport.WriteReg(kMainNode, reg::kSwctl, reg::kSwctlEnsw);
  transport.WriteReg(kMainNode, reg::kControl,
                     static_cast<uint8_t>(mstr | reg::kControlXcvrbinv | reg::kControlNewstrct));
  A2B_CHECK_EQ(bench.bus.OrderErrors(), 0);  // Set first, then kept.
  transport.WriteReg(kMainNode, reg::kControl, mstr);
  A2B_CHECK_EQ(bench.bus.OrderErrors(), 1);  // Cleared under ENSW.
  transport.WriteReg(kMainNode, reg::kControl, static_cast<uint8_t>(mstr | reg::kControlXcvrbinv));
  A2B_CHECK_EQ(bench.bus.OrderErrors(), 2);  // Set after ENSW.
  transport.WriteReg(kMainNode, reg::kSwctl, 0);
  transport.WriteReg(kMainNode, reg::kControl, mstr);
  A2B_CHECK_EQ(bench.bus.OrderErrors(), 2);  // The switch is open: any change is fine.
}

A2B_TEST(DscdoneNeedsRespCyclesInTheWindow) {
  sim::SubNodeSpec spec = Ad(0x37);
  spec.link.minRespCycles = 0x80;
  spec.link.maxRespCycles = 0xC0;
  const struct {
    uint8_t respCycles;
    bool found;
  } kCases[] = {{0x70, false}, {0x80, true}, {0xB9, true}, {0xC0, true}, {0xD0, false}};
  for (const auto& testCase : kCases) {
    Bench bench;
    bench.bus.Plug(spec);
    A2B_CHECK_EQ(DiscoverNext(bench.sim, bench.transport, kMainNode, testCase.respCycles),
                 testCase.found);
  }
}

A2B_TEST(SeparatelyPoweredNodeNeedsItsSupplyAndTheBSideOn) {
  // Its own supply, and the upstream B side passing frames: ENSW onto a node that draws the
  // bias current, or SWBYP (the TRMs, "Data-Only and Power-Only Bus Operation").
  sim::SubNodeSpec spec = Ad(0x37);
  spec.link.power = sim::LinkPower::kSeparateWires;
  Bench bench;
  sim::Node& node = bench.bus.Plug(spec);
  Transport& transport = bench.transport;
  node.SetLocalPower(false);
  A2B_CHECK(!DiscoverNext(bench.sim, transport, kMainNode));
  node.SetLocalPower(true);
  A2B_CHECK(DiscoverNext(bench.sim, transport, kMainNode));
  // Switch open and no SWBYP: the B side stays off.
  Bench open;
  open.bus.Plug(spec);
  open.transport.WriteReg(kMainNode, reg::kIntmsk2, reg::kIntmsk2Dscdone);
  open.transport.WriteReg(kMainNode, reg::kSwctl, 0x00);
  open.transport.WriteReg(kMainNode, reg::kDiscvry, test::kBenchRespCycles);
  open.sim.Advance(open.sim.GetTiming().discoveryMs);
  A2B_CHECK(ReadInterrupt(open.transport) != reg::kInttypeDscdone);
  // SWBYP: found with the switch open.
  Bench bypass(sim::ChipSpec(), reg::kControlSwbyp);
  bypass.bus.Plug(spec);
  bypass.transport.WriteReg(kMainNode, reg::kIntmsk2, reg::kIntmsk2Dscdone);
  bypass.transport.WriteReg(kMainNode, reg::kDiscvry, test::kBenchRespCycles);
  bypass.sim.Advance(bypass.sim.GetTiming().discoveryMs);
  A2B_CHECK_EQ(ReadInterrupt(bypass.transport), reg::kInttypeDscdone);
}

A2B_TEST(IsolatedNodeNeedsSwbyp) {
  // On its own supply with an isolated A side (the bench's AD2437 behind an AD2428): no
  // bias current, so the closed switch never passes frames; SWBYP does.
  sim::SubNodeSpec spec = Ad(0x37);
  spec.link.power = sim::LinkPower::kSeparateWires;
  spec.link.isolated = true;
  {
    Bench bench;
    bench.bus.Plug(spec);
    A2B_CHECK(!DiscoverNext(bench.sim, bench.transport, kMainNode));
  }
  Bench bench(sim::ChipSpec(), reg::kControlSwbyp);
  bench.bus.Plug(spec);
  A2B_CHECK(DiscoverNext(bench.sim, bench.transport, kMainNode));  // ENSW ignored.
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 1);
}

A2B_TEST(SwbypKeepsTheSwitchOpen) {
  // The TRMs: with SWBYP "the line switch will not be enabled even if A2B_SWCTL.ENSW is
  // set", so a node powered over the pair gets no power.
  Bench bench(sim::ChipSpec(), reg::kControlSwbyp);
  bench.bus.Plug(Ad(0x37));
  A2B_CHECK(!DiscoverNext(bench.sim, bench.transport, kMainNode));
  // Found once SWBYP is cleared, then dropped when it is set again.
  bench.transport.WriteReg(kMainNode, reg::kControl, reg::kControlMstr);
  A2B_CHECK(DiscoverNext(bench.sim, bench.transport, kMainNode));
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 1);
  bench.transport.WriteReg(kMainNode, reg::kControl,
                           static_cast<uint8_t>(reg::kControlMstr | reg::kControlSwbyp));
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 0);
}

A2B_TEST(NothingPluggedMeansNoDscdone) {
  Bench bench;
  A2B_CHECK(!DiscoverNext(bench.sim, bench.transport, kMainNode));
  A2B_CHECK(bench.bus.DiscoveryActive());
}

A2B_TEST(MaskedDscdoneOnlySetsIntpnd2) {
  Bench bench;
  bench.bus.Plug(Ad(0x37));
  Transport& transport = bench.transport;
  transport.WriteReg(kMainNode, reg::kSwctl, reg::kSwctlEnsw);
  transport.WriteReg(kMainNode, reg::kDiscvry, test::kBenchRespCycles);
  bench.sim.Advance(bench.sim.GetTiming().discoveryMs);
  A2B_CHECK_EQ(ReadInterrupt(transport), -1);
  uint8_t pending = 0;
  A2B_CHECK(transport.ReadReg(kMainNode, reg::kIntpnd2, pending) == Result::kOk);
  A2B_CHECK_EQ(pending, reg::kIntpnd2Dscdone);
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 1);
}

A2B_TEST(EnddscStopsDiscovery) {
  Bench bench;
  bench.bus.Plug(Ad(0x37));
  Transport& transport = bench.transport;
  transport.WriteReg(kMainNode, reg::kIntmsk2, reg::kIntmsk2Dscdone);
  transport.WriteReg(kMainNode, reg::kSwctl, reg::kSwctlEnsw);
  transport.WriteReg(kMainNode, reg::kDiscvry, test::kBenchRespCycles);
  transport.WriteReg(kMainNode, reg::kControl,
                     static_cast<uint8_t>(reg::kControlMstr | reg::kControlEnddsc));
  A2B_CHECK(!bench.bus.DiscoveryActive());
  bench.sim.Advance(100);
  A2B_CHECK_EQ(ReadInterrupt(transport), -1);
  A2B_CHECK(bench.bus.Running());
  // CONTROL keeps MSTR but not the self-clearing ENDDSC.
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kControl), reg::kControlMstr);
}

A2B_TEST(DiscvryIsIgnoredUntilTheMainNodeRuns) {
  Simulator sim;
  sim::Bus& bus = sim.AddBus(kMainAddr, kBusAddr);
  bus.Plug(Ad(0x37));
  Transport transport(sim, kMainAddr, kBusAddr);
  A2B_CHECK(!DiscoverNext(sim, transport, kMainNode));
  A2B_CHECK(!bus.DiscoveryActive());
}

A2B_TEST(ChainOfMixedChips) {
  // An AD2437 main node with AD2428, AD2437, AD2433 and AD2427 sub-nodes. The AD2437's
  // B side is an RJ45 link (inverted).
  Bench bench(sim::ChipSpec(), 0);
  bench.bus.Plug(Ad(0x28));
  bench.bus.Plug(Ad(0x37));
  sim::SubNodeSpec ad2433 = Ad(0x33);
  ad2433.link.bSideInverted = true;
  bench.bus.Plug(ad2433);
  bench.bus.Plug(Ad(0x27));
  Transport& transport = bench.transport;

  A2B_CHECK(DiscoverNext(bench.sim, transport, kMainNode));
  A2B_CHECK(DiscoverNext(bench.sim, transport, 0));
  // The AD2437's B side must be inverted for the third link.
  A2B_CHECK(!DiscoverNext(bench.sim, transport, 1));
  transport.WriteReg(1, reg::kControl, reg::kControlXcvrbinv);
  A2B_CHECK(DiscoverNext(bench.sim, transport, 1));
  A2B_CHECK(DiscoverNext(bench.sim, transport, 2));
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 4);
  // Only the last node has NODE.LAST.
  A2B_CHECK_EQ(bench.bus.Sub(2).Reg(reg::kNode), reg::kNodeDiscvd | 2);
  A2B_CHECK_EQ(bench.bus.Sub(3).Reg(reg::kNode), reg::kNodeLast | reg::kNodeDiscvd | 3);

  const uint8_t products[] = {0x28, 0x37, 0x33, 0x27};
  for (NodeAddr node = 0; node < 4; ++node) {
    uint8_t product = 0;
    A2B_CHECK(transport.ReadReg(node, reg::kProduct, product) == Result::kOk);
    A2B_CHECK_EQ(product, products[node]);
  }
  // A fifth probe finds nothing.
  A2B_CHECK(!DiscoverNext(bench.sim, transport, 3));
}

A2B_TEST(DiscvryNeedsNodeadrOnTheUpstreamNode) {
  // The TRMs: NODEADR.NODE must be n - 1 when DISCVRY is written for node n.
  Bench bench;
  bench.bus.Plug(Ad(0x37));
  bench.bus.Plug(Ad(0x37));
  Transport& transport = bench.transport;
  A2B_CHECK(DiscoverNext(bench.sim, transport, kMainNode));
  transport.WriteReg(0, reg::kSwctl, reg::kSwctlEnsw);
  transport.WriteReg(kMainNode, reg::kNodeadr, reg::kNodeadrBrcst);  // Not sub-node 0.
  transport.ForgetAll();
  transport.WriteReg(kMainNode, reg::kDiscvry, test::kBenchRespCycles);
  bench.sim.Advance(bench.sim.GetTiming().discoveryMs);
  A2B_CHECK_EQ(ReadInterrupt(transport), -1);
  A2B_CHECK(bench.bus.DiscoveryActive());
  transport.WriteReg(kMainNode, reg::kControl, reg::kControlMstr | reg::kControlEnddsc);
  // Through sub-node 0, it is found.
  A2B_CHECK(DiscoverNext(bench.sim, transport, 0));
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 2);
}

A2B_TEST(PowerConfigurationDetectedAndOverridden) {
  sim::ChipSpec bench = sim::ChipSpec();
  bench.powerConfig = 4;
  bench.lowVoltageInput = true;
  Bench ad2437(bench);
  // SWSTAT2 as the bench's nodes read it: LVI mode, CFG-4.
  uint8_t value = 0;
  A2B_CHECK(ad2437.transport.ReadReg(kMainNode, reg::kSwstat2, value) == Result::kOk);
  A2B_CHECK_EQ(value, 0xA0);
  A2B_CHECK_EQ(ad2437.bus.Main().PowerConfig(), 4);
  ad2437.transport.WriteReg(kMainNode, reg::kSwstat2, 0x00);  // Read-only.
  A2B_CHECK_EQ(ad2437.bus.Main().Reg(reg::kSwstat2), 0xA0);
  // SWCTL2.HPSW_CFG counts only while SWCTL.CFG_DET_OV is set.
  ad2437.transport.WriteReg(kMainNode, reg::kSwctl2, 0x00);
  A2B_CHECK_EQ(ad2437.bus.Main().PowerConfig(), 4);
  ad2437.transport.WriteReg(kMainNode, reg::kSwctl, reg::kSwctlCfgDetOv);
  A2B_CHECK_EQ(ad2437.bus.Main().PowerConfig(), 0);
  A2B_CHECK_EQ(ad2437.bus.Main().Reg(reg::kSwstat2), 0xA0);  // Still what it detected.
  // CFG-0 is 0x00 (AD2431-35 TRM, "Power Configurations"); the AD242x has no SWSTAT2.
  Bench cfg0;
  A2B_CHECK_EQ(cfg0.bus.Main().Reg(reg::kSwstat2), 0x00);
  sim::ChipSpec ad2428 = sim::ChipSpec();
  ad2428.productId = 0x28;
  ad2428.powerConfig = 4;
  Bench old(ad2428);
  A2B_CHECK_EQ(old.bus.Main().Reg(reg::kSwstat2), 0x00);
  A2B_CHECK_EQ(old.bus.Main().PowerConfig(), 0);
}

A2B_TEST(UnsupportedChipStillDiscovers) {
  // The stack decides what to do with a chip it does not know; the simulator just shows it.
  Bench bench;
  bench.bus.Plug(Ad(0x10));  // AD2410
  A2B_CHECK(DiscoverNext(bench.sim, bench.transport, kMainNode));
  A2B_CHECK_STREQ(bench.bus.Sub(0).Caps().name, "unknown");
}

A2B_TEST(SoftResetLosesTheChain) {
  Bench bench;
  bench.bus.Plug(Ad(0x37));
  A2B_CHECK(DiscoverNext(bench.sim, bench.transport, kMainNode));
  bench.transport.WriteReg(0, reg::kSwctl, reg::kSwctlEnsw);
  A2B_CHECK(StartMainNode(bench.sim, bench.transport));
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 0);
  uint8_t value = 0;
  A2B_CHECK(bench.transport.ReadReg(0, reg::kVendor, value) == Result::kI2cError);
  // Rediscovered, its registers are back at reset values.
  A2B_CHECK(DiscoverNext(bench.sim, bench.transport, kMainNode));
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kSwctl), 0);
}

A2B_TEST(UnplugDropsTheNodeAndEverythingBeyond) {
  Bench bench;
  for (int i = 0; i < 3; ++i) {
    bench.bus.Plug(Ad(0x37));
  }
  Transport& transport = bench.transport;
  A2B_CHECK(DiscoverNext(bench.sim, transport, kMainNode));
  A2B_CHECK(DiscoverNext(bench.sim, transport, 0));
  A2B_CHECK(DiscoverNext(bench.sim, transport, 1));
  bench.bus.Unplug(1);
  A2B_CHECK_EQ(bench.bus.PluggedCount(), 1);
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 1);
  uint8_t value = 0;
  A2B_CHECK(transport.ReadReg(0, reg::kVendor, value) == Result::kOk);
  A2B_CHECK(transport.ReadReg(1, reg::kVendor, value) == Result::kI2cError);
  A2B_CHECK(transport.ReadReg(2, reg::kVendor, value) == Result::kI2cError);
  // Plugged back in, it is found again.
  bench.bus.Plug(Ad(0x37));
  A2B_CHECK(DiscoverNext(bench.sim, transport, 0));
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 2);
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
