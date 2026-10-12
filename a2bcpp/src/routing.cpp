/**
 * @file routing.cpp
 * @brief The response-cycle calculation (the TRMs' Formula B), each node's channel map, and
 *        the default routing policy.
 *
 * **Response cycles** (AD2431/32/33/35 TRM Rev 1.2, "Response Cycle Formula", Formula B,
 * which its Table 6-10 gives for AD2426/27/28/3x and mixed systems of any size; the AD2437
 * TRM Rev 0.2 has the same formula with small differences, below). For each sub-node n of
 * the chain, from the slots on its A side:
 * - DNSLOT_ACTIVITY[n] = NUM_DNSLOTS[n] × bits per downstream slot (size + 1 parity bit,
 *   or + ECC: Table 3-3), and likewise UPSLOT_ACTIVITY[n] for the upstream slots it drives;
 * - RESPCYCS_DN[n] = (64 + DNSLOT_ACTIVITY[n] + 8 + n × 7.5 + n × 8.3
 *   + 2 × 6 × (cable from sub-node 0 to node n, m) / bit time − 8) / 4, rounded up;
 * - RESPCYCS_UP[n] = (RESPOFFS × 4 − (64 + UPSLOT_ACTIVITY[n] + 8
 *   + 2 × 6 × (cable from the main node to sub-node 0, m) / bit time) − 8) / 4, not rounded;
 * - the main node's RESPCYCS is the middle of MAX(RESPCYCS_DN) and MIN(RESPCYCS_UP),
 *   rounded; if MAX(RESPCYCS_DN) > MIN(RESPCYCS_UP) the bus can't carry the slots;
 * - sub-node n's RESPCYCS (its DISCVRY value) is ((MAIN × 4 + 7) − (n × 7.5 + n × 8.3
 *   + 2 × 6 × (cable from sub-node 0 to node n) / bit time + 7)) / 4, rounded; sub-node 0's
 *   is the main node's.
 *
 * The constants are the TRMs' averages: a turnaround of 8 bits, 7.5 bits through a node
 * downstream and 8.3 upstream, 6 ns of cable delay per metre; the bit time is one 1024th of
 * a frame (20.35 ns at 48 kHz). The TRMs' own worked example (cables of 10 m and 5 m,
 * 14/16 slots of 24 bits down, 12/10 of 16 bits up, TDM8 32-bit) gives 148 for the main node
 * and 143 for sub-node 1, and so does this code (test_response_cycles). RESPOFFS is 254
 * "when no TDM activity is present" (Rev 1.2); the stack takes that to mean a main node that
 * receives no upstream slots (Network works it out).
 *
 * The AD2437 TRM Rev 0.2 subtracts 7 instead of 8 and one cable delay instead of two in
 * RESPCYCS_UP, and rounds the main node's value down: under one cycle apart. ADI's
 * stack's calcMstrRespCycs differed more (48 kHz and 4 m per hop hard-coded, its own delay
 * terms, RESPCYCS arrays of 16 overrun at 16 sub-nodes; decisions.md, "Bugs fixed"), and its
 * DISCVRY was the main node's value less 4 per position (the AD242x TRM's Formula A).
 * SigmaStudio+ computes with RESPOFFS 254 ("accurate") unless "conservative" is set, so its
 * main-node value can be a few cycles above this stack's for the same slots.
 *
 * The arithmetic is in integers (milli-bits), as the RP2040 has no floating point unit.
 *
 * **Channel maps** (the AD242x TRM, "Managing A2B System Data Flow" and
 * "I2S/TDM Port Programming Concepts", and the AD2437 and AD2431-35 TRMs' I2SCFG, checked
 * with pdftotext). A node's frame buffers hold its TDM channels, counted across its data
 * pins as the TRMs number them (DTX0/DRX0's first, or alternately when interleaved), 32 at
 * most. Each pin carries the TDMMODE's channels (TDM2 to TDM32), times a sub-node's I2SRATE
 * multiple (2x, 4x: one list through the superframe). The pins on:
 * - AD242x: I2SCFG.TX0EN and TX1EN, RX0EN and RX1EN; with I2SGCFG.RXONDTX1 the DTX1 pin
 *   receives "in place of the DRX1 pin, and the values of TX1EN and RX1EN are ignored", so
 *   one TX pin at most and a second RX pin.
 * - AD243x: I2SCFG.TXPINS and RXPINS, a count of pins (0 to 4, or 7: two interleaved).
 * - A chip without the port (ChipCaps::hasI2s: the AD2427) has no TDM channels, whatever
 *   I2SCFG says; its RX channels are its PDM microphones.
 * PDM (both families, PDMCTL): a pin with PDMnEN takes its one or two microphones
 * (PDMnSLOTS) into the RX frame buffer, PDM0's first (the AD242x TRM's four-microphone
 * example), in place of its TDM channels; "the TDM settings do not affect the PDM block".
 * How PDM and TDM channels share the buffer when both are on isn't documented, so the
 * count is their sum, an upper bound. Not counted: PDMRATE (reduced rates are out of
 * scope) and PDMCTL2.PDMDEST (PDM to the node's own DTXn pins). A crossbar is the chip's
 * (ChipCaps::hasCrossbar): an AD242x has none, so its RX frame buffer is its RX channels in
 * order and its TX channels play its TX frame buffer in order.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "a2b/routing.h"

#include "chip_caps.h"
#include "detail/regdefs.h"

namespace a2b {
namespace {

/** TDM channels per data pin of each TDMMODE (TDM2, TDM4, TDM8, ... TDM32). */
constexpr uint8_t kTdmChannels[8] = {2, 4, 8, 12, 16, 20, 24, 32};
constexpr unsigned kFrameBuffer = 32;  ///< Locations in a frame buffer.

