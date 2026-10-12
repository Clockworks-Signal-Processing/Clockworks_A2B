/**
 * @file test_routing_planner.cpp
 * @brief Unit tests for RoutingPlanner: the registers of the bench's SigmaStudio+ project,
 *        of ADI's Pi app's routing in the G13 golden, and of the TRMs' three-node example;
 *        slot pruning, runs that carry unused channels, group pairing, and each limit;
 *        AD242x nodes (no crossbar: the TDM order), the second customer system, channels on
 *        the port, and the channel maps from the port settings (ComputeChannelMap()).
 *
 * The scenario tests (test_sim_routing) check what the plans do, by following the audio
 * through the simulator's model of the registers.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <initializer_list>

#include "a2b/routing.h"
#include "chip_caps.h"
#include "routing_planner.h"
#include "test_harness.h"

namespace a2b {
namespace {

/** A chain to plan: its chips and groups, and the routes. Node index 0 is the main node. */
struct Chain {
  ChannelGroups groups[RoutingPlanner::kMaxNodes];
  RoutingPlanner::Input input;
  RoutingPlanner planner;
  RoutingPlan plan;
  NodeAddr node = kMainNode;

  /** A main node and @p subNodes sub-nodes, all AD2437 unless @p product says otherwise,
      each port giving 32 channels each way. */
  explicit Chain(size_t subNodes, uint8_t product = 0x37) {
    input.subNodeCount = subNodes;
    for (size_t i = 0; i <= subNodes; ++i) {
      SetChip(i, product);
      input.groups[i] = &groups[i];
    }
  }

  /** Node @p index is an @p product: a crossbar if the chip has one, 32 channels each way. */
  void SetChip(size_t index, uint8_t product) {
    input.map[index].crossbar = LookUpChipCaps(kAdiVendorId, product)->hasCrossbar;
    input.map[index].rxChannels = 32;
    input.map[index].txChannels = 32;
  }

  /** Routes node @p source's RX group @p sourceGroup to @p sink's TX group @p sinkGroup. */
  void Route(uint8_t source, uint8_t sourceGroup, uint8_t sink, uint8_t sinkGroup) {
    RoutingPlanner::Route& route = input.routes[input.routeCount++];
    route.source = source;
    route.sourceGroup = sourceGroup;
    route.sink = sink;
    route.sinkGroup = sinkGroup;
  }

  /** Builds the plan. */
  RoutingError Build() { return planner.Build(input, plan, node); }

  /** Sub-node @p n's registers. */
  const NodeRouting& Sub(size_t n) const { return plan.sub[n]; }
};

/** True if every TXXBAR of @p regs but those in @p used points at the unused location. */
bool OthersUnused(const NodeRouting& regs, std::initializer_list<unsigned> used) {
  for (unsigned channel = 0; channel < 32; ++channel) {
    bool listed = false;
    for (unsigned u : used) {
      listed = listed || (u == channel);
    }
    if (!listed && regs.txXbar[channel] != RoutingPlanner::kUnusedLocation) {
      return false;
    }
  }
  return true;
}

