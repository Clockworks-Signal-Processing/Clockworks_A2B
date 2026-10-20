/**
 * @file test_example_bench.cpp
 * @brief The bench console (examples/bench) on the simulator: its commands, a built-in
 *        board, an export loaded from a file (an AD2428 main node with AD2428, AD2427 and
 *        AD2437 sub-nodes, as on the bench), link profiles (an isolated AD2437 found with
 *        `link 1 custom`), the EEPROMs ignored, the I2C trace, register and peripheral
 *        access, pause, resume and reject, the connection, routing and cable commands, an
 *        AB0020 sub-node's module info and codec program (`module`), and an AD2427 without
 *        an EEPROM taken as the AB0204's four PDM microphones, routed to an AD2428 main node.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "bench_console.h"
#include "sigma_xml.h"
#include "sim/module_info.h"
#include "sim/simulator.h"
#include "ss_plus_import.h"
#include "test_harness.h"

namespace a2b {
namespace {

/** True if @p text contains @p part. */
bool Has(const std::string& text, const char* part) { return text.find(part) != std::string::npos; }

/** Runs a command and gives its output. */
std::string Run(BenchConsole& console, const char* line) {
  console.Execute(line);
  return console.TakeOutput();
}

/** Polls the console for @p ms milliseconds of simulated time; gives what it printed. */
std::string RunFor(sim::Simulator& sim, BenchConsole& console, int ms) {
  for (int i = 0; i < ms; ++i) {
    sim.Advance(1);
    console.Poll();
  }
  return console.TakeOutput();
}

/** An AD2437 as on the bench: CFG-4, low-voltage input. */
sim::ChipSpec Ad2437() {
  sim::ChipSpec chip;
  chip.powerConfig = 4;
  chip.lowVoltageInput = true;
  return chip;
}

/** A transceiver @p product with the last chip ID byte @p id. */
sim::ChipSpec Chip(uint8_t product, uint8_t id) {
  sim::ChipSpec chip = (product == 0x37) ? Ad2437() : sim::ChipSpec();
  chip.productId = product;
  chip.chipId[5] = id;
  return chip;
}

/** Plugs in a sub-node with an EEPROM of the bench modules' kind, behind an inverted link. */
void PlugWithEeprom(sim::Bus& bus, const sim::ChipSpec& chip) {
  sim::SubNodeSpec spec;
  spec.chip = chip;
  spec.link.bSideInverted = true;
  sim::ModuleInfoHeader header;
  header.powerConfig = chip.powerConfig;
  bus.Plug(spec).AddEeprom(0x50).Load(sim::MakeModuleInfoImage(header, sim::ModuleInfoRegs()));
}

/** Plugs in a sub-node without an EEPROM. */
void PlugBlank(sim::Bus& bus, const sim::ChipSpec& chip, bool inverted,
               sim::LinkPower power = sim::LinkPower::kOnPair) {
  sim::SubNodeSpec spec;
  spec.chip = chip;
  spec.link.bSideInverted = inverted;
  spec.link.power = power;
  bus.Plug(spec);
}

/**
 * Writes the reference board's export with its main node made an AD2428 (part number and
 * product ID), as the bench's AD2428-main board's export would have it, and gives its path.
 * @param i2sPins Also give the main node DTX0 and DRX0 (its GPIO3 and GPIO5 as an AD2428
 *        export has them for I2S; the AD2437 export leaves them disabled), so its port has
 *        channels to route.
 */
std::string WriteAd2428Export(bool i2sPins = false) {
  std::FILE* in = std::fopen(A2B_REPO_ROOT "/adi/exports/rp2040/12_a2b_busconfig.c", "rb");
  std::string text;
  if (in != nullptr) {
    char buffer[4096];
    size_t got = 0;
    while ((got = std::fread(buffer, 1, sizeof(buffer), in)) > 0) {
      text.append(buffer, got);
    }
    std::fclose(in);
  }
  // The main node comes first in the export.
  const std::string part = "ADI_A2B_AD2437";
  const size_t partAt = text.find(part);
  if (partAt != std::string::npos) {
    text.replace(partAt, part.size(), "ADI_A2B_AD2428");
  }
  const std::string product = ".nProductID\t\t\t= 0x37u";
  const size_t productAt = text.find(product);
  if (productAt != std::string::npos) {
    text.replace(productAt, product.size(), ".nProductID\t\t\t= 0x28u");
  }
  const char* const pins[][2] = {{"A2B_GPIO_3_DISABLE", "A2B_GPIO_3_AS_DTX0"},
                                 {"A2B_GPIO_5_DISABLE", "A2B_GPIO_5_AS_DRX0"}};
  for (const auto& pin : pins) {
    const size_t at = i2sPins ? text.find(pin[0]) : std::string::npos;  // The main node's.
    if (at != std::string::npos) {
      text.replace(at, std::strlen(pin[0]), pin[1]);
    }
  }
  const std::string path = A2B_TEST_OUT_DIR "/ad2428_main_export.c";
  std::FILE* out = std::fopen(path.c_str(), "wb");
  if (out != nullptr) {
    std::fwrite(text.data(), 1, text.size(), out);
    std::fclose(out);
  }
  A2B_CHECK(partAt != std::string::npos && productAt != std::string::npos && out != nullptr);
  return path;
}

A2B_TEST(HelpBoardsAndRefusals) {
  sim::Simulator sim;
  BenchConsole console(sim);
  const std::string help = Run(console, "help");
  A2B_CHECK(Has(help, "i2cTrace on FILE"));
  A2B_CHECK(Has(help, "rds N REG [COUNT]"));
  A2B_CHECK(Has(help, "periRead NODE ADDR COUNT [BYTE...]"));
  A2B_CHECK(Has(help, "reject N"));
  A2B_CHECK(Has(help, "mute [on|off]"));
  A2B_CHECK(Has(help, "muteMode [local|global|none]"));
  A2B_CHECK(Has(help, "bandwidth "));
  A2B_CHECK(Has(help, "setRoute [append] S:G D:G..."));
  A2B_CHECK(Has(help, "importCon S:G D:G"));
  const std::string boards = Run(console, "board");
  A2B_CHECK(Has(boards, "ref "));
  A2B_CHECK(Has(boards, "AB0020"));
  A2B_CHECK(Has(boards, "Board: none"));
  A2B_CHECK(Has(Run(console, "start"), "No board"));
  A2B_CHECK(Has(Run(console, "frobnicate"), "Unknown command 'frobnicate'"));
  A2B_CHECK(Has(Run(console, "link 16 utp"), "N: 0-15"));
  A2B_CHECK(Has(Run(console, "link 3 xlr single"), "N: 0-15"));
  A2B_CHECK(Has(Run(console, "ignoreEEPROM 2"), "ignoreEEPROM 1|0"));
  A2B_CHECK(Has(Run(console, "board /no/such/export.c"), "Can't load /no/such/export.c"));
  A2B_CHECK(Has(Run(console, "status"), "Bus: stopped"));
  A2B_CHECK(console.Execute("quit") == false);
  A2B_CHECK_EQ(sim.TransferCount(), 0);  // Nothing was sent.
}

A2B_TEST(BuiltInBoardBringsUpTheChain) {
  sim::Simulator sim;
  sim::Bus& bus = sim.AddBus(0x68, 0x69, Ad2437());
  bus.StartRunning();
  PlugWithEeprom(bus, Chip(0x37, 1));
  PlugWithEeprom(bus, Chip(0x37, 2));
  BenchConsole console(sim);
  A2B_CHECK(Has(Run(console, "BOARD Ref"), "Board ref, chain 0"));
  // The bench's links are all RJ45, single-step: the board's, not the images' B ports (UTP).
  A2B_CHECK(Has(Run(console, "link all board"),
                "Every link not set with link N is the board's, whatever the images' B ports say "
                "(the export's, at start)"));
  A2B_CHECK(Has(Run(console, "start"), "Main node AD2437 at 0x68 (sub-nodes 0x69)"));
  const std::string events = RunFor(sim, console, 2000);
  A2B_CHECK(Has(events, "bus: running"));
  A2B_CHECK(Has(events,
                "sub-node 1: AD2437 version 0x21, chip ID 000000000002, module "
                "00AD:0037, CFG-4, 0 peripheral block(s), link rj45 single (board)\n"));
  A2B_CHECK(Has(events, "    image for not stated; B port UTP, its cable not given\n"));
  const std::string status = Run(console, "status");
  A2B_CHECK(Has(status, "Bus: running, main node AD2437 at 0x68, 2 sub-node(s)"));
  A2B_CHECK(Has(status, "Module info: the EEPROMs"));
  A2B_CHECK(Has(status, "Links: rj45 single by default"));
  A2B_CHECK(Has(Run(console, "stop"), "Bus stopped"));
  A2B_CHECK(console.Bus().State() == NetworkState::kStopped);
}

