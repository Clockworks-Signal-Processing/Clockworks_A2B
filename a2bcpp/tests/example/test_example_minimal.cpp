/**
 * @file test_example_minimal.cpp
 * @brief The minimal example app (examples/minimal) on the simulator, with the Pi's
 *        SigmaStudio+ export linked in as the app links it.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "minimal_app.h"
#include "sim/module_info.h"
#include "sim/simulator.h"
#include "test_harness.h"

extern "C" {
extern ADI_A2B_BCD sBusDescription;  // From the export.
}

namespace a2b {
namespace {

/**
 * The bench: AD2437 nodes that detect CFG-4 in LVI mode, behind inverted links (the
 * export's main node sets XCVRBINV, which the default link takes). Each has an EEPROM at
 * 0x50 with the bench modules' settings as the boot4 golden wrote them.
 */
sim::Bus& MakeBench(sim::Simulator& sim, size_t nodes) {
  sim::ChipSpec chip;
  chip.powerConfig = 4;
  chip.lowVoltageInput = true;
  sim::Bus& bus = sim.AddBus(0x68, 0x69, chip);
  bus.StartRunning();
  sim::SubNodeSpec spec;
  spec.chip = chip;
  spec.link.bSideInverted = true;
  sim::ModuleInfoHeader header;
  header.powerConfig = sim::kPowerConfigHighPower;
  sim::ModuleInfoRegs regs;
  regs.i2sgcfg = 0xE2;
  regs.gpioien = 0x80;
  for (size_t n = 0; n < nodes; ++n) {
    bus.Plug(spec).AddEeprom(0x50).Load(sim::MakeModuleInfoImage(header, regs));
  }
  return bus;
}

/** Polls the app until the bus runs, for at most 2 s. */
void RunUntilRunning(sim::Simulator& sim, MinimalApp& app) {
  for (int i = 0; i < 2000 && app.Bus().State() != NetworkState::kRunning; ++i) {
    sim.Advance(1);
    app.Loop();
  }
}

A2B_TEST(BringsUpTheChainFromItsEeproms) {
  sim::Simulator sim;
  sim::Bus& bus = MakeBench(sim, 2);
  MinimalApp app(sim);
  A2B_CHECK(app.Setup(sBusDescription) == Result::kOk);
  RunUntilRunning(sim, app);
  A2B_CHECK(app.Bus().State() == NetworkState::kRunning);
  A2B_CHECK_EQ(app.Bus().SubNodeCount(), 2);
  for (size_t n = 0; n < 2; ++n) {
    A2B_CHECK_EQ(bus.Sub(n).Reg(0x41), 0xE2);  // I2SGCFG, from the EEPROM.
    A2B_CHECK_EQ(bus.Sub(n).Reg(0x4E), 0x80);  // GPIOIEN.
    A2B_CHECK_EQ(bus.Sub(n).PowerConfig(), 4);
    A2B_CHECK_EQ(bus.Sub(n).Reg(0xA0), 0x00);  // SWCTL2 untouched.
  }
  A2B_CHECK_EQ(sim.UnlockedTransfers(), 0);
}

A2B_TEST(BringsUpTheChainWithTheEepromsIgnored) {
  sim::Simulator sim;
  sim::Bus& bus = MakeBench(sim, 2);
  MinimalApp app(sim, true);
  A2B_CHECK(app.Setup(sBusDescription) == Result::kOk);
  RunUntilRunning(sim, app);
  A2B_CHECK(app.Bus().State() == NetworkState::kRunning);
  A2B_CHECK_EQ(app.Bus().SubNodeCount(), 2);
  for (size_t n = 0; n < 2; ++n) {
    // The app's module info: GPIO0-6 inputs, PINCFG 0, and CFG-4, so nothing is overridden.
    A2B_CHECK_EQ(bus.Sub(n).Reg(0x4E), 0x7F);
    A2B_CHECK_EQ(bus.Sub(n).Reg(0x52), 0x00);
    A2B_CHECK_EQ(bus.Sub(n).Reg(0x41), 0x00);  // Not the EEPROM's I2SGCFG.
    A2B_CHECK_EQ(bus.Sub(n).PowerConfig(), 4);
  }
  A2B_CHECK_EQ(sim.UnlockedTransfers(), 0);
}

A2B_TEST(HotPlugWithoutEepromIsRejected) {
  // A fifth module plugged in at the end of the running chain, without an EEPROM.
  sim::Simulator sim;
  sim::Bus& bus = MakeBench(sim, 4);
  MinimalApp app(sim);
  A2B_CHECK(app.Setup(sBusDescription) == Result::kOk);
  RunUntilRunning(sim, app);
  A2B_CHECK_EQ(app.Bus().SubNodeCount(), 4);
  sim::SubNodeSpec spec;
  spec.chip.powerConfig = 4;
  spec.chip.lowVoltageInput = true;
  spec.link.bSideInverted = true;
  bus.Plug(spec);
  for (int i = 0; i < 1500; ++i) {
    sim.Advance(1);
    app.Loop();
  }
  A2B_CHECK_EQ(app.Bus().SubNodeCount(), 4);  // It answered discovery and was rejected.
  A2B_CHECK(app.Bus().LastRejection() == RejectReason::kNoModuleInfo);
  A2B_CHECK((bus.Sub(3).Reg(0x09) & 0x01) == 0);  // Switched off: sub-node 3's ENSW clear.
  A2B_CHECK_EQ(bus.DiscoveredCount(), 4);         // So it dropped out of the bus.
}