A2B_TEST(BenchProjectRegisters) {
  // The bench's SigmaStudio+ project (tools/SS+reference/2x_AB0020_EVM_with_2437): two
  // channels down to the sub-node's codec and two up to the main node's. Its registers:
  // main DNSLOTS 2, UPSLOTS 2; sub-node LDNSLOTS 0x80, DNMASK0 0x03, LUPSLOTS 2, DNSLOTS 0,
  // UPSLOTS 0. (It left RXMASK at 0xFF and TXXBAR at the defaults; the plan takes only the
  // channels sent and points the others at location 31.)
  Chain chain(1);
  chain.groups[0].AddRx(0, 0x3);
  chain.groups[0].AddTx(0, 0x3);
  chain.groups[1].AddRx(0, 0x3);
  chain.groups[1].AddTx(0, 0x3);
  chain.Route(0, 0, 1, 0);
  chain.Route(1, 0, 0, 0);
  A2B_CHECK(chain.Build() == RoutingError::kNone);
  const RoutingPlan& plan = chain.plan;
  A2B_CHECK_EQ(plan.main.dnSlots, 2);
  A2B_CHECK_EQ(plan.main.upSlots, 2);
  A2B_CHECK_EQ(plan.main.rxMask[0], 0x03);
  A2B_CHECK(plan.main.txXbar[0] == 0 && plan.main.txXbar[1] == 1 &&
            OthersUnused(plan.main, {0, 1}));
  const NodeRouting& sub = chain.Sub(0);
  A2B_CHECK_EQ(sub.ldnSlots, 0x80);
  A2B_CHECK_EQ(sub.dnMask, 0x3u);
  A2B_CHECK_EQ(sub.lupSlots, 2);
  A2B_CHECK_EQ(sub.upOffset, 0);
  A2B_CHECK_EQ(sub.dnSlots, 0);
  A2B_CHECK_EQ(sub.upSlots, 0);
  A2B_CHECK_EQ(sub.upMask, 0u);
  A2B_CHECK_EQ(sub.rxMask[0], 0x03);
  A2B_CHECK(sub.rxMask[1] == 0 && sub.rxMask[7] == 0);
  A2B_CHECK(sub.txXbar[0] == 0 && sub.txXbar[1] == 1 && OthersUnused(sub, {0, 1}));
  A2B_CHECK_EQ(plan.linkDnSlots[0], 2);
  A2B_CHECK_EQ(plan.linkUpSlots[0], 2);
  A2B_CHECK_EQ(plan.routed, 2);
}

A2B_TEST(G13Routing) {
  // The G13 golden's routing: 12 channels from the main node to sub-node 0, which
  // ADI's stack set with main DNSLOTS 12, LDNSLOTS 0x80 and DNMASK 0xFFF.
  Chain chain(1);
  chain.groups[0].AddRx(0, 0x0FFF);
  chain.groups[1].AddTx(0, 0x0FFF);
  chain.Route(0, 0, 1, 0);
  A2B_CHECK(chain.Build() == RoutingError::kNone);
  A2B_CHECK_EQ(chain.plan.main.dnSlots, 12);
  A2B_CHECK_EQ(chain.plan.main.upSlots, 0);
  A2B_CHECK_EQ(chain.Sub(0).ldnSlots, 0x80);
  A2B_CHECK_EQ(chain.Sub(0).dnMask, 0xFFFu);
  A2B_CHECK_EQ(chain.Sub(0).txXbar[11], 11);
}

