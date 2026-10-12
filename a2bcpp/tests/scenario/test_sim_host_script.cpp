/**
 * @file test_sim_host_script.cpp
 * @brief Simulator scenario 13, host script with delays: the AB0020 codec script from
 *        the Windows SigmaStudio+ export (adi/exports/windows/12_a2b_busconfig.c, linked in),
 *        imported and run by ScriptRunner against a simulated codec, and a script with
 *        waits that writes an EEPROM behind a sub-node.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <string>
#include <vector>

#include "a2b/network.h"
#include "script_runner.h"
#include "sim_steps.h"
#include "ss_plus_import.h"
#include "test_harness.h"

extern "C" {
extern ADI_A2B_BCD sBusDescription;  // From the export.
}

namespace a2b {
namespace {

using sim::Simulator;
using State = ScriptRunner::State;

constexpr uint8_t kMainAddr = 0x68;
constexpr uint8_t kBusAddr = 0x69;
constexpr uint8_t kCodecAddr = 0x39;     ///< The AB0020's ADAU1761.
constexpr uint32_t kCodecStartMs = 100;  ///< The export's delay after the PLL set-up.

/** A codec that records every transfer; reads return zeros. */
class Codec : public sim::I2cDevice {
 public:
  struct Transfer {
    bool read;
    uint32_t timeMs;
    std::vector<uint8_t> bytes;  ///< Written bytes; empty for the read part.
  };

  Result Write(const uint8_t* data, size_t len, uint32_t nowMs) override {
    log.push_back({false, nowMs, std::vector<uint8_t>(data, data + len)});
    return Result::kOk;
  }
  Result Read(uint8_t* data, size_t len, uint32_t nowMs) override {
    for (size_t i = 0; i < len; ++i) {
      data[i] = 0;
    }
    log.push_back({true, nowMs, {}});
    return Result::kOk;
  }

  std::vector<Transfer> log;
};

/** The Windows export's host script. */
struct Imported {
  BoardConfig board;
  FixedOpList<64> script;

  Imported() { A2B_CHECK(ImportSsPlus(sBusDescription, 0, board, script) == Result::kOk); }
};

A2B_TEST(ImportsTheCodecScript) {
  Imported imported;
  const OpList& script = imported.script;
  A2B_CHECK(imported.board.hostScript == &imported.script);
  // 40 units for the codec; the EEPROM at 0x50 is declared with none.
  A2B_CHECK_EQ(script.Size(), 40u);
  // The export configures the codec before the A2B transceiver (bPostDiscCfg off).
  A2B_CHECK_EQ(imported.board.hostOpsBeforeMainNode, 40u);
  A2B_CHECK(script[0].type == OpType::kPeripheralWrite);
  A2B_CHECK_EQ(script[0].node, kMainNode);
  A2B_CHECK_EQ(script[0].i2cAddr, kCodecAddr);
  A2B_CHECK_EQ(script[0].addrWidth, 2);
  A2B_CHECK_EQ(script[0].addr, 0x40EBu);
  A2B_CHECK(script[4].type == OpType::kWait);
  A2B_CHECK_EQ(script[4].addr, kCodecStartMs);
  A2B_CHECK_EQ(script[16].addr, 0x0800u);  // Program memory: 5-byte words.
  A2B_CHECK_EQ(script[16].len, 0x3FC);
  A2B_CHECK_EQ(script[16].addrIncrement, 5);
  A2B_CHECK(script[35].type == OpType::kPeripheralRead);
  A2B_CHECK_EQ(script[35].len, 6);
  // The Windows export has the same main node as the Pi's (checked against G13 in
  // test_ss_plus_import).
  A2B_CHECK_EQ(imported.board.mainProduct, 0x37);
}

A2B_TEST(PostDiscoveryPeripheralsComeLast) {
  // The codec marked for after the transceiver: none before the main node.
  A2B_PERIPHERAL_DEVICE_CONFIG codec = *sBusDescription.sTargetProperties.apPeriConfig[0];
  codec.bPostDiscCfg = A2B_ENABLED;
  ADI_A2B_BCD bcd = sBusDescription;
  bcd.sTargetProperties.apPeriConfig[0] = &codec;
  BoardConfig board;
  FixedOpList<64> script;
  A2B_CHECK(ImportSsPlus(bcd, 0, board, script) == Result::kOk);
  A2B_CHECK_EQ(script.Size(), 40u);
  A2B_CHECK_EQ(board.hostOpsBeforeMainNode, 0u);
}

