/**
 * @file test_sim_spi.cpp
 * @brief SPI over A2B on the simulator (docs/SPI.md): the host's SPI to the main node
 *        (SpiReadRegisters(), the wiring check), the hosts and main nodes without it, and the
 *        data tunnel to a W25Q128-like flash on a sub-node: its slots in the routing plan
 *        (followed by the simulator's own slot model), the six transaction types, at the
 *        minimum tunnel (2 slots each way) and at 4 slots (about 5 Mbit/s of payload with
 *        32-bit slots), with audio routed alongside, through an AD2428, to an AD2433, the
 *        module leaving and coming back, a restart, CLKOUT2's GPIOSEL, the errors, and
 *        the probes for a new node giving way to SPI (a probe stops the tunnel).
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "a2b/network.h"
#include "detail/regdefs.h"
#include "routing_bench.h"
#include "sim/simulator.h"
#include "sim/spi_flash.h"
#include "sim_bench.h"
#include "test_harness.h"

namespace a2b {
namespace {

using test::GroupBoard;
using test::GroupImage;
using test::Plays;
using test::PortImage;
using test::RoutingBench;

/** GroupBoard() with 32-bit slots both ways (SLOTFMT 0x66): a 2-slot tunnel is 8 bytes. */
BoardConfig Board32() {
  BoardConfig board = GroupBoard();
  board.mainNode.Set(reg::kSlotfmt, 0x66);
  return board;
}

/** A bench whose sub-nodes are AD2437 modules with GroupImage(), a flash on sub-node
    @p flashAt's slave select 0. */
struct SpiBench : RoutingBench {
  sim::SpiFlash flash;

  explicit SpiBench(size_t count = 1, size_t flashAt = 0, const BoardConfig& config = Board32(),
                    std::vector<std::vector<uint8_t>> images = {GroupImage()})
      : RoutingBench(config, std::move(images)), flash(sim) {
    PlugChain(static_cast<int>(count));
    bus.Sub(flashAt).AttachSpiDevice(0, flash);
  }

  /** Polls until an event of @p type has come (@p count of them), for at most @p ms. */
  bool RunUntilEvent(EventType type, size_t count = 1, uint32_t ms = 3000) {
    for (uint32_t i = 0; i < ms && listener.Of(type).size() < count; ++i) {
      Run(1);
    }
    return listener.Of(type).size() >= count;
  }

  /** Opens a tunnel of @p slots to @p node and runs until it is open. */
  bool Open(NodeAddr node, uint8_t slots) {
    SpiTunnelConfig config;
    config.node = node;
    config.slots = slots;
    const size_t opened = listener.Of(EventType::kSpiTunnelOpened).size();
    return network.OpenSpiTunnel(config) == Result::kOk &&
           RunUntilEvent(EventType::kSpiTunnelOpened, opened + 1) && network.SpiTunnelReady();
  }

  /** The flash's JEDEC ID through an atomic read: EF 40 18 for a W25Q128JV. */
  bool ReadsJedecId(NodeAddr node) {
    const uint8_t command = 0x9F;
    uint8_t id[3] = {};
    return network.PeripheralWriteRead(node, PeripheralAddress::Spi(0), &command, 1, id, 3) ==
               Result::kOk &&
           id[0] == 0xEF && id[1] == 0x40 && id[2] == 0x18;
  }

  /** Waits (polling) until the flash is no longer busy. */
  void WaitFlash() {
    for (int i = 0; i < 200 && flash.Busy(); ++i) {
      Run(1);
    }
  }
};

/**
 * Reads @p len bytes of the flash from @p address with the extended full duplex: one
 * remote transfer (its slave select held), 0x03 and the address, then the data, in
 * transactions of up to 256 bytes; each gives the previous one's read data, and the tunnel
 * FIFO read the last one's.
 * @return The bytes read, without the command's four.
 */
