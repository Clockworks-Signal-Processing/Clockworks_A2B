/**
 * @file test_eeprom_provisioning.cpp
 * @brief Scenario 20, provisioning (a2bcpp/docs/testing.md): blank EEPROMs, written from
 *        the bench console with the image tool's images (tools/a2b_eeprom/examples), read
 *        back in chunks and verified, then the nodes found from them; a write stopped half
 *        way leaves a blank node, never a half image; a write-protected EEPROM, a busy one
 *        (the write cycle's NAKs), an image from a file without an EEPROM (`module N file`),
 *        and the refusals.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "a2b/crc8.h"
#include "bench_console.h"
#include "eeprom_image.h"
#include "eeprom_job.h"
#include "sigma_xml.h"
#include "sim/eeprom.h"
#include "sim/simulator.h"
#include "test_harness.h"

namespace a2b {
namespace {

const char kAb0020[] = A2B_REPO_ROOT "/tools/a2b_eeprom/examples/AB0020.dat";
const char kAb0312[] = A2B_REPO_ROOT "/tools/a2b_eeprom/examples/AB0312.dat";
const char kAb0204[] = A2B_REPO_ROOT "/tools/a2b_eeprom/examples/AB0204.dat";
const char kCodecXml[] =
    A2B_REPO_ROOT "/tools/SS+reference/2x_AB0020_EVM_with_2437/AB0020-sub-node-ADAU1761.xml";

/** True if @p text contains @p part. */
bool Has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

/** Runs a command and gives its output. */
std::string Run(BenchConsole& console, const char* line) {
  console.Execute(line);
  return console.TakeOutput();
}

/** Polls for @p ms ms of simulated time; gives what was printed. */
std::string RunFor(sim::Simulator& sim, BenchConsole& console, int ms) {
  for (int i = 0; i < ms; ++i) {
    sim.Advance(1);
    console.Poll();
  }
  return console.TakeOutput();
}

/** Runs a command and polls until its EEPROM job is over (at most 60 s); gives the output. */
std::string RunJob(sim::Simulator& sim, BenchConsole& console, const char* line) {
  std::string text = Run(console, line);
  for (int ms = 0; console.Busy() && ms < 60000; ++ms) {
    sim.Advance(1);
    console.Poll();
  }
  A2B_CHECK(!console.Busy());
  return text + console.TakeOutput();
}

/** Reads a file. */
std::vector<uint8_t> ReadFile(const std::string& path) {
  std::vector<uint8_t> data;
  std::FILE* file = std::fopen(path.c_str(), "rb");
  if (file == nullptr) {
    return data;
  }
  uint8_t buf[4096];
  size_t got = 0;
  while ((got = std::fread(buf, 1, sizeof(buf), file)) > 0) {
    data.insert(data.end(), buf, buf + got);
  }
  std::fclose(file);
  return data;
}

/** An AD2437 as on the bench: CFG-4. */
sim::ChipSpec Ad2437(uint8_t id) {
  sim::ChipSpec chip;
  chip.powerConfig = 4;
  chip.lowVoltageInput = true;
  chip.chipId[5] = id;
  return chip;
}

/** A codec behind a sub-node that keeps every write. */
class CodecRecorder : public sim::I2cDevice {
 public:
  Result Write(const uint8_t* data, size_t len, uint32_t nowMs) override {
    (void)nowMs;
    writes.emplace_back(data, data + len);
    return Result::kOk;
  }
  Result Read(uint8_t* data, size_t len, uint32_t nowMs) override {
    (void)nowMs;
    for (size_t i = 0; i < len; ++i) {
      data[i] = 0;
    }
    return Result::kOk;
  }
  std::vector<std::vector<uint8_t>> writes;  ///< Each write, sub-address first.
};

/** A codec on the host bus that takes every transfer (the AB0020 board's own). */
class AnyCodec : public sim::I2cDevice {
 public:
  Result Write(const uint8_t* data, size_t len, uint32_t nowMs) override {
    (void)data;
    (void)len;
    (void)nowMs;
    return Result::kOk;
  }
  Result Read(uint8_t* data, size_t len, uint32_t nowMs) override {
    (void)nowMs;
    for (size_t i = 0; i < len; ++i) {
      data[i] = 0;
    }
    return Result::kOk;
  }
};

/** Two AB0020 modules with blank 24FC256 EEPROMs on an AB0020 main node; the first with
    an AD2437 module unless @p firstProduct says otherwise (0x28: an AD2428, CFG-0). */