A2B_TEST(AbBoardRunsItsCodecScript) {
  sim::Simulator sim;
  sim::Bus& bus = sim.AddBus(0x68, 0x69, Ad2437());
  bus.StartRunning();
  BenchConsole console(sim);
  Run(console, "board AB0020");
  A2B_CHECK(Has(Run(console, "start"), "40 host script operation(s) (40 before the main node)"));
  // No codec at 0x39 on this simulated host bus: the script's first write fails.
  const std::string events = RunFor(sim, console, 500);
  A2B_CHECK(Has(events, "error: the board's host script failed"));
}

A2B_TEST(BoardNamesTakeAnyCase) {
  // Board names are upper case (AB0020); typed in lower case they still work.
  sim::Simulator sim;
  sim.AddBus(0x68, 0x69, Ad2437());
  BenchConsole console(sim);
  A2B_CHECK(Has(Run(console, "board ab0020"), "Board AB0020, chain 0"));
  A2B_CHECK(Has(Run(console, "status"), "Board: AB0020"));
  A2B_CHECK(Has(Run(console, "module 0 ab0312"), "Sub-node 0: AB0312"));
  A2B_CHECK(Has(Run(console, "module"), "module 0 AB0312"));
}

/** A codec on the host bus that takes every transfer. */
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

A2B_TEST(AbBoardLinksAreUtp) {
  // The AB0020 is UTP on both ports: every link straight, though its export (from the
  // reference board's) sets XCVRBINV.
  sim::Simulator sim;
  sim::Bus& bus = sim.AddBus(0x68, 0x69, Ad2437());
  bus.StartRunning();
  AnyCodec codec;
  sim.AttachHostDevice(0x39, codec);
  PlugBlank(bus, Chip(0x37, 1), false);
  PlugBlank(bus, Chip(0x37, 2), false);
  BenchConsole console(sim);
  Run(console, "board AB0020");
  Run(console, "ignoreEEPROM 1");
  A2B_CHECK(Has(Run(console, "start"), "links utp by default"));
  const std::string events = RunFor(sim, console, 3000);
  A2B_CHECK(Has(events, "sub-node 0: AD2437"));
  A2B_CHECK(Has(events, "sub-node 1: AD2437"));
  A2B_CHECK(Has(events, "link utp (board)"));
  A2B_CHECK(!Has(events, "rj45"));
  A2B_CHECK_EQ(console.Bus().SubNodeCount(), 2);
  A2B_CHECK(Has(Run(console, "status"), "Links: utp by default"));
  // The reference board keeps its export's inversion.
  Run(console, "board ref");
  A2B_CHECK(Has(Run(console, "start"), "links rj45 single by default"));
}

A2B_TEST(ConnectionsFromTheConsole) {
  // The bench's first check: two AB0020 modules, the main node's stereo pair down to
  // sub-node 0 and sub-node 0's back up.
  sim::Simulator sim;
  sim::Bus& bus = sim.AddBus(0x68, 0x69, Ad2437());
  bus.StartRunning();
  AnyCodec codec;
  sim.AttachHostDevice(0x39, codec);
  PlugBlank(bus, Chip(0x37, 1), false);
  PlugBlank(bus, Chip(0x37, 2), false);
  BenchConsole console(sim);
  Run(console, "board AB0020");
  Run(console, "ignoreEEPROM 1");
  A2B_CHECK(Has(Run(console, "listCon"), "No connections"));
  A2B_CHECK(Has(Run(console, "routing"), "No routing written"));
  A2B_CHECK(Has(Run(console, "addCon m:0 0:0"), "Can't connect: a node isn't in the bus"));
  Run(console, "start");
  const std::string events = RunFor(sim, console, 3000);
  A2B_CHECK(Has(events, "    rx G0 0-1, tx G0 0-1"));  // The console's module info.
  A2B_CHECK(Has(Run(console, "groups"),
                "main node (the console's, used at start): rx G0 0-1, "
                "tx G0 0-1"));
  A2B_CHECK(Has(Run(console, "addCon m:0 0:0"), "Connected main node G0 -> sub-node 0 G0"));
  A2B_CHECK(Has(Run(console, "addCon 0:0 -1:0"), "Connected sub-node 0 G0 -> main node G0"));
  A2B_CHECK(Has(Run(console, "addCon 0:0 0:0"), "Can't connect"));
  A2B_CHECK(Has(Run(console, "addCon m:1 1:0"), "Can't connect"));  // No RX group 1.
  A2B_CHECK(Has(Run(console, "addCon m 1:0"), "addCon S:G D:G"));
  const std::string list = Run(console, "listCon");
  A2B_CHECK(Has(list, "G0: 2 channels (stereo), 0-1 -> 0-1"));
  A2B_CHECK(Has(list, "0: main G0 -> chip 000000000001 (sub-node 0) G0"));
  A2B_CHECK(Has(list, "1: chip 000000000001 (sub-node 0) G0 -> main G0"));
  A2B_CHECK(Has(Run(console, "applyRouting"), "written at the next poll"));
  A2B_CHECK(Has(RunFor(sim, console, 100),
                "routing applied: 2 connection(s) routed, main node "
                "RESPCYCS 0x"));
  const std::string routing = Run(console, "routing");
  A2B_CHECK(Has(routing, "main node: DNSLOTS 2 UPSLOTS 2"));
  A2B_CHECK(Has(routing, "sub-node 0: link in 2 down / 2 up"));
  A2B_CHECK(Has(routing, "TXXBAR channel<-location 0<-0, 1<-1"));
  const sim::AudioTrace audio = bus.TraceAudio();
  A2B_CHECK(audio.error.empty());
  A2B_CHECK(audio.Played(0, 0) == sim::Signal::From(-1, 0));
  A2B_CHECK(audio.Played(0, 1) == sim::Signal::From(-1, 1));
  A2B_CHECK(audio.Played(-1, 0) == sim::Signal::From(0, 0));
  A2B_CHECK(audio.Played(-1, 1) == sim::Signal::From(0, 1));
  A2B_CHECK(audio.Played(1, 0) == sim::Signal());
  // Removing and clearing; routed again at applyRouting.
  A2B_CHECK(Has(Run(console, "removeCon 0:0 m:0"), "Removed sub-node 0 G0 -> main node G0"));
  A2B_CHECK(Has(Run(console, "removeCon 0:0 m:0"), "Can't remove"));
  A2B_CHECK(Has(Run(console, "clearAllCon"), "Every connection removed"));
  A2B_CHECK(Has(Run(console, "listCon"), "No connections"));
  Run(console, "applyRouting");
  A2B_CHECK(Has(RunFor(sim, console, 100), "routing applied: 0 connection(s) routed"));
  A2B_CHECK(bus.TraceAudio().Played(0, 0) == sim::Signal());
  // Groups: set, shown, removed.
  A2B_CHECK(Has(Run(console, "group main rx 1 0,2,4-7"),
                "main node: rx G0 0-1, rx G1 0,2,4-7, tx G0 0-1 (at the next start)"));
  A2B_CHECK(Has(Run(console, "group image tx 0 none"),
                "console's module info: rx G0 0-1 (for nodes identified from now on)"));
  A2B_CHECK(Has(Run(console, "group main rx 1 32"), "group main|image rx|tx ID CHANNELS"));
  A2B_CHECK(Has(Run(console, "group sub rx 1 0"), "group main|image rx|tx ID CHANNELS"));
  // Cable lengths: the board's 4 m, one link's own, a routed bus planned again.
  A2B_CHECK(Has(Run(console, "cable"),
                "No cables set: each link's is the image's of the node before it, else the "
                "board's (4.0 m)"));
  A2B_CHECK(Has(Run(console, "addCon m:0 0:0"), "Connected"));
  Run(console, "applyRouting");
  RunFor(sim, console, 100);
  A2B_CHECK(Has(Run(console, "cable 0 12.5"), "Cable into sub-node 0: 12.5 m"));
  A2B_CHECK_EQ(console.Bus().CableLength(0), 125);
  A2B_CHECK(Has(RunFor(sim, console, 100), "routing applied: 1 connection(s) routed"));
  A2B_CHECK(Has(Run(console, "cable"), "cable 0 12.5 m"));
  A2B_CHECK(Has(Run(console, "status"), "Cables: 4.0 m by default; into 0 12.5 m"));
  A2B_CHECK(Has(Run(console, "cable 1 30"), "Cable into sub-node 1: 30.0 m"));
  A2B_CHECK(Has(Run(console, "cable 1 33.5"), "Cable into sub-node 1: 33.5 m"));  // 110 ft
  A2B_CHECK(Has(Run(console, "cable 1 6553.6"), "cable N METRES|default"));
  A2B_CHECK(Has(Run(console, "cable 1 0"), "cable N METRES|default"));
  A2B_CHECK(Has(Run(console, "cable 1 2.55"), "cable N METRES|default"));
  A2B_CHECK(Has(Run(console, "cable 16 4"), "cable N METRES|default"));
  A2B_CHECK(
      Has(Run(console, "cable 1 default"),
          "Cable into sub-node 1: the image's of the node before it, else the board's (4.0 m)"));
  A2B_CHECK(Has(Run(console, "cable all default"),
                "Every cable is the image's of the node before it, else the board's (4.0 m)"));
  A2B_CHECK_EQ(console.Bus().CableLength(0), 40);
}