std::vector<uint8_t> ExtendedRead(Network& network, uint32_t address, size_t len) {
  std::vector<uint8_t> out(4 + len, 0);
  out[0] = 0x03;
  out[1] = static_cast<uint8_t>(address >> 16);
  out[2] = static_cast<uint8_t>(address >> 8);
  out[3] = static_cast<uint8_t>(address);
  std::vector<uint8_t> in(out.size(), 0);
  size_t done = 0;
  size_t last = 0;
  while (done < out.size()) {
    const size_t chunk = std::min<size_t>(256, out.size() - done);
    const bool more = done + chunk < out.size();
    uint8_t previous[256];
    if (done > 0 && chunk != last) {
      // A different length: the previous one's data from the FIFO first (the TRM's note).
      if (network.SpiReadTunnelFifo(previous, last) != Result::kOk) {
        return {};
      }
      std::memcpy(&in[done - last], previous, last);
    }
    if (network.SpiFullDuplex(0, &out[done], previous, chunk,
                              more ? SpiChunk::kMore : SpiChunk::kLast) != Result::kOk) {
      return {};
    }
    if (done > 0 && chunk == last) {
      std::memcpy(&in[done - last], previous, last);
    }
    last = chunk;
    done += chunk;
  }
  uint8_t tail[256];
  if (network.SpiReadTunnelFifo(tail, last) != Result::kOk) {
    return {};
  }
  std::memcpy(&in[done - last], tail, last);
  return std::vector<uint8_t>(in.begin() + 4, in.end());
}

/** Programs a whole 256-byte page with the extended bulk write (260 bytes with 0x02 and the
    address): write enable (atomic) first. */
bool ProgramPage(Network& network, uint32_t address, const uint8_t* data) {
  const uint8_t enable = 0x06;
  if (network.PeripheralWrite(0, PeripheralAddress::Spi(0), &enable, 1) != Result::kOk) {
    return false;
  }
  std::vector<uint8_t> out = {0x02, static_cast<uint8_t>(address >> 16),
                              static_cast<uint8_t>(address >> 8), static_cast<uint8_t>(address)};
  out.insert(out.end(), data, data + 256);
  return network.SpiBulkWrite(0, out.data(), 256, SpiChunk::kMore) == Result::kOk &&
         network.SpiBulkWrite(0, out.data() + 256, 4, SpiChunk::kLast) == Result::kOk;
}

A2B_TEST(MainNodeRegistersOverSpi) {
  // The SPI local register read: VENDOR, PRODUCT, VERSION, as I2C reads them; before
  // Start() too (the board's chip says the main node has SPI).
  RoutingBench bench;
  uint8_t ids[3] = {};
  A2B_CHECK(bench.network.SpiReadRegisters(reg::kVendor, ids, 3) == Result::kOk);
  A2B_CHECK_EQ(ids[0], 0xAD);
  A2B_CHECK_EQ(ids[1], 0x37);
  A2B_CHECK_EQ(ids[2], 0x21);
  // One status read before and after the command, each SPIT line framed as the TRM's.
  const std::string trace = bench.sim.SpiTrace();
  A2B_CHECK(trace.find(",0400,") != std::string::npos);
  A2B_CHECK(trace.find(",010202000000,") != std::string::npos);
  const SpiStatus status = bench.network.LastSpiStatus();
  A2B_CHECK(!status.timedOut && !status.hostFailed && status.spiInt == 0);
  // Arguments: 1 to 32 registers, within page 0.
  uint8_t many[33] = {};
  A2B_CHECK(bench.network.SpiReadRegisters(0x00, many, 33) == Result::kInvalidArgument);
  A2B_CHECK(bench.network.SpiReadRegisters(0xF0, many, 32) == Result::kInvalidArgument);
  A2B_CHECK(bench.network.SpiReadRegisters(0x00, nullptr, 1) == Result::kInvalidArgument);
  A2B_CHECK(bench.network.SpiReadRegisters(0x00, many, 32) == Result::kOk);
}

A2B_TEST(NoSpiOnTheHostOrTheMainNode) {
  // A host without SPI (the USBi): not supported, nothing else sent.
  RoutingBench bench;
  bench.sim.SetHasSpi(false);
  uint8_t ids[3] = {};
  A2B_CHECK(bench.network.SpiReadRegisters(reg::kVendor, ids, 3) == Result::kNotSupported);
  A2B_CHECK_EQ(bench.sim.SpiTransferCount(), 0u);
  A2B_CHECK_EQ(bench.sim.TransferCount(), 0u);
  // An AD2428 main node has no SPI port: refused before anything is sent.
  BoardConfig board = test::MakeBoardFor(0x28);
  RoutingBench ad2428(board, {test::GroupImage()}, test::ChipOf(0x28));
  A2B_CHECK(ad2428.network.SpiReadRegisters(reg::kVendor, ids, 3) == Result::kNotSupported);
  A2B_CHECK_EQ(ad2428.sim.SpiTransferCount(), 0u);
}