struct Bench {
  explicit Bench(uint8_t firstProduct = 0x37)
      : bus(sim.AddBus(0x68, 0x69, Ad2437(0))), console(sim) {
    bus.StartRunning();
    sim.AttachHostDevice(0x39, hostCodec);
    for (uint8_t id = 1; id <= 2; ++id) {
      sim::SubNodeSpec spec;
      spec.chip = Ad2437(id);
      if (id == 1 && firstProduct != 0x37) {
        spec.chip.productId = firstProduct;
        spec.chip.powerConfig = 0;
        spec.chip.lowVoltageInput = false;
      }
      sim::Node& node = bus.Plug(spec);
      eeproms.push_back(&node.AddEeprom(0x50));
    }
    bus.Sub(0).AttachDevice(0x39, codec);
    Run(console, "board AB0020");
  }

  sim::Simulator sim;
  sim::Bus& bus;
  AnyCodec hostCodec;
  CodecRecorder codec;
  std::vector<sim::Eeprom*> eeproms;
  BenchConsole console;
};

/**
 * The ADAU1761 program's writes as the AB0020's image holds them: the XML's, with each write
 * over 1022 bytes split as AB0020.json splits it (1020 data bytes, 5 per program-memory
 * address, a 2-byte sub-address).
 */
std::vector<std::vector<uint8_t>> SplitProgram() {
  constexpr size_t kMax = 1022;
  constexpr size_t kChunk = 1020;
  constexpr size_t kPerAddress = 5;
  SigmaProgram program;
  std::string error;
  A2B_CHECK(ReadSigmaXml(kCodecXml, 0x39, program, error));
  std::vector<std::vector<uint8_t>> writes;
  for (const SigmaAction& action : program.actions) {
    if (action.wait) {
      continue;
    }
    if (action.bytes.size() <= kMax) {
      writes.push_back(action.bytes);
      continue;
    }
    const unsigned start = (static_cast<unsigned>(action.bytes[0]) << 8) | action.bytes[1];
    for (size_t at = 2; at < action.bytes.size(); at += kChunk) {
      const size_t len = std::min(kChunk, action.bytes.size() - at);
      const unsigned address = start + static_cast<unsigned>((at - 2) / kPerAddress);
      std::vector<uint8_t> write = {static_cast<uint8_t>(address >> 8),
                                    static_cast<uint8_t>(address)};
      write.insert(write.end(), action.bytes.begin() + static_cast<std::ptrdiff_t>(at),
                   action.bytes.begin() + static_cast<std::ptrdiff_t>(at + len));
      writes.push_back(write);
    }
  }
  return writes;
}

