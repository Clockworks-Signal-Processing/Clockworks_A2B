/**
 * @file minimal_app.cpp
 * @brief MinimalApp: the whole of an app that brings up one A2B bus.
 *
 * What an app does, in order (the quick start, a2bcpp/docs/quick_start.md, walks
 * through it):
 * 1. Fill a BoardConfig from the SigmaStudio+ export (ImportSsPlus()), and give every link
 *    the export's (its XCVRBINV; Network::SetLinkProfile()): the bench's links are all
 *    alike, RJ45 cables run single-step. Without that, the link after each sub-node would
 *    be the one its image's B port type gives (LinkProfile::ForBPort()).
 * 2. Make a Network for the bus with the Platform and the BoardConfig.
 * 3. Sub-nodes' module info comes from their EEPROMs by default: settings, power
 *    configuration and peripheral scripts. With ignoreEeprom (`--ignore-eeprom` on
 *    Linux), for blank modules, the app gives the images instead: this one gives every
 *    node the settings the bench's modules had in golden G13, GPIO0-6 as inputs and
 *    PINCFG 0, in power configuration CFG-4 (24 V). The stack uses each chip's own power
 *    configuration (CFG-4 on an AD2437, CFG-0 on the others), whatever the image says.
 * 4. Listen: the app is the bus's NetworkListener, and prints each event (states, nodes
 *    added, rejected or removed, interrupts) as the bus gives them out at the end of a
 *    Poll().
 * 5. Start() once, then Poll() often, here from Loop(). The chain plugged in is brought up
 *    node by node; a node plugged in later at the end of the chain is found too, and a
 *    node unplugged is taken out (with every node after it) and found again when it is
 *    plugged back in.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "minimal_app.h"

#include <cstdio>

#include "a2b/strings.h"

namespace {

/**
 * Builds the module-info image the app gives every sub-node: a valid header (module
 * vendor 0x00AD, product 0x0037) and module-info block, power configuration CFG-4, a UTP
 * B port (the link to the next node single-step, not inverted), GPIOIEN 0x7F and PINCFG
 * 0x00; everything else at its power-on value. Layout:
 * a2bcpp/src/module_info.cpp.
 */
void MakeImage(uint8_t* image) {
  for (size_t i = 0; i < a2b::kModuleInfoLen; ++i) {
    image[i] = 0x00;
  }
  image[0x00] = 0xAA;  // Marker.
  image[0x01] = 0xAD;  // Module vendor ID, low byte first.
  image[0x03] = 0x37;  // Module product ID.
  image[0x05] = 0x10;  // Silicon version.
  image[0x0D] = 0x20;  // The module-info block is at 0x20.
  image[0x1F] = 0x2E;  // CRC-8 of bytes 0x00-0x1E.
  image[0x20] = 0x30;  // Module-info block: type 3 ...
  image[0x21] = 0x6E;  // ... and its size.
  image[0x23] = 0x40;  // Power configuration 4 (CFG-4), no local power.
  image[0x28] = 0x02;  // B port type: UTP.
  image[0x8B] = 0x7F;  // GPIOIEN: GPIO0-6 are inputs.
  image[0x8E] = 0x00;  // PINCFG: 0 (it powers on as 0x01).
}

}  // namespace

/**
 * Makes the app; nothing is sent until Setup().
 * @param platform The host's I2C, time and logging; must outlive the app.
 * @param ignoreEeprom Give every sub-node the app's module info instead of reading its
 *        EEPROM.
 */
MinimalApp::MinimalApp(a2b::Platform& platform, bool ignoreEeprom)
    : m_Platform(platform), m_Network(platform, m_Board) {
  if (ignoreEeprom) {
    m_Network.SetModuleInfoSource(&m_Source);
  }
  m_Network.SetListener(this);
}

/**
 * Makes the link into sub-node @p position a UTP link (LinkProfile::Utp(): single-step,
 * power on the A2B pair, B side not inverted), where the export's default link may be
 * inverted (an RJ45 cable, as on the bench). The other links get the export's. Call it
 * before Setup(); it applies from the first probe of that position.
 * @param position The sub-node the link leads to, 0-15.
 * @return Result::kOk, or Result::kInvalidArgument if @p position is not 0-15.
 */
a2b::Result MinimalApp::SetUtpLink(a2b::NodeAddr position) {
  const a2b::Result result = m_Network.SetLinkProfile(position, a2b::LinkProfile::Utp());
  if (result == a2b::Result::kOk) {
    m_UtpLinks |= 1u << position;
  }
  return result;
}