A2B_TEST(FailedHostTransferAndSpiPortOff) {
  RoutingBench bench;
  uint8_t ids[3] = {};
  bench.sim.FailSpiTransfers(1);
  A2B_CHECK(bench.network.SpiReadRegisters(reg::kVendor, ids, 3) == Result::kSpiError);
  A2B_CHECK(bench.network.LastSpiStatus().hostFailed);
  A2B_CHECK(bench.network.SpiReadRegisters(reg::kVendor, ids, 3) == Result::kOk);
  A2B_CHECK(!bench.network.LastSpiStatus().hostFailed);
  // The main node's SPI port off (SPICFG 0x02): nothing drives MISO, so the status reads
  // 0xFF, busy, until the timeout (the console's `spi check` reads SPICFG over I2C first).
  A2B_CHECK(bench.network.WriteRegisters(kMainNode, reg::kSpicfg, &reg::kSpimodeOff, 1) ==
            Result::kOk);
  A2B_CHECK(bench.network.SpiReadRegisters(reg::kVendor, ids, 3) == Result::kSpiError);
  A2B_CHECK(bench.network.LastSpiStatus().timedOut);
}

/** The six transaction types against the flash on a tunnel of @p slots each way. */
void FlashThroughTunnel(uint8_t slots, uint64_t& readUs) {
  SpiBench bench;
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK(!bench.network.SpiTunnelReady());
  A2B_CHECK(!bench.ReadsJedecId(0));  // No tunnel yet.
  A2B_CHECK(bench.Open(0, slots));
  // The simulator's slot model: the tunnel's slots reach the far end and come back.
  const sim::AudioTrace audio = bench.Audio();
  A2B_CHECK(audio.error.empty() && audio.tunnelError.empty());
  A2B_CHECK(audio.tunnelNode == 0 && audio.tunnelDown && audio.tunnelUp);
  A2B_CHECK_EQ(audio.dnSlots[0], slots);
  A2B_CHECK_EQ(bench.network.Routing().tunnel.dnSlots, slots);
  // The far end is an SPI master on its slave select 0; the main node the tunnel's owner.
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kSpicfg), reg::kSpimodeMaster);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kSpipincfg), reg::kSpipincfgSpimss0en);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kSpickdiv), 4);
  A2B_CHECK((bench.bus.Main().Reg(reg::kSpicfg) & reg::kSpicfgTnlowner) != 0);
  // Atomic read: the JEDEC ID; a slave select not enabled, or another node, refused.
  A2B_CHECK(bench.ReadsJedecId(0));
  const uint8_t command = 0x9F;
  uint8_t id[3] = {};
  A2B_CHECK(bench.network.PeripheralWriteRead(0, PeripheralAddress::Spi(1), &command, 1, id, 3) ==
            Result::kInvalidArgument);
  // Atomic write: write enable, then a sector erase; full duplex: the status register, its
  // BUSY bit seen in the FIFO read.
  const uint8_t enable = 0x06;
  A2B_CHECK(bench.network.PeripheralWrite(0, PeripheralAddress::Spi(0), &enable, 1) == Result::kOk);
  const uint8_t erase[4] = {0x20, 0x00, 0x10, 0x00};
  A2B_CHECK(bench.network.PeripheralWrite(0, PeripheralAddress::Spi(0), erase, 4) == Result::kOk);
  A2B_CHECK_EQ(bench.flash.Erases(), 1u);
  const uint8_t status[2] = {0x05, 0x00};
  uint8_t previous[2] = {};
  A2B_CHECK(bench.network.SpiFullDuplex(0, status, previous, 2) == Result::kOk);
  A2B_CHECK(previous[0] == 0 && previous[1] == 0);  // The first full duplex gives zeros.
  uint8_t now[2] = {};
  A2B_CHECK(bench.network.SpiReadTunnelFifo(now, 2) == Result::kOk);
  A2B_CHECK_EQ(now[1] & 0x01, 0x01);  // BUSY: erasing.
  bench.WaitFlash();
  A2B_CHECK(bench.network.SpiFullDuplex(0, status, previous, 2) == Result::kOk);
  A2B_CHECK_EQ(previous[1] & 0x01, 0x01);  // The last one's answer.
  A2B_CHECK(bench.network.SpiReadTunnelFifo(now, 2) == Result::kOk);
  A2B_CHECK_EQ(now[1] & 0x01, 0x00);
  // Bulk (with the extended bulk): a whole page programmed.
  uint8_t page[256];
  for (size_t i = 0; i < sizeof(page); ++i) {
    page[i] = static_cast<uint8_t>(i * 7 + slots);
  }
  A2B_CHECK(ProgramPage(bench.network, 0x1000, page));
  bench.WaitFlash();
  A2B_CHECK_EQ(bench.flash.Programs(), 1u);
  A2B_CHECK(std::equal(page, page + 256, bench.flash.Memory().begin() + 0x1000));
  // A short page program with the plain bulk write.
  A2B_CHECK(bench.network.PeripheralWrite(0, PeripheralAddress::Spi(0), &enable, 1) == Result::kOk);
  const uint8_t shortProgram[] = {0x02, 0x00, 0x11, 0x00, 0x12, 0x34};
  A2B_CHECK(bench.network.SpiBulkWrite(0, shortProgram, sizeof(shortProgram)) == Result::kOk);
  bench.WaitFlash();
  A2B_CHECK(bench.flash.Memory()[0x1100] == 0x12 && bench.flash.Memory()[0x1101] == 0x34);
  // Extended full duplex: 64 KB read back in one remote transfer.
  for (size_t i = 0; i < 0x10000; ++i) {
    bench.flash.Memory()[0x20000 + i] = static_cast<uint8_t>(i ^ (i >> 8));
  }
  const uint64_t start = bench.sim.NowUs();
  const std::vector<uint8_t> read = ExtendedRead(bench.network, 0x20000, 0x10000);
  readUs = bench.sim.NowUs() - start;
  A2B_CHECK_EQ(read.size(), 0x10000u);
  A2B_CHECK(std::equal(read.begin(), read.end(), bench.flash.Memory().begin() + 0x20000));
  // Every transaction was answered without an error, and none was refused as busy.
  A2B_CHECK_EQ(bench.bus.SpiBadCommands(), 0u);
  A2B_CHECK_EQ(bench.network.LastSpiStatus().spiInt, 0);
  A2B_CHECK_EQ(bench.sim.UnlockedTransfers(), 0);
}