A2B_TEST(BlankEepromsWrittenThenFoundFromThem) {
  Bench bench;
  const std::vector<uint8_t> ab0020 = ReadFile(kAb0020);
  const std::vector<uint8_t> ab0312 = ReadFile(kAb0312);
  A2B_CHECK(ab0020.size() > 0x2000 && ab0312.size() > 0x800);
  // Blank: no module info, so both nodes need the console's to join.
  Run(bench.console, "ignoreEEPROM 1");
  Run(bench.console, "start");
  RunFor(bench.sim, bench.console, 3000);
  A2B_CHECK_EQ(bench.console.Bus().SubNodeCount(), 2);
  const std::string blank = RunJob(bench.sim, bench.console, "eeprom info 0");
  A2B_CHECK(Has(blank, "no image there (blank: byte 0 is 0xFF"));

  const std::string wrote =
      RunJob(bench.sim, bench.console, (std::string("eeprom write 0 ") + kAb0020).c_str());
  A2B_CHECK(Has(wrote, "Writing "));
  A2B_CHECK(Has(wrote, "Wrote 8376 bytes to sub-node 0's EEPROM (0x50): 131 page(s) written"));
  A2B_CHECK(Has(wrote, "start the bus again with ignoreEEPROM 0"));
  const std::vector<uint8_t>& contents = bench.eeproms[0]->Contents();
  A2B_CHECK(std::vector<uint8_t>(contents.begin(), contents.begin() + ab0020.size()) == ab0020);
  A2B_CHECK_EQ(bench.eeproms[0]->WriteCount(), 132);  // 131 pages, then the marker.
  RunJob(bench.sim, bench.console, (std::string("eeprom write 1 ") + kAb0312).c_str());
  A2B_CHECK_EQ(bench.eeproms[1]->Contents()[0], 0xAA);

  // Verified, read back in 256-byte chunks to the image's end, described.
  A2B_CHECK(
      Has(RunJob(bench.sim, bench.console, (std::string("eeprom verify 0 ") + kAb0020).c_str()),
          "holds the image (8376 bytes)"));
  const std::string out = A2B_TEST_OUT_DIR "/node0_readback.dat";
  const std::string read = RunJob(bench.sim, bench.console, ("eeprom read 0 " + out).c_str());
  A2B_CHECK(Has(read, "Read 8376 bytes from sub-node 0's EEPROM (0x50)"));
  A2B_CHECK(ReadFile(out) == ab0020);
  const std::string info = RunJob(bench.sim, bench.console, "eeprom info 0");
  A2B_CHECK(Has(info, "Image: 8376 bytes; module 0C54:0020"));
  A2B_CHECK(Has(info, "Peripheral 1 'ADAU1761': I2C at 0x39, 40 unit(s)"));
  A2B_CHECK(Has(info, "Vendor 'CLKWORKS'"));
  A2B_CHECK(Has(info, "ID 'AB0020'"));
  A2B_CHECK(Has(info, "UID 0x3E7A91C05D2B8F14 'CODEC' 48000 2 'ADAU1761'"));

  // Written again: nothing to write.
  A2B_CHECK(
      Has(RunJob(bench.sim, bench.console, (std::string("eeprom write 0 ") + kAb0020).c_str()),
          "the EEPROM already held this image: nothing written"));

  // Found from their EEPROMs: the AB0020's codec programmed from its image.
  Run(bench.console, "ignoreEEPROM 0");
  bench.codec.writes.clear();
  Run(bench.console, "start");
  const std::string events = RunFor(bench.sim, bench.console, 5000);
  A2B_CHECK_EQ(bench.console.Bus().SubNodeCount(), 2);
  A2B_CHECK(Has(events, "module 0C54:0020, CFG-4, 1 peripheral block(s)"));
  A2B_CHECK(Has(events, "rx G0 0-1, tx G0 0-1"));
  const std::vector<std::vector<uint8_t>> expected = SplitProgram();
  A2B_CHECK_EQ(bench.codec.writes.size(), 39u);
  A2B_CHECK(bench.codec.writes == expected);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(0x41), 0xE0);  // I2SGCFG, from the image.
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(0x5A), 0x81);  // CLK2CFG.
}

A2B_TEST(WriteStoppedBeforeItsFirstPageSaysTheNodeIsBlank) {
  // Stopped after the marker is cleared, before a page is written: the node has no module
  // info, and the console says so.
  Bench bench;
  bench.eeproms[0]->Load(ReadFile(kAb0020));
  Run(bench.console, "ignoreEEPROM 1");
  Run(bench.console, "start");
  RunFor(bench.sim, bench.console, 3000);
  Run(bench.console, (std::string("eeprom write 0 ") + kAb0312).c_str());
  for (int ms = 0; ms < 2000 && bench.eeproms[0]->Contents()[0] != 0xFF; ++ms) {
    RunFor(bench.sim, bench.console, 1);
  }
  A2B_CHECK_EQ(bench.eeproms[0]->Contents()[0], 0xFF);
  const std::string stopped = Run(bench.console, "eeprom abort");
  A2B_CHECK(Has(stopped, "failed: stopped (eeprom abort)"));
  A2B_CHECK(Has(stopped, "partly written, without its marker"));
}

A2B_TEST(EepromAbortAndQuitRunWhileAJobRuns) {
  // The hosts take these lines at once during a job; the others wait for it.
  A2B_CHECK(BenchConsole::RunsWhileBusy("eeprom abort"));
  A2B_CHECK(BenchConsole::RunsWhileBusy("  EEPROM  Abort "));
  A2B_CHECK(BenchConsole::RunsWhileBusy("quit"));
  A2B_CHECK(BenchConsole::RunsWhileBusy("exit"));
  A2B_CHECK(!BenchConsole::RunsWhileBusy("eeprom read 0"));
  A2B_CHECK(!BenchConsole::RunsWhileBusy("status"));
  A2B_CHECK(!BenchConsole::RunsWhileBusy(""));
}

