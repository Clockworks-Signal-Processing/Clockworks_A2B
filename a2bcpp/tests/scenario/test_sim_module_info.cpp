/**
 * @file test_sim_module_info.cpp
 * @brief Simulator scenario 6 (EEPROM invalid, bad CRC or absent; app-supplied source) and
 *        the sub-node peripheral scripts from module info: the default EEPROM source, its
 *        scan of 0x50-0x54, "ignore EEPROM" with an app source, ADI's RJ45 module image,
 *        the sub-node's I2C rate (read at 100 kHz, then the image's I2CCFG.DATARATE), and the
 *        module-info register writes of the boot4 golden's bench modules.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "a2b/eeprom_source.h"
#include "a2b/network.h"
#include "detail/regdefs.h"
#include "sim/eeprom.h"
#include "sim/module_info.h"
#include "sim/simulator.h"
#include "sim_trace.h"
#include "test_harness.h"

namespace a2b {
namespace {

using test::Access;
using test::Decode;
using test::Peripherals;
using test::PeripheralTransfer;

constexpr uint8_t kMainAddr = 0x68;
constexpr uint8_t kBusAddr = 0x69;
constexpr uint8_t kCodecAddr = 0x34;  ///< The ADAU1701 of ADI's RJ45 module image.

/** Reads a file of the repository (module-info images, goldens). */
std::vector<uint8_t> ReadRepoFile(const char* path) {
  const std::string full = std::string(A2B_REPO_ROOT) + "/" + path;
  std::vector<uint8_t> bytes;
  if (FILE* file = std::fopen(full.c_str(), "rb")) {
    int c = 0;
    while ((c = std::fgetc(file)) != EOF) {
      bytes.push_back(static_cast<uint8_t>(c));
    }
    std::fclose(file);
  }
  if (bytes.empty()) {
    std::printf("cannot read %s\n", full.c_str());
    std::exit(1);
  }
  return bytes;
}

/** A module in CFG-4 with GPIO0-6 as inputs and PINCFG 0. */
std::vector<uint8_t> MakeImage(uint16_t productId = 0x0037) {
  sim::ModuleInfoHeader header;
  header.productId = productId;
  header.powerConfig = sim::kPowerConfigHighPower;
  sim::ModuleInfoRegs regs;
  regs.gpioien = 0x7F;
  regs.pincfg = 0x00;
  return sim::MakeModuleInfoImage(header, regs);
}

/** A peripheral that records what is written to it; it can NAK. */
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
    return nak ? Result::kI2cError : Result::kOk;
  }
  std::vector<std::vector<uint8_t>> writes;
  std::vector<uint32_t> times;
  bool nak = false;
};

/** An app source ("ignore EEPROM"): one image for every node, all of it readable. */
class AppSource : public ModuleInfoSource {
 public:
  explicit AppSource(std::vector<uint8_t> image, bool whole = true)
      : m_Image(std::move(image)), m_Whole(whole) {}

  /** Copies the start of the image. */
  size_t GetModuleInfo(const SubNodeIdentity& node, uint8_t* buf, size_t bufLen) override {
    (void)node;
    ++calls;
    const size_t len = (m_Image.size() < bufLen) ? m_Image.size() : bufLen;
    for (size_t i = 0; i < len; ++i) {
      buf[i] = m_Image[i];
    }
    return len;
  }

  /** Copies more of the image, or has none (@p whole false: the interface's default). */
  Result ReadModuleInfo(const SubNodeIdentity& node, uint32_t offset, uint8_t* buf,
                        size_t len) override {
    if (!m_Whole) {
      return ModuleInfoSource::ReadModuleInfo(node, offset, buf, len);
    }
    if (offset + len > m_Image.size()) {
      return Result::kInvalidArgument;
    }
    for (size_t i = 0; i < len; ++i) {
      buf[i] = m_Image[offset + i];
    }
    ++reads;
    return Result::kOk;
  }

  int calls = 0;
  int reads = 0;

 private:
  std::vector<uint8_t> m_Image;
  bool m_Whole;
};

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

/** An AD2437 that detects CFG-4, as the bench's nodes do. */
sim::SubNodeSpec Ad2437(uint8_t chipIdByte = 0) {
  sim::SubNodeSpec spec;
  spec.chip.productId = 0x37;
  spec.chip.powerConfig = sim::kPowerConfigHighPower;
  spec.chip.lowVoltageInput = true;
  spec.chip.chipId[5] = chipIdByte;
  return spec;
}

/** A bus with a Network on it (default module-info source), polled every millisecond. */
struct Bench {
  sim::Simulator sim;
  sim::Bus& bus;
  BoardConfig board;
  Network network;

  Bench()
      : bus(sim.AddBus(kMainAddr, kBusAddr, Ad2437().chip)),
        board(MakeBoard()),
        network(sim, board) {}

