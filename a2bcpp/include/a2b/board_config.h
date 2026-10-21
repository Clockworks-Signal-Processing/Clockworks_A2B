/**
 * @file board_config.h
 * @brief Everything the stack needs to know about a board's main node: addresses, the
 *        main node's register settings, the board's peripheral script, timings and policy.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_A2B_BOARD_CONFIG_H_
#define A2BCPP_A2B_BOARD_CONFIG_H_

#include <cstdint>

#include "a2b/link_profile.h"
#include "a2b/module_info.h"
#include "a2b/op_list.h"
#include "a2b/register_image.h"
#include "a2b/routing.h"

namespace a2b {

/** @brief BoardConfig::highPowerSwitchCfg: keep the power configuration the main node
 *         detects, without checking it. */
inline constexpr uint8_t kPowerConfigAsDetected = 0xFF;

/** @brief Main-node and discovery timings, in milliseconds. The timing guide
 *         (a2bcpp/docs/timing.md) lists each wait and where its value comes from. */
struct BoardTimings {
  /** RESETn held low at Start(), on a Platform with a reset line
      (Platform::SetMainNodeReset()). The data sheets ask for 10 us (tRST); ADI's
      RP2040 app held it for 100 ms. */
  uint32_t resetPulseMs = 100;
  /** PLL lock time (tPLK): CONTROL.MSTR to the deadline for MSTR_RUNNING (SigmaStudio+
      nDiscoveryStartDelay; ADI's stack used 25 when it was 0). */
  uint32_t discoveryStartDelayMs = 25;
  /** The main node's soft reset to its CONTROL.MSTR write, at every start and restart: it
      must cover an AD243x's SUSTAIN (1024 SYNC periods, about 21.3 ms at 48 kHz), or the
      main node wedges until power-cycled. The stack never waits less than 25. The name is
      SigmaStudio+'s (nRediscWaitTime), which ImportSsPlus() copies. */
  uint32_t rediscoveryWaitMs = 250;
  /** DISCVRY to the deadline for DSCDONE. ADI's stack used 50, or 70 behind a
      medium- or high-power AD243x; the stack can't know the node in advance, so 70. */
  uint32_t discoveryTimeoutMs = 70;
  /** A probe that found nothing to the next probe (ADI's idle probe period). */
  uint32_t probeIntervalMs = 500;
  /** Interrupt checks: time between INTSRC reads (ADI's stack polled every 20). */
  uint32_t interruptPollMs = 20;
  /** Chain checks: time between reads of every node's VENDOR and PRODUCT, which find a
      dropped node (the TRMs' "Bus Drop Detection"; ADI's stack read VENDOR before
      each probe, every 500). */
  uint32_t chainCheckMs = 500;
  /** Time between the writes that clear every node's bit error count, BECNT (the TRMs ask
      for once a second, as ADI's stack did); 0 for none. */
  uint32_t becntResetMs = 1000;
  /** A rejected node to the next probe of its position, which finds a different module
      plugged in there. The same node found again is switched off without a new event. */
  uint32_t rejectedProbeMs = 2000;
};

/**
 * @brief Optional app hooks on the main node. They edit settings only: no bus access.
 *
 * Derive from it, override what you need, and point BoardConfig::hooks at it.
 */
class BoardHooks {
 public:
  virtual ~BoardHooks() = default;

  /**
   * @brief Called at every main-node start-up, before the first register write, with the
   *        copy of BoardConfig::mainNode that the stack will write. Edit it to change
   *        settings for this start-up only.
   * @param image The main-node register image.
   */
  virtual void BeforeMainNodeInit(RegisterImage& image) { (void)image; }
};

/** @brief What the app allows on the bus. NetworkListener::OnNodeIdentified() gets the
 *         power budget's decision on each node, and may overrule it. */
struct BoardPolicy {
  /** Sub-nodes to accept before discovery stops, 1-16: the most the system design has.
      Once that many are in, no more probes are made until one leaves. The chips take 16;
      the default is 15, as the 16th node has given trouble on hardware. Taken at
      Network::Start(). */
  uint8_t maxSubNodes = 15;
  /** Power budget: the most the bus-powered sub-nodes may draw together, by their module
      info's maximum power (ModuleInfo::maxPowerMw); 0 for no limit. A node that would
      take the sum over it is rejected (RejectReason::kPowerBudget). */
  uint32_t maxBusPowerMw = 0;
  /** Current budget, as #maxBusPowerMw for ModuleInfo::maxCurrentMa; 0 for no limit. */
  uint32_t maxBusCurrentMa = 0;
};

/** @brief What the routing needs to know about the board: the main node's channel
 *         groups, the frame rate and the cables, and the app's routing policy. */
