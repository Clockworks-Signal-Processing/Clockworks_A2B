/**
 * @file network_spi.cpp
 * @brief Network::Impl: SPI over A2B. The main node is still controlled over I2C; SPI
 *        reaches the main node's SPI slave port only to run the data tunnel's transactions
 *        (and to read its registers, a check of the wiring). docs/SPI.md has the design.
 *
 * Every transaction is one Platform::SpiTransfer(), framed by one slave select (ADR1 on the
 * main node), its first byte the command (AD2437 TRM, "SPI Programming Concepts", Table
 * 5-12). The main node takes no command while SPISTAT.SPIBUSY is set (a BADCMD), so each
 * transaction first waits for it to clear, reading it with the SPI status command (0x04,
 * which never raises an interrupt), then runs, then waits again, then reads SPIINT over I2C
 * and clears it: a bit other than SPIDONE is an error (with SPIDTERR, SPISTAT's error bits
 * say which, and are cleared too, as the TRM's "SPI Data Tunnel Error Handling" asks). The
 * wait is bounded by BoardTimings::spiTimeoutMs on the platform's clock.
 *
 * **The data tunnel** (OpenSpiTunnel(); the TRM's "SPI Over Distance"). The main node owns it
 * (SPICFG.TNLOWNER, DTCFG.DTFRST) and one AD243x sub-node, its far end, is its responder: an
 * SPI master to its peripherals (SPICFG.SPIMODE 1, SPICKDIV, the slave select outputs in
 * SPIPINCFG, DTCFG.DTLAST). Its slots are the routing plan's (routing_planner.cpp says
 * where they go), so a tunnel opened or closed has a plan written, as ApplyRouting() does,
 * whether or not the app routes audio; the plan's slot write carries the tunnel's registers
 * on both ends (WriteTunnel()). The tunnel follows its module by chip ID: it closes when the
 * module leaves the bus and opens again when it is back. On a node whose settings put
 * CLKOUT2 on ADR2 with PINCFG.GPIOSEL 0, the SPI port stops CLKOUT2 (anomaly 18000047, the
 * AB0020's codec clock), so while the tunnel is open that node has GPIOSEL 2, which keeps
 * it; that moves IO0, IO4, IO5 and IO6, so a node using them as GPIO is refused.
 *
 * The transactions (Table 5-12): the atomic write and read (0x0C, 0x0D, the latter read back
 * with the tunnel FIFO read, 0x0B) behind PeripheralWrite() and friends with
 * PeripheralAddress::Spi(); full duplex (0x09, extended 0x0E) whose read data comes back
 * with the next one; bulk (0x06, extended 0x0F); SPI abort (0x0A).
 *
 * These calls make their transfers at once, between polls, as the peripheral calls do. Each
 * keeps its command on the stack: up to 259 bytes, plus the call's own frame.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstring>

#include "network_impl.h"
#include "spi_protocol.h"

namespace a2b {

/**
 * Reads consecutive registers of the main node over SPI (the SPI local register read), at
 * once. It proves the host's SPI wiring to the main node without the bus: compare with
 * ReadRegisters(). Page 0 only.
 * @param reg First register (0x00-0xFF).
 * @param data Buffer for the values.
 * @param len Number of registers, 1 to #kSpiMaxRegisters, within page 0.
 * @return Result::kOk; Result::kInvalidArgument for a bad argument; Result::kNotSupported if
 *         the board's main node has no SPI port (an AD2428) or the host has no SPI to it
 *         (Platform::SpiTransfer()); Result::kSpiError if the transfer failed or the main
 *         node stayed busy.
 */
Result Network::SpiReadRegisters(uint8_t reg, uint8_t* data, size_t len) {
  return m_Impl->SpiReadRegisters(reg, data, len);
}

/**
 * What the main node said about the last SPI transaction: SPIINT's bits it raised, SPISTAT's
 * error bits, a timeout or a failed host transfer.
 * @return The status; all clear after a transaction that went well.
 */
SpiStatus Network::LastSpiStatus() const { return m_Impl->LastSpiStatus(); }

/**
 * Sets up the SPI data tunnel to a sub-node (docs/SPI.md): its slots on every link from the
 * main node to the node, the main node as its owner and the node as an SPI master to its
 * peripherals. Like ApplyRouting(), it is checked now and written at the next poll between
 * probes once the chain is up, with a routing plan (EventType::kRoutingApplied, then
 * EventType::kSpiTunnelOpened); the slots count in the plan's limits and response cycles.
 * One tunnel at a time: a new one replaces the last. It follows the node's module by chip
 * ID: when the module leaves the bus the tunnel closes (EventType::kSpiTunnelClosed), and it
 * opens again when the module is back, at any position. While it ends at the chain's last
 * node and that node plays or sends audio, no probe for a new node is made behind it
 * (docs/SPI.md): a node plugged in there is found once the tunnel is closed or moved.
 * @param config The node and the tunnel's settings.
 * @param why Set to the plan's error when it fails with Result::kInvalidArgument for a plan
 *        that can't be built (RoutingError::kTunnelSlots, kTooManySlots, ...); kNone else.
 * @return Result::kOk; Result::kNotSupported if the main node has no SPI port (an AD2428);
 *         Result::kInvalidArgument for a node not in the bus or without SPI, a setting out of
 *         range, a node that needs GPIOSEL 2 but uses IO0, IO4, IO5 or IO6 as GPIO, or a plan
 *         that can't be built.
 */