  /** Plugs in an AD2437 with no EEPROM. */
  sim::Node& Plug(uint8_t chipIdByte = 0) { return bus.Plug(Ad2437(chipIdByte)); }

  /** Plugs in an AD2437 with @p image in an EEPROM at @p addr. */
  sim::Node& PlugWithEeprom(const std::vector<uint8_t>& image, uint8_t addr = 0x50,
                            uint8_t chipIdByte = 0) {
    sim::Node& node = Plug(chipIdByte);
    node.AddEeprom(addr).Load(image);
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
    for (uint32_t i = 0; i < ms && network.State() != NetworkState::kRunning; ++i) {
      Run(1);
    }
    return network.State() == NetworkState::kRunning;
  }

  /** The transfers to sub-node @p node's peripheral at @p chip. */
  std::vector<PeripheralTransfer> To(NodeAddr node, uint8_t chip) const {
    std::vector<PeripheralTransfer> out;
    for (const PeripheralTransfer& t : Peripherals(sim.Trace())) {
      if (t.node == node && t.chip == chip) {
        out.push_back(t);
      }
    }
    return out;
  }

  /** True if sub-node 0's switch no longer powers the next node (the main node's ENSW). */
  bool SubNode0SwitchedOff() const {
    return (const_cast<sim::Bus&>(bus).Main().Reg(reg::kSwctl) & reg::kSwctlEnsw) == 0;
  }
};

// The default source: the node's EEPROM.

A2B_TEST(EepromIsTheDefaultSource) {
  Bench bench;
  bench.PlugWithEeprom(MakeImage());
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  const SubNodeInfo* node = bench.network.SubNodeInfoAt(0);
  A2B_CHECK(node != nullptr && node->moduleInfo.vendorId == 0x00AD);
  A2B_CHECK(node != nullptr && node->moduleInfo.powerConfig == 4);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kGpioien), 0x7F);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kPincfg), 0x00);
  // The marker at address 0 (one byte), then the start of the image in one read.
  const std::vector<PeripheralTransfer> reads = bench.To(0, 0x50);
  A2B_CHECK_EQ(reads.size(), 2);
  if (reads.size() == 2) {
    A2B_CHECK(reads[0].written == std::vector<uint8_t>({0x00, 0x00}));
    A2B_CHECK_EQ(reads[0].read.size(), 1);
    A2B_CHECK(reads[1].written == std::vector<uint8_t>({0x00, 0x00}));
    A2B_CHECK_EQ(reads[1].read.size(), kModuleInfoLen);
  }
}

A2B_TEST(EepromIsFoundAnywhereFrom0x50To0x54) {
  Bench bench;
  bench.PlugWithEeprom(MakeImage(0x0101), 0x53);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  const SubNodeInfo* node = bench.network.SubNodeInfoAt(0);
  A2B_CHECK(node != nullptr && node->moduleInfo.productId == 0x0101);
  // 0x50-0x52 NAK (no device), then 0x53 answers.
  for (uint8_t addr = 0x50; addr <= 0x52; ++addr) {
    const std::vector<PeripheralTransfer> tries = bench.To(0, addr);
    A2B_CHECK(tries.size() == 1 && tries[0].failed);
  }
  A2B_CHECK_EQ(bench.To(0, 0x53).size(), 2);
  A2B_CHECK(bench.To(0, 0x54).empty());
}

A2B_TEST(BlankEepromIsSkippedInTheScan) {
  Bench bench;
  sim::Node& node = bench.Plug();
  node.AddEeprom(0x50);  // Erased: every byte 0xFF.
  node.AddEeprom(0x51).Load(MakeImage());
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK_EQ(bench.To(0, 0x50).size(), 1);  // Its marker only.
  A2B_CHECK_EQ(bench.To(0, 0x51).size(), 2);
}

A2B_TEST(EepromSourceTellsWhereItFoundTheImage) {
  Bench bench;
  EepromModuleInfoSource source(bench.network);
  bench.network.SetModuleInfoSource(&source);
  bench.PlugWithEeprom(MakeImage(), 0x52, 1);
  bench.PlugWithEeprom(MakeImage(), 0x50, 2);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
  A2B_CHECK_EQ(source.EepromAddr(0), 0x52);
  A2B_CHECK_EQ(source.EepromAddr(1), 0x50);
  A2B_CHECK_EQ(source.EepromAddr(2), 0);
}

// Scenario 6: EEPROM absent, blank, or with a bad CRC or block.

A2B_TEST(Scenario6NoEeprom) {
  Bench bench;
  bench.Plug();
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 0);
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kNoModuleInfo);
  // Every address was tried once, and the node was switched off.
  for (uint8_t addr = 0x50; addr <= 0x54; ++addr) {
    const std::vector<PeripheralTransfer> tries = bench.To(0, addr);
    A2B_CHECK(tries.size() == 1 && tries[0].failed);
  }
  A2B_CHECK(bench.SubNode0SwitchedOff());
}