A2B_TEST(MuteAndRouteCommandsFromTheConsole) {
  // Mute, muteMode and bandwidth, setRoute and importCon, and the mute hooks
  // printed around a plan.
  sim::Simulator sim;
  sim::Bus& bus = sim.AddBus(0x68, 0x69, Ad2437());
  bus.StartRunning();
  AnyCodec codec;
  sim.AttachHostDevice(0x39, codec);
  PlugBlank(bus, Chip(0x37, 1), false);
  PlugBlank(bus, Chip(0x37, 2), false);
  BenchConsole console(sim);
  Run(console, "board AB0020");
  Run(console, "ignoreEEPROM 1");
  A2B_CHECK(Has(Run(console, "mute"), "The bus is not muted"));
  A2B_CHECK(Has(Run(console, "muteMode"), "Routing mute: local"));
  A2B_CHECK(Has(Run(console, "mute loud"), "mute [on|off]"));
  A2B_CHECK(Has(Run(console, "muteMode soft"), "muteMode [local|global|none]"));
  A2B_CHECK(Has(Run(console, "bandwidth"), "No plan built"));
  A2B_CHECK(Has(Run(console, "mute on"), "Bus muted at the next poll (it is written once"));
  A2B_CHECK(Has(Run(console, "muteMode global"), "Routing mute: global (the whole bus)"));
  Run(console, "start");
  RunFor(sim, console, 3000);
  A2B_CHECK(Has(Run(console, "mute"), "The bus is muted"));
  A2B_CHECK(Has(Run(console, "status"), "Mute: bus muted, routing mute global"));
  A2B_CHECK_EQ(bus.Main().Reg(0x11), 0x00);  // DATCTL, written with the image muted.
  // Routed while muted, with the whole bus muted for the plan: the hooks are printed.
  A2B_CHECK(Has(Run(console, "setRoute m:0 0:0"), "1 connection(s) kept"));
  A2B_CHECK(Has(Run(console, "setRoute append 0:0 m:0"), "2 connection(s) kept"));
  A2B_CHECK(Has(Run(console, "setRoute m:0 5:0"), "Can't set the route"));
  A2B_CHECK(Has(Run(console, "setRoute m:0"), "setRoute [append] S:G D:G..."));
  A2B_CHECK(Has(Run(console, "setRoute m:0 m:0"), "Can't set the route"));
  A2B_CHECK(Has(Run(console, "listCon"), "1: chip 000000000001 (sub-node 0) G0 -> main G0"));
  Run(console, "applyRouting");
  const std::string plan = RunFor(sim, console, 100);
  A2B_CHECK(Has(plan, "audio mute: the whole bus"));
  A2B_CHECK(Has(plan, "audio unmute"));
  A2B_CHECK(Has(plan, "routing applied: 2 connection(s) routed"));
  A2B_CHECK(bus.TraceAudio().Played(0, 0) == sim::Signal());  // Still muted by `mute on`.
  A2B_CHECK(Has(Run(console, "bandwidth"), "Busiest: sub-node 0, "));
  A2B_CHECK(Has(Run(console, "routing"), "busiest: sub-node 0, "));
  A2B_CHECK(Has(Run(console, "mute off"), "Bus unmuted at the next poll"));
  RunFor(sim, console, 200);  // Written when the bus is idle: after a probe under way.
  A2B_CHECK(bus.TraceAudio().Played(0, 0) == sim::Signal::From(-1, 0));
  A2B_CHECK(bus.TraceAudio().Played(-1, 1) == sim::Signal::From(0, 1));
  // The local mute names the nodes whose crossbar changes.
  A2B_CHECK(Has(Run(console, "muteMode local"), "Routing mute: local"));
  A2B_CHECK(Has(Run(console, "removeCon m:0 0:0"), "Removed"));
  A2B_CHECK(Has(Run(console, "addCon m:0 1:0"), "Connected"));
  Run(console, "applyRouting");
  A2B_CHECK(Has(RunFor(sim, console, 100), "audio mute: sub-node 0, sub-node 1"));
  // importCon by chip ID: a saved list put back, with a module that isn't there.
  A2B_CHECK(
      Has(Run(console, "importCon m:0 000000000001:0 000000000001:0 m:0"), "2 connection(s) kept"));
  A2B_CHECK(Has(Run(console, "listCon"), "0: main G0 -> chip 000000000001 (sub-node 0) G0"));
  A2B_CHECK(Has(Run(console, "importCon m:0 000000000099:0"), "1 connection(s) kept"));
  A2B_CHECK(Has(Run(console, "listCon"), "chip 000000000099 (not in the bus) G0"));
  A2B_CHECK(Has(Run(console, "importCon m:0 zz:0"), "importCon S:G D:G [S:G D:G...]"));
  A2B_CHECK(Has(Run(console, "importCon m:0"), "importCon S:G D:G [S:G D:G...]"));
  A2B_CHECK(Has(Run(console, "importCon m:0 m:0"), "Can't import: a connection joins a node"));
  A2B_CHECK(Has(Run(console, "importCon"), "0 connection(s) kept"));
  A2B_CHECK(Has(Run(console, "listCon"), "No connections"));
}

A2B_TEST(LocalMuteInAMixedChainFromTheConsole) {
  // An AD2437 sends its channel 1 to an AD2428 (the console's image with an RX
  // group of that one channel). Until its RXMASK is written, the AD2437's first RX frame
  // buffer location holds channel 0, which the AD2428 would play, and it has no crossbar to
  // mute: the local mute mutes the whole bus, and the console says why.
  sim::Simulator sim;
  sim::Bus& bus = sim.AddBus(0x68, 0x69, Ad2437());
  bus.StartRunning();
  AnyCodec codec;
  sim.AttachHostDevice(0x39, codec);
  PlugBlank(bus, Chip(0x37, 1), false);
  PlugBlank(bus, Chip(0x28, 2), false);
  BenchConsole console(sim);
  Run(console, "board AB0020");
  Run(console, "ignoreEEPROM 1");
  Run(console, "group image rx 1 1");
  Run(console, "start");
  RunFor(sim, console, 3000);
  A2B_CHECK(Has(Run(console, "addCon 0:1 1:0"), "Connected"));
  Run(console, "applyRouting");
  const std::string plan = RunFor(sim, console, 100);
  A2B_CHECK(Has(plan,
                "audio mute: the whole bus (local: a node that would play a wrong channel can't be "
                "muted alone)"));
  A2B_CHECK(Has(plan, "routing applied: 1 connection(s) routed"));
  A2B_CHECK(bus.TraceAudio().Played(1, 0) == sim::Signal::From(0, 1));
}

