/**
 * @file test_sim_two_step.cpp
 * @brief Two-step links on the simulator: scenario 16 (the happy path), 17 (the failures),
 *        18 and 19 (Custom, UTP and RJ45 links in one chain), and the 24 V check of a
 *        single-step link that asks for it.
 *
 * A two-step link's node runs from the upstream board's 5 V while that board's power-good
 * is high, is found with SWBYP on the upstream node, reviewed, then found again once the
 * 24 V switch closes, and its VBUS is checked with its VMTR (sim::LinkPower::kTwoStep;
 * the AD2437 TRM's Figures 7-2 and 7-3).
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <string>
#include <vector>

#include "a2b/network.h"
#include "detail/regdefs.h"
#include "sim/simulator.h"
#include "sim_bench.h"
#include "sim_trace.h"
#include "test_harness.h"

namespace a2b {
namespace {

using test::Access;
using test::Bench;
using test::Count;
using test::Decode;
using test::Find;
using test::MakeBoard;
using test::MakeBoardFor;
using test::MakeImageFor;

constexpr uint8_t kSwbyp = reg::kControlSwbyp;
constexpr uint8_t kVbusOk = sim::Node::kDefaultSupplyReading;

/** An AD2437 behind an RJ45 link (B side inverted) into a high-power switch. */
sim::SubNodeSpec Rj45Node(uint8_t chipIdByte) {
  sim::SubNodeSpec spec = test::Ad2437(chipIdByte);
  spec.link.bSideInverted = true;
  spec.link.power = sim::LinkPower::kTwoStep;
  return spec;
}

/** Sets @p profile on every link of the bench, the board's and the app's. */
void SetEveryLink(Bench& bench, const LinkProfile& profile) {
  bench.board.defaultLink = profile;
  for (NodeAddr position = 0; position < kMaxSubNodes; ++position) {
    A2B_CHECK(bench.network.SetLinkProfile(position, profile) == Result::kOk);
  }
}

/** A page-1 register access of @p node in @p accesses (Decode() follows page 0 only). */
struct Page1Access {
  bool write;
  uint8_t low;  ///< The register's low byte.
  uint8_t value;
};

/** The page-1 accesses of @p node, following its MMRPAGE writes. */
std::vector<Page1Access> Page1Of(const std::vector<Access>& accesses, NodeAddr node) {
  std::vector<Page1Access> out;
  uint8_t page = 0;
  for (const Access& a : accesses) {
    if (a.node != node) {
      continue;
    }
    if (a.write && a.reg == reg::kMmrpage) {
      page = a.value;
    } else if (page == 1) {
      out.push_back({a.write, a.reg, a.value});
    }
  }
  return out;
}

/** Counts @p node's page-1 accesses of register @p reg (0x1xx), value @p value if not -1. */
int CountPage1(const std::vector<Access>& accesses, NodeAddr node, bool write, uint16_t reg,
               int value = -1) {
  int count = 0;
  for (const Page1Access& a : Page1Of(accesses, node)) {
    count += (a.write == write && a.low == (reg & 0xFF) && (value < 0 || a.value == value)) ? 1 : 0;
  }
  return count;
}

/** Counts the log lines that are @p line. */
int CountLogged(const Bench& bench, const std::string& line) {
  int count = 0;
  for (const std::string& logged : bench.sim.LogLines()) {
    count += (logged == line) ? 1 : 0;
  }
  return count;
}