A2B_TEST(TrmThreeNodeExample) {
  // The TRMs' example (AD2431-35 TRM Rev 1.2, Figure 6-13): the main node sends 14 slots;
  // sub-node 0 plays 6, passes 8 and adds 8 (16 to sub-node 1, which plays them all);
  // sub-node 1 sends 10 up; sub-node 0 plays 6, passes 4 and adds 8; the main node plays 12.
  // The slots a node passes on come first, as the TRMs' DNSLOTS and UPSLOTS pass the first
  // slots only: channels 0-7 of the main node go to sub-node 1, 8-13 to sub-node 0.
  Chain chain(2);
  chain.groups[0].AddRx(1, 0x00FF);  // To sub-node 1.
  chain.groups[0].AddRx(2, 0x3F00);  // To sub-node 0.
  chain.groups[0].AddTx(1, 0x000F);  // From sub-node 1.
  chain.groups[0].AddTx(2, 0x0FF0);  // From sub-node 0.
  chain.groups[1].AddTx(0, 0x003F);  // From the main node.
  chain.groups[1].AddTx(1, 0x0FC0);  // From sub-node 1.
  chain.groups[1].AddRx(0, 0x00FF);  // To sub-node 1.
  chain.groups[1].AddRx(1, 0xFF00);  // To the main node.
  chain.groups[2].AddTx(0, 0x00FF);  // From the main node.
  chain.groups[2].AddTx(1, 0xFF00);  // From sub-node 0.
  chain.groups[2].AddRx(0, 0x000F);  // To the main node.
  chain.groups[2].AddRx(1, 0x03F0);  // To sub-node 0.
  chain.Route(0, 1, 2, 0);
  chain.Route(0, 2, 1, 0);
  chain.Route(1, 0, 2, 1);
  chain.Route(1, 1, 0, 2);
  chain.Route(2, 0, 0, 1);
  chain.Route(2, 1, 1, 1);
  A2B_CHECK(chain.Build() == RoutingError::kNone);
  const RoutingPlan& plan = chain.plan;
  A2B_CHECK_EQ(plan.main.dnSlots, 14);
  A2B_CHECK_EQ(plan.linkDnSlots[0], 14);
  A2B_CHECK_EQ(plan.linkDnSlots[1], 16);
  A2B_CHECK_EQ(chain.Sub(0).dnMask, 0x3F00u);  // Plays slots 8-13.
  A2B_CHECK_EQ(chain.Sub(0).dnSlots, 8);       // Passes 0-7.
  A2B_CHECK_EQ(chain.Sub(0).ldnSlots, 0x88);   // Adds 8.
  A2B_CHECK_EQ(chain.Sub(0).dnOffset, 0);
  A2B_CHECK_EQ(chain.Sub(1).dnMask, 0xFFFFu);
  A2B_CHECK_EQ(chain.Sub(1).dnSlots, 0);
  A2B_CHECK_EQ(plan.linkUpSlots[1], 10);
  A2B_CHECK_EQ(chain.Sub(1).lupSlots, 10);
  A2B_CHECK_EQ(chain.Sub(0).upMask, 0x3F0u);  // Plays upstream slots 4-9.
  A2B_CHECK_EQ(chain.Sub(0).upSlots, 4);      // Passes 0-3.
  A2B_CHECK_EQ(chain.Sub(0).lupSlots, 8);     // Adds 8, from location 8 of its RX buffer.
  A2B_CHECK_EQ(chain.Sub(0).upOffset, 8);
  A2B_CHECK_EQ(plan.linkUpSlots[0], 12);
  A2B_CHECK_EQ(plan.main.upSlots, 12);
  // Sub-node 0 plays its 6 downstream slots at locations 0-5 and its 6 upstream ones at
  // 6-11: TX channels 0-5 and 6-11.
  for (unsigned c = 0; c < 12; ++c) {
    A2B_CHECK_EQ(chain.Sub(0).txXbar[c], c);
  }
  // The main node: sub-node 1's 4 at locations 0-3 (TX 0-3), sub-node 0's 8 at 4-11.
  for (unsigned c = 0; c < 12; ++c) {
    A2B_CHECK_EQ(plan.main.txXbar[c], c);
  }
}

A2B_TEST(SlotNoOneNeedsFurtherOnIsDroppedOnlyWhenLast) {
  // The main node's channel 0 goes to sub-node 0, channel 1 to sub-node 2: sub-node 0 must
  // pass both (DNSLOTS passes the first slots), and channel 0's slot rides on unused.
  Chain far(3);
  far.groups[0].AddRx(0, 0x1);
  far.groups[0].AddRx(1, 0x2);
  far.groups[1].AddTx(0, 0x1);
  far.groups[3].AddTx(0, 0x1);
  far.Route(0, 0, 1, 0);
  far.Route(0, 1, 3, 0);
  A2B_CHECK(far.Build() == RoutingError::kNone);
  A2B_CHECK_EQ(far.Sub(0).dnMask, 0x1u);
  A2B_CHECK_EQ(far.Sub(0).dnSlots, 2);
  A2B_CHECK_EQ(far.Sub(1).dnSlots, 2);
  A2B_CHECK_EQ(far.Sub(2).dnMask, 0x2u);
  A2B_CHECK_EQ(far.Sub(2).dnSlots, 0);
  // The other way round, the slot for sub-node 0 is last and dropped there.
  Chain near(3);
  near.groups[0].AddRx(0, 0x1);
  near.groups[0].AddRx(1, 0x2);
  near.groups[1].AddTx(0, 0x1);
  near.groups[3].AddTx(0, 0x1);
  near.Route(0, 0, 3, 0);
  near.Route(0, 1, 1, 0);
  A2B_CHECK(near.Build() == RoutingError::kNone);
  A2B_CHECK_EQ(near.Sub(0).dnMask, 0x2u);
  A2B_CHECK_EQ(near.Sub(0).dnSlots, 1);
  A2B_CHECK_EQ(near.plan.linkDnSlots[1], 1);
  A2B_CHECK_EQ(near.plan.linkDnSlots[2], 1);
}