struct BoardRouting {
  /** The main node's channel groups: the RX groups it sends down the bus from its DRXn
      pins, the TX groups it plays on its DTXn pins. ImportSsPlus() leaves them empty (an
      export has none): fill them for the board. */
  ChannelGroups groups;
  /** The bus's frame rate (its SYNC rate), for the response cycles' bit time. ADI's
      stack always took 48 kHz. */
  uint32_t frameRateHz = 48000;
  /** Each link's cable length in decimetres, for the response cycles, unless
      Network::SetCableLength() gives that link its own or the upstream node's image gives
      its B-side cable (ModuleInfo::bCableDm). The AD2431-35 TRM ("Response
      Cycles"): with the lengths unknown, the calculation for 4 m is adequate, as each node
      adjusts its response time during discovery (-4 to +15 bit times). On the bench, short
      links left at 4 m dropped nodes now and then: give lengths close to the real ones.
      From 1. */
  uint16_t cableDm = 40;
  /** What the stack mutes while it writes a routing plan. The
      NetworkListener's OnAudioMute() and OnAudioUnmute() are called in any mode. */
  RoutingMute mute = RoutingMute::kLocal;
  /** The app's routing policy, or nullptr for the default; must outlive the bus. */
  const RoutingPolicy* policy = nullptr;
};

/**
 * @brief One bus's main node and the board around it. Fill it by hand or with
 *        ImportSsPlus() (a2bcpp/import/ss_plus), then give it to the bus's Network.
 *
 * See the main-node configuration guide (a2bcpp/docs/main_node_config.md).
 */
struct BoardConfig {
  /** This bus's index: the Platform's GPIO calls (reset, IRQ, power-good) take it, and
      SubNodeIdentity::bus reports it. Give each bus on a Platform its own. */
  uint8_t busIndex = 0;
  uint8_t mainAddr = 0x68;  ///< 7-bit I2C address of the main node.
  uint8_t busAddr = 0x69;   ///< 7-bit I2C address that reaches the sub-nodes.
  /** Main node's PRODUCT: 0x28, 0x33 or 0x37. (The AD2427, 0x27, is not main-node
      capable; Network::Start() refuses it.) */
  uint8_t mainProduct = 0;
  bool mainLocalPower = true;  ///< The main node has its own supply.
  /** Main node's power configuration: 0 (CFG-0), 4 (CFG-4), or #kPowerConfigAsDetected
      (the default). ImportSsPlus() takes it from the export (nHighPwrSwitchCfg). On an
      AD243x the stack overrides a different one the chip detected (SWSTAT2, SWCTL2)
      before the first probe. */
  uint8_t highPowerSwitchCfg = kPowerConfigAsDetected;

  /** Main-node register settings, in the order they are written. It holds the
      configuration only; the stack chooses when to write each register, and owns
      NODEADR, MMRPAGE, DISCVRY, the interrupt-pending registers, the CONTROL command
      bits and CONTROL.XCVRBINV (from defaultLink), SWCTL.ENSW, and the routing
      registers (the slot registers, RXMASK0-7 and TXXBAR0-31: the routing plan's).
      Its RESPCYCS is used until a routing plan sets its own; its SLOTFMT and I2SGCFG give
      the plan's slot widths and response offset. */
  RegisterImage mainNode;

  /** Operations on the board's own peripherals (e.g. a codec on the host I2C bus).
      Optional; the list and its data must outlive the bus. The first
      #hostOpsBeforeMainNode run at Start(), before the main node is touched (a codec
      that clocks the main node, as the AB0020's ADAU1761 does); the rest run once the
      main node is configured. */
  const OpList* hostScript = nullptr;

  /** How many operations at the start of #hostScript run before the main node is started:
      once per Network::Start(), not when the bus starts again (a PLL relock), since
      programming a clock source again would unlock the PLL again. ImportSsPlus() puts
      there the peripherals the export doesn't mark for after discovery (bPostDiscCfg). */
  size_t hostOpsBeforeMainNode = 0;

  /** The link to sub-node 0 (the main node's B side), unless Network::SetLinkProfile()
      sets one for it. A later link is the upstream node's SubNode::DownstreamLink(), by
      default the one its image's B port type gives; a node class may return this one
      (SubNode::BoardLink()). ImportSsPlus() takes XCVRBINV from the export's main-node
      CONTROL. */
  LinkProfile defaultLink;

  BoardTimings timings;         ///< Main-node and discovery timings.
  BoardPolicy policy;           ///< Limits.
  BoardRouting routing;         ///< The main node's channel groups, frame rate, cables.
  BoardHooks* hooks = nullptr;  ///< App hooks, or none; must outlive the bus.
};

}  // namespace a2b

#endif  // A2BCPP_A2B_BOARD_CONFIG_H_