Result Network::OpenSpiTunnel(const SpiTunnelConfig& config, RoutingError* why) {
  return m_Impl->OpenSpiTunnel(config, why);
}

/**
 * Takes the SPI data tunnel off the bus: at the next poll a plan without it is written, and
 * its far end gets its own SPI settings (its module info's) back. EventType::kSpiTunnelClosed
 * follows.
 */
void Network::CloseSpiTunnel() { m_Impl->CloseSpiTunnel(); }

/**
 * Tells whether the SPI data tunnel's transactions can run: a tunnel was opened and its plan
 * is written, with its module in the bus, and no new plan is due (OpenSpiTunnel() called
 * again, a node added or gone).
 * @return True if they can.
 */
bool Network::SpiTunnelReady() const { return m_Impl->SpiTunnelReady(); }

/**
 * The SPI data tunnel asked for (OpenSpiTunnel()), with SpiTunnelConfig::node where its
 * module was when it was opened; nullptr if none is.
 * @return The settings, or nullptr.
 */
const SpiTunnelConfig* Network::SpiTunnel() const { return m_Impl->SpiTunnel(); }

/**
 * A full-duplex transaction through the SPI data tunnel: @p len bytes out to the peripheral
 * on slave select @p select of the tunnel's far end, while its answer is read. The answer
 * comes back with the next full-duplex transaction (in @p rxPrevious, the previous one's;
 * zeros on the first after another kind), or from SpiReadTunnelFifo() at once.
 * SpiChunk::kMore (the extended full duplex) leaves the slave select asserted, so that
 * transactions of any total length make one transfer; SpiChunk::kLast ends it.
 * @param select The slave select (0-2), one SpiTunnelConfig::selects enables.
 * @param tx Bytes to send.
 * @param rxPrevious Buffer for the previous full duplex's @p len bytes read, or nullptr.
 * @param len Number of bytes, 1 to #kSpiMaxData.
 * @param chunk SpiChunk::kLast (the full duplex, 0x09), or SpiChunk::kMore (the extended
 *        full duplex, 0x0E).
 * @return As PeripheralWrite() for an SPI peripheral.
 */
Result Network::SpiFullDuplex(uint8_t select, const uint8_t* tx, uint8_t* rxPrevious, size_t len,
                              SpiChunk chunk) {
  return m_Impl->SpiFullDuplex(select, tx, rxPrevious, len, chunk);
}

/**
 * Reads the main node's tunnel read FIFO: the data of the last atomic read or full-duplex
 * transaction. Past what it holds, zeros and Result::kSpiError (SPIINT.FIFOUNF). It touches
 * only the main node: never Result::kBusy.
 * @param data Buffer for the bytes.
 * @param len Number of bytes, 1 to #kSpiMaxData.
 * @return As PeripheralWrite() for an SPI peripheral, but for Result::kBusy.
 */
Result Network::SpiReadTunnelFifo(uint8_t* data, size_t len) {
  return m_Impl->SpiReadTunnelFifo(data, len);
}

/**
 * A bulk write through the SPI data tunnel: as SpiFullDuplex() with nothing read back. The
 * far end starts writing before all of the data has arrived. SpiChunk::kMore (the extended
 * bulk write) leaves the slave select asserted; SpiChunk::kLast ends the transfer.
 * @param select The slave select (0-2).
 * @param data Bytes to write.
 * @param len Number of bytes, 1 to #kSpiMaxData.
 * @param chunk SpiChunk::kLast (the bulk write, 0x06), or SpiChunk::kMore (the extended bulk
 *        write, 0x0F).
 * @return As PeripheralWrite() for an SPI peripheral.
 */
Result Network::SpiBulkWrite(uint8_t select, const uint8_t* data, size_t len, SpiChunk chunk) {
  return m_Impl->SpiBulkWrite(select, data, len, chunk);
}

/**
 * The SPI abort: the main node ends the transaction under way and flushes its FIFOs (an
 * extended transfer left open is ended too).
 * @return Result::kOk, Result::kNotSupported, or Result::kSpiError.
 */
Result Network::SpiAbort() { return m_Impl->SpiAbort(); }

/**
 * False if the main node is known to have no SPI port (an AD2428): the chip found at the last
 * Start(), else the board's. A main node not known yet (no board chip) may have one.
 */