A2B_TEST(RunCarriesChannelsInBetween) {
  // Sub-node 1 sends its channel 0 up to sub-node 0, channel 1 down to sub-node 2 and
  // channel 2 up to the main node. Its RX frame buffer holds 0, 1, 2 in order, so the
  // upstream run is locations 0-2 with channel 1 riding on unused; the downstream run is
  // location 1 alone.
  Chain chain(3);
  chain.groups[2].AddRx(0, 0x1);
  chain.groups[2].AddRx(1, 0x2);
  chain.groups[2].AddRx(2, 0x4);
  chain.groups[1].AddTx(0, 0x1);
  chain.groups[3].AddTx(0, 0x1);
  chain.groups[0].AddTx(0, 0x1);
  chain.Route(2, 0, 1, 0);
  chain.Route(2, 1, 3, 0);
  chain.Route(2, 2, 0, 0);
  A2B_CHECK(chain.Build() == RoutingError::kNone);
  const NodeRouting& source = chain.Sub(1);
  A2B_CHECK_EQ(source.rxMask[0], 0x07);
  A2B_CHECK_EQ(source.upOffset, 0);
  A2B_CHECK_EQ(source.lupSlots, 3);
  A2B_CHECK_EQ(source.dnOffset, 1);
  A2B_CHECK_EQ(source.ldnSlots, 0x81);
  A2B_CHECK_EQ(chain.Sub(0).upMask, 0x1u);     // Channel 0, in slot 0.
  A2B_CHECK_EQ(chain.Sub(0).upSlots, 3);       // Channel 2 must reach the main node.
  A2B_CHECK_EQ(chain.plan.main.upSlots, 3);    // It receives slots 0-2,
  A2B_CHECK_EQ(chain.plan.main.txXbar[0], 2);  // and plays channel 2's, in location 2.
}

A2B_TEST(GroupsPairInOrderAndTheShorterWins) {
  // A 4-channel source group to a 2-channel sink group: its two lowest channels are sent,
  // to the sink's channels in order.
  Chain chain(1);
  chain.groups[0].AddRx(0, 0xF0);         // Channels 4-7.
  chain.groups[1].AddTx(0, 0x00000201u);  // Channels 0 and 9.
  chain.Route(0, 0, 1, 0);
  A2B_CHECK(chain.Build() == RoutingError::kNone);
  A2B_CHECK_EQ(chain.plan.main.rxMask[0], 0x30);  // Channels 4 and 5.
  A2B_CHECK_EQ(chain.plan.main.dnSlots, 2);
  A2B_CHECK_EQ(chain.Sub(0).txXbar[0], 0);  // Channel 4's slot.
  A2B_CHECK_EQ(chain.Sub(0).txXbar[9], 1);  // Channel 5's.
  A2B_CHECK(OthersUnused(chain.Sub(0), {0, 9}));
}

A2B_TEST(OneSourceToSeveralSinksTakesOneSlot) {
  Chain chain(2);
  chain.groups[0].AddRx(0, 0x3);
  chain.groups[1].AddTx(0, 0x3);
  chain.groups[2].AddTx(0, 0xC);
  chain.Route(0, 0, 1, 0);
  chain.Route(0, 0, 2, 0);
  A2B_CHECK(chain.Build() == RoutingError::kNone);
  A2B_CHECK_EQ(chain.plan.main.dnSlots, 2);
  A2B_CHECK_EQ(chain.Sub(0).dnMask, 0x3u);
  A2B_CHECK_EQ(chain.Sub(0).dnSlots, 2);
  A2B_CHECK_EQ(chain.Sub(1).dnMask, 0x3u);
  A2B_CHECK(chain.Sub(1).txXbar[2] == 0 && chain.Sub(1).txXbar[3] == 1);
}

A2B_TEST(NoConnectionsNoSlots) {
  Chain chain(2);
  A2B_CHECK(chain.Build() == RoutingError::kNone);
  A2B_CHECK_EQ(chain.plan.main.dnSlots, 0);
  A2B_CHECK_EQ(chain.plan.main.rxMask[0], 0);
  A2B_CHECK_EQ(chain.Sub(1).ldnSlots, 0x80);
  A2B_CHECK(OthersUnused(chain.Sub(1), {}));
}

