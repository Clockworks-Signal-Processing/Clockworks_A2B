/**
 * @file sim_bench.h
 * @brief A bus with a Network on it for the event and drop scenarios: bench-like AD2437
 *        nodes, module info from the app with power figures, a listener that records the
 *        events, and a node class that counts OnRemoved().
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_TESTS_SCENARIO_SIM_BENCH_H_
#define A2BCPP_TESTS_SCENARIO_SIM_BENCH_H_

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "a2b/network.h"
#include "detail/regdefs.h"
#include "sim/module_info.h"
#include "sim/simulator.h"
#include "sim_trace.h"

namespace a2b {
namespace test {

inline constexpr uint8_t kMainAddr = 0x68;  ///< The bench's main-node address.
inline constexpr uint8_t kBusAddr = 0x69;   ///< The bench's bus address.

/** The module info of a bench module: GPIO0-6 inputs, PINCFG 0, CFG-4, and the power figures. */
inline std::vector<uint8_t> MakeImage(uint16_t maxPowerMw = 0, uint16_t maxCurrentMa = 0,
                                      bool localPower = false) {
  sim::ModuleInfoHeader header;
  header.powerConfig = sim::kPowerConfigHighPower;
  header.localPower = localPower;
  header.maxPowerMw = maxPowerMw;
  header.maxCurrentMa = maxCurrentMa;
  sim::ModuleInfoRegs regs;
  regs.gpioien = 0x7F;
  regs.pincfg = 0x00;
  return sim::MakeModuleInfoImage(header, regs);
}

/** A module-info source with an image per position (the last one for the positions after). */
class PositionSource : public ModuleInfoSource {
 public:
  explicit PositionSource(std::vector<std::vector<uint8_t>> images) : m_Images(std::move(images)) {}

  /** Copies the image of the node's position. */
  size_t GetModuleInfo(const SubNodeIdentity& node, uint8_t* buf, size_t bufLen) override {
    ++calls;
    const size_t index = (static_cast<size_t>(node.position) < m_Images.size())
                             ? static_cast<size_t>(node.position)
                             : m_Images.size() - 1;
    const std::vector<uint8_t>& image = m_Images[index];
    const size_t len = (image.size() < bufLen) ? image.size() : bufLen;
    for (size_t i = 0; i < len; ++i) {
      buf[i] = image[i];
    }
    return len;
  }

  /** The rest of the node's image (its channel groups, for one). */
  Result ReadModuleInfo(const SubNodeIdentity& node, uint32_t offset, uint8_t* buf,
                        size_t len) override {
    const size_t index = (static_cast<size_t>(node.position) < m_Images.size())
                             ? static_cast<size_t>(node.position)
                             : m_Images.size() - 1;
    const std::vector<uint8_t>& image = m_Images[index];
    if (offset > image.size() || len > image.size() - offset) {
      return Result::kInvalidArgument;
    }
    for (size_t i = 0; i < len; ++i) {
      buf[i] = image[offset + i];
    }
    return Result::kOk;
  }

  int calls = 0;  ///< GetModuleInfo() calls.

 private:
  std::vector<std::vector<uint8_t>> m_Images;
};

/** An AD2437 main node in CFG-4 with a small image (SWCTL 0x11, RESPCYCS 0xB9). */
inline BoardConfig MakeBoard() {
  BoardConfig board;
  board.mainProduct = 0x37;
  board.highPowerSwitchCfg = 4;
  RegisterImage& image = board.mainNode;
  image.Set(reg::kIntmsk0, 0x10);
  image.Set(reg::kIntmsk2, 0x08);
  image.Set(reg::kBecctl, 0xEF);
  image.Set(reg::kRespcycs, 0xB9);
  image.Set(reg::kControl, 0x00);
  image.Set(reg::kSwctl, 0x11);
  return board;
}

/** An AD2437 sub-node behind a UTP link that detects CFG-4, with its last chip ID byte. */
inline sim::SubNodeSpec Ad2437(uint8_t chipIdByte) {
  sim::SubNodeSpec spec;
  spec.chip.powerConfig = sim::kPowerConfigHighPower;
  spec.chip.lowVoltageInput = true;
  spec.chip.chipId[5] = chipIdByte;
  return spec;
}

/** Power configuration of @p product's bus power switch: CFG-4 on the AD2437, CFG-0 on the
    AD2433 (the AD2431W-35W data sheet's comparison table) and the AD242x. */
inline uint8_t PowerConfigOf(uint8_t product) {
  return (product == 0x37) ? sim::kPowerConfigHighPower : 0;
}

/** The module info of a module with an @p product transceiver: as MakeImage(), with the
    chip's power configuration and an SPI setting (which only an AD243x gets). */
inline std::vector<uint8_t> MakeImageFor(uint8_t product) {
  sim::ModuleInfoHeader header;
  header.powerConfig = PowerConfigOf(product);
  sim::ModuleInfoRegs regs;
  regs.gpioien = 0x7F;
  regs.pincfg = 0x00;
  regs.spicfg = 0x19;
  return sim::MakeModuleInfoImage(header, regs);
}

/** The transceiver of an @p product module, with its last chip ID byte. */
inline sim::ChipSpec ChipOf(uint8_t product, uint8_t chipIdByte = 0) {
  sim::ChipSpec chip;
  chip.productId = product;
  chip.powerConfig = PowerConfigOf(product);
  chip.lowVoltageInput = (product == 0x37);
  chip.chipId[5] = chipIdByte;
  return chip;
}

/** A sub-node with an @p product transceiver behind a UTP link. */
inline sim::SubNodeSpec SubOf(uint8_t product, uint8_t chipIdByte) {
  sim::SubNodeSpec spec;
  spec.chip = ChipOf(product, chipIdByte);
  return spec;
}

