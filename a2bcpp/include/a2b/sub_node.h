/**
 * @file sub_node.h
 * @brief The app's view of a sub-node: SubNode, whose hooks an app overrides for its
 *        modules, and NodeFactory, which picks the class for each module.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_A2B_SUB_NODE_H_
#define A2BCPP_A2B_SUB_NODE_H_

#include <cstddef>
#include <cstdint>
#include <new>
#include <type_traits>

#include "a2b/link_profile.h"
#include "a2b/module_info.h"
#include "a2b/op_list.h"
#include "a2b/register_image.h"
#include "a2b/types.h"

namespace a2b {

/** @brief Bytes of the fixed slot a Network keeps for each sub-node's object: a class
 *         made with Make() must fit (64 on the RP2040, 128 on 64-bit hosts). */
inline constexpr size_t kSubNodeSlotSize = 16 * sizeof(void*);

/** @brief Alignment of a sub-node slot. */
inline constexpr size_t kSubNodeSlotAlign = 8;

/**
 * @brief One sub-node in the bus. The Network makes one for each node it takes in, with
 *        the class NodeFactory picks for the module (this one if none).
 *
 * Derive from it to change how a module is set up. The hooks edit settings or add
 * operations; none of them reaches the bus. The Network calls them from Poll(). The
 * stable hooks are listed in the customisation cookbook (a2bcpp/docs/cookbook.md).
 */
class SubNode {
 public:
  /** @brief What the Network gives a node it makes. Pass it on to SubNode's constructor. */
  struct Context {
    const SubNodeInfo* info = nullptr;       ///< The node's identity and module info.
    const LinkProfile* boardLink = nullptr;  ///< BoardConfig::defaultLink.
  };

  /** @brief A sub-node's interrupt masks, written before it powers the next link. */
  struct InterruptMasks {
    uint8_t intmsk0 = 0;  ///< INTMSK0.
    uint8_t intmsk1 = 0;  ///< INTMSK1.
  };

  /** @brief Most operations OnConfigured() may add. */
  static constexpr size_t kMaxConfiguredOps = 8;

  explicit SubNode(const Context& context);  ///< A node of the bus.
  virtual ~SubNode();

  SubNode(const SubNode&) = delete;
  SubNode& operator=(const SubNode&) = delete;

  const SubNodeInfo& Info() const { return *m_Info; }  ///< Identity and module info.
  /// Position on the bus.
  NodeAddr Position() const { return m_Info->identity.position; }

  virtual LinkProfile DownstreamLink() const;         ///< The link to the next sub-node.
  virtual InterruptMasks InterruptMask() const;       ///< Its interrupt masks.
  virtual void TweakRegisters(RegisterImage& image);  ///< Edits its settings before writes.
  virtual void OnConfigured(OpList& ops);             ///< May add operations after its set-up.
  virtual void OnRemoved();                           ///< It left the bus.
  virtual uint32_t TypeTag() const;                   ///< The app's tag for its class.

 protected:
  const LinkProfile& BoardLink() const;  ///< BoardConfig::defaultLink.

 private:
  const SubNodeInfo* m_Info;
  const LinkProfile* m_BoardLink;
};

/** @brief Makes a sub-node's object in its slot: Make<T> for an app class. */
using MakeNodeFn = SubNode* (*)(void* slot, const SubNode::Context& context);

/**
 * @brief Makes a @p T in a Network's sub-node slot. Give it to NodeFactory as the maker
 *        for a module: `factory.ByModule(0x00AD, 0x0037, a2b::Make<MyModule>)`.
 *
 * @p T derives from SubNode, has a constructor taking a SubNode::Context (e.g.
 * `using SubNode::SubNode;`), and fits #kSubNodeSlotSize: both are checked when it
 * compiles.
 * @param slot The slot.
 * @param context What the Network gives the node.
 * @return The node.
 */
template <class T>
SubNode* Make(void* slot, const SubNode::Context& context) {
  static_assert(std::is_base_of<SubNode, T>::value, "T must derive from a2b::SubNode");
  static_assert(sizeof(T) <= kSubNodeSlotSize, "T does not fit a sub-node slot (kSubNodeSlotSize)");
  static_assert(alignof(T) <= kSubNodeSlotAlign, "T needs more alignment than a sub-node slot");
  return new (slot) T(context);
}

/** @brief The app's choice of maker for a node, or nullptr to use the module table. */
using ChooseNodeFn = MakeNodeFn (*)(const SubNodeInfo& info, void* context);

/**
 * @brief Picks the class of each sub-node: by its module vendor and product IDs (from its
 *        module info), or by an app function that may also look at the position or the
 *        chip ID. A node nothing matches is a plain SubNode.
 *
 * Fill it before Network::Start() and give it to Network::SetNodeFactory().
 */
class NodeFactory {
 public:
  /** @brief Most modules in the table. */
  static constexpr size_t kMaxModules = 16;

  NodeFactory() = default;  ///< Empty: every node is a plain SubNode.

  Result ByModule(uint16_t vendorId, uint16_t productId,
                  MakeNodeFn make);                               ///< Maker for a module.
  void SetChooser(ChooseNodeFn choose, void* context = nullptr);  ///< App function, asked first.
  MakeNodeFn Find(const SubNodeInfo& info) const;  ///< The maker for a node, or nullptr.

 private:
  /** @brief One module of the table. */
  struct Entry {
    uint16_t vendorId;
    uint16_t productId;
    MakeNodeFn make;
  };

  Entry m_Entries[kMaxModules] = {};
  size_t m_Count = 0;
  ChooseNodeFn m_Choose = nullptr;
  void* m_ChooseContext = nullptr;
};

}  // namespace a2b

#endif  // A2BCPP_A2B_SUB_NODE_H_
