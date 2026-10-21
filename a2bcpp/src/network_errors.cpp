/**
 * @file network_errors.cpp
 * @brief Network::Impl: the error watch (Network::SetErrorWatch()): bit-error counting and the
 *        SRF-missed and BECOVF interrupts on every node, BECNT summed before each clear, and
 *        the report of what the nodes held when a probe failed or before a drop. Also the log
 *        line that names a failed access, which is always on.
 *
 * The watch only reads and writes while it is on (or being turned off): with it off the
 * stack's I2C traffic is what it was without it.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstring>

#include "network_impl.h"

namespace a2b {
namespace {

/** BECCTL while watching: every error type counted, BECOVF when BECNT > 16 (THRESHLD 3).
    Counting header-count errors double-counts most line errors (reg::kBecctlCountAll). */
constexpr uint8_t kWatchBecctl =
    static_cast<uint8_t>(reg::kBecctlCountAll | (3 << reg::kBecctlThreshldPos));
/** INTMSK0 bits the watch adds on every node: SRF missed and the bit-error overflow. */
constexpr uint8_t kWatchIntmsk0 = reg::kIntmsk0Srferr | reg::kIntmsk0Becovf;
/** The main node's INTPND2 bits a report clears (never DSCDONE, which discovery uses). */
constexpr uint8_t kReportIntpnd2 = reg::kIntpnd2I2cerr | reg::kIntpnd2Icrcerr;
/** Registers read in one block: INTPND0 to BECNT (INTTYPE, 0x17, is never read: it clears). */
constexpr size_t kErrorBlock = reg::kBecnt - reg::kIntpnd0 + 1;

/** Index of a node in the per-node arrays: [0] the main node, [n + 1] sub-node n. */
size_t IndexOf(NodeAddr node) { return static_cast<size_t>(node + 1); }

/** Appends @p text to @p out (of @p size) at @p len. */
void Append(char* out, size_t size, size_t& len, const char* text) {
  for (const char* p = text; *p != '\0' && len + 1 < size; ++p) {
    out[len++] = *p;
  }
  out[len] = '\0';
}

/** Appends @p value in decimal. */
void AppendNumber(char* out, size_t size, size_t& len, uint32_t value) {
  char digits[10];
  size_t n = 0;
  do {
    digits[n++] = static_cast<char>('0' + value % 10);
    value /= 10;
  } while (value != 0);
  while (n > 0 && len + 1 < size) {
    out[len++] = digits[--n];
  }
  out[len] = '\0';
}

/** Appends @p value as 0x and two hex digits (three for page 1). */
void AppendHex(char* out, size_t size, size_t& len, uint16_t value) {
  static const char kDigits[] = "0123456789ABCDEF";
  Append(out, size, len, "0x");
  const int digits = (value > 0xFF) ? 3 : 2;
  for (int i = digits - 1; i >= 0 && len + 1 < size; --i) {
    out[len++] = kDigits[(value >> (4 * i)) & 0xF];
  }
  out[len] = '\0';
}

}  // namespace

/**
 * Logs the access that failed: "a2b: failed access: sub-node 5, 0x4E read, 1 try, 516 ms".
 * Without the C library's printf, as LogNumber().
 */
void Network::Impl::LogFailure(const FailedAccess& failed) {
  char text[96];
  size_t len = 0;
  Append(text, sizeof(text), len, "a2b: failed access: ");
  if (failed.node == kMainNode) {
    Append(text, sizeof(text), len, "main node");
  } else if (failed.node == kNoNode) {
    Append(text, sizeof(text), len, "broadcast");
  } else {
    Append(text, sizeof(text), len, "sub-node ");
    AppendNumber(text, sizeof(text), len, static_cast<uint32_t>(failed.node));
  }
  Append(text, sizeof(text), len, ", ");
  AppendHex(text, sizeof(text), len, failed.reg);
  Append(text, sizeof(text), len, failed.write ? " write, " : " read, ");
  AppendNumber(text, sizeof(text), len, failed.tries);
  Append(text, sizeof(text), len, (failed.tries == 1) ? " try, " : " tries, ");
  AppendNumber(text, sizeof(text), len, failed.elapsedMs);
  Append(text, sizeof(text), len, " ms");
  m_Platform.Log(LogLevel::kWarning, text);
}