/** A codec that clocks the main node, as the AB0020's ADAU1761: SYNC starts with the
    first write after its PLL start-up delay. */
class ClockingCodec : public Codec {
 public:
  explicit ClockingCodec(sim::Bus& bus) : m_Bus(bus) {}
  Result Write(const uint8_t* data, size_t len, uint32_t nowMs) override {
    Codec::Write(data, len, nowMs);
    if (log.size() == 5) {  // The four PLL writes, the delay, then this one.
      m_Bus.SetSync(true);
    }
    return Result::kOk;
  }

 private:
  sim::Bus& m_Bus;
};

/** The index among the transfers in @p trace (its I2CT lines; the I2CT-BEGIN header is
    not one) of the first transfer to @p addr, or -1. */
int FirstTo(const std::string& trace, const char* addr) {
  int line = 0;
  size_t at = 0;
  while (at < trace.size()) {
    const size_t end = trace.find('\n', at);
    const std::string text = trace.substr(at, end - at);
    at = (end == std::string::npos) ? trace.size() : end + 1;
    if (text.compare(0, 5, "I2CT,") != 0) {
      continue;
    }
    // I2CT,<seq>,<t_us>,0,<addr>,...
    size_t field = 0;
    size_t pos = 0;
    for (int i = 0; i < 4 && pos != std::string::npos; ++i) {
      pos = text.find(',', pos);
      pos = (pos == std::string::npos) ? pos : pos + 1;
    }
    field = pos;
    if (field != std::string::npos && text.compare(field, 2, addr) == 0) {
      return line;
    }
    ++line;
  }
  return -1;
}

A2B_TEST(CodecProgrammedBeforeTheMainNode) {
  // The AB0020: no SYNC until the codec is programmed, so the codec goes first.
  Imported imported;
  Simulator sim;
  sim::Bus& bus = sim.AddBus(kMainAddr, kBusAddr);
  bus.StartRunning();
  bus.SetSync(false);
  ClockingCodec codec(bus);
  sim.AttachHostDevice(kCodecAddr, codec);
  Network network(sim, imported.board);
  A2B_CHECK(network.Start() == Result::kOk);
  for (int i = 0;
       i < 1000 && network.State() != NetworkState::kDiscovering &&
       network.State() != NetworkState::kRunning && network.State() != NetworkState::kFailed;
       ++i) {
    sim.Advance(1);
    network.Poll(sim.NowMs());
  }
  A2B_CHECK(network.State() == NetworkState::kDiscovering ||
            network.State() == NetworkState::kRunning);
  const std::string trace = sim.Trace();
  const int firstMain = FirstTo(trace, "68");
  A2B_CHECK(firstMain > 0);
  // Every codec transfer came before the first to the main node.
  const size_t codecTransfers = codec.log.size();
  A2B_CHECK(codecTransfers > 40);
  A2B_CHECK_EQ(static_cast<size_t>(firstMain), codecTransfers - 1);  // the read is one WR
  A2B_CHECK(FirstTo(trace, "39") == 0);

  // A PLL relock starts the bus again without programming the codec again.
  bus.SetSync(false);
  sim.Advance(5);
  bus.SetSync(true);
  for (int i = 0; i < 500; ++i) {
    sim.Advance(1);
    network.Poll(sim.NowMs());
  }
  A2B_CHECK_EQ(codec.log.size(), codecTransfers);
  A2B_CHECK(network.State() == NetworkState::kDiscovering ||
            network.State() == NetworkState::kRunning);

  // Network::Start() programs it again.
  A2B_CHECK(network.Start() == Result::kOk);
  for (int i = 0; i < 500; ++i) {
    sim.Advance(1);
    network.Poll(sim.NowMs());
  }
  A2B_CHECK_EQ(codec.log.size(), 2 * codecTransfers);
}

