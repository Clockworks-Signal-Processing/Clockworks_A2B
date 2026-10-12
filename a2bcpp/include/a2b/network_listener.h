/**
 * @file network_listener.h
 * @brief What a Network tells the app: its states, why nodes are rejected or removed, the
 *        events it queues, and NetworkListener, the app's interface for them.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_A2B_NETWORK_LISTENER_H_
#define A2BCPP_A2B_NETWORK_LISTENER_H_

#include <cstdint>

#include "a2b/module_info.h"
#include "a2b/routing.h"
#include "a2b/types.h"

namespace a2b {

class Network;

/** @brief What a Network is doing. */
enum class NetworkState : uint8_t {
  kStopped,      ///< Not started, or Stop() called.
  kStarting,     ///< RESETn pulse, main-node reset, SUSTAIN wait, PLL lock.
  kConfiguring,  ///< Writing the main node's settings and running the host script.
  kDiscovering,  ///< Finding and configuring the chain plugged in at start-up.
  /** The chain is up: a probe found nothing more, a node was rejected, or no probe is
      due (BoardPolicy::maxSubNodes are in, or discovery is paused). The chain is checked
      for drops, and its end probed for new nodes, unless one of the last two holds. */
  kRunning,
  kFailed,  ///< Stopped by an error; see Network::Error(). Start() tries again.
};

/** @brief Why a Network stopped (NetworkState::kFailed). */
enum class NetworkError : uint8_t {
  kNone,  ///< No error.
  /** A transfer to the main node failed during start-up, or it stopped answering and
      didn't answer when the bus started again. */
  kI2cError,
  kWrongMainNode,       ///< VENDOR or PRODUCT differs from BoardConfig::mainProduct.
  kMainNodeNotRunning,  ///< No MSTR_RUNNING before the deadline (no SYNC?).
  kHostScriptFailed,    ///< The board's host script failed (see the log).
};

/** @brief Why a discovered sub-node was not taken into the bus. */
enum class RejectReason : uint8_t {
  kNone,              ///< Nothing rejected; from OnNodeIdentified(): take the node in.
  kUnsupportedChip,   ///< The transceiver isn't an AD2427, AD2428, AD2433 or AD2437.
  kNoModuleInfo,      ///< No module-info source, or it had nothing for the node.
  kBadModuleInfo,     ///< The module-info image failed its checks.
  kConfigFailed,      ///< An operation its SubNode object added (OnConfigured()) failed.
  kPeripheralFailed,  ///< A peripheral script from its module info failed.
  /** Taking it would exceed BoardPolicy::maxBusPowerMw or maxBusCurrentMa (the default
      policy of NetworkListener::OnNodeIdentified()). */
  kPowerBudget,
  kByApp,  ///< The app's NetworkListener::OnNodeIdentified(), or Network::RejectNode().
};

/** @brief Why a sub-node left the bus (EventType::kNodeRemoved). */
enum class RemovalReason : uint8_t {
  /** It stopped answering (unplugged, lost power or a line fault), or a node before it
      did. */
  kDropped,
  kRejected,  ///< Network::RejectNode() for it, or for a node before it.
  /** The bus starts again: Network::Start(), a PLL relock of the main node, or a main node
      that failed its check (it was reset). */
  kRestart,
  kStopped,  ///< Network::Stop().
};

/** @brief What an Event reports. */
enum class EventType : uint8_t {
  kStateChanged,  ///< Network::State() changed; Event::state is the new state.
  kNodeAdded,     ///< Sub-node Event::node was found and configured: it is in the bus.
  /** A node answered discovery at position Event::node but was not taken in;
      Event::rejection says why. The chain ends before it. */
  kNodeRejected,
  /** Sub-node Event::node left the bus; Event::removal says why. Its SubNode object got
      OnRemoved() and is gone. */
  kNodeRemoved,
  /** A node raised an interrupt the stack doesn't consume itself (not DSCDONE or
      MSTR_RUNNING): Event::node (or #kMainNode), INTTYPE Event::interruptType. Those
      that may mean a bus drop (SRF missed, power faults, I2C and interrupt messaging
      errors) also make the stack check the chain at once. */
  kInterrupt,
  /** The routing plan is written (Network::ApplyRouting(), or again after the chain
      changed): the connections whose nodes are in the bus carry audio. See
      Network::Routing(). */
  kRoutingApplied,
  /** A routing plan could not be built or written: Event::routingError says why, and
      Event::node names the node at fault (#kNoNode if none). The last plan written stays. */
  kRoutingFailed,
};

/**
 * @brief One thing that happened on a bus. A Network queues events as they happen and
 *        gives them to its NetworkListener at the end of Network::Poll().
 *
 * Only the fields its type names are set; the others keep their defaults.
 */