/** A codec behind a sub-node that keeps every write and when it came. */
class CodecRecorder : public sim::I2cDevice {
 public:
  Result Write(const uint8_t* data, size_t len, uint32_t nowMs) override {
    writes.emplace_back(data, data + len);
    times.push_back(nowMs);
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
  std::vector<uint32_t> times;               ///< When each came.
};

A2B_TEST(Ab0310WithAnAb0312AndUnknownNodes) {
  // The AB0310 and AB0312 bench: the AB0310 makes its own SYNC and BCLK, so nothing on the
  // host bus is programmed; every link is RJ45 (the export sets XCVRBINV); an AB0312 at
  // sub-node 0 (no EEPROM, no codec), two AD2437 sub-nodes the console knows nothing of
  // after it; the main node's stereo pair down to the AB0312 and its pair back up.
  sim::Simulator sim;
  sim::Bus& bus = sim.AddBus(0x68, 0x69, Ad2437());
  bus.StartRunning();
  PlugBlank(bus, Chip(0x37, 1), true);
  PlugBlank(bus, Chip(0x37, 2), true, sim::LinkPower::kTwoStep);
  PlugBlank(bus, Chip(0x37, 3), true, sim::LinkPower::kTwoStep);
  BenchConsole console(sim);
  A2B_CHECK(Has(Run(console, "board"), "AB0310"));
  Run(console, "board AB0310");
  Run(console, "ignoreEEPROM 1");
  A2B_CHECK(Has(Run(console, "module"), "AB0312   AB0312: I2S on a header"));
  A2B_CHECK(Has(Run(console, "module 0 AB0312 file.xml"), "module N|all AB0020|AB0312"));
  A2B_CHECK(Has(Run(console, "module 0 AB0312"), "Sub-node 0: AB0312"));
  A2B_CHECK(Has(Run(console, "start"),
                "0 host script operation(s) (0 before the main node), links rj45 single by "
                "default"));
  // The AB0312's B port is ADI's RJ45: on an AD2437, two-step. The console's image for the
  // node after it gives UTP, not inverted, so nothing answers over the RJ45 cable after it.
  std::string events = RunFor(sim, console, 3000);
  A2B_CHECK_EQ(console.Bus().SubNodeCount(), 2);
  A2B_CHECK(Has(events, "    image for External; B port RJ45, its cable not given\n"));
  A2B_CHECK(bus.Sub(1).HighVoltage());
  // Every link the board's (RJ45, single-step).
  Run(console, "link all board");
  Run(console, "start");
  events = RunFor(sim, console, 3000);
  A2B_CHECK_EQ(console.Bus().SubNodeCount(), 3);
  A2B_CHECK(Has(events, "link rj45 single (board)\n"));
  A2B_CHECK(!Has(events, "link utp"));
  A2B_CHECK(Has(events, "    AB0312\n"));
  A2B_CHECK(Has(Run(console, "status"), "Boards: sub-node 0 AB0312"));
  // The AB0312's registers, as its project sets them; the others the console's image.
  A2B_CHECK_EQ(bus.Sub(0).Reg(0x41), 0xE0);
  A2B_CHECK_EQ(bus.Sub(0).Reg(0x42), 0x11);
  A2B_CHECK_EQ(bus.Sub(0).Reg(0x5A), 0x81);
  A2B_CHECK_EQ(bus.Sub(0).Reg(0xB0), 0x02);
  A2B_CHECK_EQ(bus.Sub(1).Reg(0x42), 0x11);  // The generic image's I2S port.
  Run(console, "addCon m:0 0:0");
  Run(console, "addCon 0:0 m:0");
  Run(console, "applyRouting");
  A2B_CHECK(Has(RunFor(sim, console, 100), "routing applied: 2 connection(s) routed"));
  const sim::AudioTrace audio = bus.TraceAudio();
  A2B_CHECK(audio.error.empty() && audio.sub.size() == 3);
  if (audio.sub.size() == 3) {
    A2B_CHECK(audio.Played(0, 0) == sim::Signal::From(-1, 0));
    A2B_CHECK(audio.Played(0, 1) == sim::Signal::From(-1, 1));
    A2B_CHECK(audio.Played(-1, 0) == sim::Signal::From(0, 0));
    A2B_CHECK(audio.Played(-1, 1) == sim::Signal::From(0, 1));
  }
}

A2B_TEST(Ab0020ModuleFromTheConsole) {
  // `module all AB0020`: an AB0020 at sub-node 0 (no EEPROM) gets the bench project's
  // settings, its groups and its built-in ADAU1761 program at set-up; an AD2427 at sub-node 1
  // (no AB0020 has one) reads its EEPROM instead.
  const std::string file =
      A2B_REPO_ROOT "/tools/SS+reference/2x_AB0020_EVM_with_2437/AB0020-sub-node-ADAU1761.xml";
  SigmaProgram program;
  std::string error;
  A2B_CHECK(ReadSigmaXml(file, 0x39, program, error));
  sim::Simulator sim;
  sim::Bus& bus = sim.AddBus(0x68, 0x69, Ad2437());
  bus.StartRunning();
  AnyCodec hostCodec;
  sim.AttachHostDevice(0x39, hostCodec);
  CodecRecorder codec;
  PlugBlank(bus, Chip(0x37, 1), false);
  bus.Sub(0).AttachDevice(0x39, codec);
  sim::SubNodeSpec spec;
  spec.chip = Chip(0x27, 2);
  bus.Plug(spec).AddEeprom(0x50).Load(sim::MakeModuleInfoImage(sim::ModuleInfoHeader()));
  BenchConsole console(sim);
  Run(console, "board AB0020");
  const std::string none = Run(console, "module");
  A2B_CHECK(Has(none, "No boards or images set"));
  A2B_CHECK(Has(none, "AB0020   AB0020: ADAU1761"));
  A2B_CHECK(Has(Run(console, "module 0 AB0020 /no/such/file.xml"),
                "Can't read the codec program: can't open /no/such/file.xml"));
  A2B_CHECK(Has(Run(console, "module 16 AB0020"), "module N|all AB0020|AB0312|AB0204 [FILE]"));
  A2B_CHECK(Has(Run(console, "module 0 ab0021"), "module N|all AB0020|AB0312|AB0204 [FILE]"));
  // A file in place of the built-in program: the same program here.
  A2B_CHECK(Has(Run(console, ("module 0 AB0020 " + file).c_str()),
                "Sub-node 0: AB0020, ADAU1761 program 38 writes, 6076 bytes, 1 wait(s)"));
  A2B_CHECK(Has(Run(console, "module"), ("module 0 AB0020, ADAU1761 program " + file).c_str()));
  const std::string set = Run(console, "module all AB0020");
  A2B_CHECK(Has(set, "Every sub-node: AB0020, ADAU1761 program 38 writes, 6076 bytes, 1 wait(s)"));
  A2B_CHECK(Has(set, "1 read action(s) skipped"));
  A2B_CHECK(Has(Run(console, "module"), "module 0 AB0020, ADAU1761 program the board's default"));
  Run(console, "start");
  const std::string events = RunFor(sim, console, 3000);
  A2B_CHECK_EQ(console.Bus().SubNodeCount(), 2);
  A2B_CHECK(Has(events, "AB0020: ADAU1761 programmed from the board's default (38 writes)"));
  A2B_CHECK(Has(events, "1 peripheral block(s)"));
  A2B_CHECK(Has(events, "    rx G0 0-1, tx G0 0-1"));
  A2B_CHECK(Has(events, "set as AB0020, which isn't made with an AD2427"));
  A2B_CHECK(Has(Run(console, "status"), "Boards: sub-node 0 AB0020, 1 AB0020, 2 AB0020"));
  // The AB0020's registers, as the bench project sets them.
  A2B_CHECK_EQ(bus.Sub(0).Reg(0x41), 0xE0);
  A2B_CHECK_EQ(bus.Sub(0).Reg(0x42), 0x11);
  A2B_CHECK_EQ(bus.Sub(0).Reg(0x4A), 0x80);
  A2B_CHECK_EQ(bus.Sub(0).Reg(0x4D), 0x80);
  A2B_CHECK_EQ(bus.Sub(0).Reg(0x52), 0x01);
  A2B_CHECK_EQ(bus.Sub(0).Reg(0x5A), 0x81);
  A2B_CHECK_EQ(bus.Sub(0).Reg(0xB0), 0x02);  // SPI off, or CLKOUT2 doesn't reach ADR2.
  A2B_CHECK_EQ(bus.Sub(1).Reg(0x42), 0x00);  // The AD2427's EEPROM image has none.
  // Its codec got the program, write for write, with the 100 ms wait after the fourth.
  A2B_CHECK_EQ(codec.writes.size(), 38u);
  size_t write = 0;
  bool same = true;
  for (const SigmaAction& action : program.actions) {
    if (!action.wait) {
      same = same && write < codec.writes.size() && codec.writes[write] == action.bytes;
      ++write;
    }
  }
  A2B_CHECK(same);
  A2B_CHECK(codec.times.size() > 4 && codec.times[4] - codec.times[3] >= 100);
  // The AD2427 has no crossbar; in no connection, it doesn't stop the routing.
  Run(console, "addCon m:0 0:0");
  Run(console, "addCon 0:0 m:0");
  A2B_CHECK(Has(Run(console, "applyRouting"), "written at the next poll"));
  A2B_CHECK(Has(RunFor(sim, console, 100), "routing applied: 2 connection(s) routed"));
  // The AB0020 alone, plugged in again: programmed again with nothing typed, and routed.
  bus.Unplug(0);
  RunFor(sim, console, 1500);
  A2B_CHECK_EQ(console.Bus().SubNodeCount(), 0);
  CodecRecorder again;
  PlugBlank(bus, Chip(0x37, 1), false);
  bus.Sub(0).AttachDevice(0x39, again);
  A2B_CHECK(Has(RunFor(sim, console, 3000), "routing applied: 2 connection(s) routed"));
  A2B_CHECK_EQ(again.writes.size(), 38u);
  A2B_CHECK_EQ(bus.Sub(0).Reg(0x42), 0x11);
  const sim::AudioTrace audio = bus.TraceAudio();
  A2B_CHECK(audio.error.empty() && audio.sub.size() == 1);
  if (audio.sub.size() == 1) {
    A2B_CHECK(audio.Played(0, 0) == sim::Signal::From(-1, 0));
    A2B_CHECK(audio.Played(-1, 1) == sim::Signal::From(0, 1));
  }
  A2B_CHECK(Has(Run(console, "module all default"), "Every sub-node: no board or image set"));
  A2B_CHECK(Has(Run(console, "module"), "No boards or images set"));
}

A2B_TEST(LastNodeOpenLineInterruptsHidden) {
  // An AD242x at the end of the chain reports its open B side (INTTYPE 12) when a probe
  // closes its switch: hidden by default, counted in status, shown with `interrupts all`.
  sim::Simulator sim;
  sim::Bus& bus = sim.AddBus(0x68, 0x69, Ad2437());
  bus.StartRunning();
  PlugBlank(bus, Chip(0x28, 1), true);
  PlugBlank(bus, Chip(0x28, 2), true);
  BenchConsole console(sim);
  Run(console, "board ref");
  Run(console, "link all board");
  Run(console, "ignoreEEPROM 1");
  Run(console, "start");
  RunFor(sim, console, 2000);
  A2B_CHECK_EQ(console.Bus().SubNodeCount(), 2);
  bus.RaiseInterrupt(1, 12);
  // Hidden, with a hint the first time: nothing plugged in, or a node on its own supply.
  const std::string first = RunFor(sim, console, 100);
  A2B_CHECK(!Has(first, "interrupt from sub-node 1"));
  A2B_CHECK(
      Has(first, "  hint: sub-node 1 reports an open line (interrupt type 12) toward position 2"));
  A2B_CHECK(Has(first, "for that, link 2 custom"));
  bus.RaiseInterrupt(1, 13);
  const std::string second = RunFor(sim, console, 100);
  A2B_CHECK(!Has(second, "interrupt from sub-node 1") && !Has(second, "hint:"));
  A2B_CHECK(Has(Run(console, "status"), "Interrupts: quiet, 2 open-line interrupt(s)"));
  // Not the last node, or another type: printed.
  bus.RaiseInterrupt(0, 12);
  A2B_CHECK(Has(RunFor(sim, console, 100), "interrupt from sub-node 0: type 12"));
  bus.RaiseInterrupt(1, 9);
  A2B_CHECK(Has(RunFor(sim, console, 100), "interrupt from sub-node 1: type 9"));
  A2B_CHECK(Has(Run(console, "interrupts all"), "all shown"));
  bus.RaiseInterrupt(1, 12);
  A2B_CHECK(Has(RunFor(sim, console, 100), "interrupt from sub-node 1: type 12"));
  A2B_CHECK(Has(Run(console, "interrupts quiet"), "quiet"));
  A2B_CHECK(Has(Run(console, "interrupts loud"), "interrupts quiet|all"));
}

A2B_TEST(WrongMainNodeIsReported) {
  sim::Simulator sim;
  sim::ChipSpec main;
  main.productId = 0x28;
  sim.AddBus(0x68, 0x69, main).StartRunning();
  BenchConsole console(sim);
  Run(console, "board ref");
  Run(console, "start");
  const std::string failed = RunFor(sim, console, 200);
  A2B_CHECK(Has(failed, "error: the main node is not the board's chip"));
  A2B_CHECK(Has(failed, "the main node is an AD2428, the board's export is for an AD2437"));
}

A2B_TEST(Ab0020TakesTheExportForTheMainNodesChip) {
  // The AB0020 has an export for its AD2437 module and one for its AD2428 module: `start`
  // reads the main node's VENDOR and PRODUCT and takes the export for that chip.
  for (const uint8_t product : {uint8_t{0x37}, uint8_t{0x28}}) {
    sim::Simulator sim;
    sim::ChipSpec main = (product == 0x37) ? Ad2437() : sim::ChipSpec();
    main.productId = product;
    sim::Bus& bus = sim.AddBus(0x68, 0x69, main);
    bus.StartRunning();
    AnyCodec codec;
    sim.AttachHostDevice(0x39, codec);
    PlugBlank(bus, Chip(0x28, 1), false);
    BenchConsole console(sim);
    Run(console, "board AB0020");
    Run(console, "ignoreEEPROM 1");
    const std::string started = Run(console, "start");
    char found[64];
    std::snprintf(found, sizeof(found), "Main node AD24%02X found: board AB0020's export for it",
                  product);
    char importing[32];
    std::snprintf(importing, sizeof(importing), "Main node AD24%02X at 0x68", product);
    A2B_CHECK(Has(started, found) && Has(started, importing));
    RunFor(sim, console, 3000);
    A2B_CHECK_EQ(console.Bus().SubNodeCount(), 1);
    A2B_CHECK(console.Bus().State() == a2b::NetworkState::kRunning);
  }
}

A2B_TEST(Ab0020RefusesAChipItHasNoExportFor) {
  // An AD2433 main node: the AB0020 has no export for it, so nothing is started.
  sim::Simulator sim;
  sim::ChipSpec main = Ad2437();
  main.productId = 0x33;
  sim.AddBus(0x68, 0x69, main).StartRunning();
  BenchConsole console(sim);
  Run(console, "board AB0020");
  A2B_CHECK(Has(Run(console, "start"),
                "The main node is an AD2433: board AB0020 has exports for the AD2437 or AD2428 "
                "only (board FILE loads another)"));
  RunFor(sim, console, 200);
  A2B_CHECK(console.Bus().State() == a2b::NetworkState::kStopped);
  // A main node that doesn't answer gets the board's default export (the stack then reports).
  sim::Simulator quiet;
  BenchConsole silent(quiet);
  Run(silent, "board AB0020");
  A2B_CHECK(Has(Run(silent, "start"),
                "The main node at 0x68 didn't answer: board AB0020's AD2437 export"));
}

A2B_TEST(BuiltInExportsAreForTheirChips) {
  // Each built-in board's exports are listed with their main node's chip; the import agrees.
  for (const BenchConsole::BuiltInBoard& board : BenchConsole::BuiltInBoards()) {
    A2B_CHECK(!board.exports.empty());
    for (const BenchConsole::BoardExport& each : board.exports) {
      a2b::BoardConfig config;
      a2b::FixedOpList<256> script;
      A2B_CHECK(a2b::ImportSsPlus(*each.bus, 0, config, script) == a2b::Result::kOk);
      A2B_CHECK_EQ(config.mainProduct, each.mainProduct);
    }
  }
}

A2B_TEST(Ad2428MainFromAFileWithMixedSubNodes) {
  // The bench's second system: an AD2428 main node with AD2428 and AD2427 sub-nodes with
  // blank EEPROMs, then an AD2437 at the end; the board loaded from its export's file.
  const std::string path = WriteAd2428Export();
  sim::Simulator sim;
  sim::ChipSpec main;
  main.productId = 0x28;
  sim::Bus& bus = sim.AddBus(0x68, 0x69, main);
  bus.StartRunning();
  PlugBlank(bus, Chip(0x28, 1), true);
  PlugBlank(bus, Chip(0x27, 2), true);
  PlugBlank(bus, Chip(0x27, 3), true);
  BenchConsole console(sim);
  const std::string loaded = Run(console, ("board \"" + path + "\"").c_str());
  A2B_CHECK(Has(loaded, "chain 0 (1 chain(s) in the export)"));
  A2B_CHECK(!Has(loaded, "warning"));
  Run(console, "link all board");
  A2B_CHECK(Has(Run(console, "ignoreEEPROM 1"), "EEPROMs ignored"));
  A2B_CHECK(Has(Run(console, "start"), "Main node AD2428 at 0x68"));
  std::string events = RunFor(sim, console, 3000);
  A2B_CHECK(Has(events, "sub-node 0: AD2428"));
  A2B_CHECK(Has(events,
                "sub-node 2: AD2427 version 0x21, chip ID 000000000003, module "
                "0C54:0204, CFG-0"));
  A2B_CHECK_EQ(console.Bus().SubNodeCount(), 3);

  // An AD2437 plugged in at the end: the console's image gives it CFG-4, as it detects.
  PlugBlank(bus, Chip(0x37, 4), true);
  events = RunFor(sim, console, 1500);
  A2B_CHECK(Has(events, "sub-node 3: AD2437"));
  A2B_CHECK(Has(events, "CFG-4"));
  A2B_CHECK_EQ(console.Bus().SubNodeCount(), 4);
  A2B_CHECK_EQ(bus.Sub(3).PowerConfig(), 4);
  A2B_CHECK_EQ(sim.UnlockedTransfers(), 0);
  std::remove(path.c_str());
}

A2B_TEST(Ad2427WithoutEepromIsFourMicrophones) {
  // The AD2428 bench: an AD2428 main node (the reference export made AD2428), an
  // AB0020 with an AD2428 at sub-node 0 (`module 0 AB0020`), an AB0204 at sub-node 1: an
  // AD2427 with four PDM microphones and no EEPROM, so the AB0204 default. Then an AD2427
  // with an EEPROM image, set up from it; one with a blank EEPROM, the default again; and an
  // AD2428 with no EEPROM: rejected (it has no default).
  const std::string path = WriteAd2428Export(true);
  sim::Simulator sim;
  sim::ChipSpec main;
  main.productId = 0x28;
  sim::Bus& bus = sim.AddBus(0x68, 0x69, main);
  bus.StartRunning();
  AnyCodec hostCodec;
  sim.AttachHostDevice(0x39, hostCodec);
  AnyCodec codec;
  PlugBlank(bus, Chip(0x28, 1), true);
  bus.Sub(0).AttachDevice(0x39, codec);
  PlugBlank(bus, Chip(0x27, 2), true);
  sim::SubNodeSpec spec;
  spec.chip = Chip(0x27, 3);
  spec.link.bSideInverted = true;
  sim::ModuleInfoRegs regs;
  regs.pdmctl = 0x05;  // One microphone on each PDM pin: not the AB0204's.
  bus.Plug(spec).AddEeprom(0x50).Load(sim::MakeModuleInfoImage(sim::ModuleInfoHeader(), regs));
  spec.chip = Chip(0x27, 4);
  bus.Plug(spec).AddEeprom(0x50).Load(std::vector<uint8_t>(256, 0xFF));
  PlugBlank(bus, Chip(0x28, 5), true);
  BenchConsole console(sim);
  A2B_CHECK(!Has(Run(console, ("board \"" + path + "\"").c_str()), "warning"));
  Run(console, "link all board");
  A2B_CHECK(Has(Run(console, "module"), "AB0204   AB0204: four PDM mics"));
  A2B_CHECK(Has(Run(console, "module 0 AB0020"), "Sub-node 0: AB0020"));
  Run(console, "start");
  const std::string events = RunFor(sim, console, 4000);
  A2B_CHECK_EQ(console.Bus().SubNodeCount(), 4);
  A2B_CHECK(console.Bus().LastRejection() == a2b::RejectReason::kNoModuleInfo);
  A2B_CHECK(Has(events,
                "    rx G0 0-3, rx G1 0-1, rx G2 2-3\n    image for AD2427; B port UTP, its cable "
                "not given\n    AB0204 (no EEPROM: the AD2427's "
                "default)\n  sub-node 2"));
  // The AB0204's PDM settings; I2SGCFG stays TDM2 32-bit, so BCLK is 64 x SYNC (3.072 MHz).
  A2B_CHECK_EQ(bus.Sub(1).Reg(0x47), 0x1F);
  A2B_CHECK_EQ(bus.Sub(1).Reg(0x41), 0x00);
  A2B_CHECK_EQ(bus.Sub(1).Reg(0x42), 0x00);
  A2B_CHECK_EQ(bus.Sub(1).Reg(0x5D), 0x00);  // PDMCTL2: the clock on BCLK.
  A2B_CHECK_EQ(bus.Sub(2).Reg(0x47), 0x05);  // Its EEPROM's.
  A2B_CHECK_EQ(bus.Sub(3).Reg(0x47), 0x1F);  // Blank: the default.
  // The main node's stereo pair to the AB0020's codec, the AB0204's DRX1 pair up to the
  // main node's TX 0-1.
  Run(console, "addCon m:0 0:0");
  A2B_CHECK(Has(Run(console, "addCon 1:2 m:0"), "Connected sub-node 1 G2 -> main node G0"));
  A2B_CHECK(Has(Run(console, "listCon"), "G2 -> main G0: 2 channels (stereo), 2-3 -> 0-1"));
  A2B_CHECK(Has(Run(console, "applyRouting"), "written at the next poll"));
  A2B_CHECK(Has(RunFor(sim, console, 100), "routing applied: 2 connection(s) routed"));
  const std::string routing = Run(console, "routing");
  A2B_CHECK(Has(routing, "main node: DNSLOTS 2 UPSLOTS 2"));
  A2B_CHECK(Has(routing, "no crossbar (AD2428): RXMASK and TXXBAR not written"));
  A2B_CHECK(Has(routing, "no crossbar (AD2427): RXMASK and TXXBAR not written"));
  A2B_CHECK(!Has(routing, "TXXBAR channel<-location"));
  sim::AudioTrace audio = bus.TraceAudio();
  A2B_CHECK(audio.error.empty());
  A2B_CHECK(audio.Played(0, 0) == sim::Signal::From(-1, 0));
  A2B_CHECK(audio.Played(0, 1) == sim::Signal::From(-1, 1));
  A2B_CHECK(audio.Played(-1, 0) == sim::Signal::From(1, 2));
  A2B_CHECK(audio.Played(-1, 1) == sim::Signal::From(1, 3));
  // All four to the main node's two-channel group: the lowest two, DRX0's pair.
  Run(console, "addCon 1:0 m:0");
  A2B_CHECK(Has(Run(console, "listCon"),
                "G0 -> main G0: 2 channels (stereo), 0-1 -> 0-1; source channel(s) 2-3 not sent"));
  Run(console, "applyRouting");
  A2B_CHECK(Has(RunFor(sim, console, 100), "routing applied: 2 connection(s) routed"));
  audio = bus.TraceAudio();
  A2B_CHECK(audio.error.empty());
  A2B_CHECK(audio.Played(-1, 0) == sim::Signal::From(1, 0));
  A2B_CHECK(audio.Played(-1, 1) == sim::Signal::From(1, 1));
  std::remove(path.c_str());
}

A2B_TEST(IsolatedAd2437BehindAnAd2428WithLinkCustom) {
  // The bench's mixed chain: AD2428 main node, AD2428 sub-node, then an AD2437 on its own
  // supply behind a galvanically isolated A side. The board's link closes the AD2428's
  // switch, which never finishes closing with no bias current: not found. `link 1 custom`
  // sets SWBYP on the AD2428 instead (and a straight link), and the AD2437 is found.
  const std::string path = WriteAd2428Export();
  sim::Simulator sim;
  sim::ChipSpec main;
  main.productId = 0x28;
  sim::Bus& bus = sim.AddBus(0x68, 0x69, main);
  bus.StartRunning();
  sim::SubNodeSpec sub;
  sub.chip = Chip(0x28, 1);
  sub.link.power = sim::LinkPower::kSeparateWires;
  sub.link.bSideInverted = true;  // The export's link (XCVRBINV).
  bus.Plug(sub);
  sim::SubNodeSpec isolated;
  isolated.chip = Chip(0x37, 2);
  isolated.link.power = sim::LinkPower::kSeparateWires;
  isolated.link.isolated = true;
  bus.Plug(isolated);
  BenchConsole console(sim);
  Run(console, ("board \"" + path + "\"").c_str());
  Run(console, "ignoreEEPROM 1");
  Run(console, "start");
  RunFor(sim, console, 2000);
  A2B_CHECK_EQ(console.Bus().SubNodeCount(), 1);
  // On the bench the AD2428 reports an open line at each probe (the simulator doesn't raise
  // it by itself): the console's hint names the fix.
  bus.RaiseInterrupt(0, 12);
  A2B_CHECK(Has(RunFor(sim, console, 100), "for that, link 1 custom"));
  A2B_CHECK(Has(Run(console, "link 1 custom"), "Link into sub-node 1: custom"));
  const std::string events = RunFor(sim, console, 1500);
  A2B_CHECK(Has(events, "sub-node 1: AD2437"));
  A2B_CHECK(Has(events, "link custom\n"));
  A2B_CHECK_EQ(console.Bus().SubNodeCount(), 2);
  std::remove(path.c_str());
}

A2B_TEST(LinkProfilesPerPosition) {
  // Sub-node 1's link is straight (UTP), the others inverted. With every link the board's
  // (rj45), sub-node 1 is found only with `link 1 utp`.
  sim::Simulator sim;
  sim::Bus& bus = sim.AddBus(0x68, 0x69, Ad2437());
  bus.StartRunning();
  PlugBlank(bus, Chip(0x37, 1), true);
  PlugBlank(bus, Chip(0x37, 2), false);
  BenchConsole console(sim);
  Run(console, "board ref");
  Run(console, "link all board");
  Run(console, "ignoreEEPROM on");
  Run(console, "start");
  RunFor(sim, console, 2000);
  A2B_CHECK_EQ(console.Bus().SubNodeCount(), 1);
  A2B_CHECK(Has(Run(console, "link 1 utp"), "Link into sub-node 1: utp"));
  A2B_CHECK(Has(RunFor(sim, console, 1500), "sub-node 1: AD2437"));
  A2B_CHECK_EQ(console.Bus().SubNodeCount(), 2);
  A2B_CHECK(Has(Run(console, "link"), "link 1 utp"));
  A2B_CHECK(Has(Run(console, "status"), "into 1 utp"));
  A2B_CHECK(Has(Run(console, "link 4 custom inverted"), "Link into sub-node 4: custom inverted"));
  A2B_CHECK(Has(Run(console, "link 5 rj45"), "Link into sub-node 5: rj45\n"));
  A2B_CHECK(Has(Run(console, "link 5 rj45 single"), "Link into sub-node 5: rj45 single\n"));
  A2B_CHECK(Has(Run(console, "link 5 xlr"), "Link into sub-node 5: xlr\n"));
  A2B_CHECK(Has(Run(console, "link 5 utp two-step"), "Link into sub-node 5: utp two-step\n"));
  A2B_CHECK(Has(Run(console, "link 5 utp vmtr"), "Link into sub-node 5: utp vmtr\n"));
  A2B_CHECK(Has(Run(console, "link 5 utp inverted"), "link N utp [two-step|vmtr]|rj45 [single]"));
  A2B_CHECK(Has(Run(console, "link 5 rj45 two-step"), "link N utp [two-step|vmtr]|rj45"));
  A2B_CHECK(Has(Run(console, "link 5 inverted"), "link N utp [two-step|vmtr]|rj45 [single]"));
  A2B_CHECK(Has(Run(console, "link 4 default"), "Link into sub-node 4: the board's"));
  A2B_CHECK(Has(Run(console, "link 0 default"), "Link into sub-node 0: the board's"));
  A2B_CHECK(Has(Run(console, "link all default"), "Every link: into sub-node 0 the board's"));
  A2B_CHECK(Has(Run(console, "link"), "No links set"));

  // `start` again with no links set: sub-node 0's image (the generic one) gives UTP, so
  // sub-node 1 is found from the start.
  Run(console, "start");
  RunFor(sim, console, 2000);
  A2B_CHECK_EQ(console.Bus().SubNodeCount(), 2);
  A2B_CHECK(Has(Run(console, "status"), "utp (sub-node 0's UTP port)"));
  // A new board stops the bus.
  A2B_CHECK(Has(Run(console, "board AB0020"), "Bus stopped for the new board"));
}

A2B_TEST(TraceToAFile) {
  sim::Simulator sim;
  sim::Bus& bus = sim.AddBus(0x68, 0x69, Ad2437());
  bus.StartRunning();
  BenchConsole console(sim);
  const std::string path = A2B_TEST_OUT_DIR "/bench_trace.txt";
  A2B_CHECK(Has(Run(console, ("i2cTrace on " + path).c_str()), "Tracing I2C to"));
  Run(console, "board ref");
  Run(console, "start");
  RunFor(sim, console, 300);
  A2B_CHECK(Has(Run(console, "i2cTrace"), "transfer(s)"));
  const std::string closed = Run(console, "i2cTrace off");
  A2B_CHECK(Has(closed, "closed"));
  A2B_CHECK(Has(Run(console, "i2cTrace status"), "Not tracing"));
  std::FILE* in = std::fopen(path.c_str(), "rb");
  A2B_CHECK(in != nullptr);
  if (in != nullptr) {
    char line[256] = {};
    A2B_CHECK(std::fgets(line, sizeof(line), in) != nullptr);
    // The first transfer: the main node's VENDOR, PRODUCT and VERSION (from 0x02).
    A2B_CHECK(std::string(line).find("I2CT,0,") == 0);
    A2B_CHECK(Has(line, ",0,68,WR,0,1,02,3,AD3721\n"));
    std::fclose(in);
  }
  std::remove(path.c_str());
  A2B_CHECK(Has(Run(console, "i2cTrace on /no/such/dir/trace.txt"), "Can't write"));
}

/** A console on the reference board with @p count AD2437 sub-nodes up (EEPROMs ignored). */
struct ConsoleBench {
  sim::Simulator sim;
  sim::Bus& bus;
  BenchConsole console;