A2B_TEST(FlashThroughTheMinimumTunnelAndFourSlots) {
  uint64_t minimumUs = 0;
  uint64_t fourUs = 0;
  FlashThroughTunnel(kSpiMinTunnelSlots, minimumUs);
  FlashThroughTunnel(4, fourUs);
  // The simulator's time for the 64 KB read (its model: the superframes each tunnel size
  // takes, the remote SCK at 9.8 MHz, the host's at 8 MHz): more slots, less time.
  std::printf("  64 KB read: %u us at 2 slots (%u kbit/s), %u us at 4 slots (%u kbit/s)\n",
              static_cast<unsigned>(minimumUs),
              static_cast<unsigned>(0x10000ull * 8000 / (minimumUs ? minimumUs : 1)),
              static_cast<unsigned>(fourUs),
              static_cast<unsigned>(0x10000ull * 8000 / (fourUs ? fourUs : 1)));
  A2B_CHECK(fourUs < minimumUs);
}

A2B_TEST(TunnelAlongsideAudio) {
  // Two sub-nodes, the main node's pair routed to sub-node 1 and back; a tunnel to sub-node 0
  // in the middle, then to sub-node 1 at the end: the audio and the tunnel both get through.
  SpiBench bench(2, 0);
  sim::SpiFlash second(bench.sim);
  bench.bus.Sub(1).AttachSpiDevice(0, second);
  A2B_CHECK(bench.StartWith(2));
  A2B_CHECK(bench.network.AddConnection(kMainNode, 0, 1, 0) == Result::kOk);
  A2B_CHECK(bench.network.AddConnection(1, 0, kMainNode, 0) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied());
  for (NodeAddr far : {static_cast<NodeAddr>(0), static_cast<NodeAddr>(1)}) {
    A2B_CHECK(bench.Open(far, 4));
    const sim::AudioTrace audio = bench.Audio();
    A2B_CHECK(audio.error.empty() && audio.tunnelError.empty());
    A2B_CHECK(audio.tunnelNode == far && audio.tunnelDown && audio.tunnelUp);
    A2B_CHECK(Plays(audio, 1, 0, -1, 0) && Plays(audio, 1, 1, -1, 1));
    A2B_CHECK(Plays(audio, -1, 0, 1, 0) && Plays(audio, -1, 1, 1, 1));
    A2B_CHECK(bench.ReadsJedecId(far));
    A2B_CHECK_EQ(bench.bus.AccessesInNewStructSettle(), 0u);
  }
  // Moved: the old far end's tunnel is off and its SPI port back to its image's (0).
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kDtcfg), 0);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kSpicfg), 0);
  A2B_CHECK_EQ(bench.listener.Of(EventType::kSpiTunnelClosed).size(), 1u);
  // Closed: a plan without it; the audio stays.
  bench.network.CloseSpiTunnel();
  A2B_CHECK(bench.RunUntilEvent(EventType::kSpiTunnelClosed, 2));
  A2B_CHECK(!bench.network.SpiTunnelReady());
  A2B_CHECK(bench.network.SpiTunnel() == nullptr);
  const sim::AudioTrace after = bench.Audio();
  A2B_CHECK(after.error.empty() && after.tunnelNode == -1);
  A2B_CHECK(Plays(after, 1, 0, -1, 0) && Plays(after, -1, 0, 1, 0));
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kDtcfg), 0);
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kDtcfg), 0);
  A2B_CHECK(!bench.ReadsJedecId(1));
}

