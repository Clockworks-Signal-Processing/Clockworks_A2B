/**
 * @file transport.cpp
 * @brief Register and peripheral access to the nodes of one bus, with the NODEADR,
 *        CHIP and MMRPAGE registers cached.
 *
 * Follows a2b_i2cPrepAccess (a2bstack/src/i2c.c) and the MMR page handling of
 * a2b_simpleRegWrite/Read (hwaccess.c).
 *
 * The host sees two I2C addresses per bus. The main address reaches the main node's
 * registers. The bus address reaches whatever NODEADR selects: a sub-node's registers,
 * every sub-node at once (broadcast), or a peripheral on a sub-node (at the I2C address
 * in that sub-node's CHIP register).
 *
 * Transport writes NODEADR, CHIP and MMRPAGE only when the cached value differs, so a
 * run of accesses to one target costs one transfer each:
 * - NODEADR: the last value written, for the whole bus.
 * - CHIP: per sub-node, so going back to a node's peripheral after a register
 *   access needs only a NODEADR write.
 * - MMRPAGE: per node. A page stays selected until an access needs the other page
 *   (ADI's stack switched back to page 0 after every page-1 access).
 *
 * Other code must not change these registers behind Transport's back. Writing them
 * through WriteRegs() or BroadcastWrite() is allowed and makes Transport forget the
 * cached value, so the next access rewrites it. A failed cache write also forgets it.
 *
 * Each call holds Platform::Lock() for all its transfers. Calls on one Transport must
 * come from one thread.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "transport.h"

#include <cstring>

#include "detail/regdefs.h"

namespace a2b {
namespace {

/** Holds Platform::Lock() for one Transport call. */
class LockGuard {
 public:
  explicit LockGuard(Platform& platform) : m_Platform(platform) { m_Platform.Lock(); }
  ~LockGuard() { m_Platform.Unlock(); }
  LockGuard(const LockGuard&) = delete;
  LockGuard& operator=(const LockGuard&) = delete;

 private:
  Platform& m_Platform;
};

/** The MMR page of a register address (its high byte). */
constexpr uint8_t PageOf(uint16_t reg) { return static_cast<uint8_t>(reg >> 8); }

/** The register's offset within its page (its low byte). */
constexpr uint8_t LowByte(uint16_t reg) { return static_cast<uint8_t>(reg & 0xFF); }

/** True for a non-empty run of registers within page 0 or page 1. */
constexpr bool IsValidRun(uint16_t reg, size_t len) {
  return len > 0 && PageOf(reg) <= 1 && LowByte(reg) + len <= 0x100;
}

/** True if the run of @p len registers from @p reg covers the register at @p low in its page. */
constexpr bool Covers(uint16_t reg, size_t len, uint16_t low) {
  return low >= LowByte(reg) && low < LowByte(reg) + len;
}

/** True for #kMainNode or a sub-node position. */
constexpr bool IsNode(NodeAddr node) { return node == kMainNode || IsSubNode(node); }

/** True for a 7-bit I2C address. */
constexpr bool IsI2cAddr(uint8_t addr) { return addr < 0x80; }

}  // namespace

/**
 * Makes a Transport with nothing cached.
 * @param platform Host I2C access; must outlive the Transport.
 * @param mainAddr 7-bit I2C address of the main node.
 * @param busAddr 7-bit I2C bus address (usually @p mainAddr + 1).
 */
Transport::Transport(Platform& platform, uint8_t mainAddr, uint8_t busAddr)
    : m_Platform(platform), m_MainAddr(mainAddr), m_BusAddr(busAddr) {
  ForgetAll();
}

/**
 * Writes consecutive registers of one node, split into transfers of at most #kMaxRegBurst.
 * @param node #kMainNode or a sub-node.
 * @param reg First register; above 0xFF selects MMR page 1.
 * @param data Values to write.
 * @param len Number of registers; the run must stay within one page.
 * @return Result::kOk, Result::kInvalidArgument, or Result::kI2cError.
 */
