/**
 * @file routing.h
 * @brief Audio routing: connections between nodes' channel groups, the routing plan the
 *        stack builds from them (slots, masks, crossbars, response cycles), and the policy
 *        an app may change (response cycles, each node's channel map).
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_A2B_ROUTING_H_
#define A2BCPP_A2B_ROUTING_H_

#include <cstddef>
#include <cstdint>

#include "a2b/types.h"

namespace a2b {

/** @brief Most connections a Network keeps (ADI's console's limit). */
inline constexpr size_t kMaxConnections = 32;

/** @brief Not a node of the bus: what Network::ConnectionNode() gives for an end whose
 *         node isn't in the bus. */
inline constexpr NodeAddr kNoNode = -2;

/** @brief One end of a connection: a node and one of its channel groups. */
struct ConnectionEnd {
  /** The main node; #chipId is not used. */
  bool mainNode = false;
  /** A sub-node, by its chip ID (CHIPID0-5): the connection follows the module to any
      position, and waits while it is not in the bus. */
  uint8_t chipId[6] = {};
  uint8_t group = 0;  ///< Group ID: an RX group of a source, a TX group of a sink.
};

/** @brief An audio connection: a source node's RX group to a sink node's TX group. */
struct Connection {
  ConnectionEnd source;  ///< Where the audio comes from.
  ConnectionEnd sink;    ///< Where it goes.
};

/** @brief Why a routing plan could not be built or written. */
enum class RoutingError : uint8_t {
  kNone,         ///< The plan was built (and written, for EventType::kRoutingApplied).
  kNoSuchGroup,  ///< A connection names a group its node doesn't have.
  kSameNode,     ///< A connection's source and sink are the same node.
  /** A link would carry more than 32 data slots in one direction (TRMs: DNSLOTS and
      UPSLOTS, 32 at most). */
  kTooManySlots,
  /** A node would play more than 32 channels from the bus (its TX frame buffer holds 32). */
  kTooManyChannels,
  /** The slots don't fit the superframe: no main-node response cycle is in the TRM's
      window (MAX(RESPCYCS_DN) > MIN(RESPCYCS_UP)). */
  kNoResponseCycles,
  kI2cError,  ///< Writing the plan failed (the chain is checked).
  /** A connection uses a channel its node's port doesn't give: past the RX channels of a
      source or the TX channels of a sink that RoutingPolicy::MapGroups() counts from the
      node's I2S/TDM and PDM settings (an I2S port left off gives none). */
  kNoSuchChannel,
  /** A node without a crossbar (AD242x) can't play its channels as its connections ask: its
      TX channels play the slots it takes in bus order, from channel 0 on with no gap, so a
      sink's channels must follow the slots' order on the bus (cookbook, "Routing"). */
  kChannelOrder,
};

/** @brief An audio stream for Network::SetAudioRoutes(): one source RX group sent to
 *         several sinks' TX groups (ADI's a2bpnp_StreamInfo), the nodes by position. */
struct AudioStream {
  /** @brief One sink of a stream: a node and its TX group. */
  struct Sink {
    NodeAddr node = kMainNode;  ///< The sink node: kMainNode or a sub-node's position.
    uint8_t group = 0;          ///< Its TX group's ID.
  };

  NodeAddr source = kMainNode;  ///< The source node: kMainNode or a sub-node's position.
  uint8_t sourceGroup = 0;      ///< Its RX group's ID.
  size_t sinkCount = 0;         ///< Sinks used, at most #kMaxSinks.
  /** Most sinks a stream has: every other node of a bus of 16 sub-nodes and the main node. */
  static constexpr size_t kMaxSinks = kMaxSubNodes;
  Sink sinks[kMaxSinks];  ///< The sinks, the first #sinkCount.
};

/** @brief How the stack mutes audio while it writes a routing plan (BoardRouting::mute). */
enum class RoutingMute : uint8_t {
  /** Each node that would play a wrong channel between the new slots (NEWSTRCT) and the new
      crossbars plays silence from before the slots are applied until its new crossbar is
      written (TXXBAR location 31, which nothing fills, as in ADI's stack): a node whose TX crossbar
      changes, and one that plays a slot whose channel its source's new RXMASK
      changes (RXMASK takes effect at once). Only a node with a crossbar (AD243x) can be
      muted this way; an AD242x's outputs depend on the slot registers alone, which take
      effect at once at NEWSTRCT. If a node that needs the mute can't have it (an AD242x
      playing an AD243x's slots, a node that plays all 32 locations), the plan mutes the
      whole bus instead, as kGlobal. ADI's default. */
  kLocal,
  /** The whole bus is muted (DATCTL.DNS and UPS clear, applied by NEWSTRCT) for the plan:
      the only mute that reaches an AD242x node, and a node that plays all 32 locations.
      Costs one more NEWSTRCT (ADI's bGlobalMuteDuringCrossbar). */
  kGlobal,
  /** The stack mutes nothing; the NetworkListener hooks still tell the app. */
  kNone,
};