  explicit ConsoleBench(int count) : bus(sim.AddBus(0x68, 0x69, Ad2437())), console(sim) {
    bus.StartRunning();
    for (int i = 0; i < count; ++i) {
      PlugBlank(bus, Chip(0x37, static_cast<uint8_t>(i + 1)), true);
    }
    Run(console, "board ref");
    Run(console, "link all board");
    Run(console, "ignoreEEPROM 1");
    Run(console, "start");
    RunFor(sim, console, 2000);
    A2B_CHECK_EQ(console.Bus().SubNodeCount(), static_cast<size_t>(count));
  }
};

A2B_TEST(RegistersFromTheConsole) {
  ConsoleBench bench(2);
  BenchConsole& console = bench.console;
  A2B_CHECK(Has(Run(console, "rdm 0x02 3"), "main node 0x02: AD 37 21\n"));
  A2B_CHECK(Has(Run(console, "rds 1 0x6F"), "sub-node 1 0x6F: 02\n"));
  A2B_CHECK(Has(Run(console, "wrs 1 0xC0 1 2 3"), "Wrote sub-node 1 0xC0: 01 02 03\n"));
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(0xC0), 0x01);
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(0xC2), 0x03);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(0xC0), 0x00);
  A2B_CHECK(Has(Run(console, "wrm 0x4A 0x05"), "Wrote main node 0x4A: 05\n"));
  A2B_CHECK_EQ(bench.bus.Main().Reg(0x4A), 0x05);
  // Sixteen to a line; the TX crossbar's reset values are 0x00-0x1F.
  const std::string dump = Run(console, "rds 1 0xC0 20");
  A2B_CHECK(Has(dump, "sub-node 1 0xC0: 01 02 03 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F\n"));
  A2B_CHECK(Has(dump, "sub-node 1 0xD0: 10 11 12 13\n"));
  // Page 1 on the AD2437.
  A2B_CHECK(Has(Run(console, "rds 0 0x100"), "sub-node 0 0x100: 00\n"));

  A2B_CHECK(Has(Run(console, "rds 2 0x02"), "Can't read sub-node 2: sub-node 2 is not in the bus"));
  A2B_CHECK(Has(Run(console, "wrm 0xE0 1"), "Can't write the main node: invalid argument"));
  A2B_CHECK(Has(Run(console, "rdm 0xF8 16"), "past the end of the register page"));
  A2B_CHECK(Has(Run(console, "rdm"), "rdm REG [COUNT]"));
  A2B_CHECK(Has(Run(console, "rdm 0x200"), "rdm REG [COUNT]"));
  A2B_CHECK(Has(Run(console, "rdm 0x02 0"), "rdm REG [COUNT]"));
  A2B_CHECK(Has(Run(console, "rdm 0x02 257"), "rdm REG [COUNT]"));
  A2B_CHECK(Has(Run(console, "rds 16 0x02"), "rds N REG [COUNT]"));
  A2B_CHECK(Has(Run(console, "wrs 0 0x10"), "wrs N REG VALUE..."));
  A2B_CHECK(Has(Run(console, "wrm 0x10 0x100"), "wrm REG VALUE..."));
  A2B_CHECK_EQ(console.Bus().SubNodeCount(), 2);
}

