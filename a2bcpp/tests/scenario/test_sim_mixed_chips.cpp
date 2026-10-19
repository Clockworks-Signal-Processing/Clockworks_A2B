/**
 * @file test_sim_mixed_chips.cpp
 * @brief Simulator scenario 11: each main-node capable chip (AD2428, AD2433, AD2437) as the
 *        main node of a chain mixing all four chips, through start-up, discovery, a drop
 *        and a re-plug; the board's sub-node limit under each; the AD2427, which is not
 *        main-node capable; and peripheral calls to SPI peripherals, which every main node
 *        refuses (SPI is not supported yet).
 *
 * Chip facts, from docs/ADI_chips: the AD2427 is not main-node capable (AD242x data
 * sheet, Table 1); every main node takes up to 16 sub-nodes; the AD2433 is a CFG-0
 * (standard power) chip and the AD2437 a CFG-4 one (AD2431W-35W and AD2437 data sheets);
 * only the AD243x have SWSTAT2/SWCTL2, which are checked before any of their switches is
 * closed, whatever the main node ("Considerations for Systems with Other Transceiver
 * Nodes", AD2431/32/33/35 TRM).
 *
 * On the bench, from Windows over the USBi (over the USBi): AD2437 and AD2428 main nodes, each
 * with AD2427, AD2428 and AD2437 sub-nodes (docs/bench_console.md lists the runs).
 * TODO(test on hardware): an AD2433, as the main node and as a sub-node.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <vector>

#include "a2b/network.h"
#include "chip_caps.h"
#include "detail/regdefs.h"
#include "sim/simulator.h"
#include "sim_bench.h"
#include "sim_trace.h"
#include "test_harness.h"

namespace a2b {
namespace test {
namespace {

/** The four chips; a chain under main node m takes them in turn from m's place on. */
constexpr uint8_t kChips[] = {0x37, 0x27, 0x33, 0x28};
/** The main-node capable chips. */
constexpr uint8_t kMainChips[] = {0x28, 0x33, 0x37};

/** A bench whose main node is an @p mainProduct, its modules' images by @p products. */
struct MixedBench : Bench {
  explicit MixedBench(uint8_t mainProduct, const std::vector<uint8_t>& products)
      : Bench(MakeBoardFor(mainProduct), Images(products), ChipOf(mainProduct)) {}

  /** The module images of @p products, in order. */
  static std::vector<std::vector<uint8_t>> Images(const std::vector<uint8_t>& products) {
    std::vector<std::vector<uint8_t>> images;
    for (uint8_t product : products) {
      images.push_back(MakeImageFor(product));
    }
    return images;
  }
};

/** Four chips in the order kChips gives them, starting at @p first. */
std::vector<uint8_t> Rotated(size_t first) {
  std::vector<uint8_t> products;
  for (size_t i = 0; i < 4; ++i) {
    products.push_back(kChips[(first + i) % 4]);
  }
  return products;
}

/** SWCTL mode of @p product's switch (the ChipCaps table, from the TRMs' flows). */
uint8_t ModeOf(uint8_t product) { return LookUpChipCaps(kAdiVendorId, product)->switchMode; }

/** True for an AD243x chip: it has SWSTAT2. */
bool HasSwitchConfig(uint8_t product) {
  return LookUpChipCaps(kAdiVendorId, product)->hasSwitchConfig;
}

A2B_TEST(Scenario11EachMainNodeWithAMixedChain) {
  for (size_t m = 0; m < sizeof(kMainChips); ++m) {
    const uint8_t main = kMainChips[m];
    const std::vector<uint8_t> products = Rotated(m);
    MixedBench bench(main, products);
    for (size_t i = 0; i < products.size(); ++i) {
      bench.bus.Plug(SubOf(products[i], static_cast<uint8_t>(0x10 + i)));
    }
    A2B_CHECK(bench.StartWith(4));
    A2B_CHECK(!bench.bus.Wedged());
    A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kSwctl), ModeOf(main) | reg::kSwctlEnsw);
    const std::vector<Access> accesses = bench.Accesses();
    // The main node's power configuration is checked on an AD243x only.
    A2B_CHECK_EQ(Count(accesses, kMainNode, false, reg::kSwstat2) > 0, HasSwitchConfig(main));
    for (size_t i = 0; i < products.size(); ++i) {
      const NodeAddr n = static_cast<NodeAddr>(i);
      const uint8_t product = products[i];
      const SubNodeInfo* info = bench.network.SubNodeInfoAt(n);
      A2B_CHECK(info != nullptr && info->identity.productId == product);
      // Each switch in its chip's mode: closed up to the last node, which is open.
      const bool last = (i + 1 == products.size());
      A2B_CHECK_EQ(bench.bus.Sub(i).Reg(reg::kSwctl),
                   ModeOf(product) | (last ? 0 : reg::kSwctlEnsw));
      // SWSTAT2 read, and SPI settings written, on the AD243x only.
      A2B_CHECK_EQ(Count(accesses, n, false, reg::kSwstat2) > 0, HasSwitchConfig(product));
      A2B_CHECK_EQ(Count(accesses, n, true, reg::kSpicfg) > 0, HasSwitchConfig(product));
      A2B_CHECK_EQ(Count(accesses, n, true, reg::kSwctl2), 0);  // Each detects its own config.
    }
  }
}

