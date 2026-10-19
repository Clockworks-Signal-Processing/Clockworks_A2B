/**
 * @file node_setup.cpp
 * @brief NodeSetup: a sub-node's set-up once it is identified.
 *
 * In ADI's order (a2b_dscvryNodeComplete in a2bplugin-master/src/discovery.c, then the
 * sub-node plugin's peripheral configuration in a2bplugin-slave/src/a2b_peri_config.c),
 * after the channel groups:
 * 0. **Channel groups:** the group table from a2b::kGroupTableOffset to the end of
 *    the module-info block (ModuleInfo::groupTableLen), read from the ModuleInfoSource in
 *    one call and parsed into the node's ModuleInfo::groups. A source that can't read past
 *    the start of the image (Result::kNotSupported) leaves the node without groups, so it
 *    takes no part in the routing; so does a table that fails its checks (a log line;
 *    ADI's stack read whatever was there). A failed read is a bus failure.
 * 1. **Registers.** The module info's register settings go into a RegisterImage, in
 *    ADI's write order; the SPI settings only on chips with SPI, and on an AD242x without
 *    the bits only the AD243x has (PDMCTL2.HPFCORNER, PINCFG.GPIOSEL), so one image serves a
 *    board whose plug-in module may be of either family. The node object's
 *    SubNode::TweakRegisters() may edit them. They are then written one register per
 *    transfer, as ADI's stack wrote them, skipping registers the stack owns. I2CCFG is
 *    the stack's own: FRAMERATE on a 44.1 kHz bus, so the sub-node's I2C clock keeps its
 *    standard speeds, and DATARATE if the image sets it (ModuleInfo::fastI2c), so the
 *    peripheral scripts run at 400 kHz; the image was read at 100 kHz, the reset rate. Its
 *    other bits in the image are unused, as in ADI's stack. A node object may still replace it.
 *    The image's I2SRATE (ParseModuleInfo()) is written like the other settings.
 * 2. **Peripheral scripts** from the module info, if it lists peripheral blocks
 *    (ModuleScript, which reads them from the ModuleInfoSource as they run).
 * 3. **The node object's operations:** SubNode::OnConfigured() may add some, which
 *    ScriptRunner runs.
 *
 * A register write or group read that fails is a bus failure (Outcome::kFailed). A peripheral
 * script that fails (a transfer, or data that fails its checks) rejects the node with
 * RejectReason::kPeripheralFailed, and a failed operation of the node object with
 * RejectReason::kConfigFailed; Network then switches the node off.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "node_setup.h"

#include "detail/regdefs.h"
#include "owned_regs.h"

namespace a2b {
namespace {

constexpr uint32_t kFrameRate441 = 44100;  ///< A 44.1 kHz SYNC rate (else FRAMERATE stays 0).

/** Sub-node registers that exist only on chips with SPI. */
bool IsSpiReg(uint16_t reg) {
  return reg == reg::kSpicfg || reg == reg::kSpickdiv || reg == reg::kSpipincfg;
}

/** @brief Bits of a register that only the AD243x has; the AD242x TRM marks them unused. */
struct Ad243xOnlyBits {
  uint16_t reg;  ///< Register address.
  uint8_t mask;  ///< Its AD243x-only bits.
};

/** The AD243x-only bits among the module info's register settings (SPI aside). */
constexpr Ad243xOnlyBits kAd243xOnlyBits[] = {
    {reg::kPdmctl2, reg::kPdmctl2Hpfcorner},
    {reg::kPincfg, reg::kPincfgGpiosel},
};

/**
 * The value written for a module info's register setting on a chip of @p family: on an
 * AD242x, without the bits only the AD243x has, so an image for either family (a board with
 * a plug-in module) is safe on both.
 */