A2B_TEST(SecondCustomerSystem) {
  // An AD2428 main node; sub-node 0 an AD2428 with a stereo codec (RX 0 and TX 0 on
  // channels 0-1), sub-node 1 an AD2427 with four microphones (RX 0 on channels 0-3). The
  // main node's stereo pair goes down to the codec; the microphones and the codec's pair go
  // up to the main node. Upstream, sub-node 1's slots come first (sub-node 0 adds its own
  // after those it passes), so the main node's TX channels 0-3 are the microphones and 4-5
  // the codec.
  Chain chain(2, 0x28);
  chain.SetChip(2, 0x27);
  chain.groups[0].AddRx(0, 0x03);
  chain.groups[0].AddTx(0, 0x0F);  // The microphones.
  chain.groups[0].AddTx(1, 0x30);  // The codec.
  chain.groups[1].AddRx(0, 0x3);
  chain.groups[1].AddTx(0, 0x3);
  chain.groups[2].AddRx(0, 0xF);
  chain.Route(0, 0, 1, 0);
  chain.Route(2, 0, 0, 0);
  chain.Route(1, 0, 0, 1);
  A2B_CHECK(chain.Build() == RoutingError::kNone);
  const RoutingPlan& plan = chain.plan;
  A2B_CHECK_EQ(plan.main.dnSlots, 2);
  A2B_CHECK_EQ(chain.Sub(0).dnMask, 0x3u);
  A2B_CHECK_EQ(chain.Sub(0).dnSlots, 0);
  A2B_CHECK_EQ(chain.Sub(0).ldnSlots, 0x80);
  A2B_CHECK_EQ(chain.Sub(1).lupSlots, 4);
  A2B_CHECK_EQ(chain.Sub(1).upOffset, 0);
  A2B_CHECK_EQ(chain.Sub(0).upMask, 0u);
  A2B_CHECK_EQ(chain.Sub(0).upSlots, 4);
  A2B_CHECK_EQ(chain.Sub(0).lupSlots, 2);
  A2B_CHECK_EQ(plan.linkUpSlots[0], 6);
  A2B_CHECK_EQ(plan.main.upSlots, 6);
  // Without a crossbar: every port channel in, channel n out of location n.
  for (unsigned b = 0; b < 4; ++b) {
    A2B_CHECK_EQ(plan.main.rxMask[b], 0xFF);
  }
  for (unsigned c = 0; c < 32; ++c) {
    A2B_CHECK_EQ(plan.main.txXbar[c], c);
    A2B_CHECK_EQ(chain.Sub(0).txXbar[c], c);
  }
}

A2B_TEST(TdmChannelsFollowTheBusOrder) {
  // As SecondCustomerSystem, with the main node's groups the other way round (the codec on
  // TX 0-1, the microphones on 2-5): sub-node 1's slots reach it first, so it would play
  // them on channels 0-3. Refused, at the main node; a crossbar main node takes it.
  Chain chain(2, 0x28);
  chain.SetChip(2, 0x27);
  chain.groups[0].AddTx(0, 0x3C);  // The microphones.
  chain.groups[0].AddTx(1, 0x03);  // The codec.
  chain.groups[1].AddRx(0, 0x3);
  chain.groups[2].AddRx(0, 0xF);
  chain.Route(2, 0, 0, 0);
  chain.Route(1, 0, 0, 1);
  A2B_CHECK(chain.Build() == RoutingError::kChannelOrder);
  A2B_CHECK_EQ(chain.node, kMainNode);
  chain.SetChip(0, 0x37);
  A2B_CHECK(chain.Build() == RoutingError::kNone);
  A2B_CHECK(chain.plan.main.txXbar[0] == 4 && chain.plan.main.txXbar[1] == 5);
  A2B_CHECK(chain.plan.main.txXbar[2] == 0 && chain.plan.main.txXbar[5] == 3);
}

A2B_TEST(TdmSinkChannelsStartAtZeroWithNoGap) {
  // An AD2428 sub-node's TX group on channels 2-3 alone: the two slots it plays fill
  // locations 0-1, so channels 0-1 would play them. Refused; a group on 0-1 is fine.
  Chain chain(1, 0x28);
  chain.groups[0].AddRx(0, 0x3);
  chain.groups[1].AddTx(0, 0x3);
  chain.groups[1].AddTx(1, 0xC);
  chain.Route(0, 0, 1, 1);
  A2B_CHECK(chain.Build() == RoutingError::kChannelOrder);
  A2B_CHECK_EQ(chain.node, 0);
  chain.input.routes[0].sinkGroup = 0;
  A2B_CHECK(chain.Build() == RoutingError::kNone);
  A2B_CHECK_EQ(chain.Sub(0).dnMask, 0x3u);
}

