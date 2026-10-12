/**
 * @file module_script.cpp
 * @brief ModuleScript: runs the peripheral configuration units of a module-info image.
 *
 * Follows the peripheral half of a2b_ReadVerify_ModuleInfo (a2bpnp_calc.c) and
 * a2b_pnp_periphCfgProcessing (a2bplugin-slave/src/a2b_peri_config.c), with the
 * layout of a2bpnp_local.h, checked against ADI's images in adi/module_info/ (an ADAU1701
 * with eight units in adi_a2b_node_ModuleInfo_1.3.0.0_RJ45.dat) and the boot4 golden. Every
 * block starts with a three-byte header: type (high nibble of byte 0), payload size (12
 * bits) and one more byte. Multi-byte fields are low byte first unless noted.
 *
 * The layout:
 * - 0x7A: the number of peripheral blocks (ModuleInfo::peripheralBlocks), up to 8.
 * - 0x800: the peripheral info block, type 4: a 32-bit address per peripheral block, then
 *   a CRC-8.
 * - At each of those addresses: a peripheral block, type 5, with an 80-byte payload:
 *   interface (0 I2C, 1 SPI), name (64 bytes), address, group maps, pins, widths, number
 *   of units (12 bits, high byte first) at payload 77, CRC-8 at payload 79.
 * - After each peripheral block: its configuration units, one after the other.
 *
 * A block's CRC-8 covers its header and its payload up to the CRC. The units, by type
 * (header byte 2; payload; what runs):
 * - A (0): the I2C address; the bytes to write (sub-address first); one I2C write.
 * - B (1): the I2C address; the bytes to write, then a CRC-8 of the header and those
 *   bytes; one I2C write of all but the CRC.
 * - C (2): a CRC-8 of header bytes 0-1; none; a wait of the payload size, in ms.
 *
 * ADI's stack read the peripheral info block at 0x800 whatever the module-info
 * block's pointer to it said (it only logged a mismatch), and so does this port. Its
 * peripheral block count came from byte 0x7A, and so does this one. Each write goes to
 * the unit's own address (ADI's stack did the same; the block's address field was
 * not used). Blocks for SPI peripherals are skipped (SPI is not supported yet).
 *
 * Differences from ADI's stack (each checked data ADI's stack took on trust):
 * the peripheral info block and each peripheral block must have their type and a valid
 * CRC-8; a unit longer than the buffer, or with an unknown type, stops the script. A type
 * B unit's CRC and a type C unit's CRC are checked, as before.
 *
 * Each write is one transfer however long it is (ADI's ADAU1701 program is 4090 bytes),
 * because a peripheral's sub-address step is not in the image, so a write can't be split
 * safely. The unit is read into the buffer the Network gives (4096 bytes: the 12-bit size
 * field's range), then written. Each Poll() runs units until a wait starts or one write is
 * done, so one poll takes at most one unit: at a sub-node's default 100 kHz the ADAU1701
 * program takes about 0.4 s to read and 0.4 s to write.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "module_script.h"

#include "crc8.h"

namespace a2b {
namespace {

constexpr size_t kHeaderLen = 3;             ///< Block and unit header.
constexpr uint8_t kInfoBlockType = 4;        ///< Peripheral info block.
constexpr uint8_t kPeripheralBlockType = 5;  ///< Peripheral block.
constexpr size_t kPeripheralBlockLen = 80;   ///< A peripheral block's payload.
constexpr size_t kInterface = 0;             ///< Payload offset of the interface.
constexpr uint8_t kInterfaceI2c = 0;         ///< Interface: I2C.
constexpr size_t kUnitCount = 77;            ///< Payload offset of the unit count.
constexpr uint8_t kUnitTypeA = 0;            ///< Write.
constexpr uint8_t kUnitTypeB = 1;            ///< Write with a CRC.
constexpr uint8_t kUnitTypeC = 2;            ///< Wait.

/** A block's or unit's type. */
uint8_t TypeOf(const uint8_t* header) { return static_cast<uint8_t>(header[0] >> 4); }

/** A block's or unit's payload size. */
size_t SizeOf(const uint8_t* header) {
  return (static_cast<size_t>(header[0] & 0x0F) << 8) | header[1];
}

}  // namespace

