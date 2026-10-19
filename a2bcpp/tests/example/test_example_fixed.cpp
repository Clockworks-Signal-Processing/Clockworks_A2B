/**
 * @file test_example_fixed.cpp
 * @brief The fixed app (examples/fixed) on the simulator, set up as its RP2040 program is:
 *        the AB0310 and AB0312 project's export, an AB0312 at sub-node 0 without an EEPROM,
 *        unknown AD2437 nodes after it, RJ45 links, and the stereo pairs routed both ways
 *        with nothing typed, again after a re-plug.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "fixed_app.h"
#include "sim/simulator.h"
#include "test_harness.h"

extern "C" {
extern ADI_A2B_BCD sBusDescription;  // tools/SS+reference/AB0310_AB0312_EVM_with_2437.
}

namespace a2b {
namespace {

/** An AD2437 as on the bench: CFG-4, low-voltage input, the last chip ID byte @p id. */
sim::ChipSpec Ad2437(uint8_t id) {
  sim::ChipSpec chip;
  chip.powerConfig = 4;
  chip.lowVoltageInput = true;
  chip.chipId[5] = id;
  return chip;
}

/** Plugs in an AD2437 sub-node without an EEPROM, behind an RJ45 (inverted) link. */
void PlugAd2437(sim::Bus& bus, uint8_t id) {
  sim::SubNodeSpec spec;
  spec.chip = Ad2437(id);
  spec.link.bSideInverted = true;
  bus.Plug(spec);
}

/** Polls the app for @p ms milliseconds of simulated time. */
void RunFor(sim::Simulator& sim, FixedApp& app, int ms) {
  for (int i = 0; i < ms; ++i) {
    sim.Advance(1);
    app.Loop();
  }
}

/** The heap the test reports: a fixed number, to see that the hook is called. */
size_t FakeHeap() { return 1234; }

A2B_TEST(Ab0310BenchRoutesItself) {
  sim::Simulator sim;
  sim::Bus& bus = sim.AddBus(0x68, 0x69, Ad2437(0));
  bus.StartRunning();
  PlugAd2437(bus, 1);
  PlugAd2437(bus, 2);
  PlugAd2437(bus, 3);
  // As main_rp2040.cpp sets it up.
  static const clockworks::ModuleProfile kGeneric = clockworks::GenericModule();
  static const FixedConnection kConnections[] = {{kMainNode, 0, 0, 0}, {0, 0, kMainNode, 0}};
  FixedSetup setup;
  setup.boards[0] = clockworks::FindBoard("AB0312");
  setup.fallback = &kGeneric;
  (void)setup.mainGroups.AddRx(0, 0x3);
  (void)setup.mainGroups.AddTx(0, 0x3);
  setup.connections = kConnections;
  setup.connectionCount = 2;
  setup.heapInUse = FakeHeap;
  A2B_CHECK(setup.boards[0] != nullptr);
  FixedApp app(sim);
  A2B_CHECK(app.Setup(sBusDescription, setup) == Result::kOk);
  RunFor(sim, app, 3000);
  A2B_CHECK(app.Bus().State() == NetworkState::kRunning);
  A2B_CHECK_EQ(app.Bus().SubNodeCount(), 3);  // Every RJ45 link found: XCVRBINV set.
  A2B_CHECK(app.Connected());
  A2B_CHECK_EQ(app.Bus().ConnectionCount(), 2);
  A2B_CHECK_EQ(app.Bus().Routing().routed, 2);
  // The AB0312's registers, as its project sets them; the others the generic image's.
  A2B_CHECK_EQ(bus.Sub(0).Reg(0x41), 0xE0);
  A2B_CHECK_EQ(bus.Sub(0).Reg(0x42), 0x11);
  A2B_CHECK_EQ(bus.Sub(0).Reg(0x5A), 0x81);
  A2B_CHECK_EQ(bus.Sub(0).Reg(0xB0), 0x02);
  A2B_CHECK_EQ(bus.Sub(1).Reg(0x42), 0x11);  // I2S on DTX0 and DRX0.
  A2B_CHECK_EQ(bus.Sub(1).Reg(0x4E), 0x7F);  // GPIOIEN: GPIO0-6 inputs.
  const sim::AudioTrace audio = bus.TraceAudio();
  A2B_CHECK(audio.error.empty() && audio.sub.size() == 3);
  if (audio.sub.size() == 3) {
    A2B_CHECK(audio.Played(0, 0) == sim::Signal::From(-1, 0));
    A2B_CHECK(audio.Played(0, 1) == sim::Signal::From(-1, 1));
    A2B_CHECK(audio.Played(-1, 0) == sim::Signal::From(0, 0));
    A2B_CHECK(audio.Played(-1, 1) == sim::Signal::From(0, 1));
  }
  // The AB0312 unplugged (and the nodes after it with it), then plugged back in alone:
  // found, imaged and routed again with nothing done by the app.
  bus.Unplug(0);
  RunFor(sim, app, 1500);
  A2B_CHECK_EQ(app.Bus().SubNodeCount(), 0);
  PlugAd2437(bus, 1);
  RunFor(sim, app, 3000);
  A2B_CHECK_EQ(app.Bus().SubNodeCount(), 1);
  A2B_CHECK_EQ(bus.Sub(0).Reg(0x42), 0x11);
  A2B_CHECK_EQ(app.Bus().Routing().routed, 2);
  const sim::AudioTrace again = bus.TraceAudio();
  A2B_CHECK(again.error.empty() && again.sub.size() == 1);
  if (again.sub.size() == 1) {
    A2B_CHECK(again.Played(0, 0) == sim::Signal::From(-1, 0));
    A2B_CHECK(again.Played(-1, 1) == sim::Signal::From(0, 1));
  }
  A2B_CHECK_EQ(sim.UnlockedTransfers(), 0);
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