uint8_t ForFamily(ChipFamily family, uint16_t reg, uint8_t value) {
  if (family == ChipFamily::kAd242x) {
    for (const Ad243xOnlyBits& bits : kAd243xOnlyBits) {
      if (bits.reg == reg) {
        return static_cast<uint8_t>(value & ~bits.mask);
      }
    }
  }
  return value;
}

}  // namespace

/**
 * Makes an idle set-up.
 * @param platform For log lines; must outlive it.
 * @param transport The bus; must outlive it.
 * @param runner Runs the node object's operations; must be free while they run.
 * @param buf Buffer for the channel-group table and the peripheral scripts' blocks and
 *        units; must outlive it.
 * @param bufLen Its size.
 */
NodeSetup::NodeSetup(Platform& platform, Transport& transport, ScriptRunner& runner, uint8_t* buf,
                     size_t bufLen)
    : m_Platform(platform),
      m_Transport(transport),
      m_Runner(runner),
      m_Script(transport),
      m_Buf(buf),
      m_BufLen(bufLen) {}

/**
 * Starts the set-up of a node. Nothing is sent until the next Poll().
 * @param node The node's object; its info holds the identity and the parsed module info.
 *        Must stay valid while the set-up runs.
 * @param caps The node's chip.
 * @param source Where the node's module info came from, for its channel groups and
 *        peripheral scripts.
 * @param groups Where the node's channel groups go (its ModuleInfo::groups); emptied now.
 *        Must stay valid while the set-up runs.
 * @param frameRateHz The bus's SYNC rate (BoardRouting::frameRateHz), for I2CCFG.FRAMERATE.
 */
void NodeSetup::Begin(SubNode& node, const ChipCaps& caps, ModuleInfoSource& source,
                      ChannelGroups& groups, uint32_t frameRateHz) {
  m_Node = &node;
  m_Caps = &caps;
  m_Source = &source;
  m_Groups = &groups;
  m_FrameRateHz = frameRateHz;
  groups = ChannelGroups();
  m_Rejection = RejectReason::kNone;
  m_Step = Step::kGroups;
}

/**
 * Advances the set-up.
 * @param nowMs Platform::NowMs().
 * @return Outcome::kBusy until it ends, then how it ended.
 */
NodeSetup::Outcome NodeSetup::Poll(uint32_t nowMs) {
  if (m_Step == Step::kGroups) {
    const Outcome outcome = ReadGroups();
    if (outcome != Outcome::kBusy) {
      return outcome;
    }
    m_Step = Step::kRegisters;
  }
  if (m_Step == Step::kRegisters) {
    const Outcome outcome = WriteRegisters();
    if (outcome != Outcome::kBusy) {
      return outcome;
    }
    const SubNodeInfo& info = m_Node->Info();
    m_Script.Start(*m_Source, info.identity, info.moduleInfo.peripheralBlocks, m_Buf, m_BufLen);
    m_Step = Step::kPeripherals;
  }
  if (m_Step == Step::kPeripherals) {
    switch (m_Script.Poll(nowMs)) {
      case ModuleScript::State::kRunning:
        return Outcome::kBusy;
      case ModuleScript::State::kDone:
        if (!StartOps()) {
          m_Step = Step::kIdle;
          return Outcome::kDone;
        }
        break;
      default:
        return Reject(RejectReason::kPeripheralFailed);
    }
  }
  if (m_Step == Step::kOps) {
    switch (m_Runner.Poll(nowMs)) {
      case ScriptRunner::State::kRunning:
        return Outcome::kBusy;
      case ScriptRunner::State::kDone:
        m_Step = Step::kIdle;
        return Outcome::kDone;
      default:
        return Reject(RejectReason::kConfigFailed);
    }
  }
  return Outcome::kFailed;  // Step::kIdle: not started.
}

/** Abandons the set-up; nothing more is sent. */
void NodeSetup::Cancel() {
  if (m_Step == Step::kOps) {
    m_Runner.Stop();
  }
  m_Script.Stop();
  m_Step = Step::kIdle;
}

