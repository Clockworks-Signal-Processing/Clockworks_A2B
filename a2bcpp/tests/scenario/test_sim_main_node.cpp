/**
 * @file test_sim_main_node.cpp
 * @brief Simulator tests: main-node reset (the TRMs' soft-reset rules), SUSTAIN,
 *        MSTR_RUNNING, reset and IRQ lines, PG.
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
using test::ReadInterrupt;
using test::StartMainNode;

constexpr uint8_t kMainAddr = 0x68;
constexpr uint8_t kBusAddr = 0x69;

A2B_TEST(PowerOnMainNodeIsIdle) {
  Simulator sim;
  sim::Bus& bus = sim.AddBus(kMainAddr, kBusAddr);
  Transport transport(sim, kMainAddr, kBusAddr);
  uint8_t ids[4] = {};
  A2B_CHECK(transport.ReadRegs(kMainNode, reg::kVendor, ids, sizeof(ids)) == Result::kOk);
  A2B_CHECK_EQ(ids[0], 0xAD);
  A2B_CHECK_EQ(ids[1], 0x37);
  A2B_CHECK_EQ(ids[2], 0x21);
  A2B_CHECK_EQ(ids[3], 0x03);
  A2B_CHECK_EQ(ReadInterrupt(transport), -1);
  A2B_CHECK(!bus.Running());
  sim.Advance(1000);
  A2B_CHECK_EQ(ReadInterrupt(transport), -1);
}

A2B_TEST(ResetWaitAndMstrGiveMstrRunning) {
  Simulator sim;
  sim::Bus& bus = sim.AddBus(kMainAddr, kBusAddr);
  bus.StartRunning();  // As the last session left it: no RESETn pulse at start-up.
  Transport transport(sim, kMainAddr, kBusAddr);
  transport.WriteReg(kMainNode, reg::kControl, reg::kControlSoftrst);
  A2B_CHECK(!bus.Running());
  A2B_CHECK(bus.InSustain());
  sim.Advance(test::kResetWaitMs);
  A2B_CHECK(!bus.InSustain());
  transport.WriteReg(kMainNode, reg::kI2sgcfg, reg::kI2sgcfgInv);
  transport.WriteReg(kMainNode, reg::kControl, reg::kControlMstr);
  sim.Advance(sim.GetTiming().pllLockMs - 1);
  A2B_CHECK_EQ(ReadInterrupt(transport), -1);
  sim.Advance(1);
  uint8_t source = 0;
  A2B_CHECK(transport.ReadReg(kMainNode, reg::kIntsrc, source) == Result::kOk);
  A2B_CHECK_EQ(source, reg::kIntsrcMstint);
  uint8_t status = 0;
  A2B_CHECK(transport.ReadReg(kMainNode, reg::kIntstat, status) == Result::kOk);
  A2B_CHECK_EQ(status, reg::kIntstatIrq);
  uint8_t type = 0;
  A2B_CHECK(transport.ReadReg(kMainNode, reg::kInttype, type) == Result::kOk);
  A2B_CHECK_EQ(type, reg::kInttypeMstrRunning);
  // Reading INTTYPE cleared it.
  A2B_CHECK_EQ(ReadInterrupt(transport), -1);
  A2B_CHECK(bus.Running());
  A2B_CHECK(!bus.Wedged());
}

A2B_TEST(MstrDuringSustainWedgesUntilPowerCycle) {
  Simulator sim;
  sim::Bus& bus = sim.AddBus(kMainAddr, kBusAddr);
  bus.StartRunning();
  Transport transport(sim, kMainAddr, kBusAddr);
  // ADI's stack before it waited out SUSTAIN: MSTR 0.1 ms after the soft reset.
  transport.WriteReg(kMainNode, reg::kControl, reg::kControlSoftrst);
  transport.WriteReg(kMainNode, reg::kControl, reg::kControlMstr);
  A2B_CHECK(bus.Wedged());
  sim.Advance(100);
  A2B_CHECK_EQ(ReadInterrupt(transport), -1);
  // Another reset and a proper wait do not help.
  A2B_CHECK(!StartMainNode(sim, transport));
  sim.Advance(100);
  A2B_CHECK_EQ(ReadInterrupt(transport), -1);
  A2B_CHECK(!bus.Running());
  // A power cycle does.
  bus.PowerCycle();
  A2B_CHECK(!bus.Wedged());
  A2B_CHECK(StartMainNode(sim, transport));
}

A2B_TEST(InvDuringSustainWedges) {
  Simulator sim;
  sim::Bus& bus = sim.AddBus(kMainAddr, kBusAddr);
  bus.StartRunning();
  Transport transport(sim, kMainAddr, kBusAddr);
  transport.WriteReg(kMainNode, reg::kControl, reg::kControlSoftrst);
  sim.Advance(12);
  transport.WriteReg(kMainNode, reg::kI2sgcfg, reg::kI2sgcfgInv);
  A2B_CHECK(bus.Wedged());
}

A2B_TEST(WritesWithoutInvDuringSustainAreHarmless) {
  Simulator sim;
  sim::Bus& bus = sim.AddBus(kMainAddr, kBusAddr);
  bus.StartRunning();
  Transport transport(sim, kMainAddr, kBusAddr);
  transport.WriteReg(kMainNode, reg::kControl, reg::kControlSoftrst);
  transport.WriteReg(kMainNode, reg::kI2sgcfg, 0x02);
  transport.WriteReg(kMainNode, reg::kIntmsk0, 0x10);
  A2B_CHECK(!bus.Wedged());
  sim.Advance(test::kResetWaitMs);
  transport.WriteReg(kMainNode, reg::kControl, reg::kControlMstr);
  sim.Advance(sim.GetTiming().pllLockMs);
  A2B_CHECK_EQ(ReadInterrupt(transport), reg::kInttypeMstrRunning);
}

A2B_TEST(SustainIsTimedFromTheLastReset) {
  Simulator sim;
  sim::Bus& bus = sim.AddBus(kMainAddr, kBusAddr);
  bus.StartRunning();
  Transport transport(sim, kMainAddr, kBusAddr);
  // Two resets 13 ms apart (the boot4 goldens show two, 12.6 ms apart); MSTR 21 ms after
  // the second is still inside SUSTAIN.
  transport.WriteReg(kMainNode, reg::kControl, reg::kControlSoftrst);
  sim.Advance(13);
  transport.WriteReg(kMainNode, reg::kControl, reg::kControlSoftrst);
  sim.Advance(21);
  transport.WriteReg(kMainNode, reg::kControl, reg::kControlMstr);
  A2B_CHECK(bus.Wedged());
}

A2B_TEST(ResetOfAStoppedMainNodeHasNoSustain) {
  // After a power-on (or RESETn) the PLL is not locked, so a soft reset has no SUSTAIN.
  Simulator sim;
  sim::Bus& bus = sim.AddBus(kMainAddr, kBusAddr);
  Transport transport(sim, kMainAddr, kBusAddr);
  transport.WriteReg(kMainNode, reg::kControl, reg::kControlSoftrst);
  A2B_CHECK(!bus.InSustain());
  transport.WriteReg(kMainNode, reg::kControl, reg::kControlMstr);
  sim.Advance(sim.GetTiming().pllLockMs);
  A2B_CHECK_EQ(ReadInterrupt(transport), reg::kInttypeMstrRunning);
  A2B_CHECK(!bus.Wedged());
}

A2B_TEST(Ad242xMainNodeHasNoSustain) {
  // An AD242x main node goes straight back to POWER-UP (AD242x TRM: SUSTAIN is a
  // sub-node feature), so MSTR at once after the reset is safe.
  Simulator sim;
  sim::ChipSpec ad2428;
  ad2428.productId = 0x28;
  sim::Bus& bus = sim.AddBus(kMainAddr, kBusAddr, ad2428);
  bus.StartRunning();
  Transport transport(sim, kMainAddr, kBusAddr);
  transport.WriteReg(kMainNode, reg::kControl, reg::kControlSoftrst);
  A2B_CHECK(!bus.InSustain());
  A2B_CHECK(!bus.Running());
  transport.WriteReg(kMainNode, reg::kControl, reg::kControlMstr);
  A2B_CHECK(!bus.Wedged());
  sim.Advance(sim.GetTiming().pllLockMs);
  A2B_CHECK_EQ(ReadInterrupt(transport), reg::kInttypeMstrRunning);
}

A2B_TEST(SoftResetWithMstrKeepsThePllLocked) {
  // TRM "Soft Reset": with CONTROL.MSTR = 1 the PLL stays locked. The registers reset
  // (except CONTROL) and the chain is lost, but there is no SUSTAIN and no new
  // MSTR_RUNNING.
  Simulator sim;
  sim::Bus& bus = sim.AddBus(kMainAddr, kBusAddr);
  bus.Plug(sim::SubNodeSpec());
  Transport transport(sim, kMainAddr, kBusAddr);
  A2B_CHECK(StartMainNode(sim, transport));
  A2B_CHECK(test::DiscoverNext(sim, transport, kMainNode));
  transport.WriteReg(kMainNode, reg::kI2sgcfg, 0xE2);
  transport.WriteReg(kMainNode, reg::kControl,
                     static_cast<uint8_t>(reg::kControlSoftrst | reg::kControlMstr));
  A2B_CHECK(bus.Running());
  A2B_CHECK(!bus.InSustain());
  A2B_CHECK_EQ(bus.DiscoveredCount(), 0);
  A2B_CHECK_EQ(bus.Main().Reg(reg::kI2sgcfg), 0x00);
  A2B_CHECK_EQ(bus.Main().Reg(reg::kControl), reg::kControlMstr);
  sim.Advance(100);
  A2B_CHECK_EQ(ReadInterrupt(transport), -1);
}

A2B_TEST(SoftResetKeepsControlBmmcfgAndOnAd2433Pllctl) {
  for (uint8_t productId : {0x37, 0x33}) {
    Simulator sim;
    sim::ChipSpec chip;
    chip.productId = productId;
    sim::Bus& bus = sim.AddBus(kMainAddr, kBusAddr, chip);
    Transport transport(sim, kMainAddr, kBusAddr);
    A2B_CHECK(StartMainNode(sim, transport));
    transport.WriteReg(kMainNode, reg::kBmmcfg, 0x01);
    transport.WriteReg(kMainNode, reg::kPllctl, 0x05);
    transport.WriteReg(kMainNode, reg::kPincfg, 0x00);
    transport.WriteReg(kMainNode, reg::kRespcycs, 0xB9);
    transport.WriteReg(kMainNode, reg::kControl,
                       static_cast<uint8_t>(reg::kControlSoftrst | reg::kControlXcvrbinv));
    A2B_CHECK_EQ(bus.Main().Reg(reg::kControl), reg::kControlXcvrbinv);
    A2B_CHECK_EQ(bus.Main().Reg(reg::kBmmcfg), 0x01);
    A2B_CHECK_EQ(bus.Main().Reg(reg::kPllctl), (productId == 0x33) ? 0x05 : 0x00);
    // The rest are back at their power-on values.
    A2B_CHECK_EQ(bus.Main().Reg(reg::kPincfg), reg::kPincfgDrvstr);
    A2B_CHECK_EQ(bus.Main().Reg(reg::kRespcycs), sim::Node::kResetRespCycles);
  }
}

A2B_TEST(SoftResetInPowerUpHasNoEffect) {
  // TRM: "A soft reset does not take effect when the node is in the power-up state."
  Simulator sim;
  sim::Bus& bus = sim.AddBus(kMainAddr, kBusAddr);
  Transport transport(sim, kMainAddr, kBusAddr);
  transport.WriteReg(kMainNode, reg::kI2sgcfg, 0x02);
  transport.WriteReg(kMainNode, reg::kControl, reg::kControlSoftrst);
  A2B_CHECK_EQ(bus.Main().Reg(reg::kI2sgcfg), 0x02);
  A2B_CHECK(!bus.InSustain());
}

A2B_TEST(MstrIsIgnoredOnceThePllIsLocked) {
  // TRM: once the PLL is locked, writing 0 or 1 to CONTROL.MSTR has no effect.
  Simulator sim;
  sim::Bus& bus = sim.AddBus(kMainAddr, kBusAddr);
  Transport transport(sim, kMainAddr, kBusAddr);
  A2B_CHECK(StartMainNode(sim, transport));
  transport.WriteReg(kMainNode, reg::kControl, 0x00);
  A2B_CHECK(bus.Running());
  transport.WriteReg(kMainNode, reg::kControl, reg::kControlMstr);
  sim.Advance(100);
  A2B_CHECK_EQ(ReadInterrupt(transport), -1);
}

A2B_TEST(PowerOnRegisterValues) {
  // Registers whose reset value isn't 0, from the TRM register maps.
  Simulator sim;
  sim::ChipSpec ad2428;
  ad2428.productId = 0x28;
  sim::Bus& bus37 = sim.AddBus(kMainAddr, kBusAddr);
  sim::Bus& bus28 = sim.AddBus(0x6A, 0x6B, ad2428);
  for (sim::Bus* bus : {&bus37, &bus28}) {
    const sim::Node& main = bus->Main();
    A2B_CHECK_EQ(main.Reg(reg::kChip), 0x50);
    A2B_CHECK_EQ(main.Reg(reg::kRespcycs), 0x40);
    A2B_CHECK_EQ(main.Reg(reg::kNode), reg::kNodeLast);
    A2B_CHECK_EQ(main.Reg(reg::kPincfg), reg::kPincfgDrvstr);
  }
  A2B_CHECK_EQ(bus37.Main().Reg(reg::kRxmask0 + 7), 0xFF);
  A2B_CHECK_EQ(bus37.Main().Reg(reg::kTxxbar0 + 31), 31);
  A2B_CHECK_EQ(bus37.Main().Reg(reg::kVmtrVmax0 + 3 * 6), 0xFF);
  A2B_CHECK_EQ(bus37.Main().Reg(reg::kI2srrate), 0x01);
  A2B_CHECK_EQ(bus28.Main().Reg(reg::kTxxbar0 + 31), 0);
}

A2B_TEST(NoSyncNoPllLock) {
  Simulator sim;
  sim::Bus& bus = sim.AddBus(kMainAddr, kBusAddr);
  bus.SetSync(false);
  Transport transport(sim, kMainAddr, kBusAddr);
  transport.WriteReg(kMainNode, reg::kControl, reg::kControlMstr);
  sim.Advance(1000);
  A2B_CHECK_EQ(ReadInterrupt(transport), -1);
  A2B_CHECK(!bus.Running());
  // SYNC arrives: the PLL locks tPLK later.
  bus.SetSync(true);
  sim.Advance(sim.GetTiming().pllLockMs);
  A2B_CHECK_EQ(ReadInterrupt(transport), reg::kInttypeMstrRunning);
}

A2B_TEST(SyncBreakUnlocksAndRelocks) {
  // TRMs, "Main Node PLL Locked Interrupt": the registers reset (CONTROL.MSTR kept), the
  // chain is lost, and MSTR_RUNNING comes again once SYNC is back.
  Simulator sim;
  sim::Bus& bus = sim.AddBus(kMainAddr, kBusAddr);
  bus.Plug(sim::SubNodeSpec());
  Transport transport(sim, kMainAddr, kBusAddr);
  A2B_CHECK(StartMainNode(sim, transport));
  A2B_CHECK(test::DiscoverNext(sim, transport, kMainNode));
  transport.WriteReg(kMainNode, reg::kI2sgcfg, 0xE2);
  bus.SetSync(false);
  A2B_CHECK(!bus.Running());
  A2B_CHECK(!bus.InSustain());  // A main node goes straight to POWER-UP.
  A2B_CHECK_EQ(bus.DiscoveredCount(), 0);
  A2B_CHECK_EQ(bus.Main().Reg(reg::kI2sgcfg), 0x00);
  sim.Advance(100);
  A2B_CHECK_EQ(ReadInterrupt(transport), -1);
  bus.SetSync(true);
  sim.Advance(sim.GetTiming().pllLockMs);
  A2B_CHECK_EQ(ReadInterrupt(transport), reg::kInttypeMstrRunning);
  A2B_CHECK(bus.Running());
}

A2B_TEST(ResetLine) {
  Simulator sim;
  sim::Bus& bus = sim.AddBus(kMainAddr, kBusAddr);
  A2B_CHECK(sim.SetMainNodeReset(0, true) == Result::kNotSupported);
  A2B_CHECK(sim.SetMainNodeReset(1, true) == Result::kNotSupported);
  bus.SetHasResetLine(true);
  bus.StartRunning();
  Transport transport(sim, kMainAddr, kBusAddr);
  // Wedge it, then show that RESETn recovers it with no SUSTAIN afterwards.
  transport.WriteReg(kMainNode, reg::kControl, reg::kControlSoftrst);
  transport.WriteReg(kMainNode, reg::kControl, reg::kControlMstr);
  A2B_CHECK(bus.Wedged());
  A2B_CHECK(sim.SetMainNodeReset(0, true) == Result::kOk);
  uint8_t value = 0;
  A2B_CHECK(transport.ReadReg(kMainNode, reg::kVendor, value) == Result::kI2cError);
  A2B_CHECK(sim.SetMainNodeReset(0, false) == Result::kOk);
  A2B_CHECK(!bus.Wedged());
  A2B_CHECK(!bus.Running());
  // Nothing answers for tACCESS after the release.
  A2B_CHECK(transport.ReadReg(kMainNode, reg::kVendor, value) == Result::kI2cError);
  sim.Advance(sim.GetTiming().accessAfterResetMs - 1);
  A2B_CHECK(transport.ReadReg(kMainNode, reg::kVendor, value) == Result::kI2cError);
  sim.Advance(1);
  A2B_CHECK(transport.ReadReg(kMainNode, reg::kVendor, value) == Result::kOk);
  A2B_CHECK_EQ(value, 0xAD);
  transport.ForgetAll();
  transport.WriteReg(kMainNode, reg::kControl, reg::kControlMstr);
  sim.Advance(sim.GetTiming().pllLockMs);
  A2B_CHECK_EQ(ReadInterrupt(transport), reg::kInttypeMstrRunning);
}

A2B_TEST(IrqLine) {
  Simulator sim;
  sim::Bus& bus = sim.AddBus(kMainAddr, kBusAddr);
  Transport transport(sim, kMainAddr, kBusAddr);
  // Without the line, the stack is told to look every time.
  A2B_CHECK(sim.IrqPending(0));
  bus.SetHasIrqLine(true);
  A2B_CHECK(!sim.IrqPending(0));
  transport.WriteReg(kMainNode, reg::kControl, reg::kControlMstr);
  A2B_CHECK(!sim.IrqPending(0));
  sim.Advance(sim.GetTiming().pllLockMs);
  A2B_CHECK(sim.IrqPending(0));
  A2B_CHECK_EQ(ReadInterrupt(transport), reg::kInttypeMstrRunning);
  A2B_CHECK(!sim.IrqPending(0));
}

A2B_TEST(MainNodePowerGood) {
  Simulator sim;
  sim::Bus& bus = sim.AddBus(kMainAddr, kBusAddr);
  A2B_CHECK(sim.PowerGood(0));
  bus.SetPowerGood(false);
  A2B_CHECK(!sim.PowerGood(0));
  A2B_CHECK(sim.PowerGood(5));  // No such bus: the Platform default.
}

A2B_TEST(LogsAreKept) {
  Simulator sim;
  Platform& platform = sim;
  platform.Log(LogLevel::kWarning, "node 2 rejected");
  A2B_CHECK_EQ(sim.LogLines().size(), 1);
  A2B_CHECK_STREQ(sim.LogLines()[0].c_str(), "W: node 2 rejected");
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