/**
 * Makes an idle script.
 * @param transport The bus the writes go to; must outlive the script.
 */
ModuleScript::ModuleScript(Transport& transport) : m_Transport(transport) {}

/**
 * Starts the scripts of a node. Nothing is read until the next Poll().
 * @param source Where the image comes from; must stay valid while the script runs.
 * @param node The node; its position is where the writes go.
 * @param blockCount Peripheral blocks in the image (ModuleInfo::peripheralBlocks); 0 has
 *        nothing to run.
 * @param buf Buffer for blocks and units; must stay valid while the script runs. A unit
 *        longer than it stops the script.
 * @param bufLen Its size.
 */
void ModuleScript::Start(ModuleInfoSource& source, const SubNodeIdentity& node, uint8_t blockCount,
                         uint8_t* buf, size_t bufLen) {
  m_Source = &source;
  m_Node = node;
  m_Buf = buf;
  m_BufLen = bufLen;
  m_BlockCount = blockCount;
  m_Block = 0;
  m_UnitsLeft = 0;
  m_UnitsRun = 0;
  m_SkippedBlocks = 0;
  m_Waiting = false;
  m_Error = Result::kOk;
  m_Step = Step::kInfo;
  m_State = State::kRunning;
}

/**
 * Runs units until a wait that hasn't ended, one write, a failure or the end.
 * @param nowMs Platform::NowMs().
 * @return State::kRunning while there is more to do, then State::kDone or
 *         State::kFailed; State::kIdle if not started.
 */
ModuleScript::State ModuleScript::Poll(uint32_t nowMs) {
  if (m_State != State::kRunning) {
    return m_State;
  }
  if (m_Waiting) {
    if (!TimeReached(nowMs, m_WaitEnd)) {
      return m_State;
    }
    m_Waiting = false;
  }
  for (;;) {
    Result result = Result::kOk;
    switch (m_Step) {
      case Step::kInfo:
        if (m_BlockCount == 0) {
          m_State = State::kDone;
          return m_State;
        }
        result = ReadInfo();
        if (result == Result::kNotSupported) {  // The source has only the start.
          m_State = State::kDone;
          return m_State;
        }
        m_Step = Step::kBlock;
        break;
      case Step::kBlock:
        if (m_Block == m_BlockCount) {
          m_State = State::kDone;
          return m_State;
        }
        result = ReadBlock();
        m_Step = Step::kUnit;
        break;
      case Step::kUnit:
        if (m_UnitsLeft == 0) {
          ++m_Block;
          m_Step = Step::kBlock;
          break;
        }
        result = RunUnit(nowMs);
        if (result == Result::kOk) {
          return m_State;  // One unit per poll: it waits, or it wrote.
        }
        break;
    }
    if (result != Result::kOk) {
      return Fail(result);
    }
  }
}

/** Abandons the script. */
void ModuleScript::Stop() {
  m_Source = nullptr;
  m_Waiting = false;
  m_State = State::kIdle;
}

/** Reads the peripheral info block: the address of each peripheral block. */
Result ModuleScript::ReadInfo() {
  if (m_BlockCount > kMaxBlocks) {
    return Result::kInvalidData;
  }
  size_t payloadLen = 0;
  const Result result = ReadBlockAt(kPeripheralInfoAddr, kInfoBlockType, payloadLen);
  if (result != Result::kOk) {
    return result;
  }
  if (payloadLen < 4u * m_BlockCount + 1) {
    return Result::kInvalidData;
  }
  const uint8_t* payload = m_Buf + kHeaderLen;
  for (size_t i = 0; i < m_BlockCount; ++i) {
    const uint8_t* field = payload + 4 * i;
    m_BlockAddr[i] = static_cast<uint32_t>(field[0]) | (static_cast<uint32_t>(field[1]) << 8) |
                     (static_cast<uint32_t>(field[2]) << 16) |
                     (static_cast<uint32_t>(field[3]) << 24);
  }
  return Result::kOk;
}