A2B_TEST(TunnelFollowsItsModule) {
  // The far end unplugged: the tunnel closes; plugged back: it opens again, by chip ID.
  SpiBench bench(2, 1);
  A2B_CHECK(bench.StartWith(2));
  A2B_CHECK(bench.Open(1, 2));
  bench.bus.Unplug(1);
  A2B_CHECK(bench.RunUntilEvent(EventType::kSpiTunnelClosed));
  A2B_CHECK(!bench.network.SpiTunnelReady());
  A2B_CHECK(bench.network.SpiTunnel() != nullptr);  // Still asked for.
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kDtcfg), 0);
  sim::SubNodeSpec spec = test::Ad2437(0x11);
  bench.bus.Plug(spec).AttachSpiDevice(0, bench.flash);
  A2B_CHECK(bench.RunUntilEvent(EventType::kSpiTunnelOpened, 2));
  A2B_CHECK(bench.ReadsJedecId(1));
  // A restart: closed, then open again once the chain is up.
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntilEvent(EventType::kSpiTunnelOpened, 3));
  A2B_CHECK_EQ(bench.listener.Of(EventType::kSpiTunnelClosed).size(), 2u);
  A2B_CHECK(bench.ReadsJedecId(1));
}

A2B_TEST(TunnelThroughAnAd2428AndToAnAd2433) {
  // An AD2428 between the main node and the far end passes the tunnel's slots as any others;
  // an AD2433 can be a far end (untested on hardware); an AD2428 can't.
  const auto image = [](uint8_t product) {
    sim::ModuleInfoRegs regs;
    regs.i2sgcfg = 0x07;
    regs.i2scfg = 0x11;
    return PortImage(regs, ChannelGroups(), test::PowerConfigOf(product));
  };
  RoutingBench bench(Board32(), {image(0x28), image(0x33)});
  sim::SpiFlash flash(bench.sim);
  bench.bus.Plug(test::SubOf(0x28, 0x10));
  bench.bus.Plug(test::SubOf(0x33, 0x11)).AttachSpiDevice(0, flash);
  A2B_CHECK(bench.StartWith(2));
  SpiTunnelConfig config;
  config.node = 0;
  A2B_CHECK(bench.network.OpenSpiTunnel(config) == Result::kInvalidArgument);  // AD2428.
  config.node = 1;
  A2B_CHECK(bench.network.OpenSpiTunnel(config) == Result::kOk);
  for (int i = 0; i < 2000 && !bench.network.SpiTunnelReady(); ++i) {
    bench.Run(1);
  }
  const sim::AudioTrace audio = bench.Audio();
  A2B_CHECK(audio.error.empty() && audio.tunnelError.empty() && audio.tunnelDown && audio.tunnelUp);
  const uint8_t command = 0x9F;
  uint8_t id[3] = {};
  A2B_CHECK(bench.network.PeripheralWriteRead(1, PeripheralAddress::Spi(0), &command, 1, id, 3) ==
            Result::kOk);
  A2B_CHECK(id[0] == 0xEF && id[1] == 0x40 && id[2] == 0x18);
}