A2B_TEST(TdmRunsCarryEveryChannel) {
  // An AD2428 sub-node sends channels 1 and 3 up: without RXMASK its run is channels 1-3
  // (UPOFFSET 1), channel 2 riding on unused. The AD2428 main node plays them in order, so
  // its TX group pairs with channels 0 and 2.
  Chain up(1, 0x28);
  up.groups[1].AddRx(0, 0xA);
  up.groups[0].AddTx(0, 0x5);
  up.Route(1, 0, 0, 0);
  A2B_CHECK(up.Build() == RoutingError::kNone);
  A2B_CHECK_EQ(up.Sub(0).upOffset, 1);
  A2B_CHECK_EQ(up.Sub(0).lupSlots, 3);
  A2B_CHECK_EQ(up.plan.main.upSlots, 3);
  // The main node has no offset register: it sends from channel 0, so channels 2-3 take
  // four slots. The sub-node plays slots 2-3 on its channels 0-1.
  Chain down(1, 0x28);
  down.groups[0].AddRx(0, 0xC);
  down.groups[1].AddTx(0, 0x3);
  down.Route(0, 0, 1, 0);
  A2B_CHECK(down.Build() == RoutingError::kNone);
  A2B_CHECK_EQ(down.plan.main.dnSlots, 4);
  A2B_CHECK_EQ(down.Sub(0).dnMask, 0xCu);
}

A2B_TEST(ChannelsMustBeOnThePort) {
  // Channels past those the port gives: the source's RX, the sink's TX. A sink group longer
  // than the source's uses only its lowest channels, so its others may be off the port.
  Chain chain(1);
  chain.groups[0].AddRx(0, 0xF);
  chain.groups[1].AddTx(0, 0xF);
  chain.Route(0, 0, 1, 0);
  chain.input.map[0].rxChannels = 2;
  A2B_CHECK(chain.Build() == RoutingError::kNoSuchChannel);
  A2B_CHECK_EQ(chain.node, kMainNode);
  chain.input.map[0].rxChannels = 4;
  chain.input.map[1].txChannels = 0;  // The I2S port left off.
  A2B_CHECK(chain.Build() == RoutingError::kNoSuchChannel);
  A2B_CHECK_EQ(chain.node, 0);
  chain.groups[0].AddRx(0, 0x3);
  chain.input.map[1].txChannels = 2;
  A2B_CHECK(chain.Build() == RoutingError::kNone);
}