A2B_TEST(Scenario16Rj45ChainOfFour) {
  Bench bench;
  SetEveryLink(bench, LinkProfile::Rj45());
  for (uint8_t i = 0; i < 4; ++i) {
    bench.bus.Plug(Rj45Node(static_cast<uint8_t>(0x10 + i)));
  }
  A2B_CHECK(bench.StartWith(4));
  bench.Run(1200);  // Idle probes at the end of the chain.
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 4);
  for (size_t i = 0; i < 4; ++i) {
    A2B_CHECK(bench.bus.Sub(i).HighVoltage());
    A2B_CHECK_EQ(bench.bus.Sub(i).HighVoltageCount(), 1);
    A2B_CHECK_EQ(bench.bus.Sub(i).Reg(reg::kVmtrVen), 0);  // The VMTR is off again.
  }
  // Every node reviewed once, before its 24 V, and added once.
  A2B_CHECK_EQ(bench.listener.reviews.size(), 4);
  A2B_CHECK_EQ(bench.listener.Of(EventType::kNodeAdded).size(), 4);
  A2B_CHECK_EQ(bench.bus.OrderErrors(), 0);
  // SWBYP is off everywhere now: on the main node and on the last node, after its idle probes.
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kControl) & kSwbyp, 0);
  for (size_t i = 0; i < 4; ++i) {
    A2B_CHECK_EQ(bench.bus.Sub(i).Reg(reg::kControl) & kSwbyp, 0);
    A2B_CHECK_EQ(bench.bus.Sub(i).Reg(reg::kControl) & reg::kControlXcvrbinv,
                 reg::kControlXcvrbinv);
  }

  // Sub-node 0's sequence: CONTROL with SWBYP (and NEWSTRCT), the switch, DISCVRY, DSCDONE,
  // its IDs and module info; then the switch opened, SWBYP off, the switch closed (24 V),
  // DISCVRY again, its IDs and chip ID, its VBUS; then its set-up.
  const std::vector<Access> accesses = bench.Accesses();
  const uint8_t firstControl = static_cast<uint8_t>(reg::kControlMstr | reg::kControlXcvrbinv |
                                                    kSwbyp | reg::kControlNewstrct);
  const int bypassOn = Find(accesses, 0, kMainNode, true, reg::kControl, firstControl);
  A2B_CHECK(bypassOn >= 0);
  const int switchOn =
      Find(accesses, static_cast<size_t>(bypassOn), kMainNode, true, reg::kSwctl, 0x11);
  const int probe1 = Find(accesses, static_cast<size_t>(switchOn), kMainNode, true, reg::kDiscvry);
  const int switchOff =
      Find(accesses, static_cast<size_t>(probe1), kMainNode, true, reg::kSwctl, 0x10);
  const int bypassOff = Find(accesses, static_cast<size_t>(switchOff), kMainNode, true,
                             reg::kControl, reg::kControlMstr | reg::kControlXcvrbinv);
  const int probe2 = Find(accesses, static_cast<size_t>(bypassOff), kMainNode, true, reg::kDiscvry);
  A2B_CHECK(switchOn > bypassOn && probe1 > switchOn && switchOff > probe1);
  A2B_CHECK(bypassOff > switchOff && probe2 > bypassOff);
  // Its VBUS: VMTR on, VLTG1 read, VMTR off.
  A2B_CHECK_EQ(CountPage1(accesses, 0, true, reg::kVmtrVen, 0x02), 1);
  A2B_CHECK_EQ(CountPage1(accesses, 0, false, reg::kVmtrVltg0 + 3, kVbusOk), 1);
  A2B_CHECK_EQ(CountPage1(accesses, 0, true, reg::kVmtrVen, 0x00), 1);
  // Sub-node 0's power-good (GPIO7) read before sub-node 1's first step.
  A2B_CHECK(Find(accesses, 0, 0, true, reg::kGpioien, 0xFF) >= 0);
  A2B_CHECK(Count(accesses, 0, false, reg::kGpioin) >= 1);
}

A2B_TEST(Scenario16XlrAndTheMainNodesDefaultLink) {
  Bench bench(MakeBoard());
  bench.board.defaultLink = LinkProfile::Xlr();  // The main node's link, from the board.
  sim::SubNodeSpec spec = test::Ad2437(0x20);
  spec.link.power = sim::LinkPower::kTwoStep;  // XLR: not inverted.
  bench.bus.Plug(spec);
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK_EQ(bench.bus.Sub(0).HighVoltageCount(), 1);
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kControl) & (kSwbyp | reg::kControlXcvrbinv), 0);
  A2B_CHECK_EQ(bench.bus.OrderErrors(), 0);
}