A2B_TEST(StoppedWriteLeavesTheNodeBlank) {
  Bench bench;
  bench.eeproms[0]->Load(ReadFile(kAb0020));  // An image there already.
  Run(bench.console, "ignoreEEPROM 1");
  Run(bench.console, "start");
  RunFor(bench.sim, bench.console, 3000);
  Run(bench.console, (std::string("eeprom write 0 ") + kAb0312).c_str());
  A2B_CHECK(bench.console.Busy());
  A2B_CHECK(Has(Run(bench.console, "start"), "An EEPROM job is running on sub-node 0"));
  A2B_CHECK(Has(Run(bench.console, "eeprom write 1 x.dat"), "An EEPROM job is running"));
  RunFor(bench.sim, bench.console, 25);  // Read the old image, clear the marker, a few pages.
  A2B_CHECK(bench.console.Busy());
  const std::string stopped = Run(bench.console, "eeprom abort");
  A2B_CHECK(Has(stopped, "failed: stopped (eeprom abort)"));
  A2B_CHECK(Has(stopped, "partly written, without its marker"));
  A2B_CHECK_EQ(bench.eeproms[0]->Contents()[0], 0xFF);
  // Found again from the EEPROMs: no module info at sub-node 0, not a half image.
  Run(bench.console, "ignoreEEPROM 0");
  Run(bench.console, "start");
  A2B_CHECK(Has(RunFor(bench.sim, bench.console, 3000),
                "sub-node 0 rejected: no module info (no EEPROM at 0x50-0x54, or a blank one)"));
}

A2B_TEST(WriteProtectedAndBusyEeproms) {
  Bench bench;
  Run(bench.console, "ignoreEEPROM 1");
  Run(bench.console, "start");
  RunFor(bench.sim, bench.console, 3000);
  bench.eeproms[0]->SetWriteProtect(true);
  const std::string refused =
      RunJob(bench.sim, bench.console, (std::string("eeprom write 0 ") + kAb0312).c_str());
  A2B_CHECK(Has(refused, "failed: 0x0001 holds 0xFF, not 0x54 after writing (write-protected?)"));
  // No wait after each page: the EEPROM NAKs while it writes, and the transfers are tried
  // again until it answers.
  Run(bench.console, "eeprom config cycle 0");
  const std::string busy =
      RunJob(bench.sim, bench.console, (std::string("eeprom write 1 ") + kAb0312).c_str());
  A2B_CHECK(Has(busy, "Wrote 2052 bytes to sub-node 1's EEPROM (0x50)"));
  A2B_CHECK(Has(busy, "transfer(s) tried again"));
  A2B_CHECK(Has(RunJob(bench.sim, bench.console, "eeprom erase 1"),
                "Marker cleared on sub-node 1's EEPROM (0x50): the node has no module info now"));
  A2B_CHECK_EQ(bench.eeproms[1]->Contents()[0], 0xFF);
  // An EEPROM at another address, set by hand.
  Run(bench.console, "eeprom config addr 0x51");
  A2B_CHECK(Has(RunJob(bench.sim, bench.console, "eeprom erase 1"),
                "failed: no answer at EEPROM address 0x0000 after 8 tries"));
}

A2B_TEST(ReadsWholeOrInPart) {
  Bench bench;
  bench.eeproms[1]->Load(ReadFile(kAb0312));
  Run(bench.console, "ignoreEEPROM 1");
  Run(bench.console, "start");
  RunFor(bench.sim, bench.console, 3000);
  const std::string whole = A2B_TEST_OUT_DIR "/node1_whole.dat";
  A2B_CHECK(Has(RunJob(bench.sim, bench.console, ("eeprom read 1 " + whole + " all").c_str()),
                "Read 32768 bytes"));
  A2B_CHECK_EQ(ReadFile(whole).size(), 32768u);
  const std::string part = A2B_TEST_OUT_DIR "/node1_part.dat";
  A2B_CHECK(Has(RunJob(bench.sim, bench.console, ("eeprom read 1 " + part + " 100").c_str()),
                "Read 100 bytes"));
  const std::string blank = A2B_TEST_OUT_DIR "/node0_blank.dat";
  const std::string read = RunJob(bench.sim, bench.console, ("eeprom read 0 " + blank).c_str());
  A2B_CHECK(Has(read, "Read 32768 bytes"));
  A2B_CHECK(Has(read, "no image there (blank"));
  // A whole EEPROM read back is an image to write again (its 0xFF tail and all).
  A2B_CHECK(Has(RunJob(bench.sim, bench.console, ("eeprom verify 1 " + whole).c_str()),
                "holds the image (32768 bytes)"));
}

