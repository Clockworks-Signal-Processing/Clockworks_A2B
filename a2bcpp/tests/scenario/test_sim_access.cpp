/**
 * @file test_sim_access.cpp
 * @brief Simulator tests: Transport against simulated nodes (registers, pages, broadcast,
 *        peripherals), PG and VMTR reads, EEPROM images and CRC, two buses on one I2C
 *        bus, fault injection and the trace.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdio>
#include <string>
#include <vector>

#include "crc8.h"
#include "sim/module_info.h"
#include "sim_steps.h"
#include "test_harness.h"

namespace a2b {
namespace {

using sim::Simulator;
using test::DiscoverNext;
using test::StartMainNode;

constexpr uint8_t kMainAddr = 0x68;
constexpr uint8_t kBusAddr = 0x69;
constexpr uint8_t kEepromAddr = 0x50;

/** A simulator with one bus, running, with @p count AD2437 sub-nodes discovered. */
struct Bench {
  Simulator sim;
  sim::Bus& bus;
  Transport transport;

  explicit Bench(int count, uint8_t productId = 0x37)
      : bus(sim.AddBus(kMainAddr, kBusAddr)), transport(sim, kMainAddr, kBusAddr) {
    sim::SubNodeSpec spec;
    spec.chip.productId = productId;
    for (int i = 0; i < count; ++i) {
      bus.Plug(spec);
    }
    A2B_CHECK(StartMainNode(sim, transport));
    for (int i = 0; i < count; ++i) {
      A2B_CHECK(DiscoverNext(sim, transport, static_cast<NodeAddr>(i - 1)));
    }
  }
};

/** Reads @p len bytes of an EEPROM behind @p node from @p offset (16-bit address). */
Result ReadEeprom(Transport& transport, NodeAddr node, uint16_t offset, uint8_t* data, size_t len) {
  const uint8_t address[2] = {static_cast<uint8_t>(offset >> 8),
                              static_cast<uint8_t>(offset & 0xFF)};
  return transport.PeripheralWriteRead(node, kEepromAddr, address, sizeof(address), data, len);
}

A2B_TEST(SubNodeRegistersGoToTheRightNode) {
  Bench bench(3);
  Transport& transport = bench.transport;
  const uint8_t values[3] = {0x11, 0x22, 0x33};
  for (NodeAddr node = 0; node < 3; ++node) {
    A2B_CHECK(transport.WriteReg(node, reg::kI2sgcfg, values[node]) == Result::kOk);
  }
  for (NodeAddr node = 0; node < 3; ++node) {
    A2B_CHECK_EQ(bench.bus.Sub(static_cast<size_t>(node)).Reg(reg::kI2sgcfg), values[node]);
    uint8_t read = 0;
    A2B_CHECK(transport.ReadReg(node, reg::kI2sgcfg, read) == Result::kOk);
    A2B_CHECK_EQ(read, values[node]);
  }
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kI2sgcfg), 0);
  // A burst longer than one transfer lands in consecutive registers.
  uint8_t burst[20];
  for (size_t i = 0; i < sizeof(burst); ++i) {
    burst[i] = static_cast<uint8_t>(0xA0 + i);
  }
  A2B_CHECK(transport.WriteRegs(2, 0x80, burst, sizeof(burst)) == Result::kOk);
  A2B_CHECK_EQ(bench.bus.Sub(2).Reg(0x80), 0xA0);
  A2B_CHECK_EQ(bench.bus.Sub(2).Reg(0x93), 0xB3);
  A2B_CHECK_EQ(bench.sim.UnlockedTransfers(), 0);
}