/**
 * Turns the error watch on or off, from the next poll between probes. Turning it on also
 * clears what the nodes had latched and the sums. See Network::SetErrorWatch().
 */
void Network::Impl::SetErrorWatch(bool on) {
  if (on && !m_Watch) {
    m_ClearErrorsDue = true;
  }
  m_Watch = on;
}

/** See Network::BitErrorCount(). */
uint32_t Network::Impl::BitErrorCount(NodeAddr node) const {
  if (node < kMainNode || node >= kMaxSubNodes) {
    return 0;
  }
  return m_BecntSum[IndexOf(node)];
}

/** See Network::ReadErrorState(). */
Result Network::Impl::ReadErrorState(NodeAddr node, NodeErrorState& state) {
  if (!IsReachable(node)) {
    return Result::kInvalidArgument;
  }
  return ReadErrorRegs(node, state);
}

/**
 * Reads a node's error registers once each, without retries: SWSTAT, INTPND0 to BECNT in
 * one read, and NODE on a sub-node.
 * @return The first failure, or Result::kOk; NodeErrorState::read tells which.
 */
Result Network::Impl::ReadErrorRegs(NodeAddr node, NodeErrorState& state) {
  state = NodeErrorState();
  uint8_t block[kErrorBlock] = {};
  Result result = m_Transport.ReadReg(node, reg::kSwstat, state.swstat);
  if (result == Result::kOk) {
    result = m_Transport.ReadRegs(node, reg::kIntpnd0, block, sizeof(block));
  }
  if (result == Result::kOk && node != kMainNode) {
    result = m_Transport.ReadReg(node, reg::kNode, state.node);
  }
  if (result != Result::kOk) {
    return result;
  }
  std::memcpy(state.intpnd, block, sizeof(state.intpnd));
  state.intmsk0 = block[reg::kIntmsk0 - reg::kIntpnd0];
  state.becctl = block[reg::kBecctl - reg::kIntpnd0];
  state.becnt = block[reg::kBecnt - reg::kIntpnd0];
  state.read = true;
  return Result::kOk;
}

/**
 * The INTMSK0 a sub-node's link writes: its object's masks, with the watch's added while
 * it is on.
 */
uint8_t Network::Impl::LinkIntmsk0(NodeAddr position) const {
  const uint8_t base = m_Nodes[position]->InterruptMask().intmsk0;
  return m_Watch ? static_cast<uint8_t>(base | kWatchIntmsk0) : base;
}

/**
 * Between probes: the watch's settings written to each node that hasn't got them (one that
 * joined, or all of them once it is turned on or off), the sums and latched errors cleared
 * if asked. Nothing is sent while the watch is off and every node has it off.
 */
void Network::Impl::ApplyWatch() {
  for (size_t k = 0; k <= m_SubNodeCount; ++k) {
    if (m_Watched[k] == m_Watch) {
      continue;
    }
    const NodeAddr node = static_cast<NodeAddr>(static_cast<int>(k) - 1);
    if (WatchNode(node) != Result::kOk) {
      m_CheckNow = true;
      return;
    }
    m_Watched[k] = m_Watch;
  }
  if (m_ClearErrorsDue) {
    m_ClearErrorsDue = false;
    for (uint32_t& sum : m_BecntSum) {
      sum = 0;
    }
    const uint8_t all = 0xFF;
    const uint8_t intpnd2 = kReportIntpnd2;
    const uint8_t zero = 0;
    Result result = m_Transport.WriteReg(kMainNode, reg::kIntpnd2, intpnd2);
    for (size_t k = 0; result == Result::kOk && k <= m_SubNodeCount; ++k) {
      const NodeAddr node = static_cast<NodeAddr>(static_cast<int>(k) - 1);
      result = m_Transport.WriteReg(node, reg::kIntpnd0, all);
      if (result == Result::kOk) {
        result = m_Transport.WriteReg(node, reg::kBecnt, zero);
      }
    }
    if (result != Result::kOk) {
      m_CheckNow = true;
    }
  }
}

/**
 * Writes the watch's state to one node: on, BECCTL counting every error (its own value
 * kept) and SRF missed and BECOVF added to INTMSK0; off, both as they were. A sub-node's
 * link gets the same INTMSK0, so that its next probe keeps it.
 */
