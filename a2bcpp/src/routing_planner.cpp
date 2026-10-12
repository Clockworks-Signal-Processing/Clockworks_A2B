/**
 * @file routing_planner.cpp
 * @brief RoutingPlanner: the slots, slot masks, offsets and crossbars of a chain of AD243x
 *        (crossbar) and AD242x (TDM order) nodes.
 *
 * Replaces ADI's crossbarCalc, AutoCalcSlot and the pass-list pruning of
 * a2bpnp_calc.c (decisions.md, "RoutingPolicy"), from the TRMs' slot model (AD2437 TRM,
 * "I2S/TDM Flexible Mapping" and "Slot Register Configuration"; the AD2431-35 TRM's is the
 * same), checked with pdftotext:
 * - **RX side.** A node's RXMASK0-7 pick the TDM channels it takes from its DRXn pins into
 *   its RX frame buffer, in channel order. The main node sends DNSLOTS slots down from
 *   location 0. A sub-node adds LDNSLOTS slots downstream from location DNOFFSET (with
 *   LDNSLOTS.DNMASKEN set) and LUPSLOTS upstream from UPOFFSET.
 * - **Passing on.** A sub-node passes the first DNSLOTS slots it receives on its A side to
 *   its B side, then adds its own; upstream, the first UPSLOTS from B to A, then its own.
 *   Slots after those are dropped. The main node receives the first UPSLOTS upstream slots.
 * - **TX side.** A sub-node takes the downstream slots DNMASK0-3 select, then the upstream
 *   slots UPMASK0-3 select, into its TX frame buffer from location 0, in slot order (the
 *   main node: its UPSLOTS slots). TXXBARn gives the frame buffer location of TDM channel n
 *   on its DTXn pins. 32 locations.
 * - Slot registers take effect at CONTROL.NEWSTRCT; the crossbar registers at once.
 *
 * **The plan.** A connection pairs the source's RX group channels with the sink's TX group
 * channels in order (the lowest with the lowest); a source channel without a partner is not
 * sent. Each node's RXMASK takes the channels it sends; downstream ones go out as one run of
 * its RX frame buffer (DNOFFSET, LDNSLOTS), upstream ones as another (UPOFFSET, LUPSLOTS).
 * Channels between them that go the other way only are carried in the run too, unused: the
 * frame buffer is in channel order, so a run can't skip them. Walking down the chain from
 * the main node, each node plays the slots meant for it (DNMASK) and passes on the shortest
 * run of slots that holds every slot a node after it needs (DNSLOTS); a slot no one further
 * on needs is dropped only once it is last. Then up the chain from the last node, the same
 * with UPMASK and UPSLOTS, and the main node receives up to the last slot meant for it. The
 * TX crossbar of each node points each of its sink channels at the frame buffer location
 * its slot landed in, and every other channel at location 31, which nothing fills (
 * ADI's stack's local mute used it the same way) unless all 32 are in use.
 *
 * **Nodes without a crossbar** (the AD242x TRM, "Managing A2B System Data
 * Flow": the same slot registers and the same passing on, with no RXMASK or TXXBAR). Its
 * RX frame buffer holds all of its port's RX channels in order, so a run carries every
 * channel from the lowest it sends to the highest, and its offset is the lowest channel; the
 * main node, which has no offset register, sends from channel 0. Its TX channel n plays
 * frame buffer location n, so a sink channel must be the location its slot lands in: the
 * slots it plays, in bus order, fill its channels from 0 with no gap (downstream before
 * upstream; on the bus, a node's own slots follow those it passes on, so downstream the
 * main node's come first and then each sub-node's in chain order, and upstream the last
 * sub-node's first). Otherwise the plan fails as RoutingError::kChannelOrder: the app's
 * groups and connections must follow that order. A main node without a crossbar plays
 * every slot it receives, wanted or not, so a slot no one wants ahead of a wanted one
 * shifts the wanted one too. The plan's RXMASK and TXXBAR of such a node are what it does
 * without them (Network doesn't write them on a chip that has none), so a node mapped this
 * way through RoutingPolicy::MapGroups() behaves the same on an AD243x.
 *
 * Every channel a connection uses must be on its node's port (ChannelMap: an RX channel of
 * the source, a TX channel of the sink), or the plan fails as RoutingError::kNoSuchChannel.
 *
 * Differences from ADI's calculation (decisions.md, "Bugs fixed"; a differential test
 * against a2bpnp_calc.c is planned): it kept every RX channel (RXMASK 0xFF, an
 * OR on the reset value), computed DNOFFSET/UPOFFSET from stream indexes, carried frame
 * offsets and crossbar indexes over from one node to the next, assumed a group's ID was its
 * index, and ignored the 32-slot limits.
 *
 * Limits are checked: 32 data slots on a link each way, 32 played channels per node (its
 * TX frame buffer). The response cycles (and with them the superframe's bit budget) are
 * Network's, through RoutingPolicy::RespCycles(); the channel maps too, through
 * RoutingPolicy::MapGroups(). Each node is planned by its own map, so a chain may mix the two
 * kinds of node in any order (test_sim_mixed_routing): a crossbar node places
 * only what it plays and picks only its own channels, so the TDM order a node without one
 * needs is the bus order whatever lies between.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "routing_planner.h"

#include <cstring>

#include "detail/regdefs.h"

namespace a2b {
namespace {

/** A slot's content: the node index (high byte) and its RX channel (low byte). */
uint16_t SignalOf(size_t index, unsigned channel) {
  return static_cast<uint16_t>((index << 8) | channel);
}

