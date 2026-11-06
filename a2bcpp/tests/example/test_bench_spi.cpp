/**
 * @file test_bench_spi.cpp
 * @brief The bench console's `spi`, `spiflash` and `wait` commands on the simulator
 *        (docs/SPI.md): the wiring check, the tunnel opened and closed, the raw transactions,
 *        a W25Q128-like flash's ID, erase, program, read and verify from and to files, a
 *        stream dump and send, `wait`, the exit code, a host without SPI, a tunnel opened
 *        again, and a node joining behind the far end.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "bench_console.h"
#include "sim/simulator.h"
#include "sim/spi_flash.h"
#include "test_harness.h"

namespace a2b {
namespace {

/** True if @p text contains @p part; prints it if not. */
bool Has(const std::string& text, const char* part) {
  if (text.find(part) != std::string::npos) {
    return true;
  }
  std::printf("  expected \"%s\" in:\n%s\n", part, text.c_str());
  return false;
}

/** Runs a command and gives its output. */
std::string Run(BenchConsole& console, const char* line) {
  console.Execute(line);
  return console.TakeOutput();
}

/** An AD2437 as on the bench: CFG-4, low-voltage input. */
sim::ChipSpec Ad2437(uint8_t id = 0) {
  sim::ChipSpec chip;
  chip.powerConfig = 4;
  chip.lowVoltageInput = true;
  chip.chipId[5] = id;
  return chip;
}

A2B_TEST(SpiCheckFromTheConsole) {
  sim::Simulator sim;
  sim::Bus& bus = sim.AddBus(0x68, 0x69, Ad2437());
  bus.StartRunning();
  BenchConsole console(sim);
  A2B_CHECK(Has(Run(console, "spi check"),
                "SPI check: VENDOR, PRODUCT, VERSION over SPI AD 37 21, over I2C AD 37 21: the "
                "SPI to the main node works\n"));
  A2B_CHECK(Has(Run(console, "spi"), "SPI: last transaction ended without an error\n"));
  A2B_CHECK(Has(Run(console, "SPI CHECK"), "the SPI to the main node works"));
  A2B_CHECK(Has(Run(console, "spi frobnicate"), "spi [check|open|close"));
  // The main node's port turned off: said, nothing sent over SPI.
  const size_t sent = sim.SpiTransferCount();
  Run(console, "wrm 0xB0 0x02");
  A2B_CHECK(Has(Run(console, "spi check"), "the main node's SPI port is off (SPICFG 0x02"));
  A2B_CHECK_EQ(sim.SpiTransferCount(), sent);
  A2B_CHECK(Has(Run(console, "help"), "spi check"));
}

A2B_TEST(SpiOnAHostWithoutIt) {
  // The USBi: every spi command says so and sends nothing.
  sim::Simulator sim;
  sim::Bus& bus = sim.AddBus(0x68, 0x69, Ad2437());
  bus.StartRunning();
  sim.SetHasSpi(false);
  BenchConsole console(sim);
  A2B_CHECK(Has(Run(console, "spi check"),
                "SPI check: this host has no SPI to the main node (the USBi has I2C only"));
  A2B_CHECK_EQ(sim.SpiTransferCount(), 0u);
}

/** A file in the test output folder. */
std::string OutFile(const char* name) { return std::string(A2B_TEST_OUT_DIR) + "/" + name; }

/** Writes @p data to @p path. */
void Write(const std::string& path, const std::vector<uint8_t>& data) {
  std::FILE* out = std::fopen(path.c_str(), "wb");
  if (out != nullptr) {
    std::fwrite(data.data(), 1, data.size(), out);
    std::fclose(out);
  }
}

/** Reads @p path. */
std::vector<uint8_t> ReadAll(const std::string& path) {
  std::vector<uint8_t> data;
  std::FILE* in = std::fopen(path.c_str(), "rb");
  if (in != nullptr) {
    int c = 0;
    while ((c = std::fgetc(in)) != EOF) {
      data.push_back(static_cast<uint8_t>(c));
    }
    std::fclose(in);
  }
  return data;
}

