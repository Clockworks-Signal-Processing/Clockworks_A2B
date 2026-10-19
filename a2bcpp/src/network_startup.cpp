/**
 * @file network_startup.cpp
 * @brief Network::Impl: the main node's start-up and configuration (kStarting, kConfiguring),
 *        restarts and failure; network.cpp's file comment has the sequence.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstring>

#include "network_impl.h"
#include "owned_regs.h"

namespace a2b {
namespace {

/** The shortest soft reset to MSTR wait: covers SUSTAIN (ADI's A2B_SW_RESET_DELAY). */
constexpr uint32_t kMinResetWaitMs = 25;
/** RESETn released to the first access: tACCESS is 1.5 ms (AD2437, AD2431-35 data sheets). */
constexpr uint32_t kResetAccessMs = 2;

}  // namespace

/**
 * kStarting: the RESETn pulse, then check the main node, soft reset, wait out SUSTAIN,
 * INV and MSTR, MSTR_RUNNING.
 */
void Network::Impl::StepStarting(uint32_t nowMs) {
  if (m_Step == Step::kEarlyScript) {
    switch (m_Runner.Poll(nowMs)) {
      case ScriptRunner::State::kRunning:
        return;
      case ScriptRunner::State::kDone:
        m_Step = Step::kResetLine;
        return;
      default:
        Fail(NetworkError::kHostScriptFailed, "a2b: host script (before the main node) failed");
        return;
    }
  }
  if ((m_Step == Step::kResetLine || m_Step == Step::kResetPulse || m_Step == Step::kResetAccess) &&
      !StepReset(nowMs)) {
    return;
  }
  switch (m_Step) {
    case Step::kReadId: {
      uint8_t ids[3] = {};
      if (m_Transport.ReadRegs(kMainNode, reg::kVendor, ids, sizeof(ids)) != Result::kOk) {
        Fail(NetworkError::kI2cError, "a2b: main node does not answer");
        return;
      }
      if (ids[0] != kAdiVendorId || ids[1] != m_Board.mainProduct) {
        Fail(NetworkError::kWrongMainNode, "a2b: main node is not the chip BoardConfig names");
        return;
      }
      if (m_Transport.WriteReg(kMainNode, reg::kControl, reg::kControlSoftrst) != Result::kOk) {
        Fail(NetworkError::kI2cError, "a2b: main node soft reset failed");
        return;
      }
      m_Transport.ForgetAll();
      const uint32_t waitMs = (m_Board.timings.rediscoveryWaitMs > kMinResetWaitMs)
                                  ? m_Board.timings.rediscoveryWaitMs
                                  : kMinResetWaitMs;
      m_Deadline = nowMs + waitMs;
      m_Step = Step::kSustainWait;
      return;
    }
    case Step::kSustainWait: {
      if (!TimeReached(nowMs, m_Deadline)) {
        return;
      }
      uint8_t i2sgcfg = 0;
      Result result = Result::kOk;
      if (m_Image.Get(reg::kI2sgcfg, i2sgcfg) && (i2sgcfg & reg::kI2sgcfgInv) != 0) {
        result = m_Transport.WriteReg(kMainNode, reg::kI2sgcfg, reg::kI2sgcfgInv);
      }
      if (result == Result::kOk) {
        result = m_Transport.WriteReg(kMainNode, reg::kControl, reg::kControlMstr);
      }
      if (result != Result::kOk) {
        Fail(NetworkError::kI2cError, "a2b: writing CONTROL.MSTR failed");
        return;
      }
      m_Deadline = nowMs + m_Board.timings.discoveryStartDelayMs;
      m_Step = Step::kWaitRunning;
      return;
    }
    case Step::kWaitRunning: {
      InterruptPoller::Interrupt irq;
      bool found = false;
      Result result = m_Poller.Poll(nowMs, irq, found);
      if (result == Result::kOk && !found && TimeReached(nowMs, m_Deadline)) {
        result = m_Poller.CheckNow(irq, found);  // The last look before giving up.
        if (result == Result::kOk && !found) {
          Fail(NetworkError::kMainNodeNotRunning, "a2b: no MSTR_RUNNING (is SYNC there?)");
          return;
        }
      }
      if (result != Result::kOk) {
        Fail(NetworkError::kI2cError, "a2b: interrupt read failed");
        return;
      }
      if (found && irq.type == reg::kInttypeMstrRunning) {
        m_Platform.Log(LogLevel::kInfo, "a2b: main node running");
        Configure();
      }
      return;
    }
    default:
      return;
  }
}