/** @brief What the stack mutes while it writes a routing plan: what
 *         NetworkListener::OnAudioMute() and OnAudioUnmute() are told. */
struct MuteScope {
  /** The whole bus is muted (RoutingMute::kGlobal, Network::MuteNetwork(), or kLocal for a
      plan with a node it can't mute alone): every node is silent whatever #nodes says. */
  bool network = false;
  /** Nodes whose outputs are muted locally: [0] the main node, [n + 1] sub-node n. */
  bool nodes[kMaxSubNodes + 1] = {};

  /** @brief True if node @p node (kMainNode or a position) plays silence. */
  bool Includes(NodeAddr node) const {
    return network || (node >= kMainNode && node < kMaxSubNodes && nodes[node + 1]);
  }
};

/** @brief How full the bus is (ADI's a2b_pnp_GetNwBW): the share of a superframe's 1024
 *         bits a node's frames take. */
struct BandwidthUsage {
  NodeAddr node = kMainNode;  ///< The sub-node that takes the most; kMainNode with no sub-node.
  uint16_t bits = 0;          ///< Its frames' bits of the 1024 in a superframe.
  uint16_t percent = 0;       ///< #bits as a percentage of the 1024, rounded down.
};

/** @brief What the response-cycle calculation needs: the chain's slots and links. */
struct RespCycleInput {
  size_t subNodeCount = 0;  ///< Sub-nodes in the chain.
  /** Downstream data slots each sub-node receives on its A side (NUM_DNSLOTS). */
  uint8_t dnSlots[kMaxSubNodes] = {};
  /** Upstream data slots each sub-node drives to its A side (NUM_UPSLOTS). */
  uint8_t upSlots[kMaxSubNodes] = {};
  /** Bits a downstream data slot takes on the bus: its size plus the parity bit, or plus
      ECC (AD2431-35 TRM, Table 3-3), from the main node's SLOTFMT. */
  uint8_t dnSlotBits = 25;
  uint8_t upSlotBits = 25;  ///< The same for an upstream data slot.
  /** RESPOFFS: the TRMs' table of the main node's TDM mode and channel size, or 254 when the
      main node receives no upstream slots (no TDM activity, AD2431-35 TRM Rev 1.2). */
  uint16_t respOffset = 248;
  uint32_t frameRateHz = 48000;  ///< The SYNC rate: one superframe per frame.
  /** Each link's cable, in decimetres: [n] is the cable into sub-node n. */
  uint16_t cableDm[kMaxSubNodes] = {};
  /** 0: work out the main node's value. Otherwise keep this main node's value and give
      the sub-nodes' only (Network does this for each new node's DISCVRY). */
  uint8_t mainRespCycles = 0;
};

/** @brief Response cycles for a chain. */
struct ResponseCycles {
  /** A main-node value fits the slots (the window isn't empty) and every value can be
      written (1-255); with the main node's value given, every sub-node's can be written. */
  bool fits = false;
  uint8_t main = 0;                ///< The main node's RESPCYCS.
  uint8_t sub[kMaxSubNodes] = {};  ///< Each sub-node's RESPCYCS (its DISCVRY value).
  uint16_t minMain = 0;            ///< MAX(RESPCYCS_DN[n]): the lowest main-node value.
  uint16_t maxMain = 0;            ///< MIN(RESPCYCS_UP[n]), rounded down: the highest.
};

ResponseCycles ComputeResponseCycles(
    const RespCycleInput& input);  ///< The TRMs' response-cycle Formula B.

BandwidthUsage ComputeBandwidth(
    const RespCycleInput& input);  ///< The busiest sub-node's share of a superframe.

/** @brief A node's I2S/TDM and PDM settings as the stack configures it: what
 *         RoutingPolicy::MapGroups() works from. */