/** The node index of a slot's content. */
size_t NodeOfSignal(uint16_t signal) { return signal >> 8; }

/** The RX channel of a slot's content. */
unsigned ChannelOfSignal(uint16_t signal) { return signal & 0xFF; }

/** Channels in @p channels. */
unsigned Count(uint32_t channels) {
  unsigned count = 0;
  for (; channels != 0; channels &= channels - 1) {
    ++count;
  }
  return count;
}

/** Channels of @p channels below @p channel: its rank in channel order. */
unsigned Rank(uint32_t channels, unsigned channel) {
  return Count(channels & ((channel >= 32) ? 0xFFFFFFFFu : ((1u << channel) - 1)));
}

/** The channel of rank @p rank in @p channels (it must have that many). */
unsigned Nth(uint32_t channels, unsigned rank) {
  for (unsigned channel = 0; channel < 32; ++channel) {
    if ((channels & (1u << channel)) != 0) {
      if (rank == 0) {
        return channel;
      }
      --rank;
    }
  }
  return 32;
}

/** The lowest @p count channels of @p channels. */
uint32_t Lowest(uint32_t channels, unsigned count) {
  uint32_t kept = 0;
  for (unsigned channel = 0; channel < 32 && count > 0; ++channel) {
    if ((channels & (1u << channel)) != 0) {
      kept |= 1u << channel;
      --count;
    }
  }
  return kept;
}

/** True if @p channels has channel @p channel. */
bool Has(uint32_t channels, unsigned channel) {
  return channel < 32 && (channels & (1u << channel)) != 0;
}

/** Channels 0 to @p count - 1. */
uint32_t FirstChannels(unsigned count) { return (count >= 32) ? 0xFFFFFFFFu : ((1u << count) - 1); }

}  // namespace

/**
 * Builds the routing registers of every node of a chain: slots, masks, offsets, RXMASK and
 * TXXBAR. RESPCYCS is left 0 for Network to fill in. On failure the plan holds a partial
 * result, not to be written.
 * @param input The chain's nodes (channel maps and groups) and the connections to carry.
 * @param plan Filled with the plan (RoutingPlan::valid is left false).
 * @param node Set to the node at fault when it fails: the connection end without the group
 *        or the channel, the node whose link or frame buffer overflows, the node without a
 *        crossbar whose channels can't be in order.
 * @return RoutingError::kNone, or why there is no plan.
 */
RoutingError RoutingPlanner::Build(const Input& input, RoutingPlan& plan, NodeAddr& node) {
  m_Input = &input;
  m_Plan = &plan;
  Clear(plan);
  plan.subNodeCount = input.subNodeCount;
  plan.routed = input.routeCount;
  node = kNoNode;
  RoutingError error = Check(input, node);
  if (error == RoutingError::kNone) {
    error = Downstream(node);
  }
  if (error == RoutingError::kNone) {
    error = Upstream(node);
  }
  return error;
}

/**
 * Empties a plan, field by field: `plan = RoutingPlan()` would build a 1 KB temporary on
 * the stack (the RP2040's RAM report).
 * @param plan The plan: not valid, no slots, no RESPCYCS.
 */