bool Network::Impl::MainHasSpi() const {
  const ChipCaps* caps =
      (m_MainCaps != nullptr) ? m_MainCaps : LookUpChipCaps(kAdiVendorId, m_Board.mainProduct);
  return caps == nullptr || caps->hasSpi;
}

/**
 * One transfer on the host's SPI to the main node.
 * @return Result::kOk, Result::kNotSupported (the host has no SPI), or Result::kSpiError
 *         (recorded in the status).
 */
Result Network::Impl::SpiExchange(const uint8_t* tx, uint8_t* rx, size_t len) {
  m_Platform.Lock();
  const Result result = m_Platform.SpiTransfer(m_Board.busIndex, tx, rx, len);
  m_Platform.Unlock();
  if (result == Result::kOk || result == Result::kNotSupported) {
    return result;
  }
  m_SpiStatus.hostFailed = true;
  return Result::kSpiError;
}

/**
 * Waits for SPISTAT.SPIBUSY to clear, reading it with the SPI status command, for up to
 * BoardTimings::spiTimeoutMs.
 * @return Result::kOk, Result::kNotSupported, or Result::kSpiError (a failed transfer, or
 *         a timeout, recorded in the status).
 */
Result Network::Impl::SpiWaitIdle() {
  const uint32_t start = m_Platform.NowMs();
  for (;;) {
    uint8_t buf[2] = {spi::kStatusRead, 0};
    const Result result = SpiExchange(buf, buf, sizeof(buf));
    if (result != Result::kOk) {
      return result;
    }
    if ((buf[1] & reg::kSpistatSpibusy) == 0) {
      return Result::kOk;
    }
    if (ElapsedMs(m_Platform.NowMs(), start) > m_Board.timings.spiTimeoutMs) {
      m_SpiStatus.spiStat = buf[1];
      m_SpiStatus.timedOut = true;
      return Result::kSpiError;
    }
  }
}

/**
 * Runs one transaction: waits for the main node to be idle, sends @p tx (receiving into
 * @p rx), waits for it to end, then reads and clears SPIINT, and SPISTAT's error bits when
 * SPIINT has an error.
 * @return Result::kOk, Result::kNotSupported, Result::kI2cError (SPIINT couldn't be read), or
 *         Result::kSpiError (recorded in the status).
 */
Result Network::Impl::SpiTransaction(const uint8_t* tx, uint8_t* rx, size_t len) {
  m_SpiStatus = SpiStatus();
  m_LastSpiMs = m_Platform.NowMs();
  m_SpiUsed = true;
  Result result = SpiWaitIdle();
  if (result == Result::kOk) {
    result = SpiExchange(tx, rx, len);
  }
  if (result == Result::kOk) {
    result = SpiWaitIdle();
  }
  if (result == Result::kNotSupported || result == Result::kSpiError) {
    return result;
  }
  uint8_t spiInt = 0;
  Result read = m_Transport.ReadRegs(kMainNode, reg::kSpiint, &spiInt, 1);
  if (read == Result::kOk && spiInt != 0) {
    read = m_Transport.WriteReg(kMainNode, reg::kSpiint, spiInt);  // Write 1 to clear.
  }
  m_SpiStatus.spiInt = static_cast<uint8_t>(spiInt & reg::kSpiintErrors);
  if (read == Result::kOk && (spiInt & reg::kSpiintSpidterr) != 0) {
    uint8_t spiStat = 0;
    read = m_Transport.ReadRegs(kMainNode, reg::kSpistat, &spiStat, 1);
    m_SpiStatus.spiStat = spiStat;
    if (read == Result::kOk && (spiStat & reg::kSpistatErrors) != 0) {
      read = m_Transport.WriteReg(kMainNode, reg::kSpistat,
                                  static_cast<uint8_t>(spiStat & reg::kSpistatErrors));
    }
  }
  if (read != Result::kOk) {
    return Result::kI2cError;
  }
  return (m_SpiStatus.spiInt != 0) ? Result::kSpiError : result;
}

/** See Network::SpiReadRegisters(). */
Result Network::Impl::SpiReadRegisters(uint8_t reg, uint8_t* data, size_t len) {
  if (data == nullptr || len == 0 || len > kSpiMaxRegisters || reg + len > 0x100) {
    return Result::kInvalidArgument;
  }
  if (!MainHasSpi()) {
    return Result::kNotSupported;
  }
  uint8_t buf[3 + kSpiMaxRegisters] = {spi::kLocalRead, reg, static_cast<uint8_t>(len - 1)};
  const Result result = SpiTransaction(buf, buf, 3 + len);
  if (result == Result::kOk) {
    std::memcpy(data, buf + 3, len);
  }
  return result;
}