/**
 * kStarting, before the ID check: RESETn low for BoardTimings::resetPulseMs, then high,
 * then tACCESS. Without the line (Result::kNotSupported) it goes straight to the ID check;
 * a line that fails is logged and the start-up carries on without it.
 * @return True when the ID check is due, in this poll.
 */
bool Network::Impl::StepReset(uint32_t nowMs) {
  const uint8_t bus = m_Board.busIndex;
  switch (m_Step) {
    case Step::kResetLine: {
      const Result result = m_Platform.SetMainNodeReset(bus, true);
      if (result == Result::kOk) {
        m_Deadline = nowMs + m_Board.timings.resetPulseMs;
        m_Step = Step::kResetPulse;
        return false;
      }
      if (result != Result::kNotSupported) {
        m_Platform.Log(LogLevel::kWarning, "a2b: RESETn failed; starting without it");
      }
      m_Step = Step::kReadId;
      return true;
    }
    case Step::kResetPulse:
      if (!TimeReached(nowMs, m_Deadline)) {
        return false;
      }
      if (m_Platform.SetMainNodeReset(bus, false) != Result::kOk) {
        m_Platform.Log(LogLevel::kWarning, "a2b: releasing RESETn failed");
      }
      m_Transport.ForgetAll();
      m_Deadline = nowMs + kResetAccessMs;
      m_Step = Step::kResetAccess;
      return false;
    default:  // Step::kResetAccess.
      if (!TimeReached(nowMs, m_Deadline)) {
        return false;
      }
      m_Step = Step::kReadId;
      return true;
  }
}

/** kConfiguring: the main-node image, then the host script. */
void Network::Impl::StepConfiguring(uint32_t nowMs) {
  if (m_Step == Step::kWriteImage) {
    if (WriteMainImage() != Result::kOk) {
      Fail(NetworkError::kI2cError, "a2b: writing the main-node settings failed");
      return;
    }
    const size_t early = EarlyHostOps();
    if (m_Board.hostScript == nullptr || early >= m_Board.hostScript->Size()) {
      BeginDiscovery();
      return;
    }
    m_Runner.Start(*m_Board.hostScript, early, m_Board.hostScript->Size());
    m_Step = Step::kHostScript;
  }
  switch (m_Runner.Poll(nowMs)) {
    case ScriptRunner::State::kRunning:
      return;
    case ScriptRunner::State::kDone:
      BeginDiscovery();
      return;
    default:
      Fail(NetworkError::kHostScriptFailed, "a2b: host script failed");
      return;
  }
}

/**
 * Enters kStarting: the next poll pulses RESETn (@p resetLine) or reads the main node's
 * IDs. Every sub-node is forgotten, and any rejected one; the routing plan too (it is
 * written again once the chain is up, if the app routes the bus), and probes start from
 * the image's RESPCYCS again.
 */
void Network::Impl::BeginStart(bool resetLine) {
  RemoveNodes(0, RemovalReason::kRestart);
  m_Error = NetworkError::kNone;
  m_Step = resetLine ? Step::kResetLine : Step::kReadId;
  m_HasRejected = false;
  m_RejectRequest = kNoRequest;
  m_CheckNow = false;
  m_Links[0] = m_MainLink;
  m_Settings.respCycles = m_ImageRespCycles;  // The main node's registers are reset.
  RoutingPlanner::Clear(m_Plan);
  m_XbarKnown = 0;
  m_Bandwidth = BandwidthUsage();
  m_RoutingDue = m_RoutingOn;  // Routed again once the chain is up.
  m_MuteDue = false;           // The image is written with DATCTL muted if the app muted.
  m_RouteMute = false;
  m_Settling = false;
  SetState(NetworkState::kStarting);
}