struct PortSettings {
  NodeAddr node = kMainNode;  ///< The node: kMainNode or a sub-node's position.
  uint8_t productId = 0;      ///< Its transceiver's PRODUCT register (0x28 for an AD2428).
  /** I2SGCFG: TDMMODE and, on an AD242x, RXONDTX1. The main node's comes from its
      BoardConfig image, a sub-node's from its module info; a register they don't set is 0,
      its power-on value. */
  uint8_t i2sgcfg = 0;
  uint8_t i2scfg = 0;   ///< I2SCFG: the data pins on (the two families encode it differently).
  uint8_t i2srate = 0;  ///< I2SRATE: a sub-node's 2x or 4x rate (the main node's does nothing).
  uint8_t pdmctl = 0;   ///< PDMCTL: PDM0EN and PDM0SLOTS, PDM1EN and PDM1SLOTS.
};

/** @brief How a node's channels reach the bus, for the routing planner. */
struct ChannelMap {
  /** RXMASK and TXXBAR (AD243x): any of its channels to and from any slot. Without them
      (AD242x), its RX frame buffer holds its RX channels in order, and its TX channels play
      its TX frame buffer in order. Network uses a crossbar only on a chip that has one. */
  bool crossbar = false;
  uint8_t rxChannels = 0;  ///< RX channels the port gives per superframe (0 to n - 1), 32 at most.
  uint8_t txChannels = 0;  ///< TX channels it gives, likewise.
};

ChannelMap ComputeChannelMap(
    const PortSettings& port);  ///< A node's channels, from the TRMs' port settings.

/**
 * @brief How the stack routes audio, where an app may differ: derive from it, override
 *        what you need, and point BoardRouting::policy at it.
 *
 * Hooks compute values only: no bus access. See the customisation cookbook
 * (a2bcpp/docs/cookbook.md), "Routing".
 */
class RoutingPolicy {
 public:
  virtual ~RoutingPolicy() = default;

  virtual ResponseCycles RespCycles(
      const RespCycleInput& input) const;  ///< Response cycles; ComputeResponseCycles() by default.
  virtual ChannelMap MapGroups(
      const PortSettings& port) const;  ///< A node's channels; ComputeChannelMap() by default.
};

/** @brief One node's routing registers, as the plan writes them. */
struct NodeRouting {
  uint8_t dnSlots = 0;     ///< DNSLOTS: slots passed down (the main node: sent down).
  uint8_t upSlots = 0;     ///< UPSLOTS: slots passed up (the main node: received).
  uint8_t ldnSlots = 0;    ///< LDNSLOTS, with DNMASKEN (sub-nodes): slots added downstream.
  uint8_t lupSlots = 0;    ///< LUPSLOTS (sub-nodes): slots added upstream.
  uint8_t dnOffset = 0;    ///< DNOFFSET (sub-nodes): RX frame buffer offset of LDNSLOTS.
  uint8_t upOffset = 0;    ///< UPOFFSET (sub-nodes): RX frame buffer offset of LUPSLOTS.
  uint8_t respCycles = 0;  ///< RESPCYCS.
  uint32_t dnMask = 0;     ///< DNMASK0-3 (sub-nodes): downstream slots played.
  uint32_t upMask = 0;     ///< UPMASK0-3 (sub-nodes): upstream slots played.
  /** RXMASK0-7: TDM channels taken into the RX frame buffer. A node without a crossbar
      takes all of its port's (not written: what it does without one). */
  uint8_t rxMask[8] = {};
  /** TXXBAR0-31: TX frame buffer location of each TDM channel. A node without a crossbar:
      channel n plays location n (not written). */
  uint8_t txXbar[32] = {};
};

/** @brief The routing a Network wrote last: each node's registers and each link's slots. */
struct RoutingPlan {
  bool valid = false;             ///< It was written; the fields below hold it.
  size_t subNodeCount = 0;        ///< Sub-nodes in the chain it was built for.
  size_t routed = 0;              ///< Connections it carries (both ends in the bus).
  NodeRouting main;               ///< The main node's registers.
  NodeRouting sub[kMaxSubNodes];  ///< Each sub-node's.
  /** Downstream data slots on each link: [n] is the link into sub-node n. */
  uint8_t linkDnSlots[kMaxSubNodes] = {};
  /** Upstream data slots on each link: [n] is the link out of sub-node n's A side. */
  uint8_t linkUpSlots[kMaxSubNodes] = {};
  ResponseCycles respCycles;  ///< The response cycles, and their window.
  BandwidthUsage bandwidth;   ///< How full the bus is with these slots.
};

}  // namespace a2b

#endif  // A2BCPP_A2B_ROUTING_H_