A2B_TEST(BroadcastReachesEverySubNode) {
  Bench bench(3);
  const uint8_t value = 0x5A;
  A2B_CHECK(bench.transport.BroadcastWrite(reg::kI2sgcfg, &value, 1) == Result::kOk);
  for (size_t node = 0; node < 3; ++node) {
    A2B_CHECK_EQ(bench.bus.Sub(node).Reg(reg::kI2sgcfg), value);
  }
  // The main node too (TRMs, NODEADR.BRCST: "targeted to all the nodes (including the main
  // node)").
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kI2sgcfg), value);
  // A broadcast cannot be read.
  uint8_t read = 0;
  A2B_CHECK(bench.sim.I2cRead(kBusAddr, &read, 1) == Result::kI2cError);
}

A2B_TEST(AutoBroadcastRegistersReachTheDiscoveredSubNodes) {
  // SLOTFMT, DATCTL and I2SRRATE: a main-node write is broadcast to the sub-nodes discovered
  // by then (TRMs, "Main Node Only, Auto-Broadcast"), not to one that comes later.
  Bench bench(2);
  sim::SubNodeSpec spec;
  bench.bus.Plug(spec);  // Plugged, not discovered yet.
  Transport& transport = bench.transport;
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kI2srrate), 0x01);  // Power-on RRDIV 1.
  A2B_CHECK(transport.WriteReg(kMainNode, reg::kSlotfmt, 0x44) == Result::kOk);
  A2B_CHECK(transport.WriteReg(kMainNode, reg::kDatctl, 0x03) == Result::kOk);
  A2B_CHECK(transport.WriteReg(kMainNode, reg::kI2srrate, 0x82) == Result::kOk);
  for (size_t node = 0; node < 2; ++node) {
    A2B_CHECK_EQ(bench.bus.Sub(node).Reg(reg::kSlotfmt), 0x44);
    A2B_CHECK_EQ(bench.bus.Sub(node).Reg(reg::kDatctl), 0x03);
    A2B_CHECK_EQ(bench.bus.Sub(node).Reg(reg::kI2srrate), 0x82);
    // SLOTFMT and DATCTL are in use at the next NEWSTRCT; I2SRRATE has no such step.
    A2B_CHECK_EQ(bench.bus.Sub(node).AppliedReg(reg::kSlotfmt), 0x00);
    A2B_CHECK_EQ(bench.bus.Sub(node).AppliedReg(reg::kDatctl), 0x00);
  }
  A2B_CHECK_EQ(bench.bus.Sub(2).Reg(reg::kSlotfmt), 0x00);
  A2B_CHECK_EQ(bench.bus.Sub(2).Reg(reg::kI2srrate), 0x01);
  A2B_CHECK(transport.WriteReg(kMainNode, reg::kControl, reg::kControlNewstrct) == Result::kOk);
  A2B_CHECK_EQ(bench.bus.Sub(1).AppliedReg(reg::kSlotfmt), 0x44);
  A2B_CHECK_EQ(bench.bus.Sub(1).AppliedReg(reg::kDatctl), 0x03);
  // Discovered after the writes: it has none of them until the main node's are written again.
  A2B_CHECK(DiscoverNext(bench.sim, transport, 1));
  A2B_CHECK_EQ(bench.bus.Sub(2).Reg(reg::kSlotfmt), 0x00);
  A2B_CHECK_EQ(bench.bus.Sub(2).Reg(reg::kDatctl), 0x00);
  A2B_CHECK(transport.WriteReg(kMainNode, reg::kDatctl, 0x03) == Result::kOk);
  A2B_CHECK_EQ(bench.bus.Sub(2).Reg(reg::kDatctl), 0x03);
  A2B_CHECK_EQ(bench.bus.Sub(2).Reg(reg::kSlotfmt), 0x00);  // Not written again.
}

A2B_TEST(AutoBroadcastReachesAnAd242xSubNodeToo) {
  Bench bench(1, 0x28);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kI2srrate), 0x01);
  A2B_CHECK(bench.transport.WriteReg(kMainNode, reg::kSlotfmt, 0x22) == Result::kOk);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kSlotfmt), 0x22);
}

/** Sub-node 0 plays the main node's RX channel 0 on TX 0 and its RX channel 1 on TX 1 (its
    DTX0 on, TDM2). */