/**
 * The host operations due before the main node is started: BoardConfig's
 * hostOpsBeforeMainNode, at most the script's length.
 * @return Their count; 0 without a host script.
 */
size_t Network::Impl::EarlyHostOps() const {
  if (m_Board.hostScript == nullptr) {
    return 0;
  }
  const size_t size = m_Board.hostScript->Size();
  return (m_Board.hostOpsBeforeMainNode < size) ? m_Board.hostOpsBeforeMainNode : size;
}

/** Enters kConfiguring: the next poll writes the image. */
void Network::Impl::Configure() {
  SetState(NetworkState::kConfiguring);
  m_Step = Step::kWriteImage;
}

/** Enters kDiscovering: the next poll sends the first probe; checks and BECNT follow. */
void Network::Impl::BeginDiscovery() {
  m_Platform.Log(LogLevel::kInfo, "a2b: main node configured");
  SetState(NetworkState::kDiscovering);
  m_Phase = Phase::kIdle;
  const uint32_t nowMs = m_Platform.NowMs();
  m_Deadline = nowMs;
  m_ProbeFloor = nowMs;
  m_MuteRetry = nowMs;
  m_CheckDeadline = nowMs + m_Board.timings.chainCheckMs;
  m_BecntDeadline = nowMs + m_Board.timings.becntResetMs;
  m_Poller.CheckAtNextPoll();
}

/**
 * Writes the image in its order, without the registers the stack owns, with DSCDONE added
 * to its INTMSK2 (an image without INTMSK2 gets none). A run of consecutive registers on
 * one page goes in one Transport call.
 */
Result Network::Impl::WriteMainImage() {
  uint8_t run[Transport::kMaxRegBurst];
  size_t runLen = 0;
  uint16_t runStart = 0;
  for (size_t i = 0; i <= m_Image.Size(); ++i) {
    const bool end = (i == m_Image.Size());
    const uint16_t reg = end ? 0 : m_Image.RegAt(i);
    const bool skip = !end && IsOwnedByStack(reg);
    const bool extends = !end && !skip && runLen > 0 && runLen < sizeof(run) &&
                         reg == runStart + runLen && (reg >> 8) == (runStart >> 8);
    if (runLen > 0 && !extends) {
      const Result result = m_Transport.WriteRegs(kMainNode, runStart, run, runLen);
      if (result != Result::kOk) {
        return result;
      }
      runLen = 0;
    }
    if (end || skip) {
      continue;
    }
    uint8_t value = m_Image.ValueAt(i);
    if (reg == reg::kIntmsk2) {
      value = static_cast<uint8_t>(value | reg::kIntmsk2Dscdone);
    }
    if (reg == reg::kDatctl && m_Muted) {
      value = static_cast<uint8_t>(value & ~(reg::kDatctlDns | reg::kDatctlUps));
    }
    if (runLen == 0) {
      runStart = reg;
    }
    run[runLen++] = value;
  }
  return Result::kOk;
}

/**
 * Starts again from the main-node reset, e.g. after the PLL relocked, or from RESETn
 * (@p resetLine) after the main node failed its check. The sub-nodes are taken out
 * (RemovalReason::kRestart).
 */
void Network::Impl::Restart(const char* why, bool resetLine) {
  m_Platform.Log(LogLevel::kWarning, why);
  CancelProbe();
  m_Runner.Stop();
  BeginStart(resetLine);
}

/**
 * Stops with an error; Start() tries again. The sub-nodes in the bus stay listed until
 * then; a node being set up is dropped.
 */
void Network::Impl::Fail(NetworkError error, const char* why) {
  m_Platform.Log(LogLevel::kError, why);
  CancelProbe();
  m_Runner.Stop();
  m_Error = error;
  SetState(NetworkState::kFailed);
}

}  // namespace a2b