A2B_TEST(Scenario16UtpLinkAskedTwoStep) {
  Bench bench;
  LinkProfile utp = LinkProfile::Utp();
  utp.discovery = LinkProfile::Discovery::kTwoStep;  // No power-good or 24 V check asked.
  SetEveryLink(bench, utp);
  sim::SubNodeSpec spec = test::Ad2437(0x21);
  spec.link.power = sim::LinkPower::kTwoStep;
  bench.bus.Plug(spec);
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK_EQ(bench.bus.Sub(0).HighVoltageCount(), 1);
  const std::vector<Access> accesses = bench.Accesses();
  A2B_CHECK_EQ(CountPage1(accesses, 0, true, reg::kVmtrVen), 0);
  // Two probes for sub-node 0 before it was added (its idle probes for sub-node 1 later).
  const int added = Find(accesses, 0, 0, true, reg::kIntmsk0);
  A2B_CHECK(added >= 0);
  int probes = 0;
  for (int i = 0; i < added; ++i) {
    probes +=
        (accesses[i].node == kMainNode && accesses[i].write && accesses[i].reg == reg::kDiscvry)
            ? 1
            : 0;
  }
  A2B_CHECK_EQ(probes, 2);
}

A2B_TEST(SingleStepLinkWithTheHighVoltageCheck) {
  {
    // UTP as it is: no VMTR access.
    Bench bench;
    bench.PlugChain(1);
    A2B_CHECK(bench.StartWith(1));
    A2B_CHECK_EQ(CountPage1(bench.Accesses(), 0, true, reg::kVmtrVen), 0);
  }
  // UTP with the check: VBUS read once the node is found, then VMTR off.
  Bench bench;
  LinkProfile utp = LinkProfile::Utp();
  utp.checkHighVoltage = true;
  SetEveryLink(bench, utp);
  bench.PlugChain(2);
  bench.bus.Sub(1).SetVmtrInput(1, 0x1F);  // Just under 4 V.
  A2B_CHECK(bench.StartWith(1));
  std::vector<Access> accesses = bench.Accesses();
  A2B_CHECK_EQ(CountPage1(accesses, 0, false, reg::kVmtrVltg0 + 3, kVbusOk), 1);
  // Sub-node 1's low VBUS read again through the 10 ms window, then the node rejected.
  A2B_CHECK(CountPage1(accesses, 1, false, reg::kVmtrVltg0 + 3, 0x1F) > 1);
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kVmtrVen), 0);
  const std::vector<Event> rejected = bench.listener.Of(EventType::kNodeRejected);
  A2B_CHECK_EQ(rejected.size(), 1);
  if (rejected.size() != 1) {
    return;
  }
  A2B_CHECK(rejected[0].rejection == RejectReason::kNoHighVoltage);
  A2B_CHECK_EQ(rejected[0].node, 1);
  // Its chip ID isn't kept: once VBUS is up it is found at its next probe.
  bench.bus.Sub(1).SetVmtrInput(1, 0x20);
  A2B_CHECK(bench.RunUntilCount(2, 3000));
}