/**
 * Plugs in the bench's six-node chain from @p from on: sub-nodes 0-4 behind inverted
 * (RJ45) links, sub-node 5 behind a straight (UTP) link; 0-3 with programmed EEPROMs, 4
 * and 5 with blank ones.
 */
void PlugSixNodes(sim::Bus& bus, size_t from) {
  sim::ChipSpec chip;
  chip.powerConfig = 4;
  chip.lowVoltageInput = true;
  sim::ModuleInfoHeader header;
  header.powerConfig = sim::kPowerConfigHighPower;
  for (size_t n = from; n < 6; ++n) {
    sim::SubNodeSpec spec;
    spec.chip = chip;
    spec.chip.chipId[5] = static_cast<uint8_t>(0x10 + n);
    spec.link.bSideInverted = (n != 5);
    sim::Eeprom& eeprom = bus.Plug(spec).AddEeprom(0x50);
    if (n < 4) {
      eeprom.Load(sim::MakeModuleInfoImage(header));
    }
  }
}

/** Polls the app until it has @p count sub-nodes, for at most 3 s. */
bool RunUntilCount(sim::Simulator& sim, MinimalApp& app, size_t count) {
  for (int i = 0; i < 3000 && app.Bus().SubNodeCount() != count; ++i) {
    sim.Advance(1);
    app.Loop();
  }
  return app.Bus().SubNodeCount() == count;
}

A2B_TEST(SixNodesWithAUtpLinkAtTheEnd) {
  // The bench for the drop runs: --ignore-eeprom (4 and 5 have blank EEPROMs) and
  // --utp 5. Then the cable is pulled and put back after node 4, after node 3, and
  // between the main node and node 0.
  sim::Simulator sim;
  sim::ChipSpec chip;
  chip.powerConfig = 4;
  chip.lowVoltageInput = true;
  sim::Bus& bus = sim.AddBus(0x68, 0x69, chip);
  bus.StartRunning();
  PlugSixNodes(bus, 0);
  MinimalApp app(sim, true);
  A2B_CHECK(app.SetUtpLink(5) == Result::kOk);
  A2B_CHECK(app.Setup(sBusDescription) == Result::kOk);
  A2B_CHECK(RunUntilCount(sim, app, 6));
  A2B_CHECK((bus.Sub(4).Reg(0x12) & 0x10) == 0);  // Node 4: XCVRBINV clear, for the UTP link.
  A2B_CHECK((bus.Sub(3).Reg(0x12) & 0x10) != 0);  // Node 3: still inverted.
  for (size_t cut : {size_t{5}, size_t{4}, size_t{0}}) {
    bus.Unplug(cut);
    A2B_CHECK(RunUntilCount(sim, app, cut));
    PlugSixNodes(bus, cut);
    A2B_CHECK(RunUntilCount(sim, app, 6));
  }

  // Without --utp 5, node 5 is never found: node 4's B side stays inverted.
  sim::Simulator sim2;
  sim::Bus& bus2 = sim2.AddBus(0x68, 0x69, chip);
  bus2.StartRunning();
  PlugSixNodes(bus2, 0);
  MinimalApp app2(sim2, true);
  A2B_CHECK(app2.Setup(sBusDescription) == Result::kOk);
  A2B_CHECK(RunUntilCount(sim2, app2, 5));
  for (int i = 0; i < 2000; ++i) {
    sim2.Advance(1);
    app2.Loop();
  }
  A2B_CHECK_EQ(app2.Bus().SubNodeCount(), 5);
}

A2B_TEST(UnplugAndReplug) {
  // The cable into sub-node 2 of four: sub-nodes 2 and 3 are taken out, then found again
  // when it is plugged back in.
  sim::Simulator sim;
  sim::Bus& bus = MakeBench(sim, 4);
  MinimalApp app(sim);
  A2B_CHECK(app.Setup(sBusDescription) == Result::kOk);
  RunUntilRunning(sim, app);
  A2B_CHECK_EQ(app.Bus().SubNodeCount(), 4);
  bus.Unplug(2);
  for (int i = 0; i < 600; ++i) {
    sim.Advance(1);
    app.Loop();
  }
  A2B_CHECK_EQ(app.Bus().SubNodeCount(), 2);
  sim::SubNodeSpec spec;
  spec.chip.powerConfig = 4;
  spec.chip.lowVoltageInput = true;
  spec.link.bSideInverted = true;
  sim::ModuleInfoHeader header;
  header.powerConfig = sim::kPowerConfigHighPower;
  for (int n = 0; n < 2; ++n) {
    bus.Plug(spec).AddEeprom(0x50).Load(sim::MakeModuleInfoImage(header));
  }
  for (int i = 0; i < 2000 && app.Bus().SubNodeCount() < 4; ++i) {
    sim.Advance(1);
    app.Loop();
  }
  A2B_CHECK_EQ(app.Bus().SubNodeCount(), 4);
  A2B_CHECK(app.Bus().State() == NetworkState::kRunning);
}

A2B_TEST(ReportsAMissingSync) {
  sim::Simulator sim;
  sim::Bus& bus = sim.AddBus(0x68, 0x69);
  bus.SetSync(false);
  MinimalApp app(sim);
  A2B_CHECK(app.Setup(sBusDescription) == Result::kOk);
  for (int i = 0; i < 2000 && app.Bus().State() != NetworkState::kFailed; ++i) {
    sim.Advance(1);
    app.Loop();
  }
  A2B_CHECK(app.Bus().Error() == NetworkError::kMainNodeNotRunning);
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