void RoutingPlanner::Clear(RoutingPlan& plan) {
  plan.valid = false;
  plan.subNodeCount = 0;
  plan.routed = 0;
  plan.main = NodeRouting();
  for (NodeRouting& node : plan.sub) {
    node = NodeRouting();
  }
  std::memset(plan.linkDnSlots, 0, sizeof(plan.linkDnSlots));
  std::memset(plan.linkUpSlots, 0, sizeof(plan.linkUpSlots));
  plan.respCycles = ResponseCycles();
  plan.bandwidth = BandwidthUsage();
}

/**
 * The NodeAddr of a node index.
 * @param index 0 for the main node, n + 1 for sub-node n.
 * @return kMainNode or the sub-node's position.
 */
NodeAddr RoutingPlanner::NodeOf(size_t index) {
  return (index == 0) ? kMainNode : static_cast<NodeAddr>(index - 1);
}

/**
 * Gives the bits a data slot takes on the bus (AD2431-35 TRM, Table 3-3, "Slot
 * Formatting"; the AD2437 TRM's ECC section): the slot size plus a parity bit, or with the
 * format bit set, plus 6 bits of ECC for 24-bit slots and 7 for 32-bit ones (floating-point
 * compression keeps the size and the parity bit).
 * @param slotFormat The main node's SLOTFMT.
 * @param upstream The upstream slots (UPSIZE, UPFMT), else the downstream ones.
 * @return The bits per slot.
 */
uint8_t RoutingPlanner::SlotBits(uint8_t slotFormat, bool upstream) {
  const uint8_t field =
      upstream ? static_cast<uint8_t>(slotFormat >> reg::kSlotfmtUpsizePos) : slotFormat;
  const unsigned sizeCode = field & reg::kSlotfmtDnsize;
  const bool format = (field & reg::kSlotfmtDnfp) != 0;  // DNFMT; UPFMT once shifted.
  const unsigned size = (sizeCode > 6) ? 32 : 8 + 4 * sizeCode;
  if (format && size == 24) {
    return 30;  // 24 bits and 6 of ECC.
  }
  if (format && size == 32) {
    return 39;  // 32 bits and 7 of ECC.
  }
  return static_cast<uint8_t>(size + 1);
}

/**
 * Gives the main node's response offset, RESPOFFS (the TRMs' "A2B Main Node Response Offset"
 * table: 238 for TDM2/I2S with 16-bit channels, 245 for TDM2 32-bit and TDM4 16-bit, 248
 * otherwise), or 254 when the main node receives no upstream slots ("no TDM activity",
 * AD2431-35 TRM Rev 1.2).
 * @param i2sgcfg The main node's I2SGCFG (TDMMODE, TDMSS).
 * @param receives The main node receives upstream slots (its UPSLOTS isn't 0).
 * @return RESPOFFS.
 */
uint16_t RoutingPlanner::RespOffset(uint8_t i2sgcfg, bool receives) {
  if (!receives) {
    return 254;
  }
  const unsigned mode = i2sgcfg & reg::kI2sgcfgTdmmode;
  const bool channels16 = (i2sgcfg & reg::kI2sgcfgTdmss) != 0;
  if (mode == 0) {
    return channels16 ? 238 : 245;
  }
  if (mode == 1) {
    return channels16 ? 245 : 248;
  }
  return 248;
}

/**
 * Checks the connections (their groups, distinct nodes, channels on the ports), then works
 * out each node's channels and the RX masks; every TX channel of a node with a crossbar
 * starts pointed at the unused location, of one without at its own location.
 */