/** The reference board with two AD2437 sub-nodes, a flash on sub-node 1's SS0, started. */
struct SpiConsoleBench {
  sim::Simulator sim;
  sim::Bus& bus;
  sim::SpiFlash flash;
  BenchConsole console;

  SpiConsoleBench() : bus(sim.AddBus(0x68, 0x69, Ad2437())), flash(sim), console(sim) {
    bus.StartRunning();
    for (uint8_t i = 1; i <= 2; ++i) {
      sim::SubNodeSpec spec;
      spec.chip = Ad2437(i);
      spec.link.bSideInverted = true;
      bus.Plug(spec);
    }
    bus.Sub(1).AttachSpiDevice(0, flash);
    Run(console, "board ref");
    Run(console, "link all board");
    Run(console, "ignoreEEPROM 1");
  }

  /** Polls for @p ms of simulated time; gives what the console printed. */
  std::string RunFor(int ms) {
    for (int i = 0; i < ms; ++i) {
      sim.Advance(1);
      console.Poll();
    }
    return console.TakeOutput();
  }

  /** Runs a command, then polls while the console is busy (a job, a wait). */
  std::string RunJob(const char* line) {
    std::string out = Run(console, line);
    for (int i = 0; i < 60000 && console.Busy(); ++i) {
      sim.Advance(1);
      console.Poll();
    }
    return out + console.TakeOutput();
  }
};

A2B_TEST(TunnelAndRawTransactionsFromTheConsole) {
  SpiConsoleBench bench;
  BenchConsole& console = bench.console;
  A2B_CHECK(Has(Run(console, "spi write 0x06"), "No SPI tunnel is open (spi open N)"));
  Run(console, "start");
  // wait holds the queued commands until the node is in, then says so.
  A2B_CHECK(Has(bench.RunJob("wait node 1"), "wait: the sub-node is there"));
  A2B_CHECK(Has(Run(console, "spi open 1 slots 2"),
                "SPI tunnel to sub-node 1: SS0, SCK 9830 kHz (at 48 kHz), mode 0, 2 slot(s)"));
  A2B_CHECK(Has(bench.RunJob("wait spi"), "wait: the SPI tunnel is there"));
  A2B_CHECK(Has(Run(console, "spi"), "SPI tunnel: open to sub-node 1, 2 slot(s) each way"));
  A2B_CHECK(Has(Run(console, "status"), "SPI tunnel: open to sub-node 1"));
  // The JEDEC ID, with and without the expected one; a mismatch sets the exit code.
  A2B_CHECK(Has(Run(console, "spiflash id"),
                "Flash on sub-node 1 SS0: JEDEC ID EF 40 18 (Winbond, 16 MB)"));
  A2B_CHECK(Has(Run(console, "spiflash id EF4018"), "spiflash id: matches EF 40 18"));
  A2B_CHECK_EQ(console.ExitCode(), 0);
  A2B_CHECK(Has(Run(console, "spiflash id 0xEF4017"), "spiflash id: doesn't match EF 40 17"));
  A2B_CHECK_EQ(console.ExitCode(), 1);
  A2B_CHECK(Has(Run(console, "spiflash id EF40"), "spiflash id [ID]"));
  // The raw transactions: atomic read and write, full duplex, bulk.
  A2B_CHECK(Has(Run(console, "spi read 3 0x9F"), "Read sub-node 1 SS0: EF 40 18"));
  A2B_CHECK(Has(Run(console, "spi write 0x06"), "Wrote sub-node 1 SS0 (atomic): 06"));
  const std::string fd = Run(console, "spi fd 0x05 0x00");
  A2B_CHECK(Has(fd, "the last one's data: 00 00"));
  A2B_CHECK(Has(fd, "this one's (FIFO): FF 02"));  // WEL set by the write enable.
  A2B_CHECK(Has(Run(console, "spi bulk 0x02 0x00 0x00 0x10 0xAB"),
                "Wrote sub-node 1 SS0 (bulk): 02 00 00 10 AB"));
  bench.RunFor(5);
  A2B_CHECK_EQ(bench.flash.Memory()[0x10], 0xAB);
  A2B_CHECK(Has(Run(console, "spi read 0"), "spi read LEN [BYTE...]"));
  A2B_CHECK(Has(Run(console, "spi write"), "spi write BYTE..."));
  A2B_CHECK(Has(Run(console, "spi open 9"), "Can't open the SPI tunnel"));
  A2B_CHECK(Has(Run(console, "spi open 1 slots 1"), "spi open N [ss 0-2]"));
  // Closed.
  A2B_CHECK(Has(Run(console, "spi close"), "SPI tunnel: closed at the next poll"));
  A2B_CHECK(Has(bench.RunFor(50), "SPI tunnel to sub-node 1 closed"));
  A2B_CHECK(Has(Run(console, "spi"), "SPI tunnel: none"));
}