A2B_TEST(MixedChainDropAndReplugUnderEachMainNode) {
  for (size_t m = 0; m < sizeof(kMainChips); ++m) {
    const uint8_t main = kMainChips[m];
    const std::vector<uint8_t> products = Rotated(m + 1);
    MixedBench bench(main, products);
    for (size_t i = 0; i < products.size(); ++i) {
      bench.bus.Plug(SubOf(products[i], static_cast<uint8_t>(0x10 + i)));
    }
    A2B_CHECK(bench.StartWith(4));

    // The cable into sub-node 1 is pulled: it and the nodes after it drop.
    const size_t mark = bench.listener.events.size();
    bench.bus.Unplug(1);
    bench.Run(1500);
    A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
    int removed = 0;
    for (size_t i = mark; i < bench.listener.events.size(); ++i) {
      removed += (bench.listener.events[i].type == EventType::kNodeRemoved) ? 1 : 0;
    }
    A2B_CHECK_EQ(removed, 3);
    // The switch before them is open, in its chip's mode.
    A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kSwctl), ModeOf(products[0]));

    // The same modules plugged back in are found again.
    for (size_t i = 1; i < products.size(); ++i) {
      bench.bus.Plug(SubOf(products[i], static_cast<uint8_t>(0x10 + i)));
    }
    A2B_CHECK(bench.RunUntilCount(4, 3000));
    for (size_t i = 0; i < products.size(); ++i) {
      const SubNodeInfo* info = bench.network.SubNodeInfoAt(static_cast<NodeAddr>(i));
      A2B_CHECK(info != nullptr && info->identity.productId == products[i]);
    }
  }
}

A2B_TEST(InvertedLinksInAMixedChain) {
  // An AD2428 main node; the link from the main node and the link from an AD2427 sub-node
  // both inverted (CONTROL.XCVRBINV on the upstream node, from the link profiles).
  const std::vector<uint8_t> products = {0x33, 0x27, 0x37};
  MixedBench bench(0x28, products);
  for (size_t i = 0; i < products.size(); ++i) {
    sim::SubNodeSpec spec = SubOf(products[i], static_cast<uint8_t>(0x10 + i));
    spec.link.bSideInverted = (i == 0 || i == 2);
    bench.bus.Plug(spec);
  }
  LinkProfile inverted = LinkProfile::Utp();
  inverted.bSideInverted = true;
  A2B_CHECK(bench.network.SetLinkProfile(0, inverted) == Result::kOk);
  A2B_CHECK(bench.network.SetLinkProfile(2, inverted) == Result::kOk);
  A2B_CHECK(bench.StartWith(3));
  A2B_CHECK((bench.bus.Main().Reg(reg::kControl) & reg::kControlXcvrbinv) != 0);
  A2B_CHECK((bench.bus.Sub(1).Reg(reg::kControl) & reg::kControlXcvrbinv) != 0);
  A2B_CHECK((bench.bus.Sub(0).Reg(reg::kControl) & reg::kControlXcvrbinv) == 0);
}

A2B_TEST(IsolatedNodeBehindAnAd2428NeedsACustomLink) {
  // The bench's chain: an AD2428 main node, an AD2428 sub-node on its own supply, then an
  // AD2437 on its own supply with a galvanically isolated A side. Over a UTP link the
  // AD2428's switch never finishes closing, so the AD2437 is not found; a Custom link
  // sets SWBYP on the AD2428 instead (the TRMs, "Data-Only and Power-Only Bus Operation").
  const std::vector<uint8_t> products = {0x28, 0x37};
  MixedBench bench(0x28, products);
  for (size_t i = 0; i < products.size(); ++i) {
    sim::SubNodeSpec spec = SubOf(products[i], static_cast<uint8_t>(0x10 + i));
    spec.link.power = sim::LinkPower::kSeparateWires;
    spec.link.isolated = (i == 1);
    bench.bus.Plug(spec);
  }
  A2B_CHECK(bench.StartWith(1));
  bench.Run(2000);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK(bench.network.SetLinkProfile(1, LinkProfile::Custom()) == Result::kOk);
  A2B_CHECK(bench.RunUntilCount(2, 2000));
  A2B_CHECK((bench.bus.Sub(0).Reg(reg::kControl) & reg::kControlSwbyp) != 0);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kSwctl) & reg::kSwctlEnsw, 0);
  // The main node's link stays UTP: its switch closed, no SWBYP.
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kControl) & reg::kControlSwbyp, 0);
  A2B_CHECK((bench.bus.Main().Reg(reg::kSwctl) & reg::kSwctlEnsw) != 0);
}