bool RouteMainPairToSubNode(Bench& bench) {
  Transport& transport = bench.transport;
  const uint8_t slots = 2;  // Two downstream slots from the main node.
  return transport.WriteReg(kMainNode, reg::kDnslots, slots) == Result::kOk &&
         transport.WriteReg(0, reg::kI2scfg, 0x11) == Result::kOk &&
         transport.WriteReg(0, reg::kDnmask0, 0x03) == Result::kOk &&  // Sub-node 0 plays both.
         transport.WriteReg(kMainNode, reg::kControl, reg::kControlNewstrct) == Result::kOk;
}

A2B_TEST(AudioNeedsDataControlAndTheMainNodesSlotFormat) {
  // The audio model: a node takes part in a direction only with DATCTL.DNS/UPS set, and, as a
  // sub-node, with the main node's slot size and format (the bench's silence).
  using sim::Signal;
  Bench bench(1);
  Transport& transport = bench.transport;
  const auto subPlaysPair = [&bench]() {
    const sim::AudioTrace audio = bench.bus.TraceAudio();
    return audio.error.empty() && audio.Played(0, 0) == Signal::From(-1, 0) &&
           audio.Played(0, 1) == Signal::From(-1, 1);
  };
  const auto subPlaysNothing = [&bench]() {
    const sim::AudioTrace audio = bench.bus.TraceAudio();
    return audio.error.empty() && audio.Played(0, 0) == Signal() && audio.Played(0, 1) == Signal();
  };
  A2B_CHECK(RouteMainPairToSubNode(bench));
  A2B_CHECK(subPlaysNothing());  // DATCTL is clear on every node: no data on the bus.
  // The main node's write is broadcast; with the next NEWSTRCT the data flows.
  A2B_CHECK(transport.WriteReg(kMainNode, reg::kSlotfmt, 0x44) == Result::kOk);
  A2B_CHECK(transport.WriteReg(kMainNode, reg::kDatctl, 0x03) == Result::kOk);
  A2B_CHECK(subPlaysNothing());  // Not in use until NEWSTRCT.
  A2B_CHECK(transport.WriteReg(kMainNode, reg::kControl, reg::kControlNewstrct) == Result::kOk);
  A2B_CHECK(subPlaysPair());
  // Downstream slots disabled everywhere.
  A2B_CHECK(transport.WriteReg(kMainNode, reg::kDatctl, reg::kDatctlUps) == Result::kOk);
  A2B_CHECK(transport.WriteReg(kMainNode, reg::kControl, reg::kControlNewstrct) == Result::kOk);
  A2B_CHECK(subPlaysNothing());
  A2B_CHECK(transport.WriteReg(kMainNode, reg::kDatctl, 0x03) == Result::kOk);
  // Only the sub-node without them (a local value; the broadcast would have fixed it).
  A2B_CHECK(transport.WriteReg(0, reg::kDatctl, reg::kDatctlUps) == Result::kOk);
  A2B_CHECK(transport.WriteReg(kMainNode, reg::kControl, reg::kControlNewstrct) == Result::kOk);
  A2B_CHECK(subPlaysNothing());
  A2B_CHECK(transport.WriteReg(0, reg::kDatctl, 0x03) == Result::kOk);
  A2B_CHECK(transport.WriteReg(kMainNode, reg::kControl, reg::kControlNewstrct) == Result::kOk);
  A2B_CHECK(subPlaysPair());
  // A sub-node with another downstream slot size can't use the slots.
  A2B_CHECK(transport.WriteReg(0, reg::kSlotfmt, 0x42) == Result::kOk);
  A2B_CHECK(transport.WriteReg(kMainNode, reg::kControl, reg::kControlNewstrct) == Result::kOk);
  A2B_CHECK(subPlaysNothing());
}