A2B_TEST(TunnelSlotsAndSettingsChecked) {
  // 16-bit slots need 3 at least (6 bytes); 2 is refused, with why.
  BoardConfig board = Board32();
  board.mainNode.Set(reg::kSlotfmt, 0x22);
  SpiBench bench(1, 0, board);
  A2B_CHECK(bench.StartWith(1));
  SpiTunnelConfig config;
  config.slots = 2;
  RoutingError why = RoutingError::kNone;
  A2B_CHECK(bench.network.OpenSpiTunnel(config, &why) == Result::kInvalidArgument);
  A2B_CHECK(why == RoutingError::kTunnelSlots);
  A2B_CHECK(bench.network.SpiTunnel() == nullptr);
  config.slots = 3;
  A2B_CHECK(bench.Open(0, 3));
  A2B_CHECK(bench.ReadsJedecId(0));
  // Settings out of range, a node not in the bus.
  config.slots = 13;
  A2B_CHECK(bench.network.OpenSpiTunnel(config) == Result::kInvalidArgument);
  config.slots = 4;
  config.selects = 0;
  A2B_CHECK(bench.network.OpenSpiTunnel(config) == Result::kInvalidArgument);
  config.selects = 1;
  config.node = 1;
  A2B_CHECK(bench.network.OpenSpiTunnel(config) == Result::kInvalidArgument);
  A2B_CHECK(bench.network.SpiTunnelReady());  // The tunnel open stays.
}

A2B_TEST(Clkout2KeptWithGpiosel) {
  // The AB0020's case (anomaly 18000047): CLKOUT2 on ADR2 with GPIOSEL 0 would stop with the
  // SPI port on, so the far end gets GPIOSEL 2 while the tunnel is open, and its PINCFG back
  // after. A node using IO0, IO4, IO5 or IO6 as GPIO is refused.
  sim::ModuleInfoRegs regs;
  regs.i2sgcfg = 0x07;
  regs.i2scfg = 0x11;
  regs.clk2cfg = 0x81;
  regs.pincfg = 0x01;
  regs.gpiooen = 0x80;
  sim::ModuleInfoHeader header;
  header.powerConfig = sim::kPowerConfigHighPower;
  std::vector<uint8_t> clock = sim::MakeModuleInfoImage(header, regs);
  regs.gpioien = 0x01;
  std::vector<uint8_t> clockAndIo0 = sim::MakeModuleInfoImage(header, regs);
  SpiBench bench(2, 0, Board32(), {clock, clockAndIo0});
  A2B_CHECK(bench.StartWith(2));
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kPincfg), 0x01);
  A2B_CHECK(bench.Open(0, 4));
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kPincfg), 0x81);
  A2B_CHECK(
      bench.Logged("I: a2b: SPI tunnel: PINCFG.GPIOSEL 2 on its far end, to keep CLKOUT2 on ADR2"));
  A2B_CHECK(bench.ReadsJedecId(0));
  SpiTunnelConfig config;
  config.node = 1;
  A2B_CHECK(bench.network.OpenSpiTunnel(config) == Result::kInvalidArgument);
  bench.network.CloseSpiTunnel();
  A2B_CHECK(bench.RunUntilEvent(EventType::kSpiTunnelClosed));
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kPincfg), 0x01);
}

A2B_TEST(TunnelErrorsReported) {
  SpiBench bench;
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK(bench.Open(0, 2));
  // The far end's tunnel taken off behind the stack's back: the main node reports it
  // (SPIDTERR, DTINVALID), and the bits are cleared after.
  const uint8_t off = 0;
  A2B_CHECK(bench.network.WriteRegisters(0, reg::kDtcfg, &off, 1) == Result::kOk);
  const uint8_t enable = 0x06;
  A2B_CHECK(bench.network.PeripheralWrite(0, PeripheralAddress::Spi(0), &enable, 1) ==
            Result::kSpiError);
  const SpiStatus status = bench.network.LastSpiStatus();
  A2B_CHECK_EQ(status.spiInt, reg::kSpiintSpidterr);
  A2B_CHECK_EQ(status.spiStat & reg::kSpistatErrors, reg::kSpistatDtinvalid);
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kSpiint), 0);
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kSpistat) & reg::kSpistatErrors, 0);
  // A FIFO read past the data: FIFOUNF, the bytes zero.
  uint8_t data[4] = {1, 2, 3, 4};
  A2B_CHECK(bench.network.SpiReadTunnelFifo(data, 4) == Result::kSpiError);
  A2B_CHECK_EQ(bench.network.LastSpiStatus().spiInt, reg::kSpiintFifounf);
  // An abort clears it all.
  A2B_CHECK(bench.network.SpiAbort() == Result::kOk);
  // Lengths: 1-256 bytes.
  uint8_t big[257] = {};
  A2B_CHECK(bench.network.SpiBulkWrite(0, big, 257) == Result::kInvalidArgument);
  A2B_CHECK(bench.network.SpiFullDuplex(0, big, nullptr, 0) == Result::kInvalidArgument);
  A2B_CHECK(bench.network.PeripheralWriteRead(0, PeripheralAddress::Spi(0), big, 256, data, 1) ==
            Result::kInvalidArgument);
}