A2B_TEST(Scenario17NoPowerGood) {
  {
    // The main node's power-good low: nothing probed until it is up.
    Bench bench;
    SetEveryLink(bench, LinkProfile::Rj45());
    bench.bus.Plug(Rj45Node(0x30));
    bench.bus.SetPowerGood(false);
    A2B_CHECK(bench.StartWith(0));
    bench.Run(2000);
    A2B_CHECK_EQ(Count(bench.Accesses(), kMainNode, true, reg::kDiscvry), 0);
    A2B_CHECK_EQ(Count(bench.Accesses(), kMainNode, true, reg::kSwctl, 0x11), 0);
    A2B_CHECK_EQ(CountLogged(bench, "W: a2b: no power-good on the link to sub-node 0; not probed"),
                 1);
    bench.bus.SetPowerGood(true);
    A2B_CHECK(bench.RunUntilCount(1, 1000));
    A2B_CHECK_EQ(bench.bus.Sub(0).HighVoltageCount(), 1);
  }
  // A sub-node's power-good (GPIO7) low: the chain ends before the next node.
  Bench bench;
  SetEveryLink(bench, LinkProfile::Rj45());
  bench.bus.Plug(Rj45Node(0x31));
  bench.bus.Plug(Rj45Node(0x32));
  bench.bus.Sub(0).SetGpioInput(7, false);
  A2B_CHECK(bench.StartWith(1));
  bench.Run(1500);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK_EQ(bench.bus.Sub(1).HighVoltageCount(), 0);
  A2B_CHECK_EQ(CountLogged(bench, "W: a2b: no power-good on the link to sub-node 1; not probed"),
               1);
  bench.bus.Sub(0).SetGpioInput(7, true);
  A2B_CHECK(bench.RunUntilCount(2, 1000));
}

A2B_TEST(Scenario17NodeNotCfg4StaysAtFiveVolts) {
  // A node that isn't CFG-4 (an AD2428) behind a two-step link: found at 5 V, kept there; the
  // link after it is single-step (no 24 V switch on an AD2428). No Clockworks board puts one
  // there (only AD2437 nodes sit behind RJ45 links): this is the stack's safety path.
  Bench bench(MakeBoard(), {MakeImageFor(0x28)});
  SetEveryLink(bench, LinkProfile::Rj45());
  sim::SubNodeSpec ad2428 = test::SubOf(0x28, 0x40);
  ad2428.link.bSideInverted = true;
  ad2428.link.power = sim::LinkPower::kTwoStep;
  bench.bus.Plug(ad2428);
  sim::SubNodeSpec behind = test::SubOf(0x28, 0x41);
  behind.link.bSideInverted = true;
  bench.bus.Plug(behind);
  A2B_CHECK(bench.StartWith(2));
  bench.Run(1500);  // Chain checks, BECNT and idle probes keep SWBYP.
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
  A2B_CHECK_EQ(bench.bus.Sub(0).HighVoltageCount(), 0);
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kControl) & kSwbyp, kSwbyp);
  A2B_CHECK(bench.Logged("I: a2b: sub-node 0 isn't CFG-4: kept at 5 V"));
  A2B_CHECK(bench.Logged("I: a2b: link to sub-node 1: no 24 V switch before it; single-step"));
  A2B_CHECK_EQ(bench.bus.OrderErrors(), 0);
  // Dropped: SWBYP off on the main node; plugged back: found at 5 V again.
  bench.bus.Unplug(0);
  bench.Run(600);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 0);
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kControl) & kSwbyp, 0);
  bench.bus.Plug(ad2428);
  A2B_CHECK(bench.RunUntilCount(1, 2000));
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kControl) & kSwbyp, kSwbyp);
  A2B_CHECK_EQ(bench.bus.Sub(0).HighVoltageCount(), 0);
  // Rejected by the app: SWBYP off, so the node loses its frames.
  A2B_CHECK(bench.network.RejectNode(0) == Result::kOk);
  bench.Run(10);
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kControl) & kSwbyp, 0);
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 0);
}

A2B_TEST(Scenario17NoHighVoltage) {
  // Sub-node 0 doesn't pass the 24 V on: sub-node 1 is found at 5 V and at 24 V, but its VBUS
  // is low, so it is rejected; found once the 24 V is back.
  Bench bench;
  SetEveryLink(bench, LinkProfile::Rj45());
  bench.bus.Plug(Rj45Node(0x50));
  bench.bus.Plug(Rj45Node(0x51));
  bench.bus.SetDownstreamSupply(0, false);
  A2B_CHECK(bench.StartWith(1));
  const std::vector<Event> rejected = bench.listener.Of(EventType::kNodeRejected);
  A2B_CHECK_EQ(rejected.size(), 1);
  if (rejected.size() != 1) {
    return;
  }
  A2B_CHECK(rejected[0].rejection == RejectReason::kNoHighVoltage);
  A2B_CHECK_EQ(rejected[0].node, 1);
  A2B_CHECK_EQ(rejected[0].chipId[5], 0x51);
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kNoHighVoltage);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kSwctl) & reg::kSwctlEnsw, 0);  // Switched off.
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kControl) & kSwbyp, 0);
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kVmtrVen), 0);
  // Not set up: no INTMSK0 written to it.
  A2B_CHECK_EQ(Count(bench.Accesses(), 1, true, reg::kIntmsk0), 0);
  bench.bus.SetDownstreamSupply(0, true);
  A2B_CHECK(bench.RunUntilCount(2, 3000));
  A2B_CHECK(bench.bus.Sub(1).HighVoltage());
}