A2B_TEST(VoltageMonitorOnPageOne) {
  Bench bench(1);
  Transport& transport = bench.transport;
  const uint16_t vltg1 = reg::kVmtrVltg0 + 3;
  // As ADI's two-step check: enable input 1, then read it.
  A2B_CHECK(transport.WriteReg(0, reg::kVmtrVen, 0x02) == Result::kOk);
  A2B_CHECK_EQ(bench.bus.Sub(0).Page(), 1);
  uint8_t reading = 0;
  A2B_CHECK(transport.ReadReg(0, vltg1, reading) == Result::kOk);
  A2B_CHECK_EQ(reading, sim::Node::kDefaultSupplyReading);
  // 24 V missing.
  bench.bus.Sub(0).SetVmtrInput(1, 0x10);
  A2B_CHECK(transport.ReadReg(0, vltg1, reading) == Result::kOk);
  A2B_CHECK_EQ(reading, 0x10);
  // Input disabled: no reading.
  A2B_CHECK(transport.WriteReg(0, reg::kVmtrVen, 0x00) == Result::kOk);
  A2B_CHECK(transport.ReadReg(0, vltg1, reading) == Result::kOk);
  A2B_CHECK_EQ(reading, 0);
  // Page 0 again for a page-0 register; the main node was never switched.
  A2B_CHECK(transport.ReadReg(0, reg::kVendor, reading) == Result::kOk);
  A2B_CHECK_EQ(reading, 0xAD);
  A2B_CHECK_EQ(bench.bus.Sub(0).Page(), 0);
  A2B_CHECK_EQ(bench.bus.Main().Page(), 0);
  // The main node has one too.
  A2B_CHECK(transport.WriteReg(kMainNode, reg::kVmtrVen, 0x02) == Result::kOk);
  A2B_CHECK(transport.ReadReg(kMainNode, vltg1, reading) == Result::kOk);
  A2B_CHECK_EQ(reading, sim::Node::kDefaultSupplyReading);
}

A2B_TEST(Ad2433HasAVoltageMonitorToo) {
  Bench bench(1, 0x33);
  uint8_t reading = 0;
  A2B_CHECK(bench.transport.WriteReg(0, reg::kVmtrVen, 0x02) == Result::kOk);
  A2B_CHECK(bench.transport.ReadReg(0, reg::kVmtrVltg0 + 3, reading) == Result::kOk);
  A2B_CHECK_EQ(reading, sim::Node::kDefaultSupplyReading);
}

A2B_TEST(Ad242xHasNoPageOne) {
  Bench bench(1, 0x28);  // No MMRPAGE, so it stays on page 0.
  A2B_CHECK(bench.transport.WriteReg(0, reg::kVmtrVen, 0x02) == Result::kOk);
  A2B_CHECK_EQ(bench.bus.Sub(0).Page(), 0);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(0x00), 0x02);  // It landed in CHIP.
}

A2B_TEST(PowerGoodOnSubNodeGpio7) {
  Bench bench(1);
  Transport& transport = bench.transport;
  uint8_t levels = 0xFF;
  // Input not enabled: reads 0.
  A2B_CHECK(transport.ReadReg(0, reg::kGpioin, levels) == Result::kOk);
  A2B_CHECK_EQ(levels, 0);
  A2B_CHECK(transport.WriteReg(0, reg::kGpioien, 0x80) == Result::kOk);
  A2B_CHECK(transport.ReadReg(0, reg::kGpioin, levels) == Result::kOk);
  A2B_CHECK_EQ(levels, 0x80);
  bench.bus.Sub(0).SetGpioInput(7, false);
  A2B_CHECK(transport.ReadReg(0, reg::kGpioin, levels) == Result::kOk);
  A2B_CHECK_EQ(levels, 0);
  // GPIOIN is read-only.
  A2B_CHECK(transport.WriteReg(0, reg::kGpioin, 0xFF) == Result::kOk);
  A2B_CHECK(transport.ReadReg(0, reg::kGpioin, levels) == Result::kOk);
  A2B_CHECK_EQ(levels, 0);
}