A2B_TEST(ChannelMapsFromThePortSettings) {
  // The TRMs' I2SCFG: AD242x pin bits, AD243x pin counts; TDMMODE's channels per pin, a
  // sub-node's rate multiple, PDM microphones, 32 at most.
  PortSettings port;
  port.node = 0;
  port.productId = 0x28;
  port.i2sgcfg = 0xE0;  // TDM2.
  port.i2scfg = 0x11;   // DTX0, DRX0: one pin each way on both families.
  ChannelMap map = ComputeChannelMap(port);
  A2B_CHECK(!map.crossbar && map.rxChannels == 2 && map.txChannels == 2);
  port.productId = 0x37;
  map = ComputeChannelMap(port);
  A2B_CHECK(map.crossbar && map.rxChannels == 2 && map.txChannels == 2);
  port.productId = 0x28;
  port.i2sgcfg = 0x02;  // TDM8.
  port.i2scfg = 0x33;   // Both pins each way.
  map = ComputeChannelMap(port);
  A2B_CHECK(map.rxChannels == 16 && map.txChannels == 16);
  port.i2sgcfg = 0x0A;  // TDM8, RXONDTX1: DTX1 receives, TX1EN and RX1EN ignored.
  port.i2scfg = 0x13;
  map = ComputeChannelMap(port);
  A2B_CHECK(map.rxChannels == 16 && map.txChannels == 8);
  port.productId = 0x37;  // On an AD243x that bit is SYNCDIS.
  port.i2scfg = 0x71;     // RXPINS 7 (two, interleaved), TXPINS 1.
  map = ComputeChannelMap(port);
  A2B_CHECK(map.rxChannels == 16 && map.txChannels == 8);
  port.i2scfg = 0x05;  // TXPINS 5: reserved.
  A2B_CHECK_EQ(ComputeChannelMap(port).txChannels, 0);
  port.i2sgcfg = 0x07;  // TDM32 on two pins: the frame buffer's 32.
  port.i2scfg = 0x22;
  map = ComputeChannelMap(port);
  A2B_CHECK(map.rxChannels == 32 && map.txChannels == 32);
  port.i2sgcfg = 0xE0;  // TDM2 at 2x: four channels a superframe; not on a main node.
  port.i2scfg = 0x11;
  port.i2srate = 5;
  A2B_CHECK_EQ(ComputeChannelMap(port).txChannels, 4);
  port.node = kMainNode;
  A2B_CHECK_EQ(ComputeChannelMap(port).txChannels, 2);
  port.node = 1;
  port.i2srate = 0;
  port.productId = 0x27;  // Four PDM microphones and the high-pass filter, I2S off.
  port.i2scfg = 0x00;
  port.pdmctl = 0x1F;
  map = ComputeChannelMap(port);
  A2B_CHECK(map.rxChannels == 4 && map.txChannels == 0);
  port.i2scfg = 0x11;  // The AD2427 has no I2S/TDM port, whatever I2SCFG says.
  map = ComputeChannelMap(port);
  A2B_CHECK(map.rxChannels == 4 && map.txChannels == 0);
  port.productId = 0x28;  // PDM0's two microphones in place of DRX0's TDM channels.
  port.pdmctl = 0x03;
  map = ComputeChannelMap(port);
  A2B_CHECK(map.rxChannels == 2 && map.txChannels == 2);
  port.productId = 0x99;  // A chip the stack doesn't know.
  map = ComputeChannelMap(port);
  A2B_CHECK(!map.crossbar && map.rxChannels == 0 && map.txChannels == 0);
}

A2B_TEST(Refusals) {
  {
    Chain chain(1);
    chain.groups[0].AddRx(0, 0x3);
    chain.Route(0, 0, 1, 4);  // Sub-node 0 has no TX group 4.
    A2B_CHECK(chain.Build() == RoutingError::kNoSuchGroup);
    A2B_CHECK_EQ(chain.node, 0);
  }
  {
    Chain chain(1);
    chain.groups[1].AddRx(0, 0x3);
    chain.groups[1].AddTx(0, 0x3);
    chain.Route(1, 0, 1, 0);
    A2B_CHECK(chain.Build() == RoutingError::kSameNode);
  }
}

A2B_TEST(LinkOverThirtyTwoSlots) {
  // 32 channels from the main node to sub-node 1, and 8 from sub-node 0 to sub-node 1: 40
  // slots on the link into sub-node 1.
  Chain chain(2);
  chain.groups[0].AddRx(0, 0xFFFFFFFFu);
  chain.groups[1].AddRx(0, 0xFF);
  chain.groups[2].AddTx(0, 0xFFFFFFFFu);
  chain.groups[2].AddTx(1, 0xFF);
  chain.Route(0, 0, 2, 0);
  chain.Route(1, 0, 2, 1);
  A2B_CHECK(chain.Build() == RoutingError::kTooManySlots);
  A2B_CHECK_EQ(chain.node, 0);  // Sub-node 0's B side.
}

A2B_TEST(PlaysOverThirtyTwoChannels) {
  // Sub-node 1 plays 32 channels from the main node and 8 from sub-node 2: 40 in its
  // 32-location TX frame buffer.
  Chain chain(3);
  chain.groups[0].AddRx(0, 0xFFFFFFFFu);
  chain.groups[3].AddRx(0, 0xFF);
  chain.groups[2].AddTx(0, 0xFFFFFFFFu);
  chain.groups[2].AddTx(1, 0xFF);
  chain.Route(0, 0, 2, 0);
  chain.Route(3, 0, 2, 1);
  A2B_CHECK(chain.Build() == RoutingError::kTooManyChannels);
  A2B_CHECK_EQ(chain.node, 1);
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