/** See Network::OpenSpiTunnel(). */
Result Network::Impl::OpenSpiTunnel(const SpiTunnelConfig& config, RoutingError* why) {
  if (why != nullptr) {
    *why = RoutingError::kNone;
  }
  if (!MainHasSpi()) {
    return Result::kNotSupported;
  }
  if (!IsSubNode(config.node) || static_cast<size_t>(config.node) >= m_SubNodeCount ||
      config.selects == 0 || config.selects > 0x07 || config.slots < kSpiMinTunnelSlots ||
      config.slots > kSpiMaxTunnelSlots || config.mode > 3) {
    return Result::kInvalidArgument;
  }
  const ChipCaps* caps = CapsOf(config.node);
  if (caps == nullptr || !caps->hasSpi) {
    return Result::kInvalidArgument;
  }
  if (TunnelGpioSel(config.node)) {
    const uint8_t gpio = static_cast<uint8_t>(ModuleReg(config.node, reg::kGpiooen, 0) |
                                              ModuleReg(config.node, reg::kGpioien, 0));
    if ((gpio & 0x71) != 0) {  // IO0, IO4, IO5 and IO6 move with GPIOSEL 2.
      LogNumber(m_Platform, LogLevel::kWarning, "a2b: SPI tunnel to sub-node ",
                static_cast<unsigned>(config.node),
                " refused: CLKOUT2 needs GPIOSEL 2, which moves its IO0, IO4, IO5 or IO6");
      return Result::kInvalidArgument;
    }
  }
  const SpiTunnelConfig before = m_Tunnel;
  const bool wanted = m_TunnelWanted;
  uint8_t chipId[6];
  std::memcpy(chipId, m_TunnelChipId, sizeof(chipId));
  m_Tunnel = config;
  m_TunnelWanted = true;
  std::memcpy(m_TunnelChipId, m_SubNodes[config.node].identity.chipId, sizeof(m_TunnelChipId));
  NodeAddr node = kNoNode;
  const RoutingError error = BuildPlan(m_Pending, node);
  if (error != RoutingError::kNone) {
    m_Tunnel = before;  // The tunnel that was open stays.
    m_TunnelWanted = wanted;
    std::memcpy(m_TunnelChipId, chipId, sizeof(chipId));
    if (why != nullptr) {
      *why = error;
    }
    return Result::kInvalidArgument;
  }
  if (m_Phase == Phase::kRouting) {
    m_Phase = Phase::kIdle;  // The plan being written is replaced; written again whole.
  }
  m_RoutingDue = true;
  m_RoutingRetry = m_Platform.NowMs();
  return Result::kOk;
}

/** See Network::CloseSpiTunnel(). */
void Network::Impl::CloseSpiTunnel() {
  if (!m_TunnelWanted) {
    return;
  }
  m_TunnelWanted = false;
  if (m_TunnelOnMain || m_TunnelAt != kNoNode || m_Phase == Phase::kRouting) {
    if (m_Phase == Phase::kRouting) {
      m_Phase = Phase::kIdle;
    }
    m_RoutingDue = true;  // A plan without it takes its registers off.
    m_RoutingRetry = m_Platform.NowMs();
  }
}

/** See Network::SpiTunnelReady(). */
bool Network::Impl::SpiTunnelReady() const {
  return m_TunnelWanted && !m_RoutingDue && m_Plan.valid && m_Plan.tunnel.node != kNoNode &&
         m_Plan.tunnel.node == TunnelNode() && m_Phase != Phase::kRouting;
}

/** See Network::SpiTunnel(). */
const SpiTunnelConfig* Network::Impl::SpiTunnel() const {
  return m_TunnelWanted ? &m_Tunnel : nullptr;
}

/** Where the tunnel's module is in the bus now, by its chip ID; kNoNode if it isn't. */
NodeAddr Network::Impl::TunnelNode() const {
  if (!m_TunnelWanted) {
    return kNoNode;
  }
  for (size_t i = 0; i < m_SubNodeCount; ++i) {
    if (std::memcmp(m_SubNodes[i].identity.chipId, m_TunnelChipId, sizeof(m_TunnelChipId)) == 0) {
      return static_cast<NodeAddr>(i);
    }
  }
  return kNoNode;
}

/**
 * The tunnel in a plan's input: its far end, if its module is in the bus, and its slots,
 * which must suit the main node's slot sizes (SLOTFMT): 2 to 12 each way, at least 6 bytes,
 * none of 8, 12 or 28 bits (AD2437 TRM, "SPI Data Tunnels", Tables 5-14 and 5-15).
 * @param input Its tunnel fields are set (none if no tunnel is wanted or its module is out).
 * @param node Set to the node at fault on failure.
 */