A2B_TEST(EepromWithModuleInfoBehindASubNode) {
  Bench bench(2);
  sim::ModuleInfoHeader header;
  header.powerConfig = sim::kPowerConfigHighPower;
  bench.bus.Sub(1).AddEeprom(kEepromAddr).Load(sim::MakeModuleInfoImage(header));
  Transport& transport = bench.transport;

  uint8_t image[0x24] = {};
  A2B_CHECK(ReadEeprom(transport, 1, 0x0000, image, sizeof(image)) == Result::kOk);
  A2B_CHECK_EQ(image[0x00], sim::kModuleInfoMarker);
  A2B_CHECK_EQ(image[0x01], 0xAD);
  A2B_CHECK_EQ(image[0x03], 0x37);
  A2B_CHECK_EQ(image[sim::kModuleInfoHeaderCrc], Crc8(image, sim::kModuleInfoHeaderLen));
  A2B_CHECK_EQ(image[sim::kModuleInfoPowerConfig] >> 4, sim::kPowerConfigHighPower);

  // Register access after the peripheral access still reaches the node itself.
  uint8_t vendor = 0;
  A2B_CHECK(transport.ReadReg(1, reg::kVendor, vendor) == Result::kOk);
  A2B_CHECK_EQ(vendor, 0xAD);
  // Node 0 has no EEPROM, and there is none at 0x51 behind node 1.
  A2B_CHECK(ReadEeprom(transport, 0, 0x0000, image, 1) == Result::kI2cError);
  const uint8_t address[2] = {0, 0};
  A2B_CHECK(transport.PeripheralWriteRead(1, 0x51, address, 2, image, 1) == Result::kI2cError);
}

A2B_TEST(BadHeaderCrcIsVisible) {
  Bench bench(1);
  std::vector<uint8_t> image = sim::MakeModuleInfoImage(sim::ModuleInfoHeader());
  image[0x05] ^= 0x01;  // Edited without resealing.
  bench.bus.Sub(0).AddEeprom(kEepromAddr).Load(image);
  uint8_t header[0x20] = {};
  A2B_CHECK(ReadEeprom(bench.transport, 0, 0x0000, header, sizeof(header)) == Result::kOk);
  A2B_CHECK(header[sim::kModuleInfoHeaderCrc] != Crc8(header, sim::kModuleInfoHeaderLen));
  sim::SealModuleInfoHeader(image);
  A2B_CHECK_EQ(image[sim::kModuleInfoHeaderCrc], Crc8(image.data(), sim::kModuleInfoHeaderLen));
}

A2B_TEST(AdiModuleInfoImageFromCfg) {
  const char* path = A2B_REPO_ROOT "/adi/module_info/adi_a2b_node_ModuleInfo_1.3.0.0_RJ45.dat";
  std::FILE* file = std::fopen(path, "rb");
  A2B_CHECK(file != nullptr);
  if (file == nullptr) {
    return;
  }
  std::vector<uint8_t> image;
  int byte = 0;
  while ((byte = std::fgetc(file)) != EOF) {
    image.push_back(static_cast<uint8_t>(byte));
  }
  std::fclose(file);
  A2B_CHECK_EQ(image.size(), 11602);

  Bench bench(1);
  bench.bus.Sub(0).AddEeprom(kEepromAddr).Load(image);
  uint8_t header[0x24] = {};
  A2B_CHECK(ReadEeprom(bench.transport, 0, 0x0000, header, sizeof(header)) == Result::kOk);
  A2B_CHECK_EQ(header[0], sim::kModuleInfoMarker);
  A2B_CHECK_EQ(header[sim::kModuleInfoHeaderCrc], Crc8(header, sim::kModuleInfoHeaderLen));
  A2B_CHECK_EQ(header[sim::kModuleInfoPowerConfig] >> 4, sim::kPowerConfigHighPower);
  // The last bytes, read across the 16-bit address.
  uint8_t tail[2] = {};
  A2B_CHECK(ReadEeprom(bench.transport, 0, 11600, tail, sizeof(tail)) == Result::kOk);
  A2B_CHECK_EQ(tail[0], image[11600]);
  A2B_CHECK_EQ(tail[1], image[11601]);
}

