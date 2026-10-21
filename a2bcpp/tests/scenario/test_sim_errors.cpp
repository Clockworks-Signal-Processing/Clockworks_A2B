/**
 * @file test_sim_errors.cpp
 * @brief Simulator scenarios for the error watch (Network::SetErrorWatch()): nothing sent
 *        while it is off, BECCTL and the extra interrupt masks on every node, BECNT summed
 *        before each clear, the BECOVF and SRF-missed interrupts, the report taken before a
 *        drop and after a failed probe, and the log line naming a failed access.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdint>
#include <cstring>
#include <vector>

#include "a2b/strings.h"
#include "sim_bench.h"
#include "test_harness.h"

namespace a2b {
namespace {

using test::Access;
using test::Bench;
using test::Count;
using test::kBusAddr;

/** BECCTL while watching: every error type, BECOVF past 16. */
constexpr uint8_t kWatchBecctl = 0x7F;
/** The bench image's main-node BECCTL and INTMSK0 (test::MakeBoard()). */
constexpr uint8_t kImageBecctl = 0xEF;
constexpr uint8_t kImageIntmsk0 = 0x10;

/** The accesses of the error watch's registers in @p a: SWSTAT, BECCTL and NODE. */
int WatchAccesses(const std::vector<Access>& a) {
  int count = 0;
  for (const Access& access : a) {
    count += (access.reg == reg::kSwstat || access.reg == reg::kBecctl || access.reg == reg::kNode)
                 ? 1
                 : 0;
  }
  return count;
}

/** Index of the first event of @p type in @p events at or after @p from, or -1. */
int IndexOf(const std::vector<Event>& events, EventType type, size_t from = 0) {
  for (size_t i = from; i < events.size(); ++i) {
    if (events[i].type == type) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

A2B_TEST(WatchOffSendsNothingForIt) {
  // Off by default: no error register is read or written, a drop gets no report, and only
  // the failed access is named in the log.
  Bench bench;
  bench.PlugChain(3);
  A2B_CHECK(!bench.network.ErrorWatch());
  A2B_CHECK(bench.StartWith(3));
  bench.sim.ClearTrace();
  bench.bus.Unplug(1);
  bench.Run(bench.board.timings.chainCheckMs + 5);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 1);
  A2B_CHECK_EQ(WatchAccesses(bench.Accesses()), 0);
  A2B_CHECK(bench.listener.Of(EventType::kErrorReport).empty());
  A2B_CHECK(bench.network.LastErrorReport().cause == ErrorReport::Cause::kNone);
  A2B_CHECK(bench.Logged("W: a2b: failed access: sub-node 1, 0x02 read, 3 tries, 0 ms"));
  A2B_CHECK(bench.Logged("W: a2b: sub-node 1 dropped"));
}

A2B_TEST(WatchWritesBecctlAndMasksOnEveryNode) {
  Bench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  bench.network.SetErrorWatch(true);
  A2B_CHECK(bench.network.ErrorWatch());
  bench.Run(5);
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kBecctl), kWatchBecctl);
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kIntmsk0), kImageIntmsk0 | 0x60);
  for (size_t i = 0; i < 2; ++i) {
    A2B_CHECK_EQ(bench.bus.Sub(i).Reg(reg::kBecctl), kWatchBecctl);
    A2B_CHECK_EQ(bench.bus.Sub(i).Reg(reg::kIntmsk0), 0x70);  // PWRERR, BECOVF, SRF missed.
  }
  // A node that joins gets them too, and the probe behind the last node keeps them.
  bench.bus.Plug(test::Ad2437(0x12));
  A2B_CHECK(bench.RunUntilCount(3, 2000));
  bench.Run(bench.board.timings.probeIntervalMs + 50);
  for (size_t i = 0; i < 3; ++i) {
    A2B_CHECK_EQ(bench.bus.Sub(i).Reg(reg::kBecctl), kWatchBecctl);
    A2B_CHECK_EQ(bench.bus.Sub(i).Reg(reg::kIntmsk0), 0x70);
  }
  // Off: everything as it was.
  bench.network.SetErrorWatch(false);
  bench.Run(5);
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kBecctl), kImageBecctl);
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kIntmsk0), kImageIntmsk0);
  for (size_t i = 0; i < 3; ++i) {
    A2B_CHECK_EQ(bench.bus.Sub(i).Reg(reg::kBecctl), 0);
    A2B_CHECK_EQ(bench.bus.Sub(i).Reg(reg::kIntmsk0), 0x10);
  }
  bench.sim.ClearTrace();
  bench.Run(3000);
  A2B_CHECK_EQ(WatchAccesses(bench.Accesses()), 0);
}