A2B_TEST(PeripheralsFromTheConsole) {
  sim::Simulator sim;
  sim::Bus& bus = sim.AddBus(0x68, 0x69, Ad2437());
  bus.StartRunning();
  AnyCodec codec;
  sim.AttachHostDevice(0x39, codec);
  sim::SubNodeSpec spec;
  spec.chip = Chip(0x37, 1);
  spec.link.bSideInverted = true;
  bus.Plug(spec).AddEeprom(0x50).Load({0x5A, 0xA5, 0x01, 0x02});
  BenchConsole console(sim);
  Run(console, "board ref");
  Run(console, "ignoreEEPROM 1");
  Run(console, "start");
  RunFor(sim, console, 2000);
  A2B_CHECK_EQ(console.Bus().SubNodeCount(), 1);

  // A sub-address, then the data.
  A2B_CHECK(Has(Run(console, "periRead 0 0x50 4 0 0"),
                "Peripheral 0x50 of sub-node 0:\n  00: 5A A5 01 02\n"));
  A2B_CHECK(Has(Run(console, "periWrite 0 0x50 0 0x10 0xEE 0xEF"),
                "Wrote peripheral 0x50 of sub-node 0: 00 10 EE EF\n"));
  RunFor(sim, console, 10);  // The EEPROM's write cycle.
  A2B_CHECK(Has(Run(console, "periRead 0 0x50 2 0 0x10"), "  00: EE EF\n"));
  // A read without a sub-address: the EEPROM's address counter, after the last read.
  A2B_CHECK(Has(Run(console, "periRead 0 0x50 1"), "  00: FF\n"));
  A2B_CHECK(Has(Run(console, "periRead main 0x39 2"),
                "Peripheral 0x39 of the main node:\n  00: 00 00\n"));
  A2B_CHECK(Has(Run(console, "periWrite m 0x39 0x40 0x00"),
                "Wrote peripheral 0x39 of the main node: 40 00\n"));
  const std::string many = Run(console, "periRead 0 0x50 18 0 0");
  A2B_CHECK(Has(many, "  00: 5A A5 01 02 FF FF FF FF FF FF FF FF FF FF FF FF\n"));
  A2B_CHECK(Has(many, "  10: EE EF\n"));

  A2B_CHECK(Has(Run(console, "periRead 0 0x51 1"),
                "Can't read peripheral 0x51 of sub-node 0: I2C error (no answer)"));
  A2B_CHECK(Has(Run(console, "periWrite 3 0x50 0"), "sub-node 3 is not in the bus"));
  A2B_CHECK(Has(Run(console, "periRead x 0x50 1"), "periRead NODE ADDR COUNT"));
  A2B_CHECK(Has(Run(console, "periRead 0 0x80 1"), "periRead NODE ADDR COUNT"));
  A2B_CHECK(Has(Run(console, "periRead 0 0x50 0"), "periRead NODE ADDR COUNT"));
  A2B_CHECK(Has(Run(console, "periWrite 0 0x50"), "periWrite NODE ADDR BYTE..."));
  A2B_CHECK(Has(Run(console, "periWrite 0 0x50 0x100"), "periWrite NODE ADDR BYTE..."));
}