RoutingError Network::Impl::TunnelInput(RoutingPlanner::Input& input, NodeAddr& node) const {
  input.tunnelNode = 0;
  input.tunnelDn = 0;
  input.tunnelUp = 0;
  const NodeAddr at = TunnelNode();
  if (at == kNoNode) {
    return RoutingError::kNone;
  }
  const ChipCaps* caps = CapsOf(at);
  if (!MainHasSpi() || caps == nullptr || !caps->hasSpi) {
    node = MainHasSpi() ? at : kMainNode;
    return RoutingError::kTunnelNode;
  }
  uint8_t slotFormat = 0;
  m_Image.Get(reg::kSlotfmt, slotFormat);
  for (int up = 0; up < 2; ++up) {
    const unsigned code = (slotFormat >> (up ? reg::kSlotfmtUpsizePos : 0)) & reg::kSlotfmtDnsize;
    const unsigned bits = (code > 6) ? 32 : 8 + 4 * code;
    const unsigned bytes = m_Tunnel.slots * bits / 8;
    if (bits == 8 || bits == 12 || bits == 28 || bytes < 6 || bytes > 48) {
      node = kMainNode;
      return RoutingError::kTunnelSlots;
    }
  }
  input.tunnelNode = static_cast<uint8_t>(at + 1);
  input.tunnelDn = m_Tunnel.slots;
  input.tunnelUp = m_Tunnel.slots;
  return RoutingError::kNone;
}

/** A sub-node's register setting from its module info, or @p otherwise if it has none. */
uint8_t Network::Impl::ModuleReg(NodeAddr position, uint16_t reg, uint8_t otherwise) const {
  const ModuleInfo& info = m_SubNodes[position].moduleInfo;
  for (size_t i = 0; i < info.regCount; ++i) {
    if (info.reg[i] == reg) {
      otherwise = info.value[i];
    }
  }
  return otherwise;
}

/**
 * True if a sub-node's settings put CLKOUT2 on ADR2 (CLK2CFG enabled) with PINCFG.GPIOSEL 0:
 * with its SPI port on, CLKOUT2 would stop (anomaly 18000047), so the tunnel gives it
 * GPIOSEL 2 meanwhile.
 */
bool Network::Impl::TunnelGpioSel(NodeAddr position) const {
  return (ModuleReg(position, reg::kClk2cfg, 0) & reg::kClkcfgEn) != 0 &&
         (ModuleReg(position, reg::kPincfg, reg::kPincfgDrvstr) & reg::kPincfgGpiosel) == 0;
}

/**
 * Writes the tunnel's registers for the plan being written (m_Pending.tunnel), with its slot
 * registers, before NEWSTRCT: on the main node SPICFG (slave, TNLOWNER) and DTCFG to
 * DTUPOFFS; on the far end SPICFG (master, the mode), SPICKDIV, SPIPINCFG, PINCFG.GPIOSEL if
 * it needs it, and DTCFG to DTUPOFFS. A far end the tunnel left, still in the bus, gets its
 * tunnel off and its module info's SPI settings back; so does the main node, with its
 * image's SPICFG, when there is no tunnel.
 */