A2B_TEST(FlashJobsFromTheConsole) {
  SpiConsoleBench bench;
  BenchConsole& console = bench.console;
  Run(console, "start");
  bench.RunJob("wait node 1");
  Run(console, "spi open 1");
  bench.RunJob("wait spi");
  // A 5000-byte file programmed at 0x1080 (not page-aligned), after an erase; then verified
  // and read back.
  std::vector<uint8_t> image(5000);
  for (size_t i = 0; i < image.size(); ++i) {
    image[i] = static_cast<uint8_t>(i * 13 + 1);
  }
  image[300] = 0xFF;
  const std::string in = OutFile("spi_flash_image.bin");
  const std::string out = OutFile("spi_flash_read.bin");
  Write(in, image);
  A2B_CHECK(Has(bench.RunJob("spiflash erase 0x1080 5000"),
                "SPI job on sub-node 1 done: 2 sector(s) of 4 KB erased"));
  const std::string programmed = bench.RunJob(("spiflash program 0x1080 " + in).c_str());
  A2B_CHECK(Has(programmed, "SPI job on sub-node 1 done: 21 page(s) programmed, 0 left out"));
  A2B_CHECK(std::equal(image.begin(), image.end(), bench.flash.Memory().begin() + 0x1080));
  A2B_CHECK(Has(bench.RunJob(("spiflash verify 0x1080 " + in).c_str()), "5000 bytes in"));
  A2B_CHECK(Has(bench.RunJob(("spiflash read 0x1080 5000 " + out).c_str()),
                (", written to " + out).c_str()));
  A2B_CHECK(ReadAll(out) == image);
  // A difference is found and named; the exit code is 1.
  bench.flash.Memory()[0x1080 + 4000] ^= 0x01;
  A2B_CHECK(
      Has(bench.RunJob(("spiflash verify 0x1080 " + in).c_str()), "failed: differs at 0x002020"));
  A2B_CHECK_EQ(console.ExitCode(), 1);
  // Programming over data that isn't erased: the flash only clears bits, so verify says so.
  // A page all 0xFF is left out.
  std::vector<uint8_t> blank(512, 0xFF);
  blank[300] = 0x00;
  Write(in, blank);
  A2B_CHECK(Has(bench.RunJob(("spiflash program 0x8000 " + in).c_str()),
                "1 page(s) programmed, 1 left out (all 0xFF)"));
  // A stream: spi dump (0x03 and an address, then the data) and spi send.
  A2B_CHECK(
      Has(bench.RunJob(("spi dump " + out + " 300 0x03 0x00 0x10 0x80").c_str()), "300 bytes in"));
  const std::vector<uint8_t> dumped = ReadAll(out);
  A2B_CHECK(dumped.size() == 300 &&
            std::equal(dumped.begin(), dumped.end(), bench.flash.Memory().begin() + 0x1080));
  Write(in, std::vector<uint8_t>(3, 0x5A));
  Run(console, "spi write 0x06");
  A2B_CHECK(Has(bench.RunJob(("spi send " + in + " 0x02 0x00 0x90 0x00").c_str()), "7 bytes in"));
  bench.RunFor(5);
  A2B_CHECK_EQ(bench.flash.Memory()[0x9002], 0x5A);
  // A job refuses other spi commands but abort; spi abort stops it.
  Write(in, image);
  Run(console, ("spiflash verify 0x1080 " + in).c_str());
  A2B_CHECK(console.Busy());
  A2B_CHECK(Has(Run(console, "spi read 1 0x05"), "An SPI job is running on sub-node 1"));
  A2B_CHECK(BenchConsole::RunsWhileBusy("spi abort"));
  A2B_CHECK(Has(Run(console, "spi abort"), "failed: stopped (spi abort)"));
  A2B_CHECK(!console.Busy());
  // Sub-node 1 is the last node: the probes behind it never cut into a transaction.
  A2B_CHECK_EQ(bench.bus.SpiDiscoveryAborts(), 0u);
}

