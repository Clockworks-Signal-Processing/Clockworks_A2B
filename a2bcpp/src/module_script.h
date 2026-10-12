/**
 * @file module_script.h
 * @brief Runs the peripheral scripts of a sub-node's module-info image, reading them from
 *        the ModuleInfoSource as they run. Internal; the layout is in module_script.cpp.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_MODULE_SCRIPT_H_
#define A2BCPP_MODULE_SCRIPT_H_

#include <cstddef>
#include <cstdint>

#include "a2b/module_info.h"
#include "a2b/types.h"
#include "transport.h"

namespace a2b {

/// Runs one node's peripheral configuration units across polls. One thread per object.
class ModuleScript {
 public:
  /** @brief Where the script is. */
  enum class State : uint8_t {
    kIdle,     ///< Not started, or stopped.
    kRunning,  ///< Waiting inside the script; poll again.
    kDone,     ///< Every unit ran (or the source has no peripheral blocks to give).
    kFailed,   ///< Stopped; see Error().
  };

  static constexpr uint32_t kPeripheralInfoAddr = 0x800;  ///< The peripheral info block.
  static constexpr size_t kMaxBlocks = 8;                 ///< Most peripheral blocks.

  explicit ModuleScript(Transport& transport);  ///< Idle.

  ModuleScript(const ModuleScript&) = delete;
  ModuleScript& operator=(const ModuleScript&) = delete;

  void Start(ModuleInfoSource& source, const SubNodeIdentity& node, uint8_t blockCount,
             uint8_t* buf, size_t bufLen);  ///< Starts; the next Poll() runs it.
  State Poll(uint32_t nowMs);               ///< Runs units until a wait or the end.
  void Stop();                              ///< Abandons the script.

  Result Error() const { return m_Error; }        ///< Why it failed.
  size_t UnitsRun() const { return m_UnitsRun; }  ///< Units run so far.
  /// Blocks for SPI peripherals, whose units were not run (SPI is not supported yet).
  size_t SkippedBlocks() const { return m_SkippedBlocks; }

 private:
  /** @brief The next thing to read. */
  enum class Step : uint8_t {
    kInfo,   ///< The peripheral info block: where the peripheral blocks are.
    kBlock,  ///< The next peripheral block: its interface and its units.
    kUnit,   ///< The next unit of the block.
  };

  Result ReadInfo();               ///< Reads the peripheral info block.
  Result ReadBlock();              ///< Reads the next peripheral block.
  Result RunUnit(uint32_t nowMs);  ///< Reads and runs the next unit.
  Result ReadBlockAt(uint32_t addr, uint8_t type,
                     size_t& payloadLen);  ///< Reads a block and checks its CRC.
  State Fail(Result error);                ///< Stops with an error.

  Transport& m_Transport;
  ModuleInfoSource* m_Source = nullptr;
  SubNodeIdentity m_Node;
  uint8_t* m_Buf = nullptr;
  size_t m_BufLen = 0;
  State m_State = State::kIdle;
  Step m_Step = Step::kInfo;
  Result m_Error = Result::kOk;
  bool m_Waiting = false;
  uint32_t m_WaitEnd = 0;
  uint8_t m_BlockCount = 0;
  uint8_t m_Block = 0;  ///< Next block.
  uint32_t m_BlockAddr[kMaxBlocks] = {};
  uint32_t m_UnitAddr = 0;   ///< Next unit's address.
  uint16_t m_UnitsLeft = 0;  ///< Units left in the block.
  size_t m_UnitsRun = 0;
  size_t m_SkippedBlocks = 0;
};

}  // namespace a2b

#endif  // A2BCPP_MODULE_SCRIPT_H_