/**
 * Fills the BoardConfig from chain 0 of the export and starts the bus.
 * @param bcd The export's sBusDescription.
 * @return Result::kOk, or why the import or Network::Start() refused.
 */
a2b::Result MinimalApp::Setup(const ADI_A2B_BCD& bcd) {
  a2b::Result result = a2b::ImportSsPlus(bcd, 0, m_Board, m_HostScript);
  if (result != a2b::Result::kOk) {
    std::printf("SigmaStudio+ import failed (%d)\n", static_cast<int>(result));
    return result;
  }
  for (int position = 0; position < a2b::kMaxSubNodes; ++position) {
    if ((m_UtpLinks & (1u << position)) == 0) {
      (void)m_Network.SetLinkProfile(static_cast<a2b::NodeAddr>(position), m_Board.defaultLink);
    }
  }
  result = m_Network.Start();
  if (result != a2b::Result::kOk) {
    std::printf("Network::Start() failed (%d)\n", static_cast<int>(result));
  }
  return result;
}

/** Polls the bus once; OnEvent() prints what happened. */
void MinimalApp::Loop() { m_Network.Poll(m_Platform.NowMs()); }

/**
 * Prints one event: a new state, a sub-node added (with its chip and module), rejected or
 * removed, or an interrupt the stack passed on.
 * @param network The bus.
 * @param event What happened.
 */
void MinimalApp::OnEvent(a2b::Network& network, const a2b::Event& event) {
  (void)network;
  const unsigned node = static_cast<unsigned>(event.node);
  switch (event.type) {
    case a2b::EventType::kStateChanged:
      ReportState(event.state);
      return;
    case a2b::EventType::kNodeAdded:
      ReportNode(event.node);
      return;
    case a2b::EventType::kNodeRejected:
      std::printf("  sub-node %u rejected (reason %d: %s)\n", node,
                  static_cast<int>(event.rejection), a2b::ToString(event.rejection));
      return;
    case a2b::EventType::kNodeRemoved:
      std::printf("  sub-node %u removed (%s)\n", node, a2b::ToString(event.removal));
      return;
    case a2b::EventType::kInterrupt:
      std::printf("  interrupt from %s %d: type %u\n",
                  event.node == a2b::kMainNode ? "the main node" : "sub-node",
                  event.node == a2b::kMainNode ? 0 : static_cast<int>(event.node),
                  static_cast<unsigned>(event.interruptType));
      return;
    case a2b::EventType::kRoutingApplied:
    case a2b::EventType::kRoutingFailed:
      return;  // The example routes no audio (ApplyRouting() is never called).
    case a2b::EventType::kErrorReport:
      return;  // The error watch is never turned on here.
  }
}

/** Prints a new state, and what went wrong or how many sub-nodes are up. */
void MinimalApp::ReportState(a2b::NetworkState state) {
  std::printf("bus 0: %s\n", a2b::ToString(state));
  if (state == a2b::NetworkState::kFailed) {
    std::printf("  error %d\n", static_cast<int>(m_Network.Error()));
  }
  if (state == a2b::NetworkState::kRunning) {
    std::printf("  %u sub-node(s)\n", static_cast<unsigned>(m_Network.SubNodeCount()));
  }
}

/** Prints one sub-node's chip, chip ID and module. */
void MinimalApp::ReportNode(a2b::NodeAddr position) {
  const a2b::SubNodeInfo* node = m_Network.SubNodeInfoAt(position);
  if (node == nullptr) {
    return;
  }
  const uint8_t* id = node->identity.chipId;
  const a2b::ModuleInfo& module = node->moduleInfo;
  std::printf("  sub-node %d: AD24%02X version 0x%02X, chip ID %02X%02X%02X%02X%02X%02X\n",
              position, node->identity.productId, node->identity.version, id[0], id[1], id[2],
              id[3], id[4], id[5]);
  std::printf("    module %04X:%04X, CFG-%u, %u peripheral block(s)\n", module.vendorId,
              module.productId, static_cast<unsigned>(module.powerConfig),
              static_cast<unsigned>(module.peripheralBlocks));
}

/**
 * Gives every sub-node the app's module-info image.
 * @param node The node (not used: every node gets the same).
 * @param buf Buffer for the image.
 * @param bufLen Its size.
 * @return Bytes copied.
 */
size_t MinimalApp::Source::GetModuleInfo(const a2b::SubNodeIdentity& node, uint8_t* buf,
                                         size_t bufLen) {
  (void)node;
  if (bufLen < a2b::kModuleInfoLen) {
    return 0;
  }
  MakeImage(buf);
  return a2b::kModuleInfoLen;
}