A2B_TEST(EarlyAndLateHostOperations) {
  // One write before the main node, one once it is configured.
  static const uint8_t kFirst[] = {0x11};
  static const uint8_t kLater[] = {0x22};
  FixedOpList<4> script;
  script.Add(Op::PeripheralWrite(kMainNode, kCodecAddr, 1, 0x01, kFirst, 1));
  script.Add(Op::PeripheralWrite(kMainNode, kCodecAddr, 1, 0x02, kLater, 1));
  Imported imported;
  imported.board.hostScript = &script;
  imported.board.hostOpsBeforeMainNode = 1;
  Simulator sim;
  sim.AddBus(kMainAddr, kBusAddr).StartRunning();
  Codec codec;
  sim.AttachHostDevice(kCodecAddr, codec);
  Network network(sim, imported.board);
  A2B_CHECK(network.Start() == Result::kOk);
  for (int i = 0; i < 500 && network.State() != NetworkState::kDiscovering; ++i) {
    sim.Advance(1);
    network.Poll(sim.NowMs());
  }
  A2B_CHECK(network.State() == NetworkState::kDiscovering);
  A2B_CHECK_EQ(codec.log.size(), 2u);
  const std::string trace = sim.Trace();
  A2B_CHECK_EQ(FirstTo(trace, "39"), 0);  // The first write, before anything else.
  // The second came after the main node's image: the last transfer to the codec is
  // after the first to the main node.
  const size_t second = trace.find(",39,W,0,2,0222,");
  const size_t mainNode = trace.find(",68,");
  A2B_CHECK(second != std::string::npos && mainNode != std::string::npos && second > mainNode);

  // A failing early operation fails the start before the main node is touched.
  sim.ClearTrace();
  sim.FailAddress(kCodecAddr, 1);
  A2B_CHECK(network.Start() == Result::kOk);
  for (int i = 0; i < 50; ++i) {
    sim.Advance(1);
    network.Poll(sim.NowMs());
  }
  A2B_CHECK(network.State() == NetworkState::kFailed);
  A2B_CHECK(network.Error() == NetworkError::kHostScriptFailed);
  A2B_CHECK_EQ(FirstTo(sim.Trace(), "68"), -1);
}

A2B_TEST(ScriptTooLongForTheList) {
  BoardConfig board;
  FixedOpList<39> script;
  A2B_CHECK(ImportSsPlus(sBusDescription, 0, board, script) == Result::kNoSpace);
}

A2B_TEST(RunsTheCodecScriptWithItsDelay) {
  Imported imported;
  Simulator sim;
  sim.AddBus(kMainAddr, kBusAddr);
  Codec codec;
  sim.AttachHostDevice(kCodecAddr, codec);
  Transport transport(sim, kMainAddr, kBusAddr);
  ScriptRunner runner(transport);

  sim.SetNowMs(1000);
  runner.Start(*imported.board.hostScript);
  A2B_CHECK(runner.Poll(sim.NowMs()) == State::kRunning);
  A2B_CHECK_EQ(runner.Position(), 4u);  // At the delay, after the four PLL writes.
  A2B_CHECK_EQ(codec.log.size(), 4u);
  sim.Advance(kCodecStartMs - 1);
  A2B_CHECK(runner.Poll(sim.NowMs()) == State::kRunning);
  A2B_CHECK_EQ(codec.log.size(), 4u);
  sim.Advance(1);
  A2B_CHECK(runner.Poll(sim.NowMs()) == State::kDone);
  A2B_CHECK_EQ(sim.UnlockedTransfers(), 0);

  // Every write reached the codec in order: each part at its own sub-address (one step
  // per addrIncrement bytes), and the parts join up to the export's data.
  size_t at = 0;
  const OpList& script = *imported.board.hostScript;
  for (size_t i = 0; i < script.Size(); ++i) {
    const Op& op = script[i];
    if (op.type == OpType::kWait) {
      continue;
    }
    if (op.type == OpType::kPeripheralRead) {
      A2B_CHECK(at + 1 < codec.log.size() && codec.log[at + 1].read);
      at += 2;
      continue;
    }
    const size_t step = (op.addrIncrement > 1) ? op.addrIncrement : 1;
    size_t done = 0;
    while (done < op.len && at < codec.log.size()) {
      const std::vector<uint8_t>& bytes = codec.log[at++].bytes;
      A2B_CHECK(bytes.size() > 2);
      const uint32_t addr = (static_cast<uint32_t>(bytes[0]) << 8) | bytes[1];
      A2B_CHECK_EQ(addr, op.addr + done / step);
      for (size_t k = 2; k < bytes.size(); ++k) {
        A2B_CHECK_EQ(bytes[k], op.data[done++]);
      }
    }
    A2B_CHECK_EQ(done, op.len);
  }
  A2B_CHECK_EQ(at, codec.log.size());
  // Everything after the delay came at least 100 ms after the start.
  A2B_CHECK_EQ(codec.log[4].timeMs, 1000u + kCodecStartMs);
}