A2B_TEST(WatchIsAppliedAgainAfterARestart) {
  Bench bench;
  bench.PlugChain(1);
  A2B_CHECK(bench.StartWith(1));
  bench.network.SetErrorWatch(true);
  bench.Run(5);
  A2B_CHECK(bench.network.Start() == Result::kOk);
  A2B_CHECK(bench.RunUntilCount(1, 3000));
  bench.Run(5);
  A2B_CHECK_EQ(bench.bus.Main().Reg(reg::kBecctl), kWatchBecctl);
  A2B_CHECK_EQ(bench.bus.Sub(0).Reg(reg::kBecctl), kWatchBecctl);
}

A2B_TEST(BitErrorsAreSummedBeforeEachClear) {
  Bench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  bench.network.SetErrorWatch(true);
  bench.Run(5);
  bench.bus.InjectBitErrors(1, 5);
  bench.bus.InjectBitErrors(-1, 2);
  bench.Run(bench.board.timings.becntResetMs + 5);
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kBecnt), 0);  // Cleared as before.
  A2B_CHECK_EQ(bench.network.BitErrorCount(1), 5);
  A2B_CHECK_EQ(bench.network.BitErrorCount(kMainNode), 2);
  A2B_CHECK_EQ(bench.network.BitErrorCount(0), 0);
  bench.bus.InjectBitErrors(1, 7);
  bench.Run(bench.board.timings.becntResetMs + 5);
  A2B_CHECK_EQ(bench.network.BitErrorCount(1), 12);
  A2B_CHECK_EQ(bench.network.BitErrorCount(kMaxSubNodes), 0);  // Out of range.
  bench.network.ClearErrors();
  bench.Run(5);
  A2B_CHECK_EQ(bench.network.BitErrorCount(1), 0);
}

A2B_TEST(BitErrorsAreNotCountedWithTheWatchOff) {
  Bench bench;
  bench.PlugChain(1);
  A2B_CHECK(bench.StartWith(1));
  bench.bus.InjectBitErrors(0, 5);  // BECCTL counts nothing.
  bench.Run(bench.board.timings.becntResetMs + 5);
  A2B_CHECK_EQ(bench.network.BitErrorCount(0), 0);
}

A2B_TEST(BecovfInterruptIsReportedAndNamed) {
  Bench bench;
  bench.PlugChain(3);
  A2B_CHECK(bench.StartWith(3));
  bench.network.SetErrorWatch(true);
  bench.Run(5);
  bench.listener.events.clear();
  bench.bus.InjectBitErrors(2, 17);  // Past 16.
  A2B_CHECK((bench.bus.Sub(2).Reg(reg::kIntpnd0) & 0x20) != 0);
  bench.Run(bench.board.timings.interruptPollMs + 5);
  const std::vector<Event> irqs = bench.listener.Of(EventType::kInterrupt);
  A2B_CHECK_EQ(irqs.size(), 1);
  A2B_CHECK(irqs.size() == 1 && irqs[0].node == 2 && irqs[0].interruptType == 4);
  A2B_CHECK(std::strcmp(InterruptName(4), "bit error count overflow") == 0);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 3);  // Reported only.
  A2B_CHECK(bench.listener.Of(EventType::kNodeRemoved).empty());
}