RoutingError RoutingPlanner::Check(const Input& input, NodeAddr& node) {
  const size_t nodes = input.subNodeCount + 1;
  for (size_t i = 0; i < nodes; ++i) {
    m_Used[i] = m_Down[i] = m_Up[i] = 0;
    m_Played[i] = 0;
  }
  m_Misplaced = false;
  for (size_t r = 0; r < input.routeCount; ++r) {
    const Route& route = input.routes[r];
    if (route.source == route.sink) {
      node = NodeOf(route.source);
      return RoutingError::kSameNode;
    }
    m_SourceGroup[r] = input.groups[route.source]->FindRx(route.sourceGroup);
    m_SinkGroup[r] = input.groups[route.sink]->FindTx(route.sinkGroup);
    if (m_SourceGroup[r] == nullptr || m_SinkGroup[r] == nullptr) {
      node = NodeOf((m_SourceGroup[r] == nullptr) ? route.source : route.sink);
      return RoutingError::kNoSuchGroup;
    }
    const unsigned pairs = (Count(m_SourceGroup[r]->channels) < Count(m_SinkGroup[r]->channels))
                               ? Count(m_SourceGroup[r]->channels)
                               : Count(m_SinkGroup[r]->channels);
    m_Paired[r] = Lowest(m_SourceGroup[r]->channels, pairs);
    const bool sourceOff = (m_Paired[r] & ~FirstChannels(input.map[route.source].rxChannels)) != 0;
    const bool sinkOff = (Lowest(m_SinkGroup[r]->channels, pairs) &
                          ~FirstChannels(input.map[route.sink].txChannels)) != 0;
    if (sourceOff || sinkOff) {
      node = NodeOf(sourceOff ? route.source : route.sink);
      return RoutingError::kNoSuchChannel;
    }
    m_Used[route.source] |= m_Paired[r];
    if (route.sink > route.source) {
      m_Down[route.source] |= m_Paired[r];
    } else {
      m_Up[route.source] |= m_Paired[r];
    }
  }
  for (size_t i = 0; i < nodes; ++i) {
    NodeRouting& regs = Regs(i);
    const uint32_t taken = Crossbar(i) ? m_Used[i] : FirstChannels(input.map[i].rxChannels);
    for (size_t b = 0; b < 4; ++b) {
      regs.rxMask[b] = static_cast<uint8_t>(taken >> (8 * b));
    }
    for (size_t channel = 0; channel < sizeof(regs.txXbar); ++channel) {
      regs.txXbar[channel] = Crossbar(i) ? kUnusedLocation : static_cast<uint8_t>(channel);
    }
  }
  return RoutingError::kNone;
}

/**
 * Downstream: the main node's slots, then each sub-node's played slots (DNMASK), the slots
 * it passes on (DNSLOTS) and adds (DNOFFSET, LDNSLOTS).
 */
RoutingError RoutingPlanner::Downstream(NodeAddr& node) {
  uint8_t unusedOffset = 0;
  m_InLen = Contribute(0, m_Used[0], unusedOffset, 0);  // All of them go down.
  std::memcpy(m_In, m_Out, m_InLen * sizeof(m_In[0]));
  m_Plan->main.dnSlots = static_cast<uint8_t>(m_InLen);
  const size_t nodes = m_Input->subNodeCount + 1;
  for (size_t k = 1; k < nodes; ++k) {
    NodeRouting& regs = Regs(k);
    m_Plan->linkDnSlots[k - 1] = static_cast<uint8_t>(m_InLen);
    size_t needed = 0;
    for (size_t i = 0; i < m_InLen; ++i) {
      if (Consume(k, m_In[i], m_Played[k])) {
        regs.dnMask |= 1u << i;
        ++m_Played[k];
      }
      if (Needed(m_In[i], k + 1, nodes)) {
        needed = i + 1;
      }
    }
    if (m_Misplaced) {
      node = NodeOf(k);
      return RoutingError::kChannelOrder;
    }
    regs.dnSlots = static_cast<uint8_t>(needed);
    std::memcpy(m_Out, m_In, needed * sizeof(m_In[0]));
    const size_t len = Contribute(k, m_Down[k], regs.dnOffset, needed);
    regs.ldnSlots = static_cast<uint8_t>(reg::kLdnslotsDnmasken | (len - needed));
    if (len > kMaxSlots) {
      node = NodeOf(k);
      return RoutingError::kTooManySlots;
    }
    std::memcpy(m_In, m_Out, len * sizeof(m_In[0]));
    m_InLen = len;
  }
  return RoutingError::kNone;
}

/**
 * Upstream, from the last sub-node: its played slots (UPMASK, after the downstream ones in
 * its TX frame buffer), the slots it passes on (UPSLOTS) and adds (UPOFFSET, LUPSLOTS); then
 * the main node's UPSLOTS and crossbar.
 */