A2B_TEST(Scenario6BlankEeprom) {
  Bench bench;
  bench.Plug().AddEeprom(0x50);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 0);
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kNoModuleInfo);
  A2B_CHECK(bench.SubNode0SwitchedOff());
}

A2B_TEST(Scenario6BadHeaderCrc) {
  Bench bench;
  std::vector<uint8_t> image = MakeImage();
  image[0x1F] ^= 0x01;
  bench.PlugWithEeprom(image);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 0);
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kBadModuleInfo);
  A2B_CHECK(bench.SubNode0SwitchedOff());
  // Its settings were not written.
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kGpioien), 0x00);
}

A2B_TEST(Scenario6BadModuleInfoBlock) {
  Bench bench;
  std::vector<uint8_t> image = MakeImage();
  image[0x20] = 0x41;  // Block type 4, not 3 (outside the header CRC).
  bench.PlugWithEeprom(image);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kBadModuleInfo);
}

A2B_TEST(Scenario6BadEepromEndsTheChainThere) {
  Bench bench;
  bench.PlugWithEeprom(MakeImage(), 0x50, 1);
  std::vector<uint8_t> bad = MakeImage();
  bad[0x03] ^= 0xFF;  // The product ID, without resealing the CRC.
  bench.PlugWithEeprom(bad, 0x50, 2);
  bench.PlugWithEeprom(MakeImage(), 0x50, 3);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kBadModuleInfo);
  // Sub-node 0 no longer powers sub-node 1, which dropped out of the bus.
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kSwctl) & reg::kSwctlEnsw, 0);
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 1);
}

A2B_TEST(Scenario6AppSourceIgnoresTheEeprom) {
  Bench bench;
  std::vector<uint8_t> bad = MakeImage();
  bad[0x1F] ^= 0x01;
  bench.PlugWithEeprom(bad);  // Would be rejected if it were read.
  AppSource source(MakeImage(0x0042));
  bench.network.SetModuleInfoSource(&source);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK_EQ(source.calls, 1);
  const SubNodeInfo* node = bench.network.SubNodeInfoAt(0);
  A2B_CHECK(node != nullptr && node->moduleInfo.productId == 0x0042);
  A2B_CHECK(Peripherals(bench.sim.Trace()).empty());  // Not one peripheral transfer.
}

A2B_TEST(Scenario6AppSourceWithNothingRejects) {
  Bench bench;
  bench.PlugWithEeprom(MakeImage());
  AppSource source{std::vector<uint8_t>()};
  bench.network.SetModuleInfoSource(&source);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 0);
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kNoModuleInfo);
}

A2B_TEST(Scenario6BackToTheEeprom) {
  Bench bench;
  bench.PlugWithEeprom(MakeImage(0x0077));
  AppSource source(MakeImage(0x0042));
  bench.network.SetModuleInfoSource(&source);
  bench.network.SetModuleInfoSource(nullptr);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(source.calls, 0);
  const SubNodeInfo* node = bench.network.SubNodeInfoAt(0);
  A2B_CHECK(node != nullptr && node->moduleInfo.productId == 0x0077);
}

// Peripheral scripts from module info.

/** An image with one I2C block for the codec (a write, a wait, two writes) and one SPI block. */
std::vector<uint8_t> ScriptImage() {
  std::vector<uint8_t> image = MakeImage();
  sim::PeripheralBlock codec;
  codec.name = "Codec";
  codec.i2cAddr = kCodecAddr;
  codec.units.push_back(sim::ConfigUnit::Write(kCodecAddr, {0x40, 0x00, 0x01}));
  codec.units.push_back(sim::ConfigUnit::Wait(50));
  codec.units.push_back(sim::ConfigUnit::Write(kCodecAddr, {0x40, 0x02, 0xAA, 0xBB}, false));
  codec.units.push_back(sim::ConfigUnit::Write(kCodecAddr, {0x40, 0x04, 0xCC}));
  sim::PeripheralBlock spi;
  spi.interface = 1;
  spi.name = "SPI device";
  spi.units.push_back(sim::ConfigUnit::Write(0x35, {0x01}));
  sim::AddPeripheralBlocks(image, {codec, spi});
  return image;
}

A2B_TEST(PeripheralScriptsRunFromTheEeprom) {
  Bench bench;
  Recorder codec;
  bench.PlugWithEeprom(ScriptImage()).AttachDevice(kCodecAddr, codec);
  A2B_CHECK(bench.network.Start() == Result::kOk);
  bench.Run(2000);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK_EQ(codec.writes.size(), 3);
  if (codec.writes.size() == 3) {
    A2B_CHECK(codec.writes[0] == std::vector<uint8_t>({0x40, 0x00, 0x01}));
    A2B_CHECK(codec.writes[1] == std::vector<uint8_t>({0x40, 0x02, 0xAA, 0xBB}));
    A2B_CHECK(codec.writes[2] == std::vector<uint8_t>({0x40, 0x04, 0xCC}));
    A2B_CHECK(codec.times[1] - codec.times[0] >= 50);  // The type C unit's wait.
  }
  // The SPI block's unit was skipped, with a log line.
  bool logged = false;
  for (const std::string& line : bench.sim.LogLines()) {
    logged = logged || line.find("SPI peripheral scripts skipped") != std::string::npos;
  }
  A2B_CHECK(logged);
}