/** The pins an AD243x I2SCFG.TXPINS or RXPINS field turns on: 0 to 4, or 2 for 7
    (interleaved); a reserved value (5, 6) none. */
unsigned PinCount(unsigned field) {
  if (field <= 4) {
    return field;
  }
  return (field == 7) ? 2 : 0;
}

/** @p channels held to a frame buffer's 32. */
uint8_t ToChannels(unsigned channels) {
  return static_cast<uint8_t>((channels > kFrameBuffer) ? kFrameBuffer : channels);
}

constexpr int64_t kMilli = 1000;                 ///< Milli-bits per bit.
constexpr int64_t kSyncControlBits = 64;         ///< The TRMs' 64 (SCF/SRF bits).
constexpr int64_t kTurnaroundBits = 8;           ///< Turnaround time.
constexpr int64_t kNodeDelayMilliBits = 15800;   ///< 7.5 down + 8.3 up, per node.
constexpr int64_t kCableDelayPsPerMetre = 6000;  ///< 6 ns of cable delay per metre.
constexpr int64_t kBitsPerCycle = 4;             ///< RESPCYCS counts 4 bits.
constexpr int64_t kSubOffsetBits = 7;            ///< The 7 in (RESPCYCS × 4) + 7.
constexpr int64_t kFormulaBBits = 8;             ///< Formula B's − 8.
constexpr unsigned kSuperframeBits = 1024;       ///< Bits in a superframe, at any frame rate.

/** @p a / @p b rounded up, for @p b > 0. */
int64_t DivideUp(int64_t a, int64_t b) { return (a >= 0) ? (a + b - 1) / b : -((-a) / b); }

/** @p a / @p b rounded down, for @p b > 0. */
int64_t DivideDown(int64_t a, int64_t b) { return (a >= 0) ? a / b : -((-a + b - 1) / b); }

/** @p value held to 1-255, a RESPCYCS value. */
uint8_t ToRespCycles(int64_t value) {
  if (value < 1) {
    return 1;
  }
  return static_cast<uint8_t>((value > 255) ? 255 : value);
}

}  // namespace

/**
 * Works out the response cycles of a chain with the TRMs' Formula B (see the file's
 * comment).
 * @param input The chain: each sub-node's slots, the slot widths, RESPOFFS, the frame rate,
 *        each link's cable; or a main node's value to keep.
 * @return The main node's value and each sub-node's (held to 1-255), and the window. With
 *         RespCycleInput::mainRespCycles set, that main value is kept and ResponseCycles::fits
 *         is true. A chain with no sub-node fits, with the given main value.
 */
