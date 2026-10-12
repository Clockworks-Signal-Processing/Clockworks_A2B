/**
 * @file sim_trace.h
 * @brief Decodes the simulator's I2C trace for the scenario tests: register accesses per
 *        node (following NODEADR), and peripheral transfers per node (following CHIP).
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_TESTS_SCENARIO_SIM_TRACE_H_
#define A2BCPP_TESTS_SCENARIO_SIM_TRACE_H_

#include <cstdint>
#include <string>
#include <vector>

#include "a2b/types.h"
#include "detail/regdefs.h"

namespace a2b {
namespace test {

/** Splits one I2CT line into its fields. */
inline std::vector<std::string> Fields(const std::string& line) {
  std::vector<std::string> field;
  for (size_t from = 0;;) {
    const size_t comma = line.find(',', from);
    field.push_back(line.substr(from, comma - from));
    if (comma == std::string::npos) {
      break;
    }
    from = comma + 1;
  }
  return field;
}

/** Hex text to bytes. */
inline std::vector<uint8_t> Bytes(const std::string& hex) {
  std::vector<uint8_t> bytes;
  for (size_t i = 0; i + 1 < hex.size(); i += 2) {
    bytes.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
  }
  return bytes;
}

inline constexpr NodeAddr kBroadcast = -2;  ///< Access::node of a broadcast or peripheral access.

/** One register access in the simulator's trace. */
struct Access {
  uint32_t timeMs;
  NodeAddr node;  ///< kMainNode, a sub-node, or kBroadcast.
  bool write;
  uint8_t reg;  ///< Page 0 only: these tests don't use MMR page 1.
  uint8_t value;
  uint8_t nodeadr;  ///< NODEADR when the access was made.
};

/** Decodes the simulator's trace into register accesses, following NODEADR. */
inline std::vector<Access> Decode(const std::string& trace, uint8_t mainAddr = 0x68,
                                  uint8_t busAddr = 0x69) {
  std::vector<Access> accesses;
  uint8_t nodeadr = 0;
  size_t at = 0;
  while ((at = trace.find("I2CT,", at)) != std::string::npos) {
    const size_t end = trace.find('\n', at);
    const std::string line = trace.substr(at, end - at);
    at = end;
    std::vector<std::string> field;
    for (size_t from = 0;;) {
      const size_t comma = line.find(',', from);
      field.push_back(line.substr(from, comma - from));
      if (comma == std::string::npos) {
        break;
      }
      from = comma + 1;
    }
    if (field.size() < 11 || field[6] != "0") {
      continue;  // A failed transfer.
    }
    const uint32_t timeMs = static_cast<uint32_t>(std::stoull(field[2]) / 1000);
    const uint8_t addr = static_cast<uint8_t>(std::stoul(field[4], nullptr, 16));
    const std::string& written = field[8];
    const std::string& read = field[10];
    if ((addr != mainAddr && addr != busAddr) || written.empty()) {
      continue;
    }
    NodeAddr node = kMainNode;
    if (addr == busAddr) {
      node = ((nodeadr & (reg::kNodeadrBrcst | reg::kNodeadrPeri)) != 0)
                 ? kBroadcast
                 : static_cast<NodeAddr>(nodeadr & reg::kNodeadrNode);
    }
    const uint8_t first = static_cast<uint8_t>(std::stoul(written.substr(0, 2), nullptr, 16));
    for (size_t i = 2; i + 1 < written.size(); i += 2) {
      const uint8_t value = static_cast<uint8_t>(std::stoul(written.substr(i, 2), nullptr, 16));
      const uint8_t reg = static_cast<uint8_t>(first + (i / 2) - 1);
      accesses.push_back({timeMs, node, true, reg, value, nodeadr});
      if (node == kMainNode && reg == reg::kNodeadr) {
        nodeadr = value;
      }
    }
    for (size_t i = 0; i + 1 < read.size(); i += 2) {
      const uint8_t value = static_cast<uint8_t>(std::stoul(read.substr(i, 2), nullptr, 16));
      accesses.push_back(
          {timeMs, node, false, static_cast<uint8_t>(first + i / 2), value, nodeadr});
    }
  }
  return accesses;
}

/** Counts the failed transfers (NAKs) in the simulator's trace. */
inline int FailedTransfers(const std::string& trace) {
  int count = 0;
  for (size_t at = trace.find(",W,1,"); at != std::string::npos; at = trace.find(",W,1,", at + 1)) {
    ++count;
  }
  for (size_t at = trace.find("R,1,"); at != std::string::npos; at = trace.find("R,1,", at + 1)) {
    ++count;
  }
  return count;
}

/** Index of the first access at or after @p from that matches, or -1. */
inline int Find(const std::vector<Access>& accesses, size_t from, NodeAddr node, bool write,
                uint8_t reg, int value = -1) {
  for (size_t i = from; i < accesses.size(); ++i) {
    const Access& a = accesses[i];
    if (a.node == node && a.write == write && a.reg == reg && (value < 0 || a.value == value)) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

/** Counts the accesses that match. */
inline int Count(const std::vector<Access>& accesses, NodeAddr node, bool write, uint8_t reg,
                 int value = -1) {
  int count = 0;
  for (const Access& a : accesses) {
    count += (a.node == node && a.write == write && a.reg == reg && (value < 0 || a.value == value))
                 ? 1
                 : 0;
  }
  return count;
}

/** One transfer to a sub-node's peripheral in the simulator's trace. */
struct PeripheralTransfer {
  NodeAddr node;                 ///< The sub-node.
  uint8_t chip;                  ///< Its CHIP register: the peripheral's address.
  bool failed;                   ///< NAKed.
  std::vector<uint8_t> written;  ///< Bytes written.
  std::vector<uint8_t> read;     ///< Bytes read.
};

/** The transfers to sub-nodes' peripherals, following NODEADR and each node's CHIP. */
inline std::vector<PeripheralTransfer> Peripherals(const std::string& trace,
                                                   uint8_t mainAddr = 0x68,
                                                   uint8_t busAddr = 0x69) {
  std::vector<PeripheralTransfer> transfers;
  uint8_t nodeadr = 0;
  uint8_t chip[kMaxSubNodes];
  for (uint8_t& c : chip) {
    c = 0x50;
  }
  size_t at = 0;
  while ((at = trace.find("I2CT,", at)) != std::string::npos) {
    const size_t end = trace.find('\n', at);
    const std::vector<std::string> field = Fields(trace.substr(at, end - at));
    at = end;
    if (field.size() < 11) {
      continue;
    }
    const uint8_t addr = static_cast<uint8_t>(std::stoul(field[4], nullptr, 16));
    const bool failed = (field[6] != "0");
    const std::vector<uint8_t> written = Bytes(field[8]);
    if (addr == mainAddr && !failed && written.size() >= 2 && written[0] <= reg::kNodeadr &&
        written[0] + written.size() - 1 > reg::kNodeadr) {
      nodeadr = written[reg::kNodeadr - written[0] + 1];
    } else if (addr == busAddr) {
      const NodeAddr node = static_cast<NodeAddr>(nodeadr & reg::kNodeadrNode);
      if ((nodeadr & reg::kNodeadrPeri) != 0) {
        transfers.push_back({node, chip[node], failed, written, Bytes(field[10])});
      } else if ((nodeadr & reg::kNodeadrBrcst) == 0 && !failed && written.size() >= 2 &&
                 written[0] == reg::kChip) {
        chip[node] = written[1];
      }
    }
  }
  return transfers;
}

}  // namespace test
}  // namespace a2b

#endif  // A2BCPP_TESTS_SCENARIO_SIM_TRACE_H_
