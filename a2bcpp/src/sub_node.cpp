/**
 * @file sub_node.cpp
 * @brief SubNode's hooks and their defaults, and NodeFactory.
 *
 * A Network makes one SubNode object for each sub-node it takes in, in a fixed slot of
 * its own storage (no heap), once the node is identified and its module info parsed, and
 * before its settings are written. The object lives until the node leaves the bus: it or
 * a node before it drops or is rejected by Network::RejectNode(), or the bus starts again
 * or stops (Start(), Stop(), a PLL relock, a main node that was reset). The Network then
 * calls OnRemoved() and destroys it, and queues an EventType::kNodeRemoved event. A node
 * rejected while it is reviewed or set up is destroyed without OnRemoved(): it never
 * joined.
 *
 * When each hook runs, for sub-node n (ADI's counterparts in a2bplugin-master/
 * src/discovery.c):
 * - TweakRegisters(): before n's module-info register settings are written
 *   (a2b_dscvryNodeComplete);
 * - OnConfigured(): after those settings and n's peripheral scripts; its operations run
 *   before n counts as found;
 * - InterruptMask() and DownstreamLink(): when n is found, for the link from n to
 *   sub-node n + 1; DownstreamLink() again before each probe of n + 1, so a changed
 *   answer applies to the next probe (a2b_dscvryPreSlaveInit);
 * - OnRemoved(): as above.
 *
 * Which profile a link uses: Network::SetLinkProfile() for its position if the app set
 * one, else the upstream node's DownstreamLink(), whose default is the link its image's B
 * port type gives (LinkProfile::ForBPort()); BoardConfig::defaultLink is the link to
 * sub-node 0.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "a2b/sub_node.h"

#include "detail/regdefs.h"

namespace a2b {

/**
 * Makes a node. Only the Network makes them, through the NodeFactory's makers.
 * @param context From the Network; the node keeps pointers into it, which stay valid
 *        while the node lives.
 */
SubNode::SubNode(const Context& context) : m_Info(context.info), m_BoardLink(context.boardLink) {}

/** Destroys the node. */
SubNode::~SubNode() = default;

/**
 * The link from this node's B side to the next sub-node, unless the app set one for that
 * position with Network::SetLinkProfile(). Override it for a module whose downstream
 * connector needs another link than its image gives; BoardLink() is the board's default.
 * @return The link the node's image gives: LinkProfile::ForBPort() of its B port type
 *         (ModuleInfo::bPortType) and its chip.
 */
LinkProfile SubNode::DownstreamLink() const {
  return LinkProfile::ForBPort(m_Info->moduleInfo.bPortType, m_Info->identity.productId);
}

/**
 * The board's default link (BoardConfig::defaultLink), for a DownstreamLink() that wants it
 * in place of the image's.
 * @return The board's link.
 */
const LinkProfile& SubNode::BoardLink() const { return *m_BoardLink; }

/**
 * The node's interrupt masks, written before it first powers the next link. The
 * interrupts they enable come to the app as EventType::kInterrupt events; SRF missed
 * (INTMSK0.SRFERR) and the power faults also have the chain checked for a drop at once.
 * @return INTMSK0 = PWRERR (power faults are seen during discovery) and INTMSK1 = 0:
 *         ADI's stack's masks.
 */
SubNode::InterruptMasks SubNode::InterruptMask() const {
  InterruptMasks masks;
  masks.intmsk0 = reg::kIntmsk0Pwrerr;
  masks.intmsk1 = 0x00;
  return masks;
}

/**
 * Edits the node's register settings before they are written. The image holds the module
 * info's settings in the order they will be written (SPI settings only on chips with SPI).
 * Change, add or remove values; registers the stack owns (CONTROL, SWCTL, NODEADR,
 * DISCVRY, RESPCYCS, CHIP, MMRPAGE, the interrupt status and pending registers) are
 * skipped when it is written.
 * @param image The settings. The default leaves them as they are.
 */
void SubNode::TweakRegisters(RegisterImage& image) { (void)image; }

/**
 * Adds operations to run once the node's settings and peripheral scripts are written,
 * before it counts as found: register writes, peripheral transfers, waits. Data they
 * point to must live as long as the node (a member of the derived class will do). A
 * failed operation rejects the node (RejectReason::kConfigFailed).
 * @param ops The list, empty, with room for #kMaxConfiguredOps. The default adds nothing.
 */
void SubNode::OnConfigured(OpList& ops) { (void)ops; }

/**
 * Tells the node it has left the bus (a drop, RejectNode(), a restart or Stop()); its
 * object is destroyed next. It must not reach the bus: the node may be gone. The default
 * does nothing.
 */
void SubNode::OnRemoved() {}

/**
 * The app's tag for the node's class, to tell its classes apart without RTTI before a
 * static_cast (Network::NodeObject()).
 * @return 0 for a plain SubNode; an app class returns its own non-zero tag.
 */
uint32_t SubNode::TypeTag() const { return 0; }

/**
 * Sets the maker for a module; a module already in the table gets the new maker.
 * @param vendorId Module vendor ID (ModuleInfo::vendorId).
 * @param productId Module product ID (ModuleInfo::productId).
 * @param make Its maker, e.g. Make<MyModule>.
 * @return Result::kOk; Result::kInvalidArgument if @p make is null; Result::kNoSpace if
 *         #kMaxModules modules are in the table.
 */
Result NodeFactory::ByModule(uint16_t vendorId, uint16_t productId, MakeNodeFn make) {
  if (make == nullptr) {
    return Result::kInvalidArgument;
  }
  for (size_t i = 0; i < m_Count; ++i) {
    if (m_Entries[i].vendorId == vendorId && m_Entries[i].productId == productId) {
      m_Entries[i].make = make;
      return Result::kOk;
    }
  }
  if (m_Count == kMaxModules) {
    return Result::kNoSpace;
  }
  m_Entries[m_Count++] = {vendorId, productId, make};
  return Result::kOk;
}

/**
 * Sets the app function asked first for each node, e.g. to pick a class by position or
 * chip ID. It returns a maker, or nullptr to leave the choice to the module table.
 * @param choose The function, or nullptr for none.
 * @param context Passed to it.
 */
void NodeFactory::SetChooser(ChooseNodeFn choose, void* context) {
  m_Choose = choose;
  m_ChooseContext = context;
}

/**
 * Finds the maker for a node: the app function's choice, else the module table's.
 * @param info The node.
 * @return The maker, or nullptr for a plain SubNode.
 */
MakeNodeFn NodeFactory::Find(const SubNodeInfo& info) const {
  if (m_Choose != nullptr) {
    const MakeNodeFn make = m_Choose(info, m_ChooseContext);
    if (make != nullptr) {
      return make;
    }
  }
  for (size_t i = 0; i < m_Count; ++i) {
    if (m_Entries[i].vendorId == info.moduleInfo.vendorId &&
        m_Entries[i].productId == info.moduleInfo.productId) {
      return m_Entries[i].make;
    }
  }
  return nullptr;
}

}  // namespace a2b