ResponseCycles ComputeResponseCycles(const RespCycleInput& input) {
  ResponseCycles result;
  const size_t count = (input.subNodeCount < static_cast<size_t>(kMaxSubNodes))
                           ? input.subNodeCount
                           : static_cast<size_t>(kMaxSubNodes);
  result.main = input.mainRespCycles;
  if (count == 0) {
    result.fits = true;
    return result;
  }
  const int64_t rate = (input.frameRateHz != 0) ? input.frameRateHz : 48000;
  const int64_t bitTimePs = (1000000000000LL + rate * 512) / (rate * 1024);
  // Cable delay of a length in decimetres, in milli-bits.
  auto cableMilliBits = [bitTimePs](int64_t dm) {
    return (kCableDelayPsPerMetre * kMilli * dm / 10 + bitTimePs / 2) / bitTimePs;
  };

  int64_t maxDn = 0;               // Cycles.
  int64_t minUpMilli = INT64_MAX;  // Milli-cycles.
  int64_t cableFromSub0 = 0;       // Decimetres, from sub-node 0 to node n.
  const int64_t mainCable = 2 * cableMilliBits(input.cableDm[0]);
  for (size_t n = 0; n < count; ++n) {
    if (n > 0) {
      cableFromSub0 += input.cableDm[n];
    }
    const int64_t dnBits = static_cast<int64_t>(input.dnSlots[n]) * input.dnSlotBits;
    const int64_t upBits = static_cast<int64_t>(input.upSlots[n]) * input.upSlotBits;
    const int64_t dn = (kSyncControlBits + dnBits + kTurnaroundBits - kFormulaBBits) * kMilli +
                       static_cast<int64_t>(n) * kNodeDelayMilliBits +
                       2 * cableMilliBits(cableFromSub0);
    const int64_t dnCycles = DivideUp(dn, kBitsPerCycle * kMilli);
    const int64_t up = static_cast<int64_t>(input.respOffset) * kBitsPerCycle * kMilli -
                       (kSyncControlBits + upBits + kTurnaroundBits) * kMilli - mainCable -
                       kFormulaBBits * kMilli;
    const int64_t upMilliCycles = DivideDown(up, kBitsPerCycle);
    maxDn = (dnCycles > maxDn) ? dnCycles : maxDn;
    minUpMilli = (upMilliCycles < minUpMilli) ? upMilliCycles : minUpMilli;
  }
  result.minMain = static_cast<uint16_t>((maxDn > 0xFFFF) ? 0xFFFF : maxDn);
  const int64_t maxMain = DivideDown(minUpMilli, kMilli);
  result.maxMain =
      static_cast<uint16_t>((maxMain < 0) ? 0 : ((maxMain > 0xFFFF) ? 0xFFFF : maxMain));
  if (input.mainRespCycles != 0) {
    result.fits = true;
  } else {
    result.fits = (maxDn * kMilli <= minUpMilli);
    result.main = ToRespCycles(DivideDown(maxDn * kMilli + minUpMilli + kMilli, 2 * kMilli));
  }

  cableFromSub0 = 0;
  for (size_t n = 0; n < count; ++n) {
    if (n > 0) {
      cableFromSub0 += input.cableDm[n];
    }
    const int64_t sub =
        (static_cast<int64_t>(result.main) * kBitsPerCycle + kSubOffsetBits) * kMilli -
        static_cast<int64_t>(n) * kNodeDelayMilliBits - 2 * cableMilliBits(cableFromSub0) -
        kSubOffsetBits * kMilli;
    result.sub[n] = ToRespCycles(DivideDown(sub + 2 * kMilli, kBitsPerCycle * kMilli));
  }
  return result;
}

/**
 * Works out how much of a superframe the busiest sub-node's frames take (ADI's getNwBw).
 * For sub-node n it adds the downstream and upstream terms of Formula B (see the file's
 * comment): the 64 bits of control, its slots, the turnaround, and the node and cable delays
 * to and from it. A superframe is 1024 bits at any frame rate. This is ADI's stack's
 * measure, with this stack's constants; whether the slots fit is the response-cycle window
 * (ResponseCycles::fits), which can be empty at less than 100%.
 * @param input The chain, as for ComputeResponseCycles().
 * @return The sub-node with the most bits, and its bits and percentage; kMainNode and
 *         nothing for a chain with no sub-node.
 */
BandwidthUsage ComputeBandwidth(const RespCycleInput& input) {
  BandwidthUsage usage;
  const size_t count = (input.subNodeCount < static_cast<size_t>(kMaxSubNodes))
                           ? input.subNodeCount
                           : static_cast<size_t>(kMaxSubNodes);
  const int64_t rate = (input.frameRateHz != 0) ? input.frameRateHz : 48000;
  const int64_t bitTimePs = (1000000000000LL + rate * 512) / (rate * 1024);
  auto cableMilliBits = [bitTimePs](int64_t dm) {
    return (kCableDelayPsPerMetre * kMilli * dm / 10 + bitTimePs / 2) / bitTimePs;
  };
  int64_t mostMilli = -1;
  int64_t cableFromSub0 = 0;
  const int64_t mainCable = 2 * cableMilliBits(input.cableDm[0]);
  for (size_t n = 0; n < count; ++n) {
    if (n > 0) {
      cableFromSub0 += input.cableDm[n];
    }
    const int64_t dnBits = static_cast<int64_t>(input.dnSlots[n]) * input.dnSlotBits;
    const int64_t upBits = static_cast<int64_t>(input.upSlots[n]) * input.upSlotBits;
    const int64_t milli = (2 * (kSyncControlBits + kTurnaroundBits) + dnBits + upBits) * kMilli +
                          static_cast<int64_t>(n) * kNodeDelayMilliBits +
                          2 * cableMilliBits(cableFromSub0) + mainCable;
    if (milli > mostMilli) {
      mostMilli = milli;
      usage.node = static_cast<NodeAddr>(n);
    }
  }
  if (mostMilli >= 0) {
    const int64_t bits = DivideDown(mostMilli + kMilli / 2, kMilli);
    usage.bits = static_cast<uint16_t>((bits > 0xFFFF) ? 0xFFFF : bits);
    usage.percent = static_cast<uint16_t>(usage.bits * 100u / kSuperframeBits);
  }
  return usage;
}