A2B_TEST(PauseResumeAndRejectFromTheConsole) {
  ConsoleBench bench(1);
  BenchConsole& console = bench.console;
  A2B_CHECK(Has(Run(console, "pause"), "Discovery paused"));
  A2B_CHECK(Has(Run(console, "status"), "1 sub-node(s), discovery paused"));
  PlugBlank(bench.bus, Chip(0x37, 2), true);
  RunFor(bench.sim, console, 2000);
  A2B_CHECK_EQ(console.Bus().SubNodeCount(), 1);
  A2B_CHECK(Has(Run(console, "resume"), "Discovery resumed"));
  A2B_CHECK(Has(RunFor(bench.sim, console, 1500), "sub-node 1: AD2437"));
  A2B_CHECK_EQ(console.Bus().SubNodeCount(), 2);

  A2B_CHECK(Has(Run(console, "reject 1"), "Rejecting sub-node 1 and those after it"));
  A2B_CHECK(Has(RunFor(bench.sim, console, 10), "sub-node 1 removed: rejected by the app"));
  A2B_CHECK_EQ(console.Bus().SubNodeCount(), 1);
  A2B_CHECK(Has(Run(console, "status"), "Last rejection: by the app"));
  // Found there again, it is switched off again.
  RunFor(bench.sim, console, 3000);
  A2B_CHECK_EQ(console.Bus().SubNodeCount(), 1);
  A2B_CHECK(Has(Run(console, "reject 5"), "Can't reject: sub-node 5 is not in the bus"));
  A2B_CHECK(Has(Run(console, "reject"), "reject N (N: 0-15)"));
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