/** Reads the next peripheral block: its units follow it. SPI blocks get no units. */
Result ModuleScript::ReadBlock() {
  size_t payloadLen = 0;
  const uint32_t addr = m_BlockAddr[m_Block];
  const Result result = ReadBlockAt(addr, kPeripheralBlockType, payloadLen);
  if (result != Result::kOk) {
    return result;
  }
  if (payloadLen != kPeripheralBlockLen) {
    return Result::kInvalidData;
  }
  const uint8_t* payload = m_Buf + kHeaderLen;
  m_UnitAddr = addr + kHeaderLen + kPeripheralBlockLen;
  m_UnitsLeft =
      static_cast<uint16_t>(((payload[kUnitCount] & 0x0F) << 8) | payload[kUnitCount + 1]);
  if (payload[kInterface] != kInterfaceI2c) {
    m_UnitsLeft = 0;  // SPI peripherals are not supported yet.
    ++m_SkippedBlocks;
  }
  return Result::kOk;
}

/** Reads the next unit and runs it: starts its wait, or writes its bytes. */
Result ModuleScript::RunUnit(uint32_t nowMs) {
  uint8_t header[kHeaderLen];
  Result result = m_Source->ReadModuleInfo(m_Node, m_UnitAddr, header, sizeof(header));
  if (result != Result::kOk) {
    return result;
  }
  const uint8_t type = TypeOf(header);
  const size_t size = SizeOf(header);
  if (type == kUnitTypeC) {
    if (Crc8(header, 2) != header[2]) {
      return Result::kInvalidData;
    }
    m_UnitAddr += kHeaderLen;
    --m_UnitsLeft;
    ++m_UnitsRun;
    m_WaitEnd = nowMs + static_cast<uint32_t>(size);
    m_Waiting = (size > 0);
    return Result::kOk;
  }
  const uint8_t i2cAddr = header[2];
  const size_t minSize = (type == kUnitTypeB) ? 2 : 1;
  if ((type != kUnitTypeA && type != kUnitTypeB) || size < minSize || i2cAddr > 0x7F) {
    return Result::kInvalidData;
  }
  if (size > m_BufLen) {
    return Result::kNoSpace;
  }
  result = m_Source->ReadModuleInfo(m_Node, m_UnitAddr + kHeaderLen, m_Buf, size);
  if (result != Result::kOk) {
    return result;
  }
  size_t writeLen = size;
  if (type == kUnitTypeB) {
    writeLen = size - 1;
    if (Crc8(m_Buf, writeLen, Crc8(header, sizeof(header))) != m_Buf[writeLen]) {
      return Result::kInvalidData;
    }
  }
  result = m_Transport.PeripheralWrite(m_Node.position, i2cAddr, m_Buf, writeLen);
  if (result != Result::kOk) {
    return result;
  }
  m_UnitAddr += static_cast<uint32_t>(kHeaderLen + size);
  --m_UnitsLeft;
  ++m_UnitsRun;
  return Result::kOk;
}

/**
 * Reads the block at @p addr into the buffer (header, then payload) and checks its type
 * and its CRC-8, the payload's last byte.
 * @param payloadLen Set to the payload size.
 */
Result ModuleScript::ReadBlockAt(uint32_t addr, uint8_t type, size_t& payloadLen) {
  Result result = m_Source->ReadModuleInfo(m_Node, addr, m_Buf, kHeaderLen);
  if (result != Result::kOk) {
    return result;
  }
  payloadLen = SizeOf(m_Buf);
  if (TypeOf(m_Buf) != type || payloadLen == 0) {
    return Result::kInvalidData;
  }
  if (kHeaderLen + payloadLen > m_BufLen) {
    return Result::kNoSpace;
  }
  result = m_Source->ReadModuleInfo(m_Node, addr + kHeaderLen, m_Buf + kHeaderLen, payloadLen);
  if (result != Result::kOk) {
    return result;
  }
  const size_t crcAt = kHeaderLen + payloadLen - 1;
  return (Crc8(m_Buf, crcAt) == m_Buf[crcAt]) ? Result::kOk : Result::kInvalidData;
}

/** Stops with @p error. */
ModuleScript::State ModuleScript::Fail(Result error) {
  m_Error = error;
  m_Waiting = false;
  m_State = State::kFailed;
  return m_State;
}

}  // namespace a2b
