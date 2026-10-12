/**
 * @file network.h
 * @brief One A2B bus: starts its main node and discovers its chain of sub-nodes, all from
 *        Poll().
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_A2B_NETWORK_H_
#define A2BCPP_A2B_NETWORK_H_

#include <cstddef>
#include <cstdint>

#include "a2b/board_config.h"
#include "a2b/link_profile.h"
#include "a2b/module_info.h"
#include "a2b/network_listener.h"
#include "a2b/platform.h"
#include "a2b/routing.h"
#include "a2b/sub_node.h"
#include "a2b/types.h"

namespace a2b {

/**
 * @brief One A2B bus, from its main node's start-up to its sub-nodes' configuration.
 *
 * Construct it with the Platform and the bus's BoardConfig, then call Start() once and
 * Poll() often. All bus traffic happens inside Poll(). One thread makes all calls on a
 * Network. What happens is queued as events and given to the app's NetworkListener at
 * the end of each Poll(). See the quick start (a2bcpp/docs/quick_start.md).
 *
 * A Network keeps all its state in fixed storage inside the object: it never uses the
 * heap.
 */
class Network {
 public:
  Network(Platform& platform, const BoardConfig& board);  ///< Stopped; nothing sent.
  ~Network();

  Network(const Network&) = delete;
  Network& operator=(const Network&) = delete;

  Result Start();             ///< Starts (or restarts) the bus; the next Poll() begins.
  void Stop();                ///< Stops; nothing more is sent.
  void Poll(uint32_t nowMs);  ///< Does the work that is due.
  void SetModuleInfoSource(ModuleInfoSource* source);  ///< Where module info comes from.
  void SetNodeFactory(const NodeFactory* factory);     ///< Picks each sub-node's class.
  void SetListener(NetworkListener* listener);         ///< Gets the events and reviews nodes.
  Result SetLinkProfile(NodeAddr position,
                        const LinkProfile& profile);  ///< The link to a sub-node.
  Result ClearLinkProfile(NodeAddr position);         ///< Back to the default for a link.
  Result SetCableLength(NodeAddr position, uint16_t decimetres);  ///< A link's cable.
  Result ClearCableLength(NodeAddr position);     ///< Back to the board's cable length.
  uint16_t CableLength(NodeAddr position) const;  ///< A link's cable length in use, in dm.
  void PauseDiscovery();                          ///< No new probes until ResumeDiscovery().
  void ResumeDiscovery();                         ///< Probes again at the end of the chain.
  Result RejectNode(NodeAddr position);           ///< Takes a sub-node, and those after it, out.

  Result PeripheralWrite(NodeAddr node, PeripheralAddress addr, const uint8_t* data,
                         size_t len);  ///< Writes to a node's peripheral.
  Result PeripheralRead(NodeAddr node, PeripheralAddress addr, uint8_t* data,
                        size_t len);  ///< Reads from a node's peripheral.
  Result PeripheralWriteRead(NodeAddr node, PeripheralAddress addr, const uint8_t* writeData,
                             size_t writeLen, uint8_t* readData,
                             size_t readLen);  ///< Write, repeated start, read.
  Result ReadRegisters(NodeAddr node, uint16_t reg, uint8_t* data,
                       size_t len);  ///< Reads a node's registers (raw access).
  Result WriteRegisters(NodeAddr node, uint16_t reg, const uint8_t* data,
                        size_t len);  ///< Writes a node's registers, at the app's risk.

  Result AddConnection(const Connection& connection);  ///< Adds a connection, by chip ID.
  Result AddConnection(NodeAddr source, uint8_t sourceGroup, NodeAddr sink,
                       uint8_t sinkGroup);  ///< Adds a connection between nodes in the bus.
  Result RemoveConnection(const Connection& connection);  ///< Removes a connection.
  Result RemoveConnection(NodeAddr source, uint8_t sourceGroup, NodeAddr sink,
                          uint8_t sinkGroup);  ///< Removes one, by the nodes' positions.
  void ClearConnections();                     ///< Removes every connection.
  Result SetAudioRoutes(const AudioStream* streams, size_t count,
                        bool append);  ///< Sets the connections from streams, all or nothing.
  Result ImportConnections(const Connection* connections,
                           size_t count);  ///< Replaces the connections with a saved list.
  size_t ConnectionCount() const;          ///< Connections kept.
  const Connection* GetConnection(size_t index) const;      ///< A connection, or nullptr.
  NodeAddr ConnectionNode(const ConnectionEnd& end) const;  ///< Where an end's node is.
  RoutingError ApplyRouting();         ///< Routes the connections, now and as the chain changes.
  const RoutingPlan& Routing() const;  ///< The routing plan written last.
  void MuteNetwork(bool mute);         ///< Mutes (or unmutes) the whole bus, at the next Poll().
  bool NetworkMuted() const;           ///< MuteNetwork(true) is in force.
  BandwidthUsage Bandwidth() const;    ///< How full the bus is with the plan built last.

  NetworkState State() const;                           ///< What the bus is doing.
  NetworkError Error() const;                           ///< Why it failed.
  size_t SubNodeCount() const;                          ///< Sub-nodes in the bus.
  const SubNodeInfo* SubNode(NodeAddr position) const;  ///< A sub-node, or nullptr.
  a2b::SubNode* NodeObject(NodeAddr position);          ///< A sub-node's object, or nullptr.
  RejectReason LastRejection() const;                   ///< Why the last node was rejected.
  bool DiscoveryPaused() const;                         ///< PauseDiscovery() is in force.

 private:
  class Impl;  ///< The state and the work (network.cpp).

  /** @brief Bytes of storage for the Impl (14984 on the RP2040, 16896 on 64-bit hosts;
      4 KB of it the peripheral-script buffer, 384 B the event queue, about
      6 KB the routing: connections, plans, channel groups; a2bcpp/docs/ram.md);
      network.cpp checks that it fits. */
  static constexpr size_t kImplSize = (sizeof(void*) > 4) ? 17408 : 15360;

  alignas(8) unsigned char m_Storage[kImplSize];  ///< Holds the Impl.
  Impl* m_Impl;                                   ///< The Impl in m_Storage.
};

}  // namespace a2b

#endif  // A2BCPP_A2B_NETWORK_H_