A2B_TEST(Scenario17NotFoundAtHighVoltage) {
  // Found at 5 V, then nothing once the 24 V switch closes (here its response-cycle window
  // closed after the review): rejected for no 24 V, found again later.
  Bench bench;
  SetEveryLink(bench, LinkProfile::Rj45());
  bench.bus.Plug(Rj45Node(0x60));
  bool closed = false;
  bench.listener.decide = [&](const NodeReview& review) {
    if (!closed) {
      closed = true;
      bench.bus.Sub(0).Link().maxRespCycles = 0;
    }
    return review.policy;
  };
  A2B_CHECK(bench.StartWith(0));
  const std::vector<Event> rejected = bench.listener.Of(EventType::kNodeRejected);
  A2B_CHECK_EQ(rejected.size(), 1);
  if (rejected.size() != 1) {
    return;
  }
  A2B_CHECK(rejected[0].rejection == RejectReason::kNoHighVoltage);
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kSwctl) & reg::kSwctlEnsw, 0);
  bench.bus.Sub(0).Link().maxRespCycles = 0xFF;
  A2B_CHECK(bench.RunUntilCount(1, 3000));
}

A2B_TEST(Scenario17AnotherNodeAtHighVoltage) {
  // The node found at 5 V is swapped for another before the second step: the probe fails,
  // the chain is checked, and the new node is found from the first step.
  Bench bench;
  SetEveryLink(bench, LinkProfile::Rj45());
  bench.bus.Plug(Rj45Node(0x70));
  bool swapped = false;
  bench.listener.decide = [&](const NodeReview& review) {
    if (!swapped) {
      swapped = true;
      bench.bus.Unplug(0);
      bench.bus.Plug(Rj45Node(0x71));
    }
    return review.policy;
  };
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntilCount(1, 3000));
  A2B_CHECK(bench.Logged("W: a2b: probe for sub-node 0 failed; checking the chain"));
  const SubNodeInfo* node = bench.network.SubNodeInfoAt(0);
  A2B_CHECK(node != nullptr && node->identity.chipId[5] == 0x71);
  A2B_CHECK_EQ(bench.listener.reviews.size(), 2);
}

A2B_TEST(Scenario17RejectedBeforeHighVoltage) {
  // The review rejects the node: no 24 V ever reaches it, and its chip ID is kept, so it is
  // switched off at its first step from then on, without a review.
  Bench bench;
  SetEveryLink(bench, LinkProfile::Rj45());
  bench.bus.Plug(Rj45Node(0x80));
  bench.listener.decide = [](const NodeReview&) { return RejectReason::kByApp; };
  A2B_CHECK(bench.StartWith(0));
  bench.Run(4500);
  A2B_CHECK_EQ(bench.listener.reviews.size(), 1);
  A2B_CHECK_EQ(bench.listener.Of(EventType::kNodeRejected).size(), 1);
  A2B_CHECK_EQ(bench.bus.Sub(0).HighVoltageCount(), 0);
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kControl) & kSwbyp, 0);
  A2B_CHECK(Count(bench.Accesses(), kMainNode, true, reg::kDiscvry) >= 2);  // Probed again.
}