A2B_TEST(NodeCountsOnlyAfterItsScripts) {
  Bench bench;
  Recorder codec;
  bench.PlugWithEeprom(ScriptImage()).AttachDevice(kCodecAddr, codec);
  A2B_CHECK(bench.network.Start() == Result::kOk);
  for (int i = 0; i < 5000 && codec.writes.empty(); ++i) {
    bench.Run(1);
  }
  A2B_CHECK_EQ(codec.writes.size(), 1);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 0);  // In its 50 ms wait.
  bench.Run(60);
  A2B_CHECK_EQ(codec.writes.size(), 3);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
}

A2B_TEST(PeripheralScriptWithBadCrcRejects) {
  Bench bench;
  Recorder codec;
  std::vector<uint8_t> image = ScriptImage();
  // The CRC of the codec block's last unit, found by its bytes.
  for (size_t i = 0x800; i + 3 < image.size(); ++i) {
    if (image[i] == 0x40 && image[i + 1] == 0x04 && image[i + 2] == 0xCC) {
      image[i + 3] ^= 0x01;
      break;
    }
  }
  bench.PlugWithEeprom(image).AttachDevice(kCodecAddr, codec);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 0);
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kPeripheralFailed);
  A2B_CHECK_EQ(codec.writes.size(), 2);  // The units before it ran.
  A2B_CHECK(bench.SubNode0SwitchedOff());
}

A2B_TEST(PeripheralScriptWithBadBlockCrcRejects) {
  Bench bench;
  Recorder codec;
  std::vector<uint8_t> image = ScriptImage();
  // The info block is 12 bytes (header, two addresses, CRC); the codec block follows it.
  image[0x80C + 3] ^= 0x01;  // Its interface byte: now SPI, and its CRC is wrong.
  bench.PlugWithEeprom(image).AttachDevice(kCodecAddr, codec);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kPeripheralFailed);
  A2B_CHECK(codec.writes.empty());
}

A2B_TEST(PeripheralThatDoesNotAnswerRejects) {
  Bench bench;
  Recorder codec;
  codec.nak = true;
  bench.PlugWithEeprom(ScriptImage()).AttachDevice(kCodecAddr, codec);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK(bench.network.LastRejection() == RejectReason::kPeripheralFailed);
}

A2B_TEST(AdiRj45ImageProgramsItsCodec) {
  const std::vector<uint8_t> image =
      ReadRepoFile("adi/module_info/adi_a2b_node_ModuleInfo_1.3.0.0_RJ45.dat");
  Bench bench;
  Recorder codec;
  bench.PlugWithEeprom(image).AttachDevice(kCodecAddr, codec);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  const SubNodeInfo* node = bench.network.SubNodeInfoAt(0);
  A2B_CHECK(node != nullptr && node->moduleInfo.peripheralBlocks == 3);
  // Eight units on the ADAU1701: a 16 ms wait, then seven type B writes (the payload less
  // its CRC), each in one transfer; the other two blocks have none.
  const size_t lengths[] = {4, 3792, 1332, 4090, 10, 26, 4};
  A2B_CHECK_EQ(codec.writes.size(), 7);
  for (size_t i = 0; i < codec.writes.size() && i < 7; ++i) {
    A2B_CHECK_EQ(codec.writes[i].size(), lengths[i]);
  }
  if (codec.writes.size() == 7) {
    A2B_CHECK(codec.writes[0] == std::vector<uint8_t>({0x08, 0x1C, 0x00, 0x18}));
    A2B_CHECK(codec.writes[6] == std::vector<uint8_t>({0x08, 0x1C, 0x00, 0x1C}));
  }
}

A2B_TEST(AppSourceGivesThePeripheralScriptsToo) {
  const std::vector<uint8_t> image =
      ReadRepoFile("adi/module_info/adi_a2b_node_ModuleInfo_1.3.0.0_RJ45.dat");
  Bench bench;
  Recorder codec;
  bench.Plug().AttachDevice(kCodecAddr, codec);  // No EEPROM: a blank module.
  AppSource source(image);
  bench.network.SetModuleInfoSource(&source);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK_EQ(codec.writes.size(), 7);
  A2B_CHECK(source.reads > 0);
  // The only peripheral transfers are the codec's.
  for (const PeripheralTransfer& t : Peripherals(bench.sim.Trace())) {
    A2B_CHECK_EQ(t.chip, kCodecAddr);
  }
}