Result Transport::WriteRegs(NodeAddr node, uint16_t reg, const uint8_t* data, size_t len) {
  if (!IsNode(node) || data == nullptr || !IsValidRun(reg, len)) {
    return Result::kInvalidArgument;
  }
  LockGuard lock(m_Platform);
  const uint32_t startMs = m_Platform.NowMs();
  Result result = SelectPage(node, PageOf(reg));
  if (result == Result::kOk && node != kMainNode) {
    result = SelectNodeadr(static_cast<uint8_t>(node));
  }
  const uint8_t addr = (node == kMainNode) ? m_MainAddr : m_BusAddr;
  for (size_t done = 0; result == Result::kOk && done < len; done += kMaxRegBurst) {
    const size_t chunk = (len - done < kMaxRegBurst) ? len - done : kMaxRegBurst;
    result = WriteChunk(addr, static_cast<uint8_t>(LowByte(reg) + done), data + done, chunk);
  }
  NoteRegisterWrites(node, reg, len);
  return NoteResult(result, node, reg, true, startMs);
}

/**
 * Reads consecutive registers of one node, in one write-read transfer.
 * @param node #kMainNode or a sub-node.
 * @param reg First register; above 0xFF selects MMR page 1.
 * @param data Buffer for the values.
 * @param len Number of registers; the run must stay within one page.
 * @return Result::kOk, Result::kInvalidArgument, or Result::kI2cError.
 */
Result Transport::ReadRegs(NodeAddr node, uint16_t reg, uint8_t* data, size_t len) {
  if (!IsNode(node) || data == nullptr || !IsValidRun(reg, len)) {
    return Result::kInvalidArgument;
  }
  LockGuard lock(m_Platform);
  const uint32_t startMs = m_Platform.NowMs();
  Result result = SelectPage(node, PageOf(reg));
  if (result == Result::kOk && node != kMainNode) {
    result = SelectNodeadr(static_cast<uint8_t>(node));
  }
  if (result == Result::kOk) {
    const uint8_t addr = (node == kMainNode) ? m_MainAddr : m_BusAddr;
    const uint8_t regLow = LowByte(reg);
    result = m_Platform.I2cWriteRead(addr, &regLow, 1, data, len);
  }
  return NoteResult(result, node, reg, false, startMs);
}

/**
 * Writes consecutive registers of every sub-node at once. Page 0 only: sub-nodes that
 * Transport last left on page 1 are switched back to page 0 first, one by one.
 * @param reg First register (page 0).
 * @param data Values to write.
 * @param len Number of registers.
 * @return Result::kOk, Result::kInvalidArgument, or Result::kI2cError.
 */
Result Transport::BroadcastWrite(uint16_t reg, const uint8_t* data, size_t len) {
  if (data == nullptr || !IsValidRun(reg, len) || PageOf(reg) != 0) {
    return Result::kInvalidArgument;
  }
  LockGuard lock(m_Platform);
  const uint32_t startMs = m_Platform.NowMs();
  Result result = Result::kOk;
  for (NodeAddr node = 0; result == Result::kOk && node < kMaxSubNodes; ++node) {
    result = SelectPage(node, 0);
  }
  if (result == Result::kOk) {
    result = SelectNodeadr(reg::kNodeadrBrcst);
  }
  for (size_t done = 0; result == Result::kOk && done < len; done += kMaxRegBurst) {
    const size_t chunk = (len - done < kMaxRegBurst) ? len - done : kMaxRegBurst;
    result = WriteChunk(m_BusAddr, static_cast<uint8_t>(LowByte(reg) + done), data + done, chunk);
  }
  for (NodeAddr node = 0; node < kMaxSubNodes; ++node) {
    NoteRegisterWrites(node, reg, len);
  }
  return NoteResult(result, kNoNode, reg, true, startMs);
}

/**
 * Writes to a peripheral, in one I2C transfer.
 * @param node #kMainNode (the peripheral is on the host I2C bus) or a sub-node.
 * @param i2cAddr 7-bit I2C address of the peripheral.
 * @param data Bytes to write, including any sub-address the peripheral expects.
 * @param len Number of bytes, at least 1.
 * @return Result::kOk, Result::kInvalidArgument, or Result::kI2cError.
 */