Result Network::Impl::WriteTunnel() {
  const TunnelPlan& tunnel = m_Pending.tunnel;
  Result result = Result::kOk;
  if (m_TunnelAt != kNoNode && m_TunnelAt != tunnel.node &&
      static_cast<size_t>(m_TunnelAt) < m_SubNodeCount &&
      std::memcmp(m_SubNodes[m_TunnelAt].identity.chipId, m_TunnelAtChipId,
                  sizeof(m_TunnelAtChipId)) == 0) {
    const NodeAddr old = m_TunnelAt;
    const uint8_t off[] = {0, 0, 0, 0};
    const uint8_t spi[] = {ModuleReg(old, reg::kSpicfg, 0), ModuleReg(old, reg::kSpickdiv, 0)};
    result = m_Transport.WriteRegs(old, reg::kDtcfg, off, sizeof(off));
    if (result == Result::kOk) {
      result = m_Transport.WriteReg(old, reg::kSpicfg, spi[0]);
    }
    if (result == Result::kOk) {
      result = m_Transport.WriteReg(old, reg::kSpickdiv, spi[1]);
    }
    if (result == Result::kOk) {
      result = m_Transport.WriteReg(old, reg::kSpipincfg, ModuleReg(old, reg::kSpipincfg, 0));
    }
    if (result == Result::kOk && TunnelGpioSel(old)) {
      result =
          m_Transport.WriteReg(old, reg::kPincfg, ModuleReg(old, reg::kPincfg, reg::kPincfgDrvstr));
    }
  }
  m_TunnelAt = kNoNode;
  if (result != Result::kOk) {
    return result;
  }
  uint8_t mainSpicfg = 0;
  m_Image.Get(reg::kSpicfg, mainSpicfg);
  if (tunnel.node == kNoNode) {
    if (m_TunnelOnMain) {
      const uint8_t off[] = {0, 0, 0, 0};
      result = m_Transport.WriteRegs(kMainNode, reg::kDtcfg, off, sizeof(off));
      if (result == Result::kOk) {
        result = m_Transport.WriteReg(kMainNode, reg::kSpicfg, mainSpicfg);
      }
      m_TunnelOnMain = (result != Result::kOk);
    }
    return result;
  }
  const uint8_t slots =
      static_cast<uint8_t>((tunnel.upSlots << reg::kDtslotsUpPos) | tunnel.dnSlots);
  const uint8_t mainDt[] = {static_cast<uint8_t>(reg::kDtcfgDten | reg::kDtcfgDtfrst), slots,
                            tunnel.mainDnOffset, tunnel.mainUpOffset};
  result = m_Transport.WriteReg(
      kMainNode, reg::kSpicfg,
      static_cast<uint8_t>((mainSpicfg & ~reg::kSpicfgSpimode) | reg::kSpicfgTnlowner));
  if (result == Result::kOk) {
    result = m_Transport.WriteRegs(kMainNode, reg::kDtcfg, mainDt, sizeof(mainDt));
  }
  m_TunnelOnMain = true;
  const NodeAddr far = tunnel.node;
  const uint8_t farSpicfg =
      static_cast<uint8_t>(reg::kSpimodeMaster | ((m_Tunnel.mode >> 1) & 1) << reg::kSpicfgCpolPos |
                           (m_Tunnel.mode & 1) << reg::kSpicfgCphaPos |
                           (m_Tunnel.fdClockStretch ? 1 : 0) << reg::kSpicfgEnfdcsPos);
  if (result == Result::kOk) {
    result = m_Transport.WriteReg(far, reg::kSpicfg, farSpicfg);
  }
  if (result == Result::kOk) {
    result = m_Transport.WriteReg(far, reg::kSpickdiv, m_Tunnel.clockDiv);
  }
  if (result == Result::kOk) {
    result = m_Transport.WriteReg(far, reg::kSpipincfg,
                                  static_cast<uint8_t>((m_Tunnel.selects & 0x07) << 4));
  }
  if (result == Result::kOk && TunnelGpioSel(far)) {
    const uint8_t pincfg = ModuleReg(far, reg::kPincfg, reg::kPincfgDrvstr);
    result = m_Transport.WriteReg(
        far, reg::kPincfg,
        static_cast<uint8_t>((pincfg & ~reg::kPincfgGpiosel) | reg::kPincfgGpiosel2));
    m_Platform.Log(LogLevel::kInfo,
                   "a2b: SPI tunnel: PINCFG.GPIOSEL 2 on its far end, to keep CLKOUT2 on ADR2");
  }
  const uint8_t farDt[] = {static_cast<uint8_t>(reg::kDtcfgDten | reg::kDtcfgDtlast), slots,
                           tunnel.nodeDnOffset, tunnel.nodeUpOffset};
  if (result == Result::kOk) {
    result = m_Transport.WriteRegs(far, reg::kDtcfg, farDt, sizeof(farDt));
  }
  if (result == Result::kOk) {
    m_TunnelAt = far;
    std::memcpy(m_TunnelAtChipId, m_SubNodes[far].identity.chipId, sizeof(m_TunnelAtChipId));
  }
  return result;
}

/**
 * Queues the tunnel's events for a plan written: EventType::kSpiTunnelClosed for the far
 * end it had, kSpiTunnelOpened for the one it has now (both when it moved).
 * @param before The far end of the plan written last (kNoNode: none).
 * @param after The far end of the plan written now.
 */
void Network::Impl::TunnelEvents(NodeAddr before, NodeAddr after) {
  if (before == after) {
    return;
  }
  Event event;
  if (before != kNoNode) {
    event.type = EventType::kSpiTunnelClosed;
    event.node = before;
    Queue(event);
    LogNumber(m_Platform, LogLevel::kInfo, "a2b: SPI tunnel to sub-node ",
              static_cast<unsigned>(before), " closed");
  }
  if (after != kNoNode) {
    event.type = EventType::kSpiTunnelOpened;
    event.node = after;
    Queue(event);
    LogNumber(m_Platform, LogLevel::kInfo, "a2b: SPI tunnel to sub-node ",
              static_cast<unsigned>(after), " open");
  }
}

/**
 * The node/slave-select byte for a transaction to slave select @p select of @p node,
 * if the tunnel is ready to that node and enables that slave select; a probe under way that
 * would stop the tunnel is ended first (ClearTunnelPath()).
 * @return Result::kOk, Result::kNotSupported (no SPI on the main node), Result::kBusy (the
 *         tunnel's plan is due or being written, or a node is joining), or
 *         Result::kInvalidArgument.
 */