A2B_TEST(EepromWritesWrapInThePageAndBusyTheDevice) {
  Bench bench(1);
  sim::Eeprom& eeprom = bench.bus.Sub(0).AddEeprom(kEepromAddr);
  Transport& transport = bench.transport;
  // Four bytes from 0x003E: two fit the first 64-byte page, two wrap to its start.
  const uint8_t write[6] = {0x00, 0x3E, 1, 2, 3, 4};
  A2B_CHECK(transport.PeripheralWrite(0, kEepromAddr, write, sizeof(write)) == Result::kOk);
  A2B_CHECK_EQ(eeprom.Contents()[0x3E], 1);
  A2B_CHECK_EQ(eeprom.Contents()[0x3F], 2);
  A2B_CHECK_EQ(eeprom.Contents()[0x00], 3);
  A2B_CHECK_EQ(eeprom.Contents()[0x01], 4);
  A2B_CHECK_EQ(eeprom.Contents()[0x40], 0xFF);
  // Busy for the write cycle.
  uint8_t read = 0;
  A2B_CHECK(ReadEeprom(transport, 0, 0x3E, &read, 1) == Result::kI2cError);
  bench.sim.Advance(sim::Eeprom::kDefaultWriteCycleMs);
  A2B_CHECK(ReadEeprom(transport, 0, 0x3E, &read, 1) == Result::kI2cError);
  bench.sim.Advance(1);
  A2B_CHECK(ReadEeprom(transport, 0, 0x3E, &read, 1) == Result::kOk);
  A2B_CHECK_EQ(read, 1);
  // Write protect: acknowledged, unchanged.
  eeprom.SetWriteProtect(true);
  const uint8_t overwrite[3] = {0x00, 0x3E, 9};
  A2B_CHECK(transport.PeripheralWrite(0, kEepromAddr, overwrite, sizeof(overwrite)) == Result::kOk);
  A2B_CHECK_EQ(eeprom.Contents()[0x3E], 1);
  A2B_CHECK_EQ(eeprom.WriteCount(), 1);
}

A2B_TEST(DeviceOnTheHostBus) {
  // The main node's peripherals sit on the host I2C bus.
  Simulator sim;
  sim.AddBus(kMainAddr, kBusAddr);
  sim::Eeprom eeprom;
  eeprom.Load(sim::MakeModuleInfoImage(sim::ModuleInfoHeader()));
  sim.AttachHostDevice(kEepromAddr, eeprom);
  Transport transport(sim, kMainAddr, kBusAddr);
  uint8_t marker = 0;
  A2B_CHECK(ReadEeprom(transport, kMainNode, 0x0000, &marker, 1) == Result::kOk);
  A2B_CHECK_EQ(marker, sim::kModuleInfoMarker);
  // Nothing at another address.
  A2B_CHECK(transport.PeripheralRead(kMainNode, 0x3C, &marker, 1) == Result::kI2cError);
}