A2B_TEST(InterruptNames) {
  A2B_CHECK(std::strcmp(InterruptName(5), "SRF missed") == 0);
  A2B_CHECK(std::strcmp(InterruptName(15), "power fault: undetermined cable fault") == 0);
  A2B_CHECK(std::strcmp(InterruptName(23), "IO7 pin") == 0);
  A2B_CHECK(std::strcmp(InterruptName(0xFD), "sub-node INTTYPE read error") == 0);
  A2B_CHECK(std::strcmp(InterruptName(7), "unknown") == 0);
  A2B_CHECK(std::strcmp(InterruptName(100), "unknown") == 0);
  A2B_CHECK(std::strcmp(ToString(EventType::kErrorReport), "error report") == 0);
}

A2B_TEST(PulledCableGetsAReportBeforeTheDrop) {
  // The cable into sub-node 2 is pulled: sub-node 1 misses its SRFs (the watch's interrupt
  // has the chain checked at once), becomes the last node, and the report says so.
  Bench bench;
  bench.PlugChain(4);
  A2B_CHECK(bench.StartWith(4));
  bench.network.SetErrorWatch(true);
  bench.Run(5);
  bench.listener.events.clear();
  bench.bus.Unplug(2);
  bench.Run(bench.board.timings.interruptPollMs + 5);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
  const std::vector<Event>& events = bench.listener.events;
  const int irq = IndexOf(events, EventType::kInterrupt);
  const int report = IndexOf(events, EventType::kErrorReport);
  const int removed = IndexOf(events, EventType::kNodeRemoved);
  A2B_CHECK(irq >= 0 && events[irq].node == 1 && events[irq].interruptType == 5);
  A2B_CHECK(report > irq && removed > report && events[report].node == 2);
  const ErrorReport& r = bench.network.LastErrorReport();
  A2B_CHECK(r.cause == ErrorReport::Cause::kDropped);
  A2B_CHECK_EQ(r.position, 2);
  A2B_CHECK_EQ(r.failed.node, 2);
  A2B_CHECK_EQ(r.failed.reg, reg::kVendor);
  A2B_CHECK_EQ(r.failed.tries, 3);
  A2B_CHECK(!r.failed.write);
  A2B_CHECK(!r.answeredLate);
  A2B_CHECK(r.main.read && r.main.becctl == kWatchBecctl);
  A2B_CHECK_EQ(r.subCount, 2);
  A2B_CHECK(r.subs[0].read && (r.subs[0].node & reg::kNodeLast) == 0);
  A2B_CHECK(r.subs[1].read && (r.subs[1].node & reg::kNodeLast) != 0);
  A2B_CHECK((r.subs[1].intpnd[0] & reg::kIntpnd0Srferr) != 0);
  A2B_CHECK_EQ(r.subs[1].swstat, 0);  // No power fault.
  // What the report read is cleared, so the next one shows only new errors.
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kIntpnd0) & reg::kIntpnd0Srferr, 0);
}

A2B_TEST(PowerFaultShowsInTheReport) {
  // Sub-node 1's 24 V is removed: it reports the fault, sub-node 2 drops, and the report
  // has sub-node 1's PWRERR and SWSTAT's fault code.
  Bench bench;
  bench.PlugChain(3);
  A2B_CHECK(bench.StartWith(3));
  bench.network.SetErrorWatch(true);
  bench.Run(5);
  bench.bus.SetDownstreamSupply(1, false);
  bench.Run(bench.board.timings.interruptPollMs + 5);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);
  const ErrorReport& r = bench.network.LastErrorReport();
  A2B_CHECK(r.cause == ErrorReport::Cause::kDropped && r.position == 2);
  A2B_CHECK((r.subs[1].intpnd[0] & reg::kIntmsk0Pwrerr) != 0);
  A2B_CHECK_EQ(r.subs[1].swstat, reg::kSwstatFault | 0x70);
}