Result Transport::PeripheralWrite(NodeAddr node, uint8_t i2cAddr, const uint8_t* data, size_t len) {
  if (!IsNode(node) || !IsI2cAddr(i2cAddr) || data == nullptr || len == 0) {
    return Result::kInvalidArgument;
  }
  LockGuard lock(m_Platform);
  if (node == kMainNode) {
    return m_Platform.I2cWrite(i2cAddr, data, len);
  }
  const Result result = SelectPeripheral(node, i2cAddr);
  return (result == Result::kOk) ? m_Platform.I2cWrite(m_BusAddr, data, len) : result;
}

/**
 * Reads from a peripheral, in one I2C transfer. Parameters as for PeripheralWrite().
 * @return Result::kOk, Result::kInvalidArgument, or Result::kI2cError.
 */
Result Transport::PeripheralRead(NodeAddr node, uint8_t i2cAddr, uint8_t* data, size_t len) {
  if (!IsNode(node) || !IsI2cAddr(i2cAddr) || data == nullptr || len == 0) {
    return Result::kInvalidArgument;
  }
  LockGuard lock(m_Platform);
  if (node == kMainNode) {
    return m_Platform.I2cRead(i2cAddr, data, len);
  }
  const Result result = SelectPeripheral(node, i2cAddr);
  return (result == Result::kOk) ? m_Platform.I2cRead(m_BusAddr, data, len) : result;
}

/**
 * Writes then reads a peripheral, joined by a repeated start. Parameters as for
 * PeripheralWrite(), plus the read buffer and its length.
 * @return Result::kOk, Result::kInvalidArgument, or Result::kI2cError.
 */
Result Transport::PeripheralWriteRead(NodeAddr node, uint8_t i2cAddr, const uint8_t* writeData,
                                      size_t writeLen, uint8_t* readData, size_t readLen) {
  if (!IsNode(node) || !IsI2cAddr(i2cAddr) || writeData == nullptr || writeLen == 0 ||
      readData == nullptr || readLen == 0) {
    return Result::kInvalidArgument;
  }
  LockGuard lock(m_Platform);
  if (node == kMainNode) {
    return m_Platform.I2cWriteRead(i2cAddr, writeData, writeLen, readData, readLen);
  }
  const Result result = SelectPeripheral(node, i2cAddr);
  return (result == Result::kOk)
             ? m_Platform.I2cWriteRead(m_BusAddr, writeData, writeLen, readData, readLen)
             : result;
}

/**
 * Points NODEADR at a sub-node's registers, unless it already points there. Discovery
 * needs this: the TRMs ask for NODEADR.NODE = n - 1 when DISCVRY is written for node n.
 * @param node The sub-node.
 * @return Result::kOk, Result::kInvalidArgument, or Result::kI2cError.
 */
Result Transport::SelectNode(NodeAddr node) {
  if (!IsSubNode(node)) {
    return Result::kInvalidArgument;
  }
  LockGuard lock(m_Platform);
  return SelectNodeadr(static_cast<uint8_t>(node));
}

/**
 * Forgets one sub-node's cached state after it was reset, dropped or rediscovered: CHIP
 * becomes unknown and MMRPAGE its reset value (page 0).
 * @param node The sub-node; anything else is ignored.
 */
void Transport::ForgetNode(NodeAddr node) {
  if (!IsSubNode(node)) {
    return;
  }
  m_Chip[node] = kUnknown;
  m_MmrPage[PageIndex(node)] = 0;
}

/** Forgets all cached state, after a main-node reset. */
void Transport::ForgetAll() {
  m_Nodeadr = kUnknown;
  for (uint8_t& chip : m_Chip) {
    chip = kUnknown;
  }
  for (uint8_t& page : m_MmrPage) {
    page = 0;
  }
}

/**
 * Changes the bus's I2C addresses, e.g. when its configuration changes before a start-up.
 * Everything cached is forgotten.
 * @param mainAddr 7-bit I2C address of the main node.
 * @param busAddr 7-bit I2C bus address.
 */
