/**
 * @file script_runner.cpp
 * @brief ScriptRunner: runs an OpList across polls.
 *
 * Poll() runs operations in order until it meets a wait that hasn't ended, then returns,
 * so a script's delays (e.g. a codec's 100 ms start-up) never block the caller. The wait
 * starts when Poll() reaches it, and ends at the first poll at or after its end time.
 *
 * Peripheral transfers go through Transport, so one Op reaches a peripheral on the host
 * I2C bus (`kMainNode`) or behind a sub-node. A transfer longer than `kMaxChunk` data bytes
 * is split. Each part starts at its own sub-address: the op's addrIncrement data bytes
 * make one sub-address step, as for the sub-node peripheral writes of ADI's stack
 * (a2b_genSpiCmdsAndPeriWrite in a2bplugin-slave/src/a2b_peri_config.c). ADI's
 * stack sent a host peripheral's write in one transfer however long it was; splitting
 * it keeps the buffer small, and the trace differs only in transfer boundaries.
 *
 * The first failure stops the script; nothing is retried.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "script_runner.h"

#include <cstring>

namespace a2b {

/**
 * Makes an idle runner.
 * @param transport The bus the scripts run on; must outlive the runner.
 */
ScriptRunner::ScriptRunner(Transport& transport) : m_Transport(transport) {}

/**
 * Starts a script, abandoning any other. Nothing is sent until the next Poll().
 * @param script The operations; the list and its data must stay valid until the script
 *        is done, fails or is stopped.
 */
void ScriptRunner::Start(const OpList& script) { Start(script, 0, script.Size()); }

/**
 * Starts part of a script: operations @p first to @p end - 1. The next Poll() runs them.
 * @param script The script; it and its data must outlive the run.
 * @param first The first operation to run.
 * @param end One past the last (clipped to the script's size).
 */
void ScriptRunner::Start(const OpList& script, size_t first, size_t end) {
  m_Script = &script;
  m_Next = first;
  m_End = (end < script.Size()) ? end : script.Size();
  m_Waiting = false;
  m_Error = Result::kOk;
  m_State = State::kRunning;
}

/**
 * Runs operations until a wait that hasn't ended, a failure, or the end of the script.
 * @param nowMs Platform::NowMs().
 * @return State::kRunning while a wait is pending, State::kDone at the end,
 *         State::kFailed after a failure, State::kIdle if no script is running.
 */
ScriptRunner::State ScriptRunner::Poll(uint32_t nowMs) {
  if (m_State != State::kRunning) {
    return m_State;
  }
  if (m_Waiting) {
    if (!TimeReached(nowMs, m_WaitEnd)) {
      return m_State;
    }
    m_Waiting = false;
    ++m_Next;
  }
  while (m_Next < m_End) {
    const Op& op = (*m_Script)[m_Next];
    const Result result = Run(op, nowMs);
    if (result != Result::kOk) {
      m_Error = result;
      m_State = State::kFailed;
      return m_State;
    }
    if (m_Waiting) {
      return m_State;
    }
    ++m_Next;
  }
  m_State = State::kDone;
  return m_State;
}

/** Abandons the script; the runner is idle. */
void ScriptRunner::Stop() {
  m_Script = nullptr;
  m_Waiting = false;
  m_State = State::kIdle;
}

/** Runs one operation, or starts its wait. */
Result ScriptRunner::Run(const Op& op, uint32_t nowMs) {
  switch (op.type) {
    case OpType::kRegWrite:
      return m_Transport.WriteReg(op.node, op.reg, op.value);
    case OpType::kWait:
      if (op.addr > 0) {
        m_WaitEnd = nowMs + op.addr;
        m_Waiting = true;
      }
      return Result::kOk;
    case OpType::kPeripheralWrite:
      return WritePeripheral(op);
    case OpType::kPeripheralRead:
      return ReadPeripheral(op);
  }
  return Result::kInvalidArgument;
}

/** Writes the op's data in transfers of at most #kMaxChunk bytes, each after its sub-address. */
Result ScriptRunner::WritePeripheral(const Op& op) {
  if (op.addrWidth > 4 || op.data == nullptr || op.len == 0) {
    return Result::kInvalidArgument;
  }
  const size_t step = (op.addrIncrement > 1) ? op.addrIncrement : 1;
  const size_t chunk = kMaxChunk - (kMaxChunk % step);
  if (chunk == 0) {
    return Result::kInvalidArgument;
  }
  size_t done = 0;
  while (done < op.len) {
    const size_t len = (op.len - done < chunk) ? op.len - done : chunk;
    const size_t addrLen = PutAddr(op, op.addr + static_cast<uint32_t>(done / step));
    std::memcpy(m_Buf + addrLen, op.data + done, len);
    const Result result = m_Transport.PeripheralWrite(op.node, op.i2cAddr, m_Buf, addrLen + len);
    if (result != Result::kOk) {
      return result;
    }
    done += len;
  }
  return Result::kOk;
}

/** Reads the op's length in reads of at most #kMaxChunk bytes, each after its sub-address. */
Result ScriptRunner::ReadPeripheral(const Op& op) {
  if (op.addrWidth > 4 || op.len == 0) {
    return Result::kInvalidArgument;
  }
  const size_t step = (op.addrIncrement > 1) ? op.addrIncrement : 1;
  const size_t chunk = kMaxChunk - (kMaxChunk % step);
  if (chunk == 0) {
    return Result::kInvalidArgument;
  }
  size_t done = 0;
  while (done < op.len) {
    const size_t len = (op.len - done < chunk) ? op.len - done : chunk;
    uint8_t addr[4];
    const size_t addrLen = PutAddr(op, op.addr + static_cast<uint32_t>(done / step));
    std::memcpy(addr, m_Buf, addrLen);
    const Result result =
        (addrLen == 0)
            ? m_Transport.PeripheralRead(op.node, op.i2cAddr, m_Buf, len)
            : m_Transport.PeripheralWriteRead(op.node, op.i2cAddr, addr, addrLen, m_Buf, len);
    if (result != Result::kOk) {
      return result;
    }
    done += len;
  }
  return Result::kOk;
}

/** Puts the op's addrWidth bytes of @p addr, MSB first, at the start of m_Buf. */
size_t ScriptRunner::PutAddr(const Op& op, uint32_t addr) {
  for (size_t i = 0; i < op.addrWidth; ++i) {
    m_Buf[i] = static_cast<uint8_t>(addr >> (8 * (op.addrWidth - 1 - i)));
  }
  return op.addrWidth;
}

}  // namespace a2b