Result Network::Impl::TunnelSelect(NodeAddr node, uint8_t select, uint8_t& nodeSelect) {
  if (!MainHasSpi()) {
    return Result::kNotSupported;
  }
  if (TunnelPending()) {
    return Result::kBusy;  // Ready once the plan is written (SpiTunnelReady()).
  }
  if (!SpiTunnelReady() || node != m_Plan.tunnel.node || select > 2 ||
      ((m_Tunnel.selects >> select) & 1) == 0) {
    return Result::kInvalidArgument;
  }
  const Result clear = ClearTunnelPath();
  if (clear != Result::kOk) {
    return clear;
  }
  nodeSelect = spi::NodeSelect(select, static_cast<uint8_t>(node));
  return Result::kOk;
}

/**
 * True while the tunnel's module is in the bus and its plan is due, being written, or in
 * its NEWSTRCT settle: the tunnel is ready after that.
 */
bool Network::Impl::TunnelPending() const {
  const bool planPending = m_RoutingDue || m_Phase == Phase::kRouting ||
                           (m_Settling && !TimeReached(m_Platform.NowMs(), m_SettleUntil));
  return m_TunnelWanted && planPending && TunnelNode() != kNoNode;
}

/**
 * True while the SPI tunnel written ends at the chain's last node and that node plays or
 * sends audio: then no probe is made behind it, as if discovery were paused. The TRM's
 * last-node tunnel configuration (LDNSLOTS.DNMASKEN = 0) is valid only while the node is
 * last, and a probe clears NODE.LAST for its length: on the bench the far end's audio
 * glitched at every probe (docs/SPI.md). Closing the tunnel, moving it, or routing no audio
 * to and from that node lets the probes start again. Logs the change each way.
 */
bool Network::Impl::TunnelHoldsProbes() {
  bool hold = false;
  const NodeAddr far = m_TunnelAt;
  if (far != kNoNode && m_Plan.valid && static_cast<size_t>(far) + 1 == m_SubNodeCount) {
    const NodeRouting& regs = m_Plan.sub[far];
    hold = (regs.ldnSlots & ~reg::kLdnslotsDnmasken) != 0 || regs.lupSlots != 0;
  }
  if (hold != m_ProbesHeld) {
    m_ProbesHeld = hold;
    if (hold) {
      LogNumber(m_Platform, LogLevel::kInfo, "a2b: SPI tunnel at last sub-node ",
                static_cast<unsigned>(far), " with audio: no probes");
    } else {
      m_Platform.Log(LogLevel::kInfo, "a2b: probes again");
    }
  }
  return hold;
}

/**
 * A probe for a new node stops the tunnel while it runs: the bench showed every tunnel
 * transaction ending DTABORT once DISCVRY went out behind the far end (docs/SPI.md). A probe
 * that nothing has answered yet is ended here, as one that found nothing (the next comes
 * after BoardTimings::spiProbeHoldMs of no SPI); a node found and joining isn't, nor a probe
 * whose DSCDONE has come but hasn't been read yet.
 * @return Result::kOk (no probe now), or Result::kBusy (a node is joining).
 */
Result Network::Impl::ClearTunnelPath() {
  if (m_Phase == Phase::kIdle) {
    return Result::kOk;
  }
  if (m_Phase != Phase::kProbe || !m_Discovery.Searching()) {
    return Result::kBusy;
  }
  if (m_Discovery.AwaitingDscdone()) {
    // DSCDONE may have come and not been read yet (interrupts are polled): DISCSTAT.DSCACT
    // clear says a node answered, and it is let join.
    uint8_t discstat = 0;
    if (m_Transport.ReadRegs(kMainNode, reg::kDiscstat, &discstat, 1) != Result::kOk ||
        (discstat & reg::kDiscstatDscact) == 0) {
      return Result::kBusy;
    }
  }
  LogNumber(m_Platform, LogLevel::kInfo, "a2b: probe for sub-node ",
            static_cast<unsigned>(m_SubNodeCount), " ended for an SPI transaction");
  AbandonProbe();
  EndProbe(Discovery::Outcome::kNothing, m_Platform.NowMs());
  return Result::kOk;
}

/**
 * An SPI peripheral's access through the tunnel (Network::PeripheralWrite() and friends): an
 * atomic write (@p readData null), or an atomic read (up to 255 bytes written first) and
 * then the tunnel FIFO read for its data.
 */
