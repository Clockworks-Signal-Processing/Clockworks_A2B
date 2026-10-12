/**
 * @file test_sim_app_access.cpp
 * @brief Simulator tests of the app's raw register access (Network::ReadRegisters() and
 *        WriteRegisters()): the nodes it reaches, page 1 on the AD243x only, MMRPAGE kept
 *        from the app, NODEADR and CHIP rewritten after the app wrote them, the joining
 *        node from a module-info source, and two of the documented risks (a switch
 *        opened, an interrupt read).
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <vector>

#include "sim_bench.h"
#include "test_harness.h"

namespace a2b {
namespace {

using test::Bench;
using test::ChipOf;
using test::MakeBoardFor;
using test::MakeImage;
using test::MakeImageFor;
using test::SubOf;

A2B_TEST(ReadsTheMainNodeAndEachSubNode) {
  Bench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  uint8_t ids[3] = {};
  A2B_CHECK(bench.network.ReadRegisters(kMainNode, reg::kVendor, ids, 3) == Result::kOk);
  A2B_CHECK_EQ(ids[0], 0xAD);
  A2B_CHECK_EQ(ids[1], 0x37);
  for (NodeAddr node = 0; node < 2; ++node) {
    uint8_t chipId[6] = {};
    A2B_CHECK(bench.network.ReadRegisters(node, reg::kChipid0, chipId, 6) == Result::kOk);
    A2B_CHECK_EQ(chipId[5], 0x10 + node);
  }
  A2B_CHECK_EQ(bench.sim.UnlockedTransfers(), 0);
}

A2B_TEST(WritesReachTheNode) {
  Bench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  // Twenty registers of sub-node 1 (more than one transfer's 16), one of the main node.
  std::vector<uint8_t> values;
  for (int i = 0; i < 20; ++i) {
    values.push_back(static_cast<uint8_t>(0x40 + i));
  }
  A2B_CHECK(bench.network.WriteRegisters(1, reg::kTxxbar0, values.data(), values.size()) ==
            Result::kOk);
  for (int i = 0; i < 20; ++i) {
    A2B_CHECK_EQ(bench.bus.Sub(1).Reg(static_cast<uint16_t>(reg::kTxxbar0 + i)), 0x40 + i);
    A2B_CHECK_EQ(bench.bus.Sub(0).Reg(static_cast<uint16_t>(reg::kTxxbar0 + i)),
                 i);  // Reset value.
  }
  const uint8_t gpio = 0x05;
  A2B_CHECK(bench.network.WriteRegisters(kMainNode, reg::kGpiodat, &gpio, 1) == Result::kOk);
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kGpiodat), 0x05);
  // The stack carries on: the chain is still there a while later.
  bench.Run(2000);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
  A2B_CHECK(bench.listener.Of(EventType::kNodeRemoved).empty());
}

A2B_TEST(PageOneOnTheAd243xOnly) {
  // An AD2428 main node with an AD2428 and then an AD2437 sub-node.
  Bench bench(MakeBoardFor(0x28), {MakeImageFor(0x28), MakeImageFor(0x37)}, ChipOf(0x28));
  bench.bus.Plug(SubOf(0x28, 1));
  bench.bus.Plug(SubOf(0x37, 2));
  A2B_CHECK(bench.StartWith(2));
  const size_t sent = bench.sim.TransferCount();
  uint8_t value = 0x01;
  A2B_CHECK(bench.network.ReadRegisters(kMainNode, reg::kVmtrVen, &value, 1) ==
            Result::kInvalidArgument);
  A2B_CHECK(bench.network.WriteRegisters(0, reg::kVmtrVen, &value, 1) == Result::kInvalidArgument);
  A2B_CHECK_EQ(bench.sim.TransferCount(), sent);  // Nothing was sent.
  // The AD2437 has page 1: VMTR_VEN written and read back, then page 0 again.
  A2B_CHECK(bench.network.WriteRegisters(1, reg::kVmtrVen, &value, 1) == Result::kOk);
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kVmtrVen), 0x01);
  value = 0;
  A2B_CHECK(bench.network.ReadRegisters(1, reg::kVmtrVen, &value, 1) == Result::kOk);
  A2B_CHECK_EQ(value, 0x01);
  A2B_CHECK(bench.network.ReadRegisters(1, reg::kProduct, &value, 1) == Result::kOk);
  A2B_CHECK_EQ(value, 0x37);
  // Page 2 is nowhere.
  A2B_CHECK(bench.network.ReadRegisters(1, 0x200, &value, 1) == Result::kInvalidArgument);
}

A2B_TEST(MmrPageIsNotTheApps) {
  Bench bench;
  bench.PlugChain(1);
  A2B_CHECK(bench.StartWith(1));
  const size_t sent = bench.sim.TransferCount();
  const uint8_t one[3] = {0x01, 0x01, 0x01};
  A2B_CHECK(bench.network.WriteRegisters(kMainNode, reg::kMmrpage, one, 1) ==
            Result::kInvalidArgument);
  A2B_CHECK(bench.network.WriteRegisters(0, reg::kMmrpage, one, 1) == Result::kInvalidArgument);
  A2B_CHECK(bench.network.WriteRegisters(0, reg::kMmrpage + 0x100, one, 1) ==
            Result::kInvalidArgument);
  // A run that covers it.
  A2B_CHECK(bench.network.WriteRegisters(0, reg::kMmrpage - 1, one, 3) == Result::kInvalidArgument);
  A2B_CHECK_EQ(bench.sim.TransferCount(), sent);
  // It may be read.
  uint8_t page = 0xFF;
  A2B_CHECK(bench.network.ReadRegisters(0, reg::kMmrpage, &page, 1) == Result::kOk);
  A2B_CHECK_EQ(page, 0);
}

A2B_TEST(NodeadrAndChipWrittenByTheAppAreRewritten) {
  Bench bench;
  bench.PlugChain(2);
  bench.bus.Sub(1).AddEeprom(0x50).Load({0x5A, 0xA5});
  A2B_CHECK(bench.StartWith(2));
  const uint8_t address[2] = {0x00, 0x00};
  uint8_t data[2] = {};
  A2B_CHECK(bench.network.PeripheralWriteRead(1, PeripheralAddress::I2c(0x50), address, 2, data,
                                              2) == Result::kOk);
  A2B_CHECK_EQ(data[0], 0x5A);

  // NODEADR pointed at sub-node 0 by the app: a read of sub-node 1 still reaches it.
  const uint8_t nodeadr = 0x00;
  A2B_CHECK(bench.network.WriteRegisters(kMainNode, reg::kNodeadr, &nodeadr, 1) == Result::kOk);
  uint8_t chipId5 = 0;
  A2B_CHECK(bench.network.ReadRegisters(1, reg::kChipid0 + 5, &chipId5, 1) == Result::kOk);
  A2B_CHECK_EQ(chipId5, 0x11);

  // Sub-node 1's CHIP changed by the app: the next peripheral access sets it again.
  const uint8_t chip = 0x51;
  A2B_CHECK(bench.network.WriteRegisters(1, reg::kChip, &chip, 1) == Result::kOk);
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kChip), 0x51);
  data[0] = 0;
  A2B_CHECK(bench.network.PeripheralWriteRead(1, PeripheralAddress::I2c(0x50), address, 2, data,
                                              2) == Result::kOk);
  A2B_CHECK_EQ(data[0], 0x5A);
  A2B_CHECK_EQ(data[1], 0xA5);
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kChip), 0x50);
}

A2B_TEST(OnlyTheNodesInTheBus) {
  Bench bench;
  bench.PlugChain(1);
  // Before Start(): the main node at the board's address, page 0 only (its chip is not
  // known yet); no sub-node.
  uint8_t value = 0;
  A2B_CHECK(bench.network.ReadRegisters(kMainNode, reg::kVendor, &value, 1) == Result::kOk);
  A2B_CHECK_EQ(value, 0xAD);
  A2B_CHECK(bench.network.ReadRegisters(kMainNode, reg::kVmtrVen, &value, 1) ==
            Result::kInvalidArgument);
  A2B_CHECK(bench.network.ReadRegisters(0, reg::kVendor, &value, 1) == Result::kInvalidArgument);

  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK(bench.network.ReadRegisters(kMainNode, reg::kVmtrVen, &value, 1) == Result::kOk);
  A2B_CHECK(bench.network.ReadRegisters(1, reg::kVendor, &value, 1) == Result::kInvalidArgument);
  A2B_CHECK(bench.network.ReadRegisters(0, reg::kVendor, nullptr, 1) == Result::kInvalidArgument);
  A2B_CHECK(bench.network.WriteRegisters(0, reg::kGpiodat, nullptr, 1) == Result::kInvalidArgument);
  A2B_CHECK(bench.network.ReadRegisters(0, reg::kVendor, &value, 0) == Result::kInvalidArgument);
  uint8_t run[32] = {};
  A2B_CHECK(bench.network.ReadRegisters(0, 0xF0, run, sizeof(run)) == Result::kInvalidArgument);
  A2B_CHECK(bench.network.ReadRegisters(0, 0xF0, run, 16) == Result::kOk);

  // Stopped: the main node only.
  bench.network.Stop();
  A2B_CHECK(bench.network.ReadRegisters(0, reg::kVendor, &value, 1) == Result::kInvalidArgument);
  A2B_CHECK(bench.network.ReadRegisters(kMainNode, reg::kVendor, &value, 1) == Result::kOk);
}

/** A module-info source that reads the joining node's PRODUCT and VMTR_VEN first. */
class ReadingSource : public ModuleInfoSource {
 public:
  explicit ReadingSource(Network& network) : m_Network(network) {}