Result Network::Impl::WatchNode(NodeAddr node) {
  const size_t k = IndexOf(node);
  if (m_Watch) {
    Result result = m_Transport.ReadReg(node, reg::kBecctl, m_SavedBecctl[k]);
    if (result == Result::kOk && node == kMainNode) {
      result = m_Transport.ReadReg(node, reg::kIntmsk0, m_SavedMainIntmsk0);
    }
    if (result != Result::kOk) {
      return result;
    }
  }
  uint8_t intmsk0 = 0;
  if (node == kMainNode) {
    intmsk0 =
        m_Watch ? static_cast<uint8_t>(m_SavedMainIntmsk0 | kWatchIntmsk0) : m_SavedMainIntmsk0;
  } else {
    intmsk0 = LinkIntmsk0(node);
    m_Links[k].intmsk0 = intmsk0;
  }
  Result result = m_Transport.WriteReg(node, reg::kIntmsk0, intmsk0);
  if (result == Result::kOk) {
    result = m_Transport.WriteReg(node, reg::kBecctl, m_Watch ? kWatchBecctl : m_SavedBecctl[k]);
  }
  return result;
}

/**
 * At each BECNT clear while watching: every node's BECNT is read and added to its sum
 * before the clear wipes it.
 */
void Network::Impl::SumBitErrors() {
  for (size_t k = 0; k <= m_SubNodeCount; ++k) {
    if (!m_Watched[k]) {
      continue;
    }
    uint8_t count = 0;
    const NodeAddr node = static_cast<NodeAddr>(static_cast<int>(k) - 1);
    if (m_Transport.ReadReg(node, reg::kBecnt, count) != Result::kOk) {
      m_CheckNow = true;
      return;
    }
    m_BecntSum[k] += count;
  }
}

/**
 * Takes the error watch's report: the error registers of the main node and of each sub-node
 * before @p position, then, before a drop, one more read of the dropped node's IDs. The
 * latched bits read are then cleared, so the next report shows only new ones, and an
 * EventType::kErrorReport is queued (before the drop's kNodeRemoved events).
 * @param cause Why.
 * @param position The position probed, or the node about to be dropped.
 * @param failed The access that failed.
 */
void Network::Impl::TakeReport(ErrorReport::Cause cause, NodeAddr position,
                               const FailedAccess& failed) {
  ErrorReport& report = m_Report;  // Field by field: a whole ErrorReport is too big for the stack.
  report.answeredLate = false;
  report.lateIds[0] = 0;
  report.lateIds[1] = 0;
  report.cause = cause;
  report.position = position;
  report.timeMs = m_Platform.NowMs();
  report.failed = failed;
  (void)ReadErrorRegs(kMainNode, report.main);
  const size_t count =
      (cause == ErrorReport::Cause::kDropped) ? static_cast<size_t>(position) : m_SubNodeCount;
  size_t read = 0;
  while (read < count) {  // The nodes behind one that doesn't answer can't either.
    const Result result = ReadErrorRegs(static_cast<NodeAddr>(read), report.subs[read]);
    ++read;
    if (result != Result::kOk) {
      break;
    }
  }
  report.subCount = static_cast<uint8_t>(read);
  if (cause == ErrorReport::Cause::kDropped) {
    report.answeredLate = m_Transport.ReadRegs(position, reg::kVendor, report.lateIds,
                                               sizeof(report.lateIds)) == Result::kOk;
  }
  if (report.main.read) {
    const uint8_t intpnd2 = static_cast<uint8_t>(report.main.intpnd[2] & kReportIntpnd2);
    if (report.main.intpnd[0] != 0) {
      (void)m_Transport.WriteReg(kMainNode, reg::kIntpnd0, report.main.intpnd[0]);
    }
    if (intpnd2 != 0) {
      (void)m_Transport.WriteReg(kMainNode, reg::kIntpnd2, intpnd2);
    }
  }
  for (size_t i = 0; i < read; ++i) {
    if (report.subs[i].read && report.subs[i].intpnd[0] != 0) {
      (void)m_Transport.WriteReg(static_cast<NodeAddr>(i), reg::kIntpnd0, report.subs[i].intpnd[0]);
    }
  }
  Event event;
  event.type = EventType::kErrorReport;
  event.node = position;
  Queue(event);
}

}  // namespace a2b