struct Event {
  EventType type = EventType::kStateChanged;        ///< What happened.
  NodeAddr node = kMainNode;                        ///< The node: a position, or #kMainNode.
  NetworkState state = NetworkState::kStopped;      ///< kStateChanged: the new state.
  RejectReason rejection = RejectReason::kNone;     ///< kNodeRejected: why.
  RemovalReason removal = RemovalReason::kDropped;  ///< kNodeRemoved: why.
  uint8_t interruptType = 0;                        ///< kInterrupt: INTTYPE.
  RoutingError routingError = RoutingError::kNone;  ///< kRoutingFailed: why.
  /** kNodeAdded, kNodeRejected, kNodeRemoved: the node's chip ID (CHIPID0-5), which tells
      the module apart when positions change. */
  uint8_t chipId[6] = {};
};

/**
 * @brief What NetworkListener::OnNodeIdentified() gets: a node that answered discovery
 *        and passed the stack's own checks, and the board policy's decision on it.
 */
struct NodeReview {
  const SubNodeInfo* node = nullptr;  ///< Its identity and module info. Not null.
  /** The default policy's decision: RejectReason::kNone, or RejectReason::kPowerBudget
      if its module info's maximum power or current would take the bus-powered nodes
      over BoardPolicy::maxBusPowerMw or maxBusCurrentMa. */
  RejectReason policy = RejectReason::kNone;
  /** Sum of ModuleInfo::maxPowerMw over the bus-powered sub-nodes in the bus, without
      this one. */
  uint32_t busPowerMw = 0;
  /** Sum of ModuleInfo::maxCurrentMa over the bus-powered sub-nodes in the bus, without
      this one. */
  uint32_t busCurrentMa = 0;
};

/**
 * @brief The app's interface to a Network: it is told what happens and decides which
 *        nodes to take in. Derive from it, override what you need, and give it to
 *        Network::SetListener().
 *
 * Both calls come from Network::Poll(), on the thread that polls. See the customisation
 * cookbook (a2bcpp/docs/cookbook.md), "Events".
 */
class NetworkListener {
 public:
  virtual ~NetworkListener() = default;

  /**
   * @brief Called for each queued event, in order, at the end of Network::Poll(), when the
   *        bus is between steps. It may call any Network function (e.g. RejectNode(),
   *        PeripheralWrite(), Stop(), Start()) except Poll(), which returns at once when
   *        called from here. Events those calls queue come in the same round.
   * @param network The bus.
   * @param event What happened.
   */
  virtual void OnEvent(Network& network, const Event& event) {
    (void)network;
    (void)event;
  }

  /**
   * @brief Called when a sub-node has been identified and its module info passed its
   *        checks, before it is set up: take it in, or reject it. Read-only: it may read
   *        the Network (e.g. SubNode(), SubNodeCount()) but must not call anything
   *        that changes it or reaches the bus.
   * @param review The node, and the default policy's decision on it.
   * @return RejectReason::kNone to take it in, or why it is rejected (RejectReason::kByApp
   *         for the app's own reasons). The default returns review.policy: the power
   *         budget in BoardPolicy.
   */
  virtual RejectReason OnNodeIdentified(const NodeReview& review) { return review.policy; }

  /**
   * @brief Called before the stack writes a routing plan (ADI's A2B_PNP_AUDIO_MUTE): mute
   *        what you feed the bus (a DSP's output, say) and what you play from it. The stack
   *        then mutes by BoardRouting::mute, writes the plan and unmutes, and
   *        OnAudioUnmute() follows. The calls pair: OnAudioMute() is not called again for a
   *        plan that is written again after a failed write. It is called at that moment,
   *        from Network::Poll(), not as a queued event, so the mute is in place before the
   *        bus changes. It may use the Network's peripheral and register calls
   *        (PeripheralWrite(), WriteRegisters()), not Poll(); a call that changes the
   *        routing (ApplyRouting()) starts the plan again.
   * @param network The bus.
   * @param scope What the stack mutes itself: the whole bus, or the nodes whose TX crossbar
   *        changes. Empty with RoutingMute::kNone, and for a plan that changes no crossbar of
   *        a node with one.
   */
  virtual void OnAudioMute(Network& network, const MuteScope& scope) {
    (void)network;
    (void)scope;
  }

  /**
   * @brief Called once the plan is written and the stack has unmuted (ADI's
   *        A2B_PNP_AUDIO_UNMUTE_ROUTING_DONE): unmute what OnAudioMute() muted. A bus the
   *        app muted with Network::MuteNetwork() stays muted. The same rules as OnAudioMute().
   * @param network The bus.
   * @param scope What was muted.
   */
  virtual void OnAudioUnmute(Network& network, const MuteScope& scope) {
    (void)network;
    (void)scope;
  }
};

}  // namespace a2b

#endif  // A2BCPP_A2B_NETWORK_LISTENER_H_