A2B_TEST(ImageDatarateSetsTheCodecsRate) {
  // The AB0020's image sets I2CCFG.DATARATE: the stack reads it at 100 kHz, then moves the
  // node's I2C port to 400 kHz for the codec program. A copy with it clear stays at 100 kHz.
  const std::vector<uint8_t> image = ReadFile(kAb0020);
  A2B_CHECK(image.size() > 0x14C);
  if (image.size() <= 0x14C) {
    return;
  }
  std::vector<uint8_t> slow = image;
  slow[0x7F] = 0x00;
  slow[0x14C] = Crc8(&slow[0x20], 0x14C - 0x20);  // The module-info block's CRC.
  A2B_CHECK(Has(DescribeImage(image), "I2C port: 400 kHz once the start is read"));
  A2B_CHECK(!Has(DescribeImage(slow), "I2C port: 400 kHz"));
  for (int fast = 1; fast >= 0; --fast) {
    Bench bench;
    bench.eeproms[0]->Load(fast ? image : slow);
    Run(bench.console, "start");
    const std::string events = RunFor(bench.sim, bench.console, 5000);
    A2B_CHECK(Has(events, "its I2C port at 400 kHz (the image's I2CCFG.DATARATE)") == (fast != 0));
    A2B_CHECK(bench.codec.writes == SplitProgram());
    A2B_CHECK_EQ(bench.bus.Sub(0).Reg(0x3F), fast ? 0x01 : 0x00);  // I2CCFG.
  }
}

A2B_TEST(ExternalImageTakesTheChipsPowerConfig) {
  // The AB0020's image says CFG-4, an AD2437 module's; with an AD2428 module the node is
  // CFG-0, its chip's (an AD2428 module, whatever the image says).
  Bench bench(0x28);
  Run(bench.console, (std::string("module 0 file ") + kAb0020).c_str());
  Run(bench.console, "start");
  const std::string events = RunFor(bench.sim, bench.console, 5000);
  A2B_CHECK(Has(events, "sub-node 0: AD2428"));
  A2B_CHECK(Has(events, "module 0C54:0020, CFG-0, 1 peripheral block(s)"));
  A2B_CHECK(!Has(events, "CFG isn't its chip's"));  // External: expected, not logged.
}

A2B_TEST(ImageFromAFileWithoutAnEeprom) {
  // `module 0 file`: the AB0020's image tried before any EEPROM is written.
  Bench bench;
  A2B_CHECK(Has(Run(bench.console, (std::string("module 0 file ") + kAb0020).c_str()),
                "Sub-node 0: the image"));
  A2B_CHECK(Has(Run(bench.console, "module"), "module 0 file "));
  Run(bench.console, "start");
  const std::string events = RunFor(bench.sim, bench.console, 5000);
  A2B_CHECK(Has(events, "module info from the image "));
  A2B_CHECK(Has(events, "    image for External; B port UTP, its cable not given\n"));
  A2B_CHECK(Has(events, "sub-node 1 rejected: no module info"));  // Its EEPROM is blank.
  A2B_CHECK_EQ(bench.codec.writes.size(), 39u);
  A2B_CHECK(Has(Run(bench.console, "status"), "Boards: sub-node 0 image "));
  A2B_CHECK(Has(Run(bench.console, "module 0 default"), "no board or image set"));
}

A2B_TEST(Refusals) {
  Bench bench;
  A2B_CHECK(Has(Run(bench.console, "eeprom read 0 x.dat"), "Sub-node 0 isn't in the bus"));
  A2B_CHECK(Has(Run(bench.console, "eeprom frob 0"), "eeprom [config ...]"));
  A2B_CHECK(Has(Run(bench.console, "eeprom read 16 x.dat"), "N is a sub-node, 0-15"));
  A2B_CHECK(Has(Run(bench.console, "eeprom config page 3"), "a power of two"));
  A2B_CHECK(Has(Run(bench.console, "eeprom config size 4096 page 32"),
                "EEPROM: address where found (else 0x50), 4096 bytes, 32-byte pages"));
  A2B_CHECK(Has(Run(bench.console, "eeprom abort"), "No EEPROM job is running"));
  Run(bench.console, "ignoreEEPROM 1");
  Run(bench.console, "start");
  RunFor(bench.sim, bench.console, 3000);
  A2B_CHECK(Has(Run(bench.console, (std::string("eeprom write 0 ") + kAb0020).c_str()),
                "8376 bytes: more than the EEPROM's 4096; nothing sent"));
  A2B_CHECK(Has(Run(bench.console, "eeprom write 0 /no/such/image.dat"), "Can't read"));
  A2B_CHECK(Has(Run(bench.console, (std::string("eeprom write 0 ") + kCodecXml).c_str()),
                "not a sound image"));
  A2B_CHECK(Has(Run(bench.console, (std::string("module 0 file ") + kCodecXml).c_str()),
                "not a sound image"));
  // The AB0204's image names its soldered-down AD2427: not for an AD2437 node.
  Run(bench.console, "eeprom config size 32768");
  A2B_CHECK(Has(Run(bench.console, (std::string("eeprom write 0 ") + kAb0204).c_str()),
                "is for the AD2427; sub-node 0 is an AD2437, which the stack would reject with "
                "it: nothing sent"));
  A2B_CHECK(Has(Run(bench.console, (std::string("module 0 file ") + kAb0204).c_str()),
                "note: it is for the AD2427; sub-node 0 is an AD2437"));
  A2B_CHECK_EQ(bench.eeproms[0]->WriteCount(), 0);
}