Result Network::Impl::SpiAtomic(NodeAddr node, uint8_t select, const uint8_t* writeData,
                                size_t writeLen, uint8_t* readData, size_t readLen) {
  uint8_t nodeSelect = 0;
  const Result ready = TunnelSelect(node, select, nodeSelect);
  if (ready != Result::kOk) {
    return ready;
  }
  uint8_t buf[spi::kHeaderLen + kSpiMaxData];
  if (readData == nullptr) {
    if (writeData == nullptr || writeLen == 0 || writeLen > kSpiMaxData) {
      return Result::kInvalidArgument;
    }
    buf[0] = spi::kAtomicWrite;
    buf[1] = nodeSelect;
    std::memcpy(buf + 2, writeData, writeLen);
    return SpiTransaction(buf, buf, 2 + writeLen);
  }
  if (readLen == 0 || readLen > kSpiMaxData || writeLen > kSpiMaxData - 1 ||
      (writeLen > 0 && writeData == nullptr)) {
    return Result::kInvalidArgument;
  }
  buf[0] = spi::kAtomicRead;
  buf[1] = nodeSelect;
  buf[2] = static_cast<uint8_t>(readLen - 1);
  if (writeLen > 0) {
    std::memcpy(buf + spi::kHeaderLen, writeData, writeLen);
  }
  const Result result = SpiTransaction(buf, buf, spi::kHeaderLen + writeLen);
  if (result != Result::kOk) {
    return result;
  }
  return SpiReadTunnelFifo(readData, readLen);
}

/** See Network::SpiFullDuplex(). */
Result Network::Impl::SpiFullDuplex(uint8_t select, const uint8_t* tx, uint8_t* rxPrevious,
                                    size_t len, SpiChunk chunk) {
  uint8_t nodeSelect = 0;
  const Result ready = TunnelSelect(m_Plan.tunnel.node, select, nodeSelect);
  if (ready != Result::kOk) {
    return ready;
  }
  if (tx == nullptr || len == 0 || len > kSpiMaxData) {
    return Result::kInvalidArgument;
  }
  uint8_t buf[spi::kHeaderLen + kSpiMaxData];
  buf[0] = (chunk == SpiChunk::kMore) ? spi::kExtFullDuplex : spi::kFullDuplex;
  buf[1] = nodeSelect;
  buf[2] = static_cast<uint8_t>(len - 1);
  std::memcpy(buf + spi::kHeaderLen, tx, len);
  const Result result = SpiTransaction(buf, buf, spi::kHeaderLen + len);
  if (result == Result::kOk && rxPrevious != nullptr) {
    std::memcpy(rxPrevious, buf + spi::kHeaderLen, len);
  }
  return result;
}

/** See Network::SpiReadTunnelFifo(). */
Result Network::Impl::SpiReadTunnelFifo(uint8_t* data, size_t len) {
  if (!MainHasSpi()) {
    return Result::kNotSupported;
  }
  if (data == nullptr || len == 0 || len > kSpiMaxData) {
    return Result::kInvalidArgument;
  }
  uint8_t buf[1 + kSpiMaxData] = {spi::kTunnelFifoRead};
  const Result result = SpiTransaction(buf, buf, 1 + len);
  if (result == Result::kOk || result == Result::kSpiError) {
    std::memcpy(data, buf + 1, len);
  }
  return result;
}

/** See Network::SpiBulkWrite(). */
Result Network::Impl::SpiBulkWrite(uint8_t select, const uint8_t* data, size_t len,
                                   SpiChunk chunk) {
  uint8_t nodeSelect = 0;
  const Result ready = TunnelSelect(m_Plan.tunnel.node, select, nodeSelect);
  if (ready != Result::kOk) {
    return ready;
  }
  if (data == nullptr || len == 0 || len > kSpiMaxData) {
    return Result::kInvalidArgument;
  }
  uint8_t buf[spi::kHeaderLen + kSpiMaxData];
  buf[0] = (chunk == SpiChunk::kMore) ? spi::kExtBulk : spi::kBulk;
  buf[1] = nodeSelect;
  buf[2] = static_cast<uint8_t>(len - 1);
  std::memcpy(buf + spi::kHeaderLen, data, len);
  return SpiTransaction(buf, buf, spi::kHeaderLen + len);
}

/** See Network::SpiAbort(). */
Result Network::Impl::SpiAbort() {
  if (!MainHasSpi()) {
    return Result::kNotSupported;
  }
  m_SpiStatus = SpiStatus();
  m_LastSpiMs = m_Platform.NowMs();
  m_SpiUsed = true;
  uint8_t buf[1] = {spi::kAbort};
  const Result result = SpiExchange(buf, buf, sizeof(buf));
  if (result != Result::kOk) {
    return result;
  }
  uint8_t spiInt = 0;  // What the aborted transaction left: cleared.
  if (m_Transport.ReadRegs(kMainNode, reg::kSpiint, &spiInt, 1) == Result::kOk && spiInt != 0) {
    (void)m_Transport.WriteReg(kMainNode, reg::kSpiint, spiInt);
  }
  uint8_t spiStat = 0;
  if (m_Transport.ReadRegs(kMainNode, reg::kSpistat, &spiStat, 1) == Result::kOk &&
      (spiStat & reg::kSpistatErrors) != 0) {
    (void)m_Transport.WriteReg(kMainNode, reg::kSpistat,
                               static_cast<uint8_t>(spiStat & reg::kSpistatErrors));
  }
  return Result::kOk;
}

}  // namespace a2b