/** A board whose main node is an @p product: MakeBoard() with that chip's switch mode and
    power configuration (an AD242x has no SWSTAT2, so none is checked). */
inline BoardConfig MakeBoardFor(uint8_t product) {
  BoardConfig board = MakeBoard();
  board.mainProduct = product;
  const bool ad243x = (product == 0x33 || product == 0x37);
  board.highPowerSwitchCfg = ad243x ? PowerConfigOf(product) : kPowerConfigAsDetected;
  board.mainNode.Set(reg::kSwctl, ad243x ? 0x11 : 0x01);
  return board;
}

/** Records every event, and runs a test's function on each. */
class Recorder : public NetworkListener {
 public:
  /** Keeps the event, then calls #onEvent if set. */
  void OnEvent(Network& network, const Event& event) override {
    events.push_back(event);
    if (onEvent) {
      onEvent(network, event);
    }
  }

  /** Keeps the review, then asks #decide if set, else the policy. */
  RejectReason OnNodeIdentified(const NodeReview& review) override {
    reviews.push_back(review);
    return decide ? decide(review) : review.policy;
  }

  /** The events of @p type, in order. */
  std::vector<Event> Of(EventType type) const {
    std::vector<Event> out;
    for (const Event& event : events) {
      if (event.type == type) {
        out.push_back(event);
      }
    }
    return out;
  }

  std::vector<Event> events;                              ///< Every event, in order.
  std::vector<NodeReview> reviews;                        ///< Every review, in order.
  std::function<void(Network&, const Event&)> onEvent;    ///< Run on each event.
  std::function<RejectReason(const NodeReview&)> decide;  ///< Decides each review.
};

/** A node that counts its OnRemoved() calls and may enable more interrupts. */
class CountingNode : public SubNode {
 public:
  using SubNode::SubNode;

  /** Counts. */
  void OnRemoved() override { ++removed; }
  /** The stack's masks, plus SRF missed if #srfInterrupt. */
  InterruptMasks InterruptMask() const override {
    InterruptMasks masks = SubNode::InterruptMask();
    if (srfInterrupt) {
      masks.intmsk0 = static_cast<uint8_t>(masks.intmsk0 | reg::kIntmsk0Srferr);
    }
    return masks;
  }

  static int removed;        ///< OnRemoved() calls, over all nodes.
  static bool srfInterrupt;  ///< Enable the SRF-missed interrupt.
};

inline int CountingNode::removed = 0;
inline bool CountingNode::srfInterrupt = false;

/** A bus with a Network on it, polled every millisecond. */
struct Bench {
  sim::Simulator sim;
  sim::Bus& bus;
  BoardConfig board;
  PositionSource source;
  NodeFactory factory;
  Recorder listener;
  Network network;

  /** A bench whose modules take @p images (by position), with @p mainChip as its main
      node; counting nodes; the listener set. */
  explicit Bench(const BoardConfig& config = MakeBoard(),
                 std::vector<std::vector<uint8_t>> images = {MakeImage()},
                 const sim::ChipSpec& mainChip = MainChip())
      : bus(sim.AddBus(kMainAddr, kBusAddr, mainChip)),
        board(config),
        source(std::move(images)),
        network(sim, board) {
    CountingNode::removed = 0;
    CountingNode::srfInterrupt = false;
    factory.ByModule(0x00AD, 0x0037, Make<CountingNode>);
    network.SetModuleInfoSource(&source);
    network.SetNodeFactory(&factory);
    network.SetListener(&listener);
  }

  /** The main node: an AD2437 that detects CFG-4. */
  static sim::ChipSpec MainChip() {
    sim::ChipSpec chip;
    chip.powerConfig = sim::kPowerConfigHighPower;
    chip.lowVoltageInput = true;
    return chip;
  }

  /** Plugs in @p count AD2437 sub-nodes, their chip IDs ending in @p firstId, +1, ... */
  void PlugChain(int count, uint8_t firstId = 0x10) {
    for (int i = 0; i < count; ++i) {
      bus.Plug(Ad2437(static_cast<uint8_t>(firstId + i)));
    }
  }

  /** Polls every millisecond for @p ms. */
  void Run(uint32_t ms) {
    for (uint32_t i = 0; i < ms; ++i) {
      sim.Advance(1);
      network.Poll(sim.NowMs());
    }
  }

  /** Polls until the state is @p state, for at most @p ms. */
  bool RunUntil(NetworkState state, uint32_t ms = 5000) {
    for (uint32_t i = 0; i < ms && network.State() != state; ++i) {
      Run(1);
    }
    return network.State() == state;
  }

  /** Polls until @p count sub-nodes are in the bus, for at most @p ms. */
  bool RunUntilCount(size_t count, uint32_t ms = 5000) {
    for (uint32_t i = 0; i < ms && network.SubNodeCount() != count; ++i) {
      Run(1);
    }
    return network.SubNodeCount() == count;
  }

  /** Starts the bus and polls until it runs with @p count sub-nodes. */
  bool StartWith(size_t count) {
    return network.Start() == Result::kOk && RunUntil(NetworkState::kRunning, 5000) &&
           network.SubNodeCount() == count;
  }

  /** The register accesses so far. */
  std::vector<Access> Accesses() const { return Decode(sim.Trace()); }

  /** True if the stack logged @p line. */
  bool Logged(const std::string& line) const {
    for (const std::string& logged : sim.LogLines()) {
      if (logged == line) {
        return true;
      }
    }
    return false;
  }
};

}  // namespace test
}  // namespace a2b

#endif  // A2BCPP_TESTS_SCENARIO_SIM_BENCH_H_