A2B_TEST(ChecksAndWalks) {
  // CheckImage() and WalkImage() on the image tool's images and broken copies.
  std::vector<uint8_t> image = ReadFile(kAb0020);
  std::string why;
  A2B_CHECK(CheckImage(image, 32768, why));
  A2B_CHECK(why.empty());
  size_t end = 0;
  A2B_CHECK(WalkImage(image.data(), 0x100, false, false, end, why) == ImageWalk::kNeedMore);
  A2B_CHECK_EQ(end, 0x14Du);  // The module-info block's end.
  A2B_CHECK(WalkImage(image.data(), 0x900, false, false, end, why) == ImageWalk::kNeedMore);
  A2B_CHECK(WalkImage(image.data(), image.size(), true, true, end, why) == ImageWalk::kComplete);
  A2B_CHECK_EQ(end, image.size());
  std::vector<uint8_t> broken = image;
  broken[0x1000] ^= 1;  // Inside a unit.
  A2B_CHECK(!CheckImage(broken, 32768, why));
  A2B_CHECK(Has(why, "not sound"));
  broken = image;
  broken[0x85] = 0x01;                                // A reduced I2S rate: the stack refuses it.
  broken[0x14C] = Crc8(&broken[0x20], 0x14C - 0x20);  // The block's CRC made right again.
  A2B_CHECK(!CheckImage(broken, 32768, why));
  A2B_CHECK(Has(why, "the stack would refuse it"));
  broken = image;
  broken[0x14C] ^= 1;  // The module-info block's CRC, which the stack doesn't check: a note.
  A2B_CHECK(CheckImage(broken, 32768, why));
  A2B_CHECK(Has(why, "module-info block's CRC is wrong (the stack doesn't check it)"));
  broken = image;
  broken[0x4FF] ^= 1;  // The vendor block's.
  A2B_CHECK(CheckImage(broken, 32768, why));
  A2B_CHECK(Has(why, "supplier information block's CRC is wrong"));
  A2B_CHECK(Has(DescribeImage(broken), "Note: the supplier information block's CRC is wrong"));
  broken = image;
  broken[0x1E] ^= 1;  // The header's, which the stack checks.
  A2B_CHECK(!CheckImage(broken, 32768, why));
  std::vector<uint8_t> tail = image;
  tail.resize(32768, 0xFF);
  A2B_CHECK(CheckImage(tail, 32768, why));
  A2B_CHECK(Has(why, "runs on past the image's 8376 bytes"));
  SupplierInfo supplier;
  A2B_CHECK(ParseSupplierInfo(image, supplier));
  A2B_CHECK(supplier.vendorId == "CLKWORKS");
  A2B_CHECK(supplier.crcOk);
  clockworks::ClockworksSection section;
  A2B_CHECK(clockworks::ParseClockworksSection(supplier.vendorId, supplier.data.data(),
                                               supplier.data.size(), section));
  A2B_CHECK(section.id == "AB0020");
  A2B_CHECK_EQ(section.features.size(), 1u);
  if (!section.features.empty()) {
    A2B_CHECK_EQ(section.features[0].uid, 0x3E7A91C05D2B8F14ull);
    A2B_CHECK_EQ(section.features[0].value0, 48000);
    A2B_CHECK(section.features[0].text == "ADAU1761");
  }
  A2B_CHECK(!clockworks::ParseClockworksSection("000000AD", supplier.data.data(),
                                                supplier.data.size(), section));
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