A2B_TEST(AppSourceWithOnlyTheStartSkipsTheScripts) {
  const std::vector<uint8_t> image =
      ReadRepoFile("adi/module_info/adi_a2b_node_ModuleInfo_1.3.0.0_RJ45.dat");
  Bench bench;
  Recorder codec;
  bench.Plug().AttachDevice(kCodecAddr, codec);
  AppSource source(image, false);
  bench.network.SetModuleInfoSource(&source);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK(codec.writes.empty());
  // Its register settings were written all the same (I2SGCFG 0xE0 in ADI's image).
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kI2sgcfg), 0xE0);
}

// The sub-node's I2C rate: the image is read at 100 kHz, then its I2CCFG.DATARATE is applied.

/** A device that notes the sub-node's I2CCFG at each transfer, then passes it on. */
class RateProbe : public sim::I2cDevice {
 public:
  RateProbe(const sim::Node& node, sim::I2cDevice& device) : m_Node(node), m_Device(device) {}

  /** Notes I2CCFG, then writes. */
  Result Write(const uint8_t* data, size_t len, uint32_t nowMs) override {
    writes.push_back(m_Node.Reg(reg::kI2ccfg));
    return m_Device.Write(data, len, nowMs);
  }
  /** Notes I2CCFG, then reads. */
  Result Read(uint8_t* data, size_t len, uint32_t nowMs) override {
    reads.push_back(m_Node.Reg(reg::kI2ccfg));
    return m_Device.Read(data, len, nowMs);
  }

  std::vector<uint8_t> writes;  ///< I2CCFG at each write.
  std::vector<uint8_t> reads;   ///< I2CCFG at each read.

 private:
  const sim::Node& m_Node;
  sim::I2cDevice& m_Device;
};

/** What I2CCFG was at each EEPROM read and codec write, for an image's I2CCFG. */
struct RateRun {
  std::vector<uint8_t> eepromReads;
  std::vector<uint8_t> codecWrites;
  uint8_t final = 0xFF;  ///< I2CCFG once the node is found.
  bool found = false;
};

/** Finds a node whose image has I2CCFG @p i2ccfg and a codec script, on a bus at @p frameRateHz. */
RateRun RunWithI2ccfg(uint8_t i2ccfg, uint32_t frameRateHz = 48000) {
  std::vector<uint8_t> image = ScriptImage();
  image[0x7F] = i2ccfg;  // Outside the CRCs the stack checks.
  Bench bench;
  bench.board.routing.frameRateHz = frameRateHz;
  sim::Node& node = bench.Plug();
  sim::Eeprom eeprom;
  eeprom.Load(image);
  RateProbe eepromProbe(node, eeprom);
  node.AttachDevice(0x50, eepromProbe);
  Recorder codec;
  RateProbe codecProbe(node, codec);
  node.AttachDevice(kCodecAddr, codecProbe);
  RateRun run;
  run.found = bench.StartAndRun() && bench.network.SubNodeCount() == 1;
  run.eepromReads = eepromProbe.reads;
  run.codecWrites = codecProbe.writes;
  run.final = bench.bus.Sub(0).Reg(reg::kI2ccfg);
  return run;
}

A2B_TEST(ImageDatarateSpeedsUpTheScripts) {
  const RateRun run = RunWithI2ccfg(reg::kI2ccfgDatarate);
  A2B_CHECK(run.found);
  // The marker and the start: at the reset rate, 100 kHz.
  A2B_CHECK(run.eepromReads.size() >= 2);
  if (run.eepromReads.size() >= 2) {
    A2B_CHECK_EQ(run.eepromReads[0], 0);
    A2B_CHECK_EQ(run.eepromReads[1], 0);
  }
  // Then the scripts' reads and the codec's writes: 400 kHz.
  A2B_CHECK(run.eepromReads.back() == reg::kI2ccfgDatarate);
  A2B_CHECK_EQ(run.codecWrites.size(), 3);
  for (uint8_t i2ccfg : run.codecWrites) {
    A2B_CHECK_EQ(i2ccfg, reg::kI2ccfgDatarate);
  }
  A2B_CHECK_EQ(run.final, reg::kI2ccfgDatarate);
}

A2B_TEST(ImageWithoutDatarateStaysAt100kHz) {
  const RateRun run = RunWithI2ccfg(0x00);
  A2B_CHECK(run.found);
  A2B_CHECK_EQ(run.codecWrites.size(), 3);
  for (uint8_t i2ccfg : run.eepromReads) {
    A2B_CHECK_EQ(i2ccfg, 0);
  }
  for (uint8_t i2ccfg : run.codecWrites) {
    A2B_CHECK_EQ(i2ccfg, 0);
  }
  A2B_CHECK_EQ(run.final, 0);
}