A2B_TEST(Scenario17DropReplugAndRejectNode) {
  Bench bench;
  SetEveryLink(bench, LinkProfile::Rj45());
  bench.bus.Plug(Rj45Node(0x90));
  bench.bus.Plug(Rj45Node(0x91));
  A2B_CHECK(bench.StartWith(2));
  // Sub-node 1 unplugged and plugged back: found from the first step again.
  bench.bus.Unplug(1);
  bench.Run(600);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kSwctl) & reg::kSwctlEnsw, 0);
  sim::Node& back = bench.bus.Plug(Rj45Node(0x91));
  A2B_CHECK(bench.RunUntilCount(2, 2000));
  A2B_CHECK_EQ(back.HighVoltageCount(), 1);
  // The app rejects sub-node 1: switched off, and switched off again at its first step.
  A2B_CHECK(bench.network.RejectNode(1) == Result::kOk);
  bench.Run(4500);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK_EQ(back.HighVoltageCount(), 1);
  A2B_CHECK(!back.HighVoltage());
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kControl) & kSwbyp, 0);
  A2B_CHECK_EQ(bench.bus.OrderErrors(), 0);
}

A2B_TEST(TwoStepFallsBackWithoutAHighPowerSwitch) {
  // An AD2428 main node has no 24 V switch: its RJ45 link runs single-step, inverted, with no
  // SWBYP, power-good or VMTR.
  Bench bench(MakeBoardFor(0x28), {test::MakeImage()}, test::ChipOf(0x28));
  SetEveryLink(bench, LinkProfile::Rj45());
  sim::SubNodeSpec spec = test::Ad2437(0xA0);
  spec.link.bSideInverted = true;
  bench.bus.Plug(spec);
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK(bench.Logged("I: a2b: link to sub-node 0: no 24 V switch before it; single-step"));
  const std::vector<Access> accesses = bench.Accesses();
  for (const Access& a : accesses) {
    if (a.node == kMainNode && a.write && a.reg == reg::kControl) {
      A2B_CHECK_EQ(a.value & kSwbyp, 0);
    }
  }
  A2B_CHECK_EQ(CountPage1(accesses, 0, true, reg::kVmtrVen), 0);
}

A2B_TEST(Scenarios18And19MixedChain) {
  // UTP into sub-node 0, RJ45 two-step into 1, Custom (own supply, isolated) into 2, UTP into 3.
  Bench bench;
  A2B_CHECK(bench.network.SetLinkProfile(0, LinkProfile::Utp()) == Result::kOk);
  A2B_CHECK(bench.network.SetLinkProfile(1, LinkProfile::Rj45()) == Result::kOk);
  A2B_CHECK(bench.network.SetLinkProfile(2, LinkProfile::Custom()) == Result::kOk);
  A2B_CHECK(bench.network.SetLinkProfile(3, LinkProfile::Utp()) == Result::kOk);
  bench.bus.Plug(test::Ad2437(0xB0));
  bench.bus.Plug(Rj45Node(0xB1));
  sim::SubNodeSpec own = test::Ad2437(0xB2);
  own.link.power = sim::LinkPower::kSeparateWires;
  own.link.isolated = true;
  bench.bus.Plug(own);
  bench.bus.Plug(test::Ad2437(0xB3));
  A2B_CHECK(bench.StartWith(4));
  A2B_CHECK_EQ(bench.bus.Sub(1).HighVoltageCount(), 1);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kControl) & (kSwbyp | reg::kControlXcvrbinv),
               reg::kControlXcvrbinv);
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kControl) & kSwbyp, kSwbyp);  // Custom: SWBYP.
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kSwctl) & reg::kSwctlEnsw, 0);
  A2B_CHECK_EQ(bench.bus.Sub(2).Reg(reg::kSwctl), 0x11);
  A2B_CHECK_EQ(bench.bus.OrderErrors(), 0);
  // A drop past the RJ45 link: the two-step node stays at 24 V.
  bench.bus.Unplug(3);
  bench.Run(600);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 3);
  A2B_CHECK_EQ(bench.bus.Sub(1).HighVoltageCount(), 1);
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
