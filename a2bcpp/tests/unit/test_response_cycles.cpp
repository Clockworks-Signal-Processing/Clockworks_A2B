/**
 * @file test_response_cycles.cpp
 * @brief Unit tests for ComputeResponseCycles() (the TRMs' Formula B) and the slot widths
 *        and response offsets the stack gives it, against the TRMs' worked example and
 *        values worked out by hand.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "a2b/routing.h"
#include "routing_planner.h"
#include "test_harness.h"

namespace a2b {
namespace {

/** The TRMs' three-node example (AD2431-35 TRM Rev 1.2, "Example Main Node A2B_RESPCYCS
    Calculation", Formula B): 14 and 16 slots of 24 bits down, 12 and 10 of 16 bits up,
    TDM8 32-bit (RESPOFFS 248), cables of 10 m and 5 m. */
RespCycleInput TrmExample() {
  RespCycleInput input;
  input.subNodeCount = 2;
  input.dnSlots[0] = 14;
  input.dnSlots[1] = 16;
  input.upSlots[0] = 12;
  input.upSlots[1] = 10;
  input.dnSlotBits = 25;
  input.upSlotBits = 17;
  input.respOffset = 248;
  input.cableDm[0] = 100;
  input.cableDm[1] = 50;
  return input;
}

A2B_TEST(MatchesTheTrmsWorkedExample) {
  // The TRM: RESPCYCS_DN 104 and 121, RESPCYCS_UP 175.53 and 184.03, the main node 148
  // (0x94), sub-node 1 143.
  const ResponseCycles cycles = ComputeResponseCycles(TrmExample());
  A2B_CHECK(cycles.fits);
  A2B_CHECK_EQ(cycles.minMain, 121);
  A2B_CHECK_EQ(cycles.maxMain, 175);
  A2B_CHECK_EQ(cycles.main, 148);
  A2B_CHECK_EQ(cycles.sub[0], 148);
  A2B_CHECK_EQ(cycles.sub[1], 143);
}

A2B_TEST(BenchProjectWithTheTableOffset) {
  // The bench's SigmaStudio+ project: one sub-node, 2 slots of 24 bits each way, I2S
  // 32-bit (RESPOFFS 245), 4 m: DN 28.5 -> 29, UP 211.9, so 120 (0x78). SigmaStudio+ gave
  // 0x7D, with its "accurate" offset of 254.
  RespCycleInput input;
  input.subNodeCount = 1;
  input.dnSlots[0] = 2;
  input.upSlots[0] = 2;
  input.respOffset = 245;
  input.cableDm[0] = 40;
  const ResponseCycles cycles = ComputeResponseCycles(input);
  A2B_CHECK(cycles.fits);
  A2B_CHECK_EQ(cycles.minMain, 29);
  A2B_CHECK_EQ(cycles.maxMain, 211);
  A2B_CHECK_EQ(cycles.main, 0x78);
  input.respOffset = 254;
  A2B_CHECK_EQ(ComputeResponseCycles(input).main, 0x7D);
}

A2B_TEST(GivenMainValueGivesEachPositionsDiscvry) {
  // The DISCVRY values of a probe: the main node's 0xB9 kept, 4 m cables; worked out with
  // floating point: 185, 180.46, 175.92, ... 116.90 (4.54 less a position).
  const uint8_t expected[kMaxSubNodes] = {0xB9, 0xB4, 0xB0, 0xAB, 0xA7, 0xA2, 0x9E, 0x99,
                                          0x95, 0x90, 0x8C, 0x87, 0x83, 0x7E, 0x79, 0x75};
  RespCycleInput input;
  input.subNodeCount = kMaxSubNodes;
  input.mainRespCycles = 0xB9;
  for (int n = 0; n < kMaxSubNodes; ++n) {
    input.cableDm[n] = 40;
  }
  const ResponseCycles cycles = ComputeResponseCycles(input);
  A2B_CHECK(cycles.fits);
  A2B_CHECK_EQ(cycles.main, 0xB9);
  for (int n = 0; n < kMaxSubNodes; ++n) {
    A2B_CHECK_EQ(cycles.sub[n], expected[n]);
  }
}

A2B_TEST(FrameRateSetsTheBitTime) {
  // At 44.1 kHz a bit takes 22.14 ns (20.35 at 48 kHz), so the 60 m from sub-node 0 to
  // position 15 is 2 x 6 x 60 / 22.14 = 32.51 bits (35.39 at 48 kHz): position 15 of 0xB9
  // is (747 - (15 x 15.8 + 32.51 + 7)) / 4 = 117.62 -> 118 (0x76), against 116.90 -> 117
  // (0x75) at 48 kHz. ADI's stack took 48 kHz always.
  RespCycleInput input;
  input.subNodeCount = kMaxSubNodes;
  input.mainRespCycles = 0xB9;
  for (int n = 0; n < kMaxSubNodes; ++n) {
    input.cableDm[n] = 40;
  }
  A2B_CHECK_EQ(ComputeResponseCycles(input).sub[15], 0x75);
  input.frameRateHz = 44100;
  A2B_CHECK_EQ(ComputeResponseCycles(input).sub[15], 0x76);
}