/** Polls until a probe for a new node is out (DISCVRY written), for at most @p ms. */
bool RunUntilProbe(SpiBench& bench, uint32_t ms = 2000) {
  for (uint32_t i = 0; i < ms && !bench.bus.DiscoveryActive(); ++i) {
    bench.Run(1);
  }
  return bench.bus.DiscoveryActive();
}

A2B_TEST(ProbeEndsForAnSpiTransaction) {
  // A probe behind the far end (the last node) stops the tunnel: the simulator aborts a
  // transaction then (DTABORT), as the bench did. The stack ends the probe first, as one that
  // found nothing, and the transaction goes through; no probe for spiProbeHoldMs after.
  SpiBench bench(1, 0);
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK(bench.Open(0, 2));
  A2B_CHECK(RunUntilProbe(bench));
  // The model: a tunnel command sent past the stack now ends DTABORT.
  uint8_t raw[3] = {0x0C, 0x00, 0x06};  // Atomic write of a write enable, SS0 of sub-node 0.
  A2B_CHECK(bench.sim.SpiTransfer(0, raw, raw, sizeof(raw)) == Result::kOk);
  A2B_CHECK_EQ(bench.bus.SpiDiscoveryAborts(), 1u);
  A2B_CHECK(bench.network.SpiAbort() == Result::kOk);  // Its SPIINT and SPISTAT cleared.
  // Through the stack: the probe is ended, then the transaction runs.
  A2B_CHECK(bench.ReadsJedecId(0));
  A2B_CHECK_EQ(bench.bus.SpiDiscoveryAborts(), 1u);
  A2B_CHECK(!bench.bus.DiscoveryActive());
  A2B_CHECK(bench.Logged("I: a2b: probe for sub-node 1 ended for an SPI transaction"));
  A2B_CHECK(bench.network.State() == NetworkState::kRunning);
  const size_t transfers = bench.sim.SpiTransferCount();
  // No probe within the hold of the last transaction; then they come again.
  for (uint32_t i = 0; i < 950; ++i) {
    bench.Run(1);
    A2B_CHECK(!bench.bus.DiscoveryActive());
  }
  A2B_CHECK(RunUntilProbe(bench, 600));
  A2B_CHECK_EQ(bench.sim.SpiTransferCount(), transfers);
}

A2B_TEST(ProbesHeldWhileSpiIsInUse) {
  // A node plugged in behind the far end while SPI is in use every 100 ms: no probe, so
  // no transaction aborts; once SPI is idle for the hold, the node is found, the tunnel is
  // planned again (its far end no longer the last node), and works.
  SpiBench bench(1, 0);
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK(bench.Open(0, 4));
  bench.bus.Plug(test::Ad2437(0x11));
  bool probed = false;
  for (int round = 0; round < 30; ++round) {
    A2B_CHECK(bench.ReadsJedecId(0));
    for (uint32_t i = 0; i < 100; ++i) {
      bench.Run(1);
      probed = probed || bench.bus.DiscoveryActive();
    }
  }
  A2B_CHECK(!probed);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1u);
  A2B_CHECK_EQ(bench.bus.SpiDiscoveryAborts(), 0u);
  const uint32_t idleFrom = bench.sim.NowMs();
  A2B_CHECK(bench.RunUntilCount(2, 3000));
  A2B_CHECK(bench.sim.NowMs() - idleFrom >= 900u);
  A2B_CHECK(bench.sim.NowMs() - idleFrom <= 1600u);
  A2B_CHECK(bench.RunUntilEvent(EventType::kRoutingApplied, 2));
  for (uint32_t i = 0; i < 100 && !bench.network.SpiTunnelReady(); ++i) {
    bench.Run(1);
  }
  A2B_CHECK(bench.network.SpiTunnelReady());
  A2B_CHECK(bench.ReadsJedecId(0));
  A2B_CHECK_EQ(bench.bus.SpiDiscoveryAborts(), 0u);
}