A2B_TEST(NodeThatAnswersLateIsReportedAndStillDropped) {
  // Sub-node 1 fails the chain check's three reads but answers the report's late read: the
  // report says so; the drop goes ahead (the watch only reports), and the node comes back.
  BoardConfig board = test::MakeBoard();
  board.timings.becntResetMs = 0;  // Only the chain check reads sub-node 1.
  Bench bench(board);
  bench.PlugChain(3);
  A2B_CHECK(bench.StartWith(3));
  bench.network.SetErrorWatch(true);
  bench.Run(5);
  bench.bus.FailNodeAccesses(1, 3);
  bench.Run(bench.board.timings.chainCheckMs + 5);
  const ErrorReport& r = bench.network.LastErrorReport();
  A2B_CHECK(r.cause == ErrorReport::Cause::kDropped && r.position == 1);
  A2B_CHECK(r.answeredLate);
  A2B_CHECK(r.lateIds[0] == 0xAD && r.lateIds[1] == 0x37);
  A2B_CHECK_EQ(r.subCount, 1);
  A2B_CHECK((r.subs[0].node & reg::kNodeLast) == 0);  // Sub-node 0 still had its downstream.
  A2B_CHECK(bench.Logged("W: a2b: sub-node 1 dropped"));
  A2B_CHECK(bench.RunUntilCount(3, 3000));
}

A2B_TEST(FailedProbeGetsAReport) {
  Bench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  bench.network.SetErrorWatch(true);
  bench.Run(5);
  bench.listener.events.clear();
  bench.sim.FailRegisterWrite(kBusAddr, reg::kSwctl, 1);  // The next probe's first switch write.
  bench.Run(bench.board.timings.probeIntervalMs + 50);
  A2B_CHECK(bench.Logged("W: a2b: probe for sub-node 2 failed; checking the chain"));
  A2B_CHECK(bench.Logged("W: a2b: failed access: sub-node 0, 0x09 write, 1 try, 0 ms"));
  const std::vector<Event> reports = bench.listener.Of(EventType::kErrorReport);
  A2B_CHECK_EQ(reports.size(), 1);
  A2B_CHECK(reports.size() == 1 && reports[0].node == 2);
  const ErrorReport& r = bench.network.LastErrorReport();
  A2B_CHECK(r.cause == ErrorReport::Cause::kProbeFailed && r.position == 2);
  A2B_CHECK(r.failed.node == 0 && r.failed.reg == reg::kSwctl && r.failed.write);
  A2B_CHECK_EQ(r.subCount, 2);
  A2B_CHECK(r.subs[0].read && r.subs[1].read);
  A2B_CHECK_EQ(bench.network.SubNodeCount(), 2);  // Nothing dropped.
}

A2B_TEST(ReadErrorStateReadsWithoutClearing) {
  Bench bench;
  bench.PlugChain(2);
  A2B_CHECK(bench.StartWith(2));
  bench.network.SetErrorWatch(true);
  bench.Run(5);
  bench.bus.InjectBitErrors(1, 17);
  NodeErrorState state;
  A2B_CHECK(bench.network.ReadErrorState(1, state) == Result::kOk);
  A2B_CHECK(state.read && state.becnt == 17 && (state.intpnd[0] & 0x20) != 0);
  A2B_CHECK_EQ(state.becctl, kWatchBecctl);
  A2B_CHECK((bench.bus.Sub(1).Reg(reg::kIntpnd0) & 0x20) != 0);
  A2B_CHECK(bench.network.ReadErrorState(kMainNode, state) == Result::kOk && state.read);
  A2B_CHECK(bench.network.ReadErrorState(2, state) == Result::kInvalidArgument);
  bench.network.ClearErrors();
  bench.Run(5);
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kIntpnd0), 0);
  A2B_CHECK_EQ(bench.bus.Sub(1).Reg(reg::kBecnt), 0);
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