  /** Reads the node's registers, then gives the bench's image. */
  size_t GetModuleInfo(const SubNodeIdentity& node, uint8_t* buf, size_t bufLen) override {
    product = m_Network.ReadRegisters(node.position, reg::kProduct, &productValue, 1);
    page1 = m_Network.ReadRegisters(node.position, reg::kVmtrVen, &page1Value, 1);
    const std::vector<uint8_t> image = MakeImage();
    const size_t len = (image.size() < bufLen) ? image.size() : bufLen;
    for (size_t i = 0; i < len; ++i) {
      buf[i] = image[i];
    }
    return len;
  }

  Result product = Result::kNotSupported;  ///< The PRODUCT read.
  uint8_t productValue = 0;                ///< What it gave.
  Result page1 = Result::kNotSupported;    ///< The VMTR_VEN read.
  uint8_t page1Value = 0xFF;               ///< What it gave.

 private:
  Network& m_Network;
};

A2B_TEST(TheJoiningNodeFromAModuleInfoSource) {
  Bench bench;
  bench.PlugChain(1);
  ReadingSource source(bench.network);
  bench.network.SetModuleInfoSource(&source);
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK(source.product == Result::kOk);
  A2B_CHECK_EQ(source.productValue, 0x37);
  A2B_CHECK(source.page1 == Result::kOk);  // Its chip is known once it is identified.
  A2B_CHECK_EQ(source.page1Value, 0x00);
}