A2B_TEST(ReopenAndAJoiningNodeFromTheConsole) {
  SpiConsoleBench bench;
  BenchConsole& console = bench.console;
  Run(console, "start");
  bench.RunJob("wait node 1");
  Run(console, "spi open 1 slots 4");
  A2B_CHECK(Has(bench.RunJob("wait spi"), "wait: the SPI tunnel is there"));
  // Opened again at another size: not ready until its new plan is written; wait spi waits.
  Run(console, "spi open 1 slots 2");
  A2B_CHECK(Has(Run(console, "spi read 3 0x9F"), "The SPI tunnel isn't ready yet"));
  A2B_CHECK(Has(bench.RunJob("wait spi"), "wait: the SPI tunnel is there"));
  A2B_CHECK(Has(Run(console, "spi"), "SPI tunnel: open to sub-node 1, 2 slot(s) each way"));
  A2B_CHECK(Has(Run(console, "spi read 3 0x9F"), "Read sub-node 1 SS0: EF 40 18"));
  // A node plugged in behind the far end: found once SPI is idle; while it joins, a
  // transaction is refused (busy), and works again once the tunnel is planned again.
  sim::SubNodeSpec spec;
  spec.chip = Ad2437(3);
  spec.link.bSideInverted = true;
  bench.bus.Plug(spec);
  for (int i = 0; i < 3000 && bench.bus.DiscoveredCount() < 3; ++i) {
    bench.RunFor(1);
  }
  A2B_CHECK_EQ(bench.bus.DiscoveredCount(), 3u);
  A2B_CHECK(Has(Run(console, "spi read 3 0x9F"), "spi read failed: busy: a node is joining"));
  // A job started meanwhile waits for the stack (the same step at the next poll), then runs.
  A2B_CHECK(Has(bench.RunJob("spiflash erase 0x2000 4096"),
                "SPI job on sub-node 1 done: 1 sector(s) of 4 KB erased"));
  A2B_CHECK(Has(bench.RunJob("wait node 2"), "wait: the sub-node is there"));
  A2B_CHECK(Has(bench.RunJob("wait spi"), "wait: the SPI tunnel is there"));
  A2B_CHECK(Has(Run(console, "spi read 3 0x9F"), "Read sub-node 1 SS0: EF 40 18"));
  A2B_CHECK_EQ(bench.bus.SpiDiscoveryAborts(), 0u);
}

A2B_TEST(WaitThatIsNotMet) {
  SpiConsoleBench bench;
  BenchConsole& console = bench.console;
  // No start: the node never comes, the wait times out, the exit code is 1.
  A2B_CHECK(Has(bench.RunJob("wait node 1 2"), "wait: the sub-node didn't come in time"));
  A2B_CHECK_EQ(console.ExitCode(), 1);
  A2B_CHECK(Has(Run(console, "wait"), "wait node N [S]"));
  A2B_CHECK(Has(bench.RunJob("wait 1"), "wait: done"));
  Run(console, "wait spi 100");
  A2B_CHECK(console.Busy());
  A2B_CHECK(Has(Run(console, "wait abort"), "wait: stopped"));
  A2B_CHECK(!console.Busy());
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