RoutingError RoutingPlanner::Upstream(NodeAddr& node) {
  m_InLen = 0;
  for (size_t k = m_Input->subNodeCount; k >= 1; --k) {
    NodeRouting& regs = Regs(k);
    size_t needed = 0;
    for (size_t i = 0; i < m_InLen; ++i) {
      if (Consume(k, m_In[i], m_Played[k])) {
        regs.upMask |= 1u << i;
        ++m_Played[k];
      }
      if (Needed(m_In[i], 0, k)) {
        needed = i + 1;
      }
    }
    if (m_Played[k] > kMaxSlots) {
      node = NodeOf(k);
      return RoutingError::kTooManyChannels;
    }
    if (m_Misplaced) {
      node = NodeOf(k);
      return RoutingError::kChannelOrder;
    }
    regs.upSlots = static_cast<uint8_t>(needed);
    std::memcpy(m_Out, m_In, needed * sizeof(m_In[0]));
    const size_t len = Contribute(k, m_Up[k], regs.upOffset, needed);
    regs.lupSlots = static_cast<uint8_t>(len - needed);
    if (len > kMaxSlots) {
      node = NodeOf(k);
      return RoutingError::kTooManySlots;
    }
    m_Plan->linkUpSlots[k - 1] = static_cast<uint8_t>(len);
    std::memcpy(m_In, m_Out, len * sizeof(m_In[0]));
    m_InLen = len;
  }
  size_t received = 0;
  for (size_t i = 0; i < m_InLen; ++i) {
    if (Needed(m_In[i], 0, 1)) {
      received = i + 1;
    }
  }
  for (size_t i = 0; i < received; ++i) {
    (void)Consume(0, m_In[i], i);  // Every slot received takes a location.
  }
  if (m_Misplaced) {
    node = kMainNode;
    return RoutingError::kChannelOrder;
  }
  m_Plan->main.upSlots = static_cast<uint8_t>(received);
  return RoutingError::kNone;
}

/**
 * Appends node @p index's run of RX frame buffer locations that holds @p channels to m_Out
 * from @p at: from the location of its lowest channel to that of its highest, every channel
 * of the RX mask in between. Without a crossbar every channel is in the frame buffer, and
 * the main node's run starts at channel 0 (it has no offset).
 * @param offset Set to the run's first location (DNOFFSET or UPOFFSET); 0 if empty.
 * @return The slots in m_Out: @p at plus the run's length.
 */
size_t RoutingPlanner::Contribute(size_t index, uint32_t channels, uint8_t& offset, size_t at) {
  offset = 0;
  if (channels == 0) {
    return at;
  }
  const bool crossbar = Crossbar(index);
  const uint32_t used = crossbar ? m_Used[index] : 0xFFFFFFFFu;
  unsigned low = 0;
  if (crossbar || index != 0) {
    while (!Has(channels, low)) {
      ++low;
    }
  }
  unsigned high = 31;
  while (!Has(channels, high)) {
    --high;
  }
  offset = static_cast<uint8_t>(Rank(used, low));
  for (unsigned channel = low; channel <= high; ++channel) {
    if (Has(used, channel)) {
      m_Out[at++] = SignalOf(index, channel);
    }
  }
  return at;
}

/**
 * Plays a slot at node @p index if a connection to it wants the slot's channel: each of its
 * sink channels paired with that channel gets TX frame buffer location @p location. A node
 * without a crossbar plays location n on channel n, so a sink channel other than
 * @p location sets m_Misplaced.
 * @return True if the node plays the slot.
 */
bool RoutingPlanner::Consume(size_t index, uint16_t signal, size_t location) {
  const size_t source = NodeOfSignal(signal);
  const unsigned channel = ChannelOfSignal(signal);
  bool played = false;
  for (size_t r = 0; r < m_Input->routeCount; ++r) {
    const Route& route = m_Input->routes[r];
    if (route.source != source || route.sink != index || !Has(m_Paired[r], channel)) {
      continue;
    }
    const unsigned sinkChannel = Nth(m_SinkGroup[r]->channels, Rank(m_Paired[r], channel));
    if (!Crossbar(index)) {
      m_Misplaced = m_Misplaced || (sinkChannel != location);
    } else if (sinkChannel < 32 && location < kMaxSlots) {
      Regs(index).txXbar[sinkChannel] = static_cast<uint8_t>(location);
    }
    played = true;
  }
  return played;
}

/**
 * Tells whether a node with index @p from to @p to - 1 is a sink for a slot's channel.
 * @return True if one is.
 */
bool RoutingPlanner::Needed(uint16_t signal, size_t from, size_t to) const {
  const size_t source = NodeOfSignal(signal);
  const unsigned channel = ChannelOfSignal(signal);
  for (size_t r = 0; r < m_Input->routeCount; ++r) {
    const Route& route = m_Input->routes[r];
    if (route.source == source && route.sink >= from && route.sink < to &&
        Has(m_Paired[r], channel)) {
      return true;
    }
  }
  return false;
}

/** The registers of node @p index in the plan being built. */
NodeRouting& RoutingPlanner::Regs(size_t index) {
  return (index == 0) ? m_Plan->main : m_Plan->sub[index - 1];
}

}  // namespace a2b
