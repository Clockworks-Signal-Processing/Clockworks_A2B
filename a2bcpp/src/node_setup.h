/**
 * @file node_setup.h
 * @brief Sets up a sub-node once it is identified: its register settings, its peripheral
 *        scripts and the operations its SubNode object adds. Internal; Network drives it.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_NODE_SETUP_H_
#define A2BCPP_NODE_SETUP_H_

#include <cstddef>
#include <cstdint>

#include "a2b/module_info.h"
#include "a2b/network.h"
#include "a2b/op_list.h"
#include "a2b/platform.h"
#include "a2b/register_image.h"
#include "a2b/sub_node.h"
#include "chip_caps.h"
#include "module_script.h"
#include "script_runner.h"
#include "transport.h"

namespace a2b {

/// Configures one identified sub-node across polls. One thread per object.
class NodeSetup {
 public:
  /** @brief How the set-up ended. */
  enum class Outcome : uint8_t {
    kBusy,      ///< Still working; poll again.
    kDone,      ///< The node is set up.
    kRejected,  ///< Its set-up failed; see Rejection(). The node is to be switched off.
    kFailed,    ///< A register write to the node failed.
  };

  NodeSetup(Platform& platform, Transport& transport, ScriptRunner& runner, uint8_t* buf,
            size_t bufLen);  ///< Idle; @p buf holds the group table and peripheral units.

  NodeSetup(const NodeSetup&) = delete;
  NodeSetup& operator=(const NodeSetup&) = delete;

  void Begin(SubNode& node, const ChipCaps& caps, ModuleInfoSource& source, ChannelGroups& groups,
             uint32_t frameRateHz);  ///< Starts; the next Poll() reads and writes.
  Outcome Poll(uint32_t nowMs);      ///< Advances the set-up.
  void Cancel();                     ///< Abandons it; nothing more is sent.

  RejectReason Rejection() const { return m_Rejection; }   ///< Why the node was rejected.
  const ModuleScript& Script() const { return m_Script; }  ///< Its peripheral scripts.

 private:
  /** @brief Where the set-up is. */
  enum class Step : uint8_t {
    kIdle,         ///< Not set up.
    kGroups,       ///< Read the channel groups.
    kRegisters,    ///< Write the register settings.
    kPeripherals,  ///< Run the peripheral scripts.
    kOps,          ///< Run the node object's operations.
  };

  Outcome ReadGroups();                 ///< The channel groups.
  Outcome WriteRegisters();             ///< The register settings.
  bool StartOps();                      ///< The node object's operations.
  Outcome Reject(RejectReason reason);  ///< Ends with a rejection.

  Platform& m_Platform;
  Transport& m_Transport;
  ScriptRunner& m_Runner;
  ModuleScript m_Script;
  uint8_t* m_Buf;
  size_t m_BufLen;
  SubNode* m_Node = nullptr;
  const ChipCaps* m_Caps = nullptr;
  ModuleInfoSource* m_Source = nullptr;
  ChannelGroups* m_Groups = nullptr;
  uint32_t m_FrameRateHz = 48000;  ///< The bus's SYNC rate.
  Step m_Step = Step::kIdle;
  RejectReason m_Rejection = RejectReason::kNone;
  RegisterImage m_Image;                          ///< The node's register settings.
  FixedOpList<SubNode::kMaxConfiguredOps> m_Ops;  ///< What OnConfigured() adds.
};

}  // namespace a2b

#endif  // A2BCPP_NODE_SETUP_H_