A2B_TEST(ImageI2ccfgGivesOnlyDatarate) {
  // DISI2C, FMPLUS and EACK are not applied: DISI2C would cut the node off from its EEPROM.
  RateRun run = RunWithI2ccfg(0x8B);
  A2B_CHECK(run.found);
  A2B_CHECK_EQ(run.final, reg::kI2ccfgDatarate);
  // On a 44.1 kHz bus, FRAMERATE comes with it; the image's FRAMERATE alone gives nothing.
  run = RunWithI2ccfg(reg::kI2ccfgDatarate, 44100);
  A2B_CHECK(run.found);
  A2B_CHECK_EQ(run.final, reg::kI2ccfgDatarate | reg::kI2ccfgFramerate);
  run = RunWithI2ccfg(reg::kI2ccfgFramerate);
  A2B_CHECK(run.found);
  A2B_CHECK_EQ(run.final, 0);
}

// The B port, the target chip and the cable length (header 0x07, 0x28 and 0x6C).

/** An image with a B port type, a B-side cable length and a target chip. */
std::vector<uint8_t> PortImage(uint8_t bPortType, uint16_t cableM = 0,
                               uint16_t targetChip = kChipNotStated, uint8_t powerConfig = 4) {
  sim::ModuleInfoHeader header;
  header.powerConfig = powerConfig;
  header.bPortType = bPortType;
  header.bCableM = cableM;
  header.targetChip = targetChip;
  sim::ModuleInfoRegs regs;
  regs.gpioien = 0x7F;
  regs.pincfg = 0x00;
  return sim::MakeModuleInfoImage(header, regs);
}

/** Plugs in a node of chip @p product with @p image in its EEPROM; its A side inverted or not. */
sim::Node& PlugChip(Bench& bench, uint8_t product, const std::vector<uint8_t>& image, bool inverted,
                    uint8_t chipIdByte) {
  sim::SubNodeSpec spec = Ad2437(chipIdByte);
  if (product != 0x37) {
    spec.chip.productId = product;
    spec.chip.powerConfig = 0;
    spec.chip.lowVoltageInput = false;
  }
  spec.link.bSideInverted = inverted;
  sim::Node& node = bench.bus.Plug(spec);
  node.AddEeprom(0x50).Load(image);
  return node;
}

/** True if the stack logged a line with @p text. */
bool Logged(const Bench& bench, const char* text) {
  for (const std::string& line : bench.sim.LogLines()) {
    if (line.find(text) != std::string::npos) {
      return true;
    }
  }
  return false;
}

A2B_TEST(ImageBPortGivesTheNextLink) {
  {
    // UTP: single-step, straight.
    Bench bench;
    PlugChip(bench, 0x37, PortImage(2), false, 1);
    PlugChip(bench, 0x37, PortImage(2), false, 2);
    A2B_CHECK(bench.StartAndRun());
    bench.Run(600);
    A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
    A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kControl) & reg::kControlXcvrbinv, 0);
  }
  {
    // ADI's RJ45 on an AD2437: two-step, inverted (test_sim_two_step has the sequence).
    Bench bench;
    PlugChip(bench, 0x37, PortImage(1), false, 1);
    PlugChip(bench, 0x37, PortImage(2), true, 2).Link().power = sim::LinkPower::kTwoStep;
    A2B_CHECK(bench.StartAndRun());
    bench.Run(1500);
    A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
    A2B_CHECK(bench.bus.Sub(1).HighVoltage());
    A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kControl) & reg::kControlXcvrbinv,
                 reg::kControlXcvrbinv);
  }
  {
    // The app's link wins over the image's: inverted, single-step.
    Bench bench;
    PlugChip(bench, 0x37, PortImage(1), false, 1);
    PlugChip(bench, 0x37, PortImage(2), true, 2);
    LinkProfile inverted = LinkProfile::Utp();
    inverted.bSideInverted = true;
    A2B_CHECK(bench.network.SetLinkProfile(1, inverted) == Result::kOk);
    A2B_CHECK(bench.StartAndRun());
    bench.Run(600);
    A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
    A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kControl) & reg::kControlXcvrbinv,
                 reg::kControlXcvrbinv);
  }
  {
    // RJ45 and XLR on an AD2428: UTP (two-step needs the AD2437's 24 V switch).
    for (uint8_t type : {0, 1}) {
      Bench bench;
      PlugChip(bench, 0x28, PortImage(type, 0, kChipNotStated, 0), false, 1);
      PlugChip(bench, 0x28, PortImage(2, 0, kChipNotStated, 0), false, 2);
      A2B_CHECK(bench.StartAndRun());
      bench.Run(600);
      A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
      A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kControl) & reg::kControlXcvrbinv, 0);
    }
  }
}