A2B_TEST(StopsWhenTheCodecNaks) {
  Imported imported;
  Simulator sim;
  sim.AddBus(kMainAddr, kBusAddr);
  Codec codec;
  sim.AttachHostDevice(kCodecAddr, codec);
  Transport transport(sim, kMainAddr, kBusAddr);
  ScriptRunner runner(transport);

  sim.FailAddress(kCodecAddr, 1);
  runner.Start(*imported.board.hostScript);
  A2B_CHECK(runner.Poll(sim.NowMs()) == State::kFailed);
  A2B_CHECK_EQ(runner.Position(), 0u);
  A2B_CHECK(runner.Error() == Result::kI2cError);
}

A2B_TEST(WritesASubNodeEepromWithWaits) {
  Simulator sim;
  sim::Bus& bus = sim.AddBus(kMainAddr, kBusAddr);
  sim::Node& node = bus.Plug(sim::SubNodeSpec());
  sim::Eeprom& eeprom = node.AddEeprom(0x50);
  Transport transport(sim, kMainAddr, kBusAddr);
  A2B_CHECK(test::StartMainNode(sim, transport));
  A2B_CHECK(test::DiscoverNext(sim, transport, kMainNode));

  // Two page writes: the second is refused unless the script waits out the first's
  // write cycle (a poll exactly tWR later still gets a NAK).
  constexpr uint32_t kWaitMs = sim::Eeprom::kDefaultWriteCycleMs + 1;
  const uint8_t first[4] = {0x0A, 0x0B, 0x0C, 0x0D};
  const uint8_t second[2] = {0x1A, 0x1B};
  FixedOpList<4> script;
  script.Add(Op::PeripheralWrite(0, 0x50, 2, 0x0000, first, 4));
  script.Add(Op::Wait(kWaitMs));
  script.Add(Op::PeripheralWrite(0, 0x50, 2, 0x0040, second, 2));
  ScriptRunner runner(transport);
  runner.Start(script);
  A2B_CHECK(runner.Poll(sim.NowMs()) == State::kRunning);
  sim.Advance(kWaitMs - 1);
  A2B_CHECK(runner.Poll(sim.NowMs()) == State::kRunning);
  sim.Advance(1);
  A2B_CHECK(runner.Poll(sim.NowMs()) == State::kDone);
  const std::vector<uint8_t>& memory = eeprom.Contents();
  A2B_CHECK_EQ(memory[0x00], 0x0A);
  A2B_CHECK_EQ(memory[0x03], 0x0D);
  A2B_CHECK_EQ(memory[0x40], 0x1A);
  A2B_CHECK_EQ(memory[0x41], 0x1B);

  // Without the wait, the EEPROM is still busy and NAKs the second write.
  sim.Advance(kWaitMs);
  FixedOpList<4> hasty;
  hasty.Add(Op::PeripheralWrite(0, 0x50, 2, 0x0100, first, 4));
  hasty.Add(Op::PeripheralWrite(0, 0x50, 2, 0x0140, second, 2));
  runner.Start(hasty);
  A2B_CHECK(runner.Poll(sim.NowMs()) == State::kFailed);
  A2B_CHECK_EQ(runner.Position(), 1u);
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