/**
 * Gives the response cycles of a chain, for the routing plan and for each new node's
 * DISCVRY. Override it to take known cable lengths or another formula into account.
 * @param input The chain (see RespCycleInput).
 * @return ComputeResponseCycles(input).
 */
ResponseCycles RoutingPolicy::RespCycles(const RespCycleInput& input) const {
  return ComputeResponseCycles(input);
}

/**
 * Counts the channels a node's port gives each way per superframe, and tells whether its
 * chip has a crossbar (see the file's comment).
 * @param port The node's chip and its I2S/TDM and PDM settings.
 * @return Its channel map; no channels and no crossbar for a chip the stack doesn't know.
 */
ChannelMap ComputeChannelMap(const PortSettings& port) {
  ChannelMap map;
  const ChipCaps* caps = LookUpChipCaps(kAdiVendorId, port.productId);
  if (caps == nullptr) {
    return map;
  }
  map.crossbar = caps->hasCrossbar;
  unsigned txPins = 0;
  unsigned rxPins = 0;
  if (!caps->hasI2s) {
    // No I2S/TDM port (AD2427): only its PDM microphones, below.
  } else if (caps->family == ChipFamily::kAd242x) {
    const bool rxOnDtx1 = (port.i2sgcfg & reg::kI2sgcfgRxondtx1) != 0;
    txPins = ((port.i2scfg & reg::kI2scfgTx0en) != 0 ? 1u : 0u) +
             (!rxOnDtx1 && (port.i2scfg & reg::kI2scfgTx1en) != 0 ? 1u : 0u);
    rxPins = ((port.i2scfg & reg::kI2scfgRx0en) != 0 ? 1u : 0u) +
             (rxOnDtx1 || (port.i2scfg & reg::kI2scfgRx1en) != 0 ? 1u : 0u);
  } else {
    txPins = PinCount(port.i2scfg & reg::kI2scfgTxpins);
    rxPins = PinCount((port.i2scfg & reg::kI2scfgRxpins) >> reg::kI2scfgRxpinsPos);
  }
  unsigned pdmPins = 0;
  unsigned pdmChannels = 0;
  if ((port.pdmctl & reg::kPdmctlPdm0en) != 0) {
    ++pdmPins;
    pdmChannels += ((port.pdmctl & reg::kPdmctlPdm0slots) != 0) ? 2 : 1;
  }
  if ((port.pdmctl & reg::kPdmctlPdm1en) != 0) {
    ++pdmPins;
    pdmChannels += ((port.pdmctl & reg::kPdmctlPdm1slots) != 0) ? 2 : 1;
  }
  rxPins = (rxPins > pdmPins) ? rxPins - pdmPins : 0;  // A PDM pin gives no TDM channels.
  unsigned rate = 1;
  if (port.node != kMainNode) {
    const unsigned field = port.i2srate & reg::kI2srateRate;
    rate = (field == reg::kI2srateX2) ? 2 : (field == reg::kI2srateX4) ? 4 : 1;
  }
  const unsigned perPin = kTdmChannels[port.i2sgcfg & reg::kI2sgcfgTdmmode] * rate;
  map.txChannels = ToChannels(txPins * perPin);
  map.rxChannels = ToChannels(rxPins * perPin + pdmChannels);
  return map;
}

/**
 * Gives a node's channel map, for every routing plan: whether its channels reach the bus
 * through a crossbar, and how many its port gives each way. Override it when a node's port
 * is set up some other way than the stack knows (a SubNode hook writing I2SCFG, say).
 * Network uses a crossbar only on a chip that has one.
 * @param port The node's chip and its port settings, as the stack configured them.
 * @return ComputeChannelMap(port).
 */
ChannelMap RoutingPolicy::MapGroups(const PortSettings& port) const {
  return ComputeChannelMap(port);
}

}  // namespace a2b