A2B_TEST(CustomBPortsAreNotProbedPast) {
  // RJ45-CUSTOM, RJ45-CUSTOM-INV, CUSTOM and a reserved type: no handler yet.
  for (uint8_t type : {3, 4, 5, 9}) {
    Bench bench;
    PlugChip(bench, 0x37, PortImage(type), false, 1);
    PlugChip(bench, 0x37, PortImage(2), false, 2);
    A2B_CHECK(bench.StartAndRun());
    bench.Run(1500);
    A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
    A2B_CHECK(Logged(bench, "link to sub-node 1: Unsupported B port type; not probed"));
  }
}

A2B_TEST(ImageForAnotherChipIsRejected) {
  {
    // An AB0204's image (its AD2427 soldered down) on an AD2437: rejected.
    Bench bench;
    PlugChip(bench, 0x37, PortImage(2, 0, 0x2427), false, 1);
    A2B_CHECK(bench.network.Start() == Result::kOk);
    bench.Run(1500);
    A2B_CHECK_EQ(bench.network.SubNodeCount(), 0);
    A2B_CHECK(bench.network.LastRejection() == RejectReason::kWrongChip);
  }
  for (uint16_t chip : {kChipExternal, static_cast<uint16_t>(0x2437), kChipNotStated}) {
    Bench bench;
    PlugChip(bench, 0x37, PortImage(2, 0, chip), false, 1);
    A2B_CHECK(bench.StartAndRun());
    A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  }
}

A2B_TEST(EachChipKeepsItsOwnPowerConfig) {
  // A board with a plug-in module (External) whose image says CFG-4, as the AB0020's does.
  // Each chip has one power configuration: the AD2428's (none) and the AD2433's is CFG-0,
  // the AD2437's CFG-4. The probes behind the node check what it detected against it, so an
  // AD2433 must not be overridden to CFG-4.
  const std::vector<uint8_t> image = PortImage(2, 0, kChipExternal, 4);
  const uint8_t products[] = {0x28, 0x33, 0x37};
  const uint8_t expected[] = {0, 0, 4};
  for (size_t i = 0; i < 3; ++i) {
    Bench bench;
    sim::Node& node = PlugChip(bench, products[i], image, false, 1);
    A2B_CHECK(bench.StartAndRun());
    bench.Run(1500);  // Probes behind it, each checking its power configuration.
    const SubNodeInfo* info = bench.network.SubNodeInfoAt(0);
    A2B_CHECK(info != nullptr);
    if (info != nullptr) {
      A2B_CHECK_EQ(info->moduleInfo.powerConfig, expected[i]);
    }
    A2B_CHECK_EQ(node.PowerConfig(), expected[i]);
    A2B_CHECK_EQ(node.Reg(reg::kSwctl) & reg::kSwctlCfgDetOv, 0);
    A2B_CHECK(!Logged(bench, "image's CFG isn't its chip's"));  // External: no log line.
  }
  // An image that names no plug-in module and disagrees with its chip is logged.
  Bench bench;
  PlugChip(bench, 0x28, PortImage(2, 0, kChipNotStated, 4), false, 1);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK(Logged(bench, "a2b: sub-node 0: image's CFG isn't its chip's; chip's used"));
  A2B_CHECK_EQ(bench.network.SubNodeInfoAt(0)->moduleInfo.powerConfig, 0);
}

A2B_TEST(Ad243xOnlyBitsAreClearedOnAnAd242x) {
  // An External board's image with an HPF corner and a GPIO mode for an AD243x module.
  sim::ModuleInfoHeader header;
  header.powerConfig = 0;
  header.bPortType = 2;
  header.targetChip = kChipExternal;
  sim::ModuleInfoRegs regs;
  regs.pdmctl2 = 0x40;
  regs.pincfg = 0x81;
  const std::vector<uint8_t> image = sim::MakeModuleInfoImage(header, regs);
  for (uint8_t product : {0x28, 0x37}) {
    Bench bench;
    PlugChip(bench, product, image, false, 1);
    A2B_CHECK(bench.StartAndRun());
    A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
    const bool ad242x = (product == 0x28);
    A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kPdmctl2), ad242x ? 0x00 : 0x40);
    A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kPincfg), ad242x ? 0x01 : 0x81);
  }
}