A2B_TEST(ASwitchOpenedByTheAppDropsTheNodesAfterIt) {
  // A documented risk: SWCTL is the stack's. Sub-node 0's switch opened takes the power
  // from sub-node 1, which the chain check then removes as dropped; the next probe there
  // finds it again.
  Bench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  const uint8_t off = 0x00;
  A2B_CHECK(bench.network.WriteRegisters(0, reg::kSwctl, &off, 1) == Result::kOk);
  bench.Run(1000);
  const std::vector<Event> removed = bench.listener.Of(EventType::kNodeRemoved);
  A2B_CHECK(!removed.empty());
  if (!removed.empty()) {
    A2B_CHECK_EQ(removed[0].node, 1);
    A2B_CHECK(removed[0].removal == RemovalReason::kDropped);
  }
  A2B_CHECK(bench.RunUntilCount(2, 3000));
}

A2B_TEST(MstrClearedByTheAppStartsTheBusAgain) {
  // A documented risk: a soft reset with MSTR clear unlocks the main node's PLL; the next
  // chain check finds MSTR clear and the bus starts again.
  Bench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  bench.listener.events.clear();
  const uint8_t reset = reg::kControlSoftrst;
  A2B_CHECK(bench.network.WriteRegisters(kMainNode, reg::kControl, &reset, 1) == Result::kOk);
  A2B_CHECK(bench.RunUntil(NetworkState::kStarting, 1000));
  A2B_CHECK(bench.RunUntil(NetworkState::kRunning, 5000));
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
}

A2B_TEST(ASoftResetWithMstrKeptLosesTheChain) {
  // A documented risk: with MSTR kept the PLL stays locked, but the main node's registers
  // reset (INTMSK2's DSCDONE among them) and the chain is lost. The sub-nodes are removed
  // as dropped, and not found again until the bus starts again: the main node's image is
  // written only at start-up.
  Bench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kBecctl), 0xEF);
  const uint8_t reset = reg::kControlSoftrst | reg::kControlMstr;
  A2B_CHECK(bench.network.WriteRegisters(kMainNode, reg::kControl, &reset, 1) == Result::kOk);
  bench.Run(3000);
  const std::vector<Event> removed = bench.listener.Of(EventType::kNodeRemoved);
  A2B_CHECK(!removed.empty());
  if (!removed.empty()) {
    A2B_CHECK_EQ(removed[0].node, 0);
    A2B_CHECK(removed[0].removal == RemovalReason::kDropped);
  }
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 0);
  A2B_CHECK(bench.network.State() == NetworkState::kRunning);
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kBecctl), 0x00);  // Not written again.
  A2B_CHECK(bench.StartWith(2));
}

A2B_TEST(AnInterruptReadByTheAppIsLostToTheStack) {
  // A documented risk: a read of INTTYPE clears the interrupt it reports.
  Bench bench;
  bench.PlugChain(1);
  A2B_CHECK(bench.StartWith(1));
  bench.bus.RaiseInterrupt(0, 9);
  uint8_t irq[2] = {};
  A2B_CHECK(bench.network.ReadRegisters(kMainNode, reg::kIntsrc, irq, 2) == Result::kOk);
  A2B_CHECK_EQ(irq[1], 9);
  bench.Run(200);
  A2B_CHECK(bench.listener.Of(EventType::kInterrupt).empty());
  // Not read by the app: the stack passes it on.
  bench.bus.RaiseInterrupt(0, 9);
  bench.Run(200);
  A2B_CHECK_EQ(bench.listener.Of(EventType::kInterrupt).size(), 1);
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
