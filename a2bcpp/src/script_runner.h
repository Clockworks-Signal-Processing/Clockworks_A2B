/**
 * @file script_runner.h
 * @brief Runs an OpList across polls, without blocking on its waits. Internal.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_SCRIPT_RUNNER_H_
#define A2BCPP_SCRIPT_RUNNER_H_

#include <cstddef>
#include <cstdint>

#include "a2b/op_list.h"
#include "a2b/types.h"
#include "transport.h"

namespace a2b {

/// Runs one script at a time on one bus's Transport. One thread per ScriptRunner.
class ScriptRunner {
 public:
  /** @brief Where the runner is. */
  enum class State : uint8_t {
    kIdle,     ///< No script started, or stopped.
    kRunning,  ///< Waiting inside the script; poll again.
    kDone,     ///< Every operation succeeded.
    kFailed,   ///< An operation failed; see Error() and Position().
  };

  /** @brief Most data bytes in one peripheral transfer; longer ones are split. */
  static constexpr size_t kMaxChunk = 32;

  explicit ScriptRunner(Transport& transport);  ///< Idle runner.

  ScriptRunner(const ScriptRunner&) = delete;
  ScriptRunner& operator=(const ScriptRunner&) = delete;

  void Start(const OpList& script);  ///< Starts a script; the next Poll() runs it.
  void Start(const OpList& script, size_t first,
             size_t end);      ///< Starts operations first to end - 1 of a script.
  State Poll(uint32_t nowMs);  ///< Runs operations until a wait or the end.
  void Stop();                 ///< Abandons the script.

  State GetState() const { return m_State; }  ///< Where the runner is.
  size_t Position() const { return m_Next; }  ///< Next operation, or the one that failed.
  Result Error() const { return m_Error; }    ///< Why the script failed.

 private:
  Result Run(const Op& op, uint32_t nowMs);     ///< Runs one operation.
  Result WritePeripheral(const Op& op);         ///< Runs a peripheral write, in chunks.
  Result ReadPeripheral(const Op& op);          ///< Runs a peripheral read, in chunks.
  size_t PutAddr(const Op& op, uint32_t addr);  ///< Puts a sub-address in m_Buf.

  Transport& m_Transport;
  const OpList* m_Script = nullptr;
  size_t m_Next = 0;
  size_t m_End = 0;  ///< One past the last operation to run.
  bool m_Waiting = false;
  uint32_t m_WaitEnd = 0;
  State m_State = State::kIdle;
  Result m_Error = Result::kOk;
  uint8_t m_Buf[4 + kMaxChunk] = {};  ///< Sub-address and data of one transfer.
};

}  // namespace a2b

#endif  // A2BCPP_SCRIPT_RUNNER_H_