A2B_TEST(MaxSubNodesUnderEachMainNode) {
  // Every main node takes 16 sub-nodes; BoardPolicy::maxSubNodes (default 15) is the
  // system design's limit. Once it is reached, no probe is made until a node leaves.
  std::vector<uint8_t> products;
  for (size_t i = 0; i < kMaxSubNodes + 1; ++i) {  // One more than any limit.
    products.push_back(kChips[i % 4]);
  }
  for (uint8_t main : kMainChips) {
    for (uint8_t limit : {uint8_t{15}, uint8_t{16}, uint8_t{5}}) {
      MixedBench bench(main, products);
      if (limit != 15) {
        bench.board.policy.maxSubNodes = limit;  // 15 is the default.
      }
      for (size_t i = 0; i < products.size(); ++i) {
        bench.bus.Plug(SubOf(products[i], static_cast<uint8_t>(0x10 + i)));
      }
      A2B_CHECK(bench.StartWith(limit));
      A2B_CHECK_EQ(bench.bus.DiscoveredCount(), limit);
      // No more probes: the chain is checked, but DISCVRY is not written.
      bench.sim.ClearTrace();
      bench.Run(3000);
      A2B_CHECK_EQ(bench.network.SubNodeCount(), limit);
      A2B_CHECK_EQ(Count(bench.Accesses(), kMainNode, true, reg::kDiscvry), 0);
      A2B_CHECK(Count(bench.Accesses(), static_cast<NodeAddr>(limit - 1), false, reg::kVendor) >=
                5);
    }
  }

  // Below the limit after a drop, the stack probes again and finds the node.
  MixedBench bench(0x28, products);
  bench.board.policy.maxSubNodes = 5;
  for (size_t i = 0; i < 6; ++i) {
    bench.bus.Plug(SubOf(products[i], static_cast<uint8_t>(0x10 + i)));
  }
  A2B_CHECK(bench.StartWith(5));
  bench.bus.Unplug(4);
  bench.Run(1000);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 4);
  bench.bus.Plug(SubOf(products[4], 0x14));
  bench.bus.Plug(SubOf(products[5], 0x15));
  A2B_CHECK(bench.RunUntilCount(5, 3000));
  bench.Run(2000);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 5);
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 5);
}

A2B_TEST(Ad2427IsNotAMainNode) {
  MixedBench bench(0x27, {0x37});
  bench.bus.Plug(SubOf(0x37, 0x10));
  A2B_CHECK(bench.network.Start() == Result::kInvalidArgument);
  bench.Run(100);
  A2B_CHECK(bench.network.State() == NetworkState::kStopped);
  A2B_CHECK_EQ(bench.sim.TransferCount(), 0);
}

A2B_TEST(SpiPeripheralsRefusedOnEveryMainNode) {
  // Scenario 11's SPI clause: an SPI call on a main node without SPI (AD2428) returns an
  // error. For now every SPI call does, on every main node and sub-node.
  PeripheralAddress spi;
  spi.kind = PeripheralAddress::Kind::kSpi;
  const uint8_t data[2] = {0x00, 0x01};
  uint8_t readBack[2] = {};
  for (uint8_t main : kMainChips) {
    MixedBench bench(main, {0x37});
    bench.bus.Plug(SubOf(0x37, 0x10));
    A2B_CHECK(bench.StartWith(1));
    bench.sim.ClearTrace();
    A2B_CHECK(bench.network.PeripheralWrite(kMainNode, spi, data, 2) == Result::kNotSupported);
    A2B_CHECK(bench.network.PeripheralRead(0, spi, readBack, 2) == Result::kNotSupported);
    A2B_CHECK(bench.network.PeripheralWriteRead(kMainNode, spi, data, 1, readBack, 1) ==
              Result::kNotSupported);
    A2B_CHECK_EQ(bench.sim.TransferCount(), 0);
  }
}

}  // namespace
}  // namespace test
}  // namespace a2b

A2B_TEST_MAIN()