A2B_TEST(NoWindowWhenTheSlotsDontFit) {
  // 32 slots of 32 bits each way on one link: DN = (64 + 32 x 33) / 4 = 280, above any
  // main-node value.
  RespCycleInput input;
  input.subNodeCount = 1;
  input.dnSlots[0] = 32;
  input.upSlots[0] = 32;
  input.dnSlotBits = 33;
  input.upSlotBits = 33;
  input.cableDm[0] = 40;
  const ResponseCycles cycles = ComputeResponseCycles(input);
  A2B_CHECK(!cycles.fits);
  A2B_CHECK_EQ(cycles.minMain, 280);
}

A2B_TEST(EmptyChainKeepsTheGivenValue) {
  RespCycleInput input;
  input.mainRespCycles = 0x40;
  const ResponseCycles cycles = ComputeResponseCycles(input);
  A2B_CHECK(cycles.fits);
  A2B_CHECK_EQ(cycles.main, 0x40);
}

A2B_TEST(SlotBitsFromSlotFormat) {
  // AD2431-35 TRM, Table 3-3: size + 1 parity bit; with the format bit, 24 -> 30 and
  // 32 -> 39 (ECC), and the floating-point sizes keep their parity bit.
  A2B_CHECK_EQ(RoutingPlanner::SlotBits(0x44, false), 25);  // 24-bit both ways.
  A2B_CHECK_EQ(RoutingPlanner::SlotBits(0x44, true), 25);
  A2B_CHECK_EQ(RoutingPlanner::SlotBits(0x24, false), 25);  // The TRM example.
  A2B_CHECK_EQ(RoutingPlanner::SlotBits(0x24, true), 17);
  A2B_CHECK_EQ(RoutingPlanner::SlotBits(0x00, false), 9);   // The power-on 8 bits.
  A2B_CHECK_EQ(RoutingPlanner::SlotBits(0x0C, false), 30);  // 24-bit ECC.
  A2B_CHECK_EQ(RoutingPlanner::SlotBits(0xE0, true), 39);   // 32-bit ECC upstream.
  A2B_CHECK_EQ(RoutingPlanner::SlotBits(0x09, false), 13);  // 12-bit FP.
}

A2B_TEST(ResponseOffsetFromTheMainNodesTdm) {
  // The TRMs' RESPOFFS table (TDMMODE, TDMSS 1 = 16-bit), and 254 without upstream slots.
  A2B_CHECK_EQ(RoutingPlanner::RespOffset(0x10, true), 238);  // TDM2 16-bit.
  A2B_CHECK_EQ(RoutingPlanner::RespOffset(0xE0, true), 245);  // I2S 32-bit (INV, EARLY).
  A2B_CHECK_EQ(RoutingPlanner::RespOffset(0x11, true), 245);  // TDM4 16-bit.
  A2B_CHECK_EQ(RoutingPlanner::RespOffset(0x01, true), 248);  // TDM4 32-bit.
  A2B_CHECK_EQ(RoutingPlanner::RespOffset(0xE2, true), 248);  // TDM8.
  A2B_CHECK_EQ(RoutingPlanner::RespOffset(0x07, true), 248);  // TDM32.
  A2B_CHECK_EQ(RoutingPlanner::RespOffset(0xE0, false), 254);
}

A2B_TEST(BandwidthIsTheBusiestSubNodesBitsOfTheSuperframe) {
  // The TRM example worked out by hand, in the formula's terms: sub-node n has
  // 2 x (64 + 8) bits of control and turnaround, its slots (25 bits down, 17 up), n x 15.8 bits
  // of node delays, and the cable delays at 20.345 ns a bit (48 kHz): the cable into sub-node 0
  // (10 m) counted both ways, and the one between sub-nodes (5 m) both ways for sub-node 1.
  //   sub-node 0: 144 + 350 + 204 + 5.898 = 703.9 -> 704 bits
  //   sub-node 1: 144 + 400 + 170 + 15.8 + 2.95 + 5.898 = 738.6 -> 739 bits = 72 %
  const BandwidthUsage usage = ComputeBandwidth(TrmExample());
  A2B_CHECK_EQ(usage.node, 1);
  A2B_CHECK_EQ(usage.bits, 739);
  A2B_CHECK_EQ(usage.percent, 72);
  // One sub-node, two 24-bit slots down and none up, 4 m: 144 + 50 + 2.36 = 196.4 -> 196, 19 %.
  RespCycleInput one;
  one.subNodeCount = 1;
  one.dnSlots[0] = 2;
  one.dnSlotBits = 25;
  one.cableDm[0] = 40;
  const BandwidthUsage small = ComputeBandwidth(one);
  A2B_CHECK_EQ(small.node, 0);
  A2B_CHECK_EQ(small.bits, 196);
  A2B_CHECK_EQ(small.percent, 19);
  // No sub-node: nothing used.
  const BandwidthUsage none = ComputeBandwidth(RespCycleInput());
  A2B_CHECK_EQ(none.node, kMainNode);
  A2B_CHECK_EQ(none.bits, 0);
  A2B_CHECK_EQ(none.percent, 0);
  // A frame rate that is not 48 kHz changes the bit time, so the cable delays: 44.1 kHz has
  // longer bits and fewer of them to the metre.
  RespCycleInput slow = TrmExample();
  slow.frameRateHz = 44100;
  A2B_CHECK(ComputeBandwidth(slow).bits < usage.bits);
}

A2B_TEST(PolicyDefaultIsTheFormula) {
  const RoutingPolicy policy;
  A2B_CHECK_EQ(policy.RespCycles(TrmExample()).main, 148);
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