A2B_TEST(ImageCableLengthGoesIntoTheResponseCycles) {
  // Sub-node 0's image gives the cable on its B side: the link to sub-node 1.
  uint8_t discvry[2] = {};
  const uint16_t metres[2] = {0, 20};
  for (size_t i = 0; i < 2; ++i) {
    Bench bench;
    PlugChip(bench, 0x37, PortImage(2, metres[i]), false, 1);
    PlugChip(bench, 0x37, PortImage(2), false, 2);
    A2B_CHECK(bench.StartAndRun());
    A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
    A2B_CHECK_EQ(bench.network.CableLength(0), 40);  // The board's, into sub-node 0.
    A2B_CHECK_EQ(bench.network.CableLength(1), i == 0 ? 40 : 200);
    bench.Run(600);  // A probe behind sub-node 1, past both cables.
    discvry[i] = bench.bus.Main().Reg(reg::kDiscvry);
    if (i == 1) {
      // The app's length wins; cleared, the image's again.
      A2B_CHECK(bench.network.SetCableLength(1, 50) == Result::kOk);
      A2B_CHECK_EQ(bench.network.CableLength(1), 50);
      A2B_CHECK(bench.network.ClearCableLength(1) == Result::kOk);
      A2B_CHECK_EQ(bench.network.CableLength(1), 200);
    }
  }
  A2B_CHECK(discvry[0] != discvry[1]);
  // Longer than the data sheets' 30 m: taken as it is (110 ft has run on an AD2428).
  Bench bench;
  PlugChip(bench, 0x37, PortImage(2, 34), false, 1);
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.network.CableLength(1), 340);
}

// The bench's modules, from the boot4 golden (ADI's stack, EEPROMs read).

/**
 * The bench modules' image as far as the boot4 golden shows it: the header, the start of
 * the module-info block, the register settings it wrote, and one peripheral block with
 * no units (the golden reads the peripheral info block and the block, nothing more).
 */
std::vector<uint8_t> BenchImage() {
  sim::ModuleInfoHeader header;
  header.powerConfig = sim::kPowerConfigHighPower;
  sim::ModuleInfoRegs regs;
  regs.spicfg = 0x02;
  regs.i2scfg = 0x82;
  regs.i2sgcfg = 0xE2;
  regs.clk2cfg = 0x81;
  regs.gpiooen = 0x06;
  regs.gpioien = 0x80;
  regs.gpiodat = 0xE9;
  std::vector<uint8_t> image = sim::MakeModuleInfoImage(header, regs);
  const uint8_t version[] = {0x01, 0x03, 0x01, 0x00};  // 0x09: software version.
  for (size_t i = 0; i < sizeof(version); ++i) {
    image[0x09 + i] = version[i];
  }
  image[0x20] = 0x31;  // Module-info block: type 3, size 0x105.
  image[0x21] = 0x05;
  sim::PeripheralBlock block;
  block.name = "Peripheral";
  sim::AddPeripheralBlocks(image, {block});
  return image;
}

A2B_TEST(Boot4ModuleInfoWritesMatchTheGolden) {
  // ADI's stack's writes to sub-node 0 from its CHIPID read to its INTMSK0 write:
  // LDNSLOTS (routing) and then the module info's settings.
  const std::vector<uint8_t> golden = ReadRepoFile("a2bcpp/tests/golden/boot4_pi_run1.txt");
  const std::vector<Access> goldenAccesses = Decode(std::string(golden.begin(), golden.end()));
  const auto window = [](const std::vector<Access>& accesses) {
    std::vector<std::pair<uint8_t, uint8_t>> writes;
    bool in = false;
    for (const Access& a : accesses) {
      if (a.node != 0) {
        continue;
      }
      if (!a.write && a.reg == reg::kChipid0) {
        in = true;
      } else if (in && a.write && a.reg == reg::kIntmsk0) {
        break;
      } else if (in && a.write && a.reg != reg::kChip && a.reg != 0x0B) {  // LDNSLOTS: routing's.
        writes.emplace_back(a.reg, a.value);
      }
    }
    return writes;
  };
  const std::vector<std::pair<uint8_t, uint8_t>> expected = window(goldenAccesses);
  A2B_CHECK_EQ(expected.size(), 7);

  Bench bench;
  bench.PlugWithEeprom(BenchImage());
  bench.PlugWithEeprom(BenchImage());
  A2B_CHECK(bench.StartAndRun());
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
  const std::vector<std::pair<uint8_t, uint8_t>> actual = window(Decode(bench.sim.Trace()));
  A2B_CHECK(actual == expected);
  for (size_t i = 0; i < actual.size() && i < expected.size(); ++i) {
    if (actual[i] != expected[i]) {
      std::printf("  write %zu: 0x%02X=0x%02X, golden 0x%02X=0x%02X\n", i, actual[i].first,
                  actual[i].second, expected[i].first, expected[i].second);
    }
  }
  // The same EEPROM areas as ADI's stack: the start, the channel-group table (to the
  // end of the module-info block; ADI's stack read the block from 0x23 in one go), the
  // peripheral info block and the peripheral block, at 0x50.
  std::vector<uint32_t> offsets;
  for (const PeripheralTransfer& t : bench.To(0, 0x50)) {
    offsets.push_back((static_cast<uint32_t>(t.written[0]) << 8) | t.written[1]);
  }
  A2B_CHECK(offsets ==
            std::vector<uint32_t>({0x0000, 0x0000, 0x0100, 0x0800, 0x0803, 0x0808, 0x080B}));
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