void Transport::SetAddresses(uint8_t mainAddr, uint8_t busAddr) {
  m_MainAddr = mainAddr;
  m_BusAddr = busAddr;
  ForgetAll();
}

/** Writes NODEADR = @p value unless it already holds it. */
Result Transport::SelectNodeadr(uint8_t value) {
  if (m_Nodeadr == value) {
    return Result::kOk;
  }
  const uint8_t buf[2] = {static_cast<uint8_t>(reg::kNodeadr), value};
  const Result result = m_Platform.I2cWrite(m_MainAddr, buf, sizeof(buf));
  m_Nodeadr = (result == Result::kOk) ? value : kUnknown;
  return result;
}

/** Writes the node's MMRPAGE = @p page unless it is already on that page. */
Result Transport::SelectPage(NodeAddr node, uint8_t page) {
  uint8_t& cached = m_MmrPage[PageIndex(node)];
  if (cached == page) {
    return Result::kOk;
  }
  uint8_t addr = m_MainAddr;
  if (node != kMainNode) {
    const Result result = SelectNodeadr(static_cast<uint8_t>(node));
    if (result != Result::kOk) {
      return result;
    }
    addr = m_BusAddr;
  }
  const uint8_t buf[2] = {static_cast<uint8_t>(reg::kMmrpage), page};
  const Result result = m_Platform.I2cWrite(addr, buf, sizeof(buf));
  cached = (result == Result::kOk) ? page : kUnknown;
  return result;
}

/** Sets the sub-node's CHIP to @p i2cAddr if needed, then NODEADR to its peripheral. */
Result Transport::SelectPeripheral(NodeAddr node, uint8_t i2cAddr) {
  uint8_t& cached = m_Chip[node];
  if (cached != i2cAddr) {
    Result result = SelectNodeadr(static_cast<uint8_t>(node));
    if (result != Result::kOk) {
      return result;
    }
    const uint8_t buf[2] = {static_cast<uint8_t>(reg::kChip), i2cAddr};
    result = m_Platform.I2cWrite(m_BusAddr, buf, sizeof(buf));
    cached = (result == Result::kOk) ? i2cAddr : kUnknown;
    if (result != Result::kOk) {
      return result;
    }
  }
  return SelectNodeadr(static_cast<uint8_t>(node) | reg::kNodeadrPeri);
}

/** Writes up to #kMaxRegBurst registers from @p regLow in one transfer. */
Result Transport::WriteChunk(uint8_t i2cAddr, uint8_t regLow, const uint8_t* data, size_t len) {
  uint8_t buf[1 + kMaxRegBurst];
  buf[0] = regLow;
  std::memcpy(&buf[1], data, len);
  return m_Platform.I2cWrite(i2cAddr, buf, len + 1);
}

/**
 * Keeps a failed register access as LastFailure().
 * @return @p result.
 */
Result Transport::NoteResult(Result result, NodeAddr node, uint16_t reg, bool write,
                             uint32_t startMs) {
  if (result == Result::kI2cError) {
    m_LastFailure.node = node;
    m_LastFailure.reg = reg;
    m_LastFailure.write = write;
    m_LastFailure.tries = 1;
    m_LastFailure.elapsedMs = m_Platform.NowMs() - startMs;
  }
  return result;
}

/** Forgets a cached NODEADR, CHIP or MMRPAGE that a register write just covered. */
void Transport::NoteRegisterWrites(NodeAddr node, uint16_t reg, size_t len) {
  // MMRPAGE sits at the same offset on both pages; NODEADR and CHIP only on page 0.
  if (Covers(reg, len, reg::kMmrpage)) {
    m_MmrPage[PageIndex(node)] = kUnknown;
  }
  if (PageOf(reg) != 0) {
    return;
  }
  if (node == kMainNode) {
    if (Covers(reg, len, reg::kNodeadr)) {
      m_Nodeadr = kUnknown;
    }
  } else if (Covers(reg, len, reg::kChip)) {
    m_Chip[node] = kUnknown;
  }
}

}  // namespace a2b