A2B_TEST(TwoBusesOnOneI2cBus) {
  Simulator sim;
  sim::Bus& busA = sim.AddBus(0x68, 0x69);
  sim::Bus& busB = sim.AddBus(0x6A, 0x6B);
  busA.Plug(sim::SubNodeSpec());
  busB.Plug(sim::SubNodeSpec());
  busB.Plug(sim::SubNodeSpec());
  Transport transportA(sim, 0x68, 0x69);
  Transport transportB(sim, 0x6A, 0x6B);
  A2B_CHECK(StartMainNode(sim, transportA));
  A2B_CHECK(StartMainNode(sim, transportB));
  A2B_CHECK(DiscoverNext(sim, transportA, kMainNode));
  A2B_CHECK(DiscoverNext(sim, transportB, kMainNode));
  A2B_CHECK(DiscoverNext(sim, transportB, 0));
  A2B_CHECK_EQ(busA.DiscoveredCount(), 1);
  A2B_CHECK_EQ(busB.DiscoveredCount(), 2);
  // Interleaved accesses keep each bus's NODEADR and pages apart.
  A2B_CHECK(transportA.WriteReg(0, reg::kI2sgcfg, 0x0A) == Result::kOk);
  A2B_CHECK(transportB.WriteReg(1, reg::kI2sgcfg, 0x0B) == Result::kOk);
  A2B_CHECK(transportB.WriteReg(0, reg::kVmtrVen, 0x02) == Result::kOk);
  A2B_CHECK(transportA.WriteReg(0, reg::kI2sgcfg, 0x1A) == Result::kOk);
  A2B_CHECK_EQ(busA.Sub(0).Reg(reg::kI2sgcfg), 0x1A);
  A2B_CHECK_EQ(busB.Sub(0).Reg(reg::kI2sgcfg), 0);
  A2B_CHECK_EQ(busB.Sub(1).Reg(reg::kI2sgcfg), 0x0B);
  A2B_CHECK_EQ(busA.Sub(0).Page(), 0);
  A2B_CHECK_EQ(busB.Sub(0).Page(), 1);
  A2B_CHECK(sim.IrqPending(1));
}

A2B_TEST(TransientI2cErrors) {
  Bench bench(1);
  Transport& transport = bench.transport;
  uint8_t value = 0;
  bench.sim.FailTransfer(0);
  A2B_CHECK(transport.ReadReg(0, reg::kVendor, value) == Result::kI2cError);
  A2B_CHECK(transport.ReadReg(0, reg::kVendor, value) == Result::kOk);
  A2B_CHECK_EQ(value, 0xAD);
  // The bus address fails twice, then works.
  bench.sim.FailAddress(kBusAddr, 2);
  A2B_CHECK(transport.ReadReg(0, reg::kProduct, value) == Result::kI2cError);
  A2B_CHECK(transport.ReadReg(0, reg::kProduct, value) == Result::kI2cError);
  A2B_CHECK(transport.ReadReg(0, reg::kProduct, value) == Result::kOk);
  A2B_CHECK_EQ(value, 0x37);
  // The main address was not affected.
  A2B_CHECK(transport.ReadReg(kMainNode, reg::kProduct, value) == Result::kOk);
}

A2B_TEST(TraceUsesTheI2ctFormat) {
  Simulator sim;
  sim.AddBus(kMainAddr, kBusAddr);
  Transport transport(sim, kMainAddr, kBusAddr);
  sim.SetNowMs(2);
  transport.WriteReg(kMainNode, reg::kControl, reg::kControlSoftrst);
  sim.Advance(1);
  uint8_t ids[3] = {};
  transport.ReadRegs(kMainNode, reg::kVendor, ids, sizeof(ids));
  uint8_t value = 0;
  transport.ReadReg(0, reg::kVendor, value);  // NODEADR write, then a NAK: nothing discovered.
  const std::string trace = sim.Trace();
  A2B_CHECK_STREQ(trace.c_str(),
                  "I2CT-BEGIN,1,4,0,0,0\n"
                  "I2CT,0,2000,0,68,W,0,2,1204,0,\n"
                  "I2CT,1,3000,0,68,WR,0,1,02,3,AD3721\n"
                  "I2CT,2,3000,0,68,W,0,2,0100,0,\n"
                  "I2CT,3,3000,0,69,WR,1,1,02,1,00\n"
                  "I2CT-END\n");
  A2B_CHECK_EQ(sim.TransferCount(), 4);
  sim.ClearTrace();
  A2B_CHECK_EQ(sim.TransferCount(), 0);
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