/**
 * Step 0: the channel groups, from the module-info source into the node's module info.
 * @return Outcome::kBusy when they are read, or there are none (the set-up goes on), else
 *         Outcome::kFailed.
 */
NodeSetup::Outcome NodeSetup::ReadGroups() {
  const SubNodeInfo& info = m_Node->Info();
  const size_t len =
      (info.moduleInfo.groupTableLen < m_BufLen) ? info.moduleInfo.groupTableLen : m_BufLen;
  if (len == 0) {
    return Outcome::kBusy;
  }
  const Result read = m_Source->ReadModuleInfo(info.identity, kGroupTableOffset, m_Buf, len);
  if (read == Result::kNotSupported) {
    return Outcome::kBusy;  // The source has only the start of the image: no groups.
  }
  if (read != Result::kOk) {
    m_Step = Step::kIdle;
    return Outcome::kFailed;
  }
  const Result parsed = ParseChannelGroups(m_Buf, len, *m_Groups);
  if (parsed == Result::kNoSpace) {
    m_Platform.Log(LogLevel::kWarning, "a2b: module info lists more channel groups than kept");
  } else if (parsed != Result::kOk) {
    m_Platform.Log(LogLevel::kWarning, "a2b: module info's channel-group table is malformed");
  }
  return Outcome::kBusy;
}

/**
 * Step 1: the module info's settings, edited by the node object, one register per
 * transfer.
 * @return Outcome::kBusy when they are written (the set-up goes on), else
 *         Outcome::kFailed.
 */
NodeSetup::Outcome NodeSetup::WriteRegisters() {
  const SubNodeInfo& info = m_Node->Info();
  const ModuleInfo& moduleInfo = info.moduleInfo;
  m_Image.Clear();
  for (size_t i = 0; i < moduleInfo.regCount; ++i) {
    const uint16_t reg = moduleInfo.reg[i];
    if (!IsSpiReg(reg) || m_Caps->hasSpi) {
      // Fits: kMaxRegs < kCapacity.
      m_Image.Set(reg, ForFamily(m_Caps->family, reg, moduleInfo.value[i]));
    }
  }
  uint8_t i2ccfg = 0;
  if (m_FrameRateHz == kFrameRate441) {
    i2ccfg |= reg::kI2ccfgFramerate;  // The sub-node's I2C clock follows it.
  }
  if (moduleInfo.fastI2c) {
    i2ccfg |= reg::kI2ccfgDatarate;  // Before the peripheral scripts.
  }
  if (i2ccfg != 0) {
    m_Image.Set(reg::kI2ccfg, i2ccfg);
  }
  m_Node->TweakRegisters(m_Image);
  const NodeAddr position = info.identity.position;
  for (size_t i = 0; i < m_Image.Size(); ++i) {
    const uint16_t reg = m_Image.RegAt(i);
    if (IsOwnedByStack(reg)) {
      continue;
    }
    if (m_Transport.WriteReg(position, reg, m_Image.ValueAt(i)) != Result::kOk) {
      m_Step = Step::kIdle;
      return Outcome::kFailed;
    }
  }
  return Outcome::kBusy;
}

/**
 * Step 3: asks the node object for its operations and starts them, if there are any.
 * @return True if operations were started (Step::kOps).
 */
bool NodeSetup::StartOps() {
  m_Ops.Clear();
  m_Node->OnConfigured(m_Ops);
  if (m_Ops.Size() == 0) {
    return false;
  }
  m_Runner.Start(m_Ops);
  m_Step = Step::kOps;
  return true;
}

/** Ends with a rejection for @p reason. */
NodeSetup::Outcome NodeSetup::Reject(RejectReason reason) {
  if (m_Step == Step::kOps) {
    m_Runner.Stop();
  }
  m_Script.Stop();
  m_Step = Step::kIdle;
  m_Rejection = reason;
  return Outcome::kRejected;
}

}  // namespace a2b