A2B_TEST(BusyWhileANodeJoinsBehindTheFarEnd) {
  // Once a probe has found a node, a transaction isn't sent (Result::kBusy, nothing on the
  // host's SPI) until the node is in and the tunnel is planned again.
  SpiBench bench(1, 0);
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK(bench.Open(0, 2));
  bench.bus.Plug(test::Ad2437(0x11));
  for (uint32_t i = 0; i < 3000 && bench.bus.DiscoveredCount() < 2; ++i) {
    bench.Run(1);
  }
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 2u);
  bench.Run(1);  // The stack reads DSCDONE: the node is joining.
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1u);
  const size_t transfers = bench.sim.SpiTransferCount();
  const uint8_t command = 0x9F;
  uint8_t id[3] = {};
  A2B_CHECK(bench.network.PeripheralWriteRead(0, PeripheralAddress::Spi(0), &command, 1, id, 3) ==
            Result::kBusy);
  A2B_CHECK_EQ(bench.sim.SpiTransferCount(), transfers);
  A2B_CHECK(bench.RunUntilCount(2, 3000));
  // In, and its tunnel plan due: still busy, and not ready.
  A2B_CHECK(!bench.network.SpiTunnelReady());
  A2B_CHECK(bench.network.PeripheralWriteRead(0, PeripheralAddress::Spi(0), &command, 1, id, 3) ==
            Result::kBusy);
  for (uint32_t i = 0; i < 500 && !bench.network.SpiTunnelReady(); ++i) {
    bench.Run(1);
  }
  A2B_CHECK(bench.network.SpiTunnelReady());
  A2B_CHECK(bench.ReadsJedecId(0));
  A2B_CHECK_EQ(bench.bus.SpiDiscoveryAborts(), 0u);
}

A2B_TEST(ReopenedTunnelReadyOnlyWithItsNewPlan) {
  // Opened again with another size: not ready (and busy) until the new plan is written.
  SpiBench bench(1, 0);
  A2B_CHECK(bench.StartWith(1));
  A2B_CHECK(bench.Open(0, 4));
  SpiTunnelConfig config;
  config.node = 0;
  config.slots = 2;
  A2B_CHECK(bench.network.OpenSpiTunnel(config) == Result::kOk);
  A2B_CHECK(!bench.network.SpiTunnelReady());
  const uint8_t enable = 0x06;
  A2B_CHECK(bench.network.PeripheralWrite(0, PeripheralAddress::Spi(0), &enable, 1) ==
            Result::kBusy);
  for (uint32_t i = 0; i < 500 && !bench.network.SpiTunnelReady(); ++i) {
    bench.Run(1);
  }
  A2B_CHECK(bench.network.SpiTunnelReady());
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kDtslots), 0x22);
  A2B_CHECK(bench.ReadsJedecId(0));
}

A2B_TEST(NoProbesWhileTheTunnelEndsAtTheLastNodeWithAudio) {
  // The far end as the last node, with audio to it: its TRM last-node configuration is
  // valid only while it is last, and a probe makes it not last (on the bench its audio
  // glitched at every probe). No probe then; a node plugged in behind it is found once the
  // tunnel is closed. Without audio there (ProbeEndsForAnSpiTransaction) the probes go on.
  SpiBench bench(2, 1);
  A2B_CHECK(bench.StartWith(2));
  A2B_CHECK(bench.network.AddConnection(kMainNode, 0, 1, 0) == Result::kOk);
  A2B_CHECK(bench.network.ApplyRouting() == RoutingError::kNone);
  A2B_CHECK(bench.RunUntilApplied());
  A2B_CHECK(RunUntilProbe(bench));  // Probes behind it with the audio alone.
  A2B_CHECK(bench.Open(1, 4));
  A2B_CHECK(Plays(bench.Audio(), 1, 0, -1, 0));
  bench.bus.Plug(test::Ad2437(0x12));
  bool probed = false;
  for (uint32_t i = 0; i < 3000; ++i) {
    bench.Run(1);
    probed = probed || bench.bus.DiscoveryActive();
  }
  A2B_CHECK(!probed);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 2u);
  A2B_CHECK(bench.network.State() == NetworkState::kRunning);
  A2B_CHECK(bench.Logged("I: a2b: SPI tunnel at last sub-node 1 with audio: no probes"));
  A2B_CHECK(bench.ReadsJedecId(1));
  // Closed: the probes start again and find the node.
  bench.network.CloseSpiTunnel();
  A2B_CHECK(bench.RunUntilCount(3, 3000));
  A2B_CHECK(bench.Logged("I: a2b: probes again"));
  A2B_CHECK(Plays(bench.Audio(), 1, 0, -1, 0));
  // Opened again to sub-node 1, now in the middle: the probes go on behind sub-node 2.
  A2B_CHECK(bench.Open(1, 4));
  A2B_CHECK(RunUntilProbe(bench));
  A2B_CHECK(Plays(bench.Audio(), 1, 0, -1, 0));
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
