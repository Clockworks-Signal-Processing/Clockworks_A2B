/**
 * @file simulator.cpp
 * @brief A simulated A2B system for host tests: a Platform whose host I2C bus reaches
 *        simulated main nodes, sub-nodes and their peripherals.
 *
 * What it models, and where the model comes from:
 * - Register access as Transport uses it: the main address, the bus address steered by
 *   NODEADR (a sub-node, broadcast, or a peripheral at a sub-node's CHIP address), and
 *   MMR page 1 per node on AD243x.
 * - Main-node start-up (the "Soft Reset" and "Main Node Bring-Up"
 *   sections of the AD242x, AD2437 and AD2431/32/33/35 TRMs):
 *   - a soft reset with CONTROL.MSTR = 0 of a running main node unlocks the PLL. An
 *     AD243x then spends SUSTAIN (1024 SYNC periods); an AD242x main node never enters
 *     SUSTAIN. CONTROL.MSTR or I2SGCFG.INV written during SUSTAIN leaves an AD243x main
 *     node unusable until it is power-cycled;
 *   - a soft reset with MSTR = 1 leaves the PLL locked;
 *   - either way the registers return to their power-on values except CONTROL and
 *     BMMCFG, and PLLCTL on the AD2433; a soft reset in POWER-UP has no effect;
 *   - MSTR starts the PLL, and MSTR_RUNNING follows. Once the PLL is locked, writing MSTR
 *     has no effect.
 * - The reset line (Bus::SetHasResetLine()): asserting it power-cycles the bus, and after
 *   it is released nothing answers for tACCESS (data sheets: at least 1.5 ms).
 * - Power-on register values from the TRM register maps (the ones that aren't zero).
 * - Power configuration on AD243x nodes: SWSTAT2 shows the configuration the chip detected
 *   (ChipSpec::powerConfig) and LVI_MODE; SWCTL2.HPSW_CFG with SWCTL.CFG_DET_OV overrides
 *   it (Node::PowerConfig()). Nothing else depends on it yet.
 * - Discovery: a DISCVRY write discovers the next plugged sub-node. DSCDONE comes only
 *   when the link is powered (ENSW on the upstream node, or the sub-node's own supply), the
 *   upstream node's B side passes frames (the TRMs, SWSTAT.FIN and CONTROL.SWBYP: once its
 *   switch has closed, or with SWBYP, which keeps the switch open; a node on its own
 *   supply whose A side is isolated, LinkSpec::isolated, draws no bias current, so the
 *   switch never finishes closing and only SWBYP will do, as on the bench), the
 *   upstream node's XCVRBINV matches the link's polarity, the DISCVRY value is in the
 *   sub-node's response-cycle window, and, for sub-node n > 0, NODEADR pointed at sub-node
 *   n - 1 when DISCVRY was written (the TRMs' DISCVRY register: "the NODEADR.NODE bit field
 *   must be set to value of the previous node"; what the hardware does otherwise is not
 *   documented, so the simulator finds nothing). Otherwise discovery stays active and
 *   nothing comes; the stack's timeout handles it.
 * - Interrupts: MSTR_RUNNING, and DSCDONE when INTMSK2 enables it. INTSRC shows the oldest,
 *   and reading INTTYPE clears it. A sub-node's interrupt (Bus::RaiseInterrupt(), for any
 *   type a test needs) shows INTSRC.SLVINT with its position.
 * - Power and drops (TRMs, "Subordinate Node Interrupt Handling", "SRFMISSERR", "Bus Drop
 *   Detection"): a sub-node has power while the upstream node's SWCTL.ENSW is set without
 *   CONTROL.SWBYP (or its own supply is on, for LinkPower::kSeparateWires). A discovered sub-node
 * that loses power, is unplugged or resets (Bus::ResetSub(), a PLL unlock) drops out of the bus
 *   with every node after it: they are no longer discovered, their registers reset, and
 *   accesses to them NAK. The node before them, now the last, sets NODE.LAST and latches
 *   INTPND0.SRFERR (after the TRMs' 32 missed SRFs, which are well under a millisecond),
 *   and raises the SRF-missed interrupt (INTTYPE 5) if its INTMSK0 enables it; for the
 *   main node, its own INTMSK0. A node whose downstream supply is removed
 *   (Bus::SetDownstreamSupply(): its VBUS input, e.g. 24 V, while it keeps running from
 *   the A2B pair) also reports a power fault first: INTPND0.PWRERR and, if INTMSK0 enables
 *   it (the stack's default masks do), INTTYPE 15, "undetermined cable fault". That is what
 *   the Pi bench showed when the 24 V was removed at sub-node 2 to switch off
 *   the last of four AD2437 nodes: INTSRC 0x42, INTTYPE 0x0F from sub-node 2, then NAKs from
 *   sub-node 3. A pulled cable (Bus::Unplug()) raises no interrupt, as on the bench (six
 *   locally powered nodes, RJ45 and UTP links pulled): the next chain check
 *   or probe finds the drop. A power fault seen there on a re-plug (the connector making
 *   contact while the switch was closed) is not modelled. An interrupt
 *   of a dropped node still waiting in INTTYPE becomes 0xFD (sub-node INTTYPE read error).
 *   A discovery under way when its chain drops finds nothing.
 * - Power-good: the main node's input (Platform::PowerGood()) and each node's GPIO
 *   inputs (GPIOIN, gated by GPIOIEN; GPIO7 carries PG by default). VMTR readings on
 *   AD243x nodes (VLTGn, gated by VMTR_VEN).
 * - EEPROMs and other I2C devices behind sub-nodes and on the host bus.
 * - Faults: failed transfers, a sub-node unplugged or reset, a node's downstream supply
 *   removed, PG and VMTR values, the
 *   SUSTAIN trap, no SYNC (no PLL lock) and a SYNC break (the main node's PLL unlocks and
 *   relocks).
 * - Audio routing (AD2437 TRM, "I2S/TDM Flexible Mapping" and "Slot Register
 *   Configuration", checked with pdftotext), apart from the stack's planner: the slot
 *   registers (BCDNSLOTS to DATCTL, UPMASK0 to DNOFFSET) and RESPCYCS take effect at
 *   CONTROL.NEWSTRCT, on the main node and every discovered sub-node at once; RXMASK and
 *   TXXBAR at once. Bus::TraceAudio() follows each RX channel through the RX frame buffers,
 *   the slots each node passes on, adds and plays (DNSLOTS, LDNSLOTS with DNMASKEN,
 *   DNOFFSET, DNMASK; UPSLOTS, LUPSLOTS, UPOFFSET, UPMASK; the main node's DNSLOTS and
 *   UPSLOTS) and the TX crossbars, to what each node plays on each TX channel. Transfers to
 *   a bus in the millisecond after a NEWSTRCT are counted (the TRMs apply a new structure
 *   within 5 superframes, and hold off the main node's INTTYPE reads meanwhile).
 * - AD242x nodes (the AD242x TRM, "Managing A2B System Data Flow" and "I2S/TDM
 *   Port Programming Concepts"): the same slot registers, no crossbar. The RX frame buffer
 *   holds the port's RX channels in order (TDMMODE's channels on each pin I2SCFG turns on,
 *   RX0EN, RX1EN or I2SGCFG.RXONDTX1, times the I2SRATE multiple; each PDM microphone
 *   instead of a PDM pin's), and TX channel n plays frame buffer location n. A crossbar
 *   register written on one is an AudioTrace error (the AD242x map ends at 0x9B). On every
 *   node, a TX channel past those of the pins I2SCFG turns on plays nothing (AD243x:
 *   TXPINS); the AD243x RX side follows RXMASK alone. An AD2427 has no I2S/TDM port (its
 *   data sheet's Table 1): its RX channels are its PDM microphones, and it plays nothing.
 * - Auto-broadcast (the AD242x, AD2433 and AD2437 TRMs, "Main Node Only, Auto-Broadcast"):
 *   a write of SLOTFMT, DATCTL or I2SRRATE on the main node is copied to every sub-node
 *   discovered by then, and only those (the bench found no DATCTL.DNS/UPS in a sub-node
 *   discovered after the start-up writes); a sub-node discovered later has
 *   the power-on value until the main node's register is written again. SLOTFMT and
 *   DATCTL take effect at NEWSTRCT, like the slot registers; I2SRRATE, which the TRMs
 *   don't tie to NEWSTRCT, at once. Bus::TraceAudio() follows DATCTL and SLOTFMT: a node
 *   plays and adds nothing in a direction its DATCTL.DNS/UPS leaves disabled, or whose
 *   slot size and format (DNSIZE/DNFMT, UPSIZE/UPFMT) differ from the main node's, though
 *   the slots still pass through it. What a real node does there isn't documented (the
 *   TRMs have every node alike); the model makes the bench's symptom, silence. Not
 *   followed: STANDBY, and the TRMs' "local host writes to a sub-node have no effect"
 *   (the simulator has no local port: a write to the register through the bus address is
 *   taken). Bus monitoring (ENDSNIFF) and the reduced data rate (I2SRRATE's RBUS and
 *   RRDIV, I2SRATE.REDUCE) are out of scope for the stack.
 * - Increased I2S rate (the TRMs, "I2S Increased Data Rate"): a sub-node's
 *   I2SRATE 5 or 6 runs its TDM port at 2x or 4x the superframe rate, and AudioTrace
 *   reports it (AudioTrace::rate). The TDM channels are one list through the superframe at
 *   any rate ("the mapping of frame buffer entries to TDM slots happens sequentially"),
 *   so the slot arithmetic is the same; how many channels a TDM mode gives a node is the channel
 * map's (routing.cpp). A reduced rate (SHARE, REDUCE, I2SRATE 1 to 4) is an AudioTrace error.
 * - A trace of every transfer in the I2CT format, for trace_decode.py/trace_diff.py.
 *
 * Time moves only when the test calls Simulator::Advance(); nothing happens inside a
 * transfer. Bus index n in the Platform calls is the n-th bus added.
 *
 * Not modelled yet: two-step discovery's switch bypass and 24 V check; power faults other
 * than a removed downstream supply; SPI. The drop model is from the TRMs, checked against
 * the bench's cable pulls (goldens G4 and G5 were not captured).
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "sim/simulator.h"

#include <cstdio>
#include <cstring>

namespace a2b {
namespace sim {
namespace {

/** The MMR page of a register address (its high byte). */
constexpr uint8_t PageOf(uint16_t reg) { return static_cast<uint8_t>(reg >> 8); }
/** The register's offset within its page (its low byte). */
constexpr uint8_t LowByte(uint16_t reg) { return static_cast<uint8_t>(reg & 0xFF); }

/** VMTR input measured by the page-1 register @p low, or -1 if it is not a VLTGn register. */
int VmtrInputOf(uint8_t low) {
  constexpr uint8_t kFirst = LowByte(reg::kVmtrVltg0);
  if (low < kFirst || low >= kFirst + 3 * reg::kVmtrInputs || (low - kFirst) % 3 != 0) {
    return -1;
  }
  return (low - kFirst) / 3;
}

/** Data pins an AD243x I2SCFG.TXPINS or RXPINS value turns on: 0 to 4, or 2 for 7
    (interleaved); none for a reserved value. */
size_t PinsOfCount(unsigned field) {
  if (field <= 4) {
    return field;
  }
  return (field == 7) ? 2 : 0;
}

/** @brief The channels a node's I2S/TDM and PDM port gives per superframe. */
struct PortChannels {
  size_t rx = 0;  ///< RX channels.
  size_t tx = 0;  ///< TX channels.
};

/**
 * The channels a node's port gives (the file's comment), from its registers as a host read
 * sees them: the TDMMODE's channels on each data pin on, times @p rate (a sub-node's I2SRATE
 * multiple), and each PDM microphone in place of its pin's TDM channels; 32 at most.
 */
PortChannels PortChannelsOf(const Node& node, unsigned rate) {
  static constexpr size_t kPerPin[8] = {2, 4, 8, 12, 16, 20, 24, 32};
  const uint8_t gcfg = node.Reg(reg::kI2sgcfg);
  const uint8_t cfg = node.Reg(reg::kI2scfg);
  const uint8_t pdm = node.Reg(reg::kPdmctl);
  size_t txPins = 0;
  size_t rxPins = 0;
  if (!node.Caps().hasI2s) {
    // No I2S/TDM port (the AD2427, its data sheet's Table 1): PDM microphones only.
  } else if (node.Caps().family == ChipFamily::kAd242x) {
    const bool rxOnDtx1 = (gcfg & reg::kI2sgcfgRxondtx1) != 0;  // DTX1 receives, not DRX1.
    txPins = ((cfg & reg::kI2scfgTx0en) != 0 ? 1 : 0) +
             (!rxOnDtx1 && (cfg & reg::kI2scfgTx1en) != 0 ? 1 : 0);
    rxPins = ((cfg & reg::kI2scfgRx0en) != 0 ? 1 : 0) +
             (rxOnDtx1 || (cfg & reg::kI2scfgRx1en) != 0 ? 1 : 0);
  } else {
    txPins = PinsOfCount(cfg & reg::kI2scfgTxpins);
    rxPins = PinsOfCount((cfg & reg::kI2scfgRxpins) >> reg::kI2scfgRxpinsPos);
  }
  size_t pdmPins = 0;
  size_t microphones = 0;
  if ((pdm & reg::kPdmctlPdm0en) != 0) {
    ++pdmPins;
    microphones += ((pdm & reg::kPdmctlPdm0slots) != 0) ? 2 : 1;
  }
  if ((pdm & reg::kPdmctlPdm1en) != 0) {
    ++pdmPins;
    microphones += ((pdm & reg::kPdmctlPdm1slots) != 0) ? 2 : 1;
  }
  rxPins = (rxPins > pdmPins) ? rxPins - pdmPins : 0;
  const size_t perPin = kPerPin[gcfg & reg::kI2sgcfgTdmmode] * rate;
  PortChannels port;
  port.tx = (txPins * perPin > 32) ? 32 : txPins * perPin;
  port.rx = (rxPins * perPin + microphones > 32) ? 32 : rxPins * perPin + microphones;
  return port;
}

}  // namespace

// Node

/**
 * Makes a powered-on node: registers at reset values, GPIO7 (power-good) high, VMTR input 1
 * reading 24 V.
 * @param chip The identity it reports; an unsupported chip gets a ChipCaps row with no
 *        features.
 * @param link The link from its upstream node (ignored for a main node).
 */
Node::Node(const ChipSpec& chip, const LinkSpec& link) : m_Chip(chip), m_Link(link) {
  const ChipCaps* caps = LookUpChipCaps(chip.vendorId, chip.productId);
  if (caps != nullptr) {
    m_Caps = *caps;
  } else {  // An unsupported chip gets a row with no features.
    m_Caps = ChipCaps{};
    m_Caps.productId = chip.productId;
    m_Caps.name = "unknown";
    m_Caps.family = ChipFamily::kAd242x;
  }
  m_Vmtr[1] = kDefaultSupplyReading;
  ResetRegisters();
}

/**
 * A register's value as a host read would see it: GPIOIN gated by GPIOIEN, VLTGn gated by
 * VMTR_VEN, MMRPAGE the selected page. The main node's INTSTAT, INTSRC, INTTYPE and
 * DISCSTAT are kept by its Bus: check them with Bus::PendingInterrupts() and
 * Bus::DiscoveryActive().
 * @param reg Register; above 0xFF is page 1.
 */
uint8_t Node::Reg(uint16_t reg) const {
  const uint8_t page = PageOf(reg);
  const uint8_t low = LowByte(reg);
  if (page > 1) {
    return 0;
  }
  if (low == reg::kMmrpage) {
    return m_Page;
  }
  if (page == 0 && low == reg::kGpioin) {
    return static_cast<uint8_t>(m_GpioInputs & m_Regs[0][reg::kGpioien]);
  }
  if (page == 0 && low == reg::kSwstat2 && m_Caps.hasSwitchConfig) {
    return static_cast<uint8_t>(
        (m_Chip.lowVoltageInput ? reg::kSwstat2LviMode : 0) |
        ((m_Chip.powerConfig << reg::kSwstat2HpswCfgDetPos) & reg::kSwstat2HpswCfgDet));
  }
  if (page == 1 && m_Caps.hasVoltageMonitor) {
    const int input = VmtrInputOf(low);
    if (input >= 0) {
      const bool enabled = ((m_Regs[1][LowByte(reg::kVmtrVen)] >> input) & 1) != 0;
      return enabled ? m_Vmtr[input] : 0;
    }
  }
  return m_Regs[page][low];
}

/**
 * A register as the node uses it: for a structure register (IsStructureReg()), the value
 * written before the last CONTROL.NEWSTRCT; otherwise as Reg() reads it.
 * @param reg Register; above 0xFF is page 1.
 */
uint8_t Node::AppliedReg(uint16_t reg) const {
  if (PageOf(reg) == 0 && IsStructureReg(LowByte(reg))) {
    return m_Applied[LowByte(reg)];
  }
  return Reg(reg);
}

/**
 * True for the page-0 registers that take effect at CONTROL.NEWSTRCT (AD2437 TRM, register
 * descriptions): the slot registers BCDNSLOTS to UPSLOTS, RESPCYCS, SLOTFMT, DATCTL, and
 * UPMASK0 to DNOFFSET.
 */
bool Node::IsStructureReg(uint8_t low) {
  return (low >= reg::kBcdnslots && low <= reg::kDatctl) ||
         (low >= reg::kUpmask0 && low <= reg::kDnoffset);
}

/**
 * True for the main-node registers whose writes are broadcast to the discovered sub-nodes
 * (AD242x, AD2433 and AD2437 TRMs, "Main Node Only, Auto-Broadcast"): SLOTFMT, DATCTL and
 * I2SRRATE.
 */
bool Node::IsAutoBroadcastReg(uint8_t low) {
  return low == reg::kSlotfmt || low == reg::kDatctl || low == reg::kI2srrate;
}

/** CONTROL.NEWSTRCT: the structure registers written take effect. */
void Node::ApplyStructure() {
  for (unsigned low = 0; low < 256; ++low) {
    if (IsStructureReg(static_cast<uint8_t>(low))) {
      m_Applied[low] = m_Regs[0][low];
    }
  }
}

/**
 * The power configuration the node uses: SWCTL2.HPSW_CFG while SWCTL.CFG_DET_OV is set,
 * else the one it detected (ChipSpec::powerConfig). Chips without SWSTAT2 report 0.
 */
uint8_t Node::PowerConfig() const {
  if (!m_Caps.hasSwitchConfig) {
    return 0;
  }
  if ((m_Regs[0][reg::kSwctl] & reg::kSwctlCfgDetOv) != 0) {
    return static_cast<uint8_t>(m_Regs[0][reg::kSwctl2] & reg::kSwctl2HpswCfg);
  }
  return m_Chip.powerConfig;
}

/** Sets a GPIO input level; GPIOIN shows it when GPIOIEN enables the pin. */
void Node::SetGpioInput(int pin, bool level) {
  const uint8_t bit = static_cast<uint8_t>(1u << pin);
  m_GpioInputs =
      level ? static_cast<uint8_t>(m_GpioInputs | bit) : static_cast<uint8_t>(m_GpioInputs & ~bit);
}

/** Sets what VMTR input @p input measures; VLTGn shows it when VMTR_VEN enables it. */
void Node::SetVmtrInput(int input, uint8_t reading) {
  if (input >= 0 && input < reg::kVmtrInputs) {
    m_Vmtr[input] = reading;
  }
}

/**
 * Adds an erased EEPROM at @p i2cAddr behind this node. The node owns it.
 * @return The EEPROM, e.g. to Load() an image into.
 */
Eeprom& Node::AddEeprom(uint8_t i2cAddr, size_t size) {
  m_Eeproms.push_back(std::make_unique<Eeprom>(size));
  AttachDevice(i2cAddr, *m_Eeproms.back());
  return *m_Eeproms.back();
}

/** Attaches a device the test owns at @p i2cAddr, replacing any device already there. */
void Node::AttachDevice(uint8_t i2cAddr, I2cDevice& device) {
  for (auto& entry : m_Devices) {
    if (entry.first == i2cAddr) {
      entry.second = &device;
      return;
    }
  }
  m_Devices.emplace_back(i2cAddr, &device);
}

/** The device at @p i2cAddr behind this node, or nullptr. */
I2cDevice* Node::FindDevice(uint8_t i2cAddr) const {
  for (const auto& entry : m_Devices) {
    if (entry.first == i2cAddr) {
      return entry.second;
    }
  }
  return nullptr;
}

/**
 * Sets every register to its power-on value, with page 0 selected. The values that aren't
 * zero come from the register maps of the TRMs: CHIP 0x50, RESPCYCS 0x40, NODE.LAST,
 * PINCFG.DRVSTR, and on AD243x I2SRRATE 0x01, RXMASKn 0xFF, TXXBARn = n and VMTR_VMAXn 0xFF.
 */
void Node::ResetRegisters() {
  std::memset(m_Regs, 0, sizeof(m_Regs));
  m_Regs[0][reg::kChip] = kResetChip;
  m_Regs[0][reg::kVendor] = m_Chip.vendorId;
  m_Regs[0][reg::kProduct] = m_Chip.productId;
  m_Regs[0][reg::kVersion] = m_Chip.version;
  m_Regs[0][reg::kCapability] = m_Chip.capability;
  m_Regs[0][reg::kRespcycs] = kResetRespCycles;
  m_Regs[0][reg::kNode] = reg::kNodeLast;
  m_Regs[0][reg::kPincfg] = reg::kPincfgDrvstr;
  for (int i = 0; i < reg::kCountChipid; ++i) {
    m_Regs[0][reg::kChipid0 + i] = m_Chip.chipId[i];
  }
  m_Regs[0][reg::kI2srrate] = 0x01;  // RRDIV 1 on both families (TRM register maps).
  if (m_Caps.family == ChipFamily::kAd243x) {
    for (int i = 0; i < 8; ++i) {
      m_Regs[0][reg::kRxmask0 + i] = 0xFF;
    }
    for (int i = 0; i < 32; ++i) {
      m_Regs[0][reg::kTxxbar0 + i] = static_cast<uint8_t>(i);
    }
    for (int i = 0; i < reg::kVmtrInputs - 1; ++i) {
      m_Regs[1][LowByte(reg::kVmtrVmax0) + 3 * i] = 0xFF;
    }
  }
  m_Page = 0;
  m_Pointer = 0;
  ApplyStructure();  // The power-on values are in use.
}

/**
 * A soft reset's effect on the registers: all return to their power-on values except
 * CONTROL and BMMCFG (AD2437 and AD243x TRMs, "Soft Reset"), and PLLCTL on the AD2431/32/33/35
 * (their TRM lists it with the other two).
 */
void Node::SoftResetRegisters() {
  const uint8_t control = m_Regs[0][reg::kControl];
  const uint8_t bmmcfg = m_Regs[0][reg::kBmmcfg];
  const uint8_t pllctl = m_Regs[0][reg::kPllctl];
  ResetRegisters();
  m_Regs[0][reg::kControl] = control;
  m_Regs[0][reg::kBmmcfg] = bmmcfg;
  if (m_Chip.productId == 0x33) {
    m_Regs[0][reg::kPllctl] = pllctl;
  }
}

/**
 * A host write of one register: read-only registers ignore it, INTPNDn clear the bits
 * written as 1, and MMRPAGE selects the page (AD243x only).
 */
void Node::Write(uint16_t reg, uint8_t value) {
  const uint8_t page = PageOf(reg);
  const uint8_t low = LowByte(reg);
  if (page > 1) {
    return;
  }
  if (low == reg::kMmrpage) {
    if (m_Caps.family == ChipFamily::kAd243x) {
      m_Page = value & 0x01;
    }
    return;
  }
  if (page == 0) {
    switch (low) {
      case reg::kVendor:
      case reg::kProduct:
      case reg::kVersion:
      case reg::kCapability:
      case reg::kGpioin:
      case reg::kSwstat2:
        return;  // Read-only.
      case reg::kIntpnd0:
      case reg::kIntpnd1:
      case reg::kIntpnd2:
        m_Regs[0][low] = static_cast<uint8_t>(m_Regs[0][low] & ~value);  // Write 1 to clear.
        return;
      default:
        if (low >= reg::kChipid0 && low < reg::kChipid0 + reg::kCountChipid) {
          return;  // Read-only.
        }
        break;
    }
  }
  m_Regs[page][low] = value;
}

/** A write transfer to the node: a register address, then values for consecutive registers. */
void Node::WriteBurst(const uint8_t* data, size_t len) {
  m_Pointer = data[0];
  for (size_t i = 1; i < len; ++i) {
    Write(Full(m_Pointer), data[i]);
    ++m_Pointer;
  }
}

/** A read transfer from the node: consecutive registers from the register pointer. */
void Node::ReadBurst(uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    data[i] = Read(Full(m_Pointer));
    ++m_Pointer;
  }
}

// Bus

/**
 * Makes a powered-on bus with no sub-nodes; the main node is not running.
 * @param sim The simulator that owns it (for the clock and timings).
 * @param mainAddr 7-bit I2C address of the main node.
 * @param busAddr 7-bit I2C bus address.
 * @param mainChip The main node's identity.
 */
Bus::Bus(Simulator& sim, uint8_t mainAddr, uint8_t busAddr, const ChipSpec& mainChip)
    : m_Sim(sim),
      m_MainAddr(mainAddr),
      m_BusAddr(busAddr),
      m_Main(std::make_unique<Node>(mainChip, LinkSpec())) {}

/** Plugs a sub-node in at the end of the chain; it is found by the next discovery. */
Node& Bus::Plug(const SubNodeSpec& spec) {
  m_Subs.push_back(std::make_unique<Node>(spec.chip, spec.link));
  return *m_Subs.back();
}

/** Unplugs the sub-node at @p position and everything beyond it; they drop out of the bus. */
void Bus::Unplug(size_t position) {
  if (position >= m_Subs.size()) {
    return;
  }
  DropFrom(position, false);
  m_Subs.erase(m_Subs.begin() + static_cast<std::ptrdiff_t>(position), m_Subs.end());
}

/**
 * Removes (@p on false) or restores the supply sub-node @p position passes downstream
 * through its switch (its VBUS input, e.g. the 24 V of a CFG-4 node), as on the Pi bench
 * (as the Pi bench showed): the node itself keeps running, powered over the A2B pair. Removed, the
 * next node, if it is powered over the pair through a closed switch, loses power and drops
 * out with the nodes after it, and the node reports a power fault: INTPND0.PWRERR and, if
 * its INTMSK0 enables it (the stack's default masks do), INTTYPE 15, "undetermined cable
 * fault", once. Until the supply is restored, nothing downstream is powered or discovered.
 */
void Bus::SetDownstreamSupply(size_t position, bool on) {
  if (position >= m_Subs.size()) {
    return;
  }
  const size_t next = position + 1;
  const bool cut = !on && m_Subs[position]->m_DownstreamSupply && next < m_Discovered &&
                   m_Subs[next]->m_Link.power == LinkPower::kOnPair && Powered(next);
  m_Subs[position]->m_DownstreamSupply = on;
  if (cut) {
    DropFrom(next, true);
  }
}

/**
 * The sub-node at @p position resets, as when its PLL unlocks on a noisy link: it and the
 * nodes after it drop out of the bus, but stay plugged in and powered, so a discovery can
 * find them again.
 */
void Bus::ResetSub(size_t position) {
  if (position < m_Discovered) {
    DropFrom(position, false);
  }
}

/**
 * Sub-node @p position raises interrupt @p type: INTSRC shows SLVINT and its position,
 * INTTYPE the type. Ignored if the node is not discovered.
 */
void Bus::RaiseInterrupt(size_t position, uint8_t type) {
  if (position < m_Discovered) {
    m_Interrupts.push_back(
        {static_cast<uint8_t>(reg::kIntsrcSlvint | (position & reg::kIntsrcInode)), type});
  }
}

/** Power-cycles every node: registers reset, main node stopped, SUSTAIN trap cleared. */
void Bus::PowerCycle() {
  m_Main->ResetRegisters();
  LoseChain();
  m_Interrupts.clear();
  m_Running = false;
  m_Locking = false;
  m_Sustaining = false;
  m_Wedged = false;
  m_Discovering = false;
  m_DiscoveryPending = false;
}

/**
 * Puts the main node in the state a previous session leaves it in: running, no sub-nodes
 * discovered. A soft reset then starts SUSTAIN.
 */
void Bus::StartRunning() {
  PowerCycle();
  m_Main->m_Regs[0][reg::kControl] = reg::kControlMstr;
  m_Running = true;
}

/**
 * Starts or stops SYNC on the main node's SYNC pin (it is present by default). Without it
 * the PLL can't lock, so MSTR brings no MSTR_RUNNING. Losing it unlocks a running main
 * node's PLL: the node goes back to POWER-UP (no SUSTAIN on a main node), its registers
 * reset except CONTROL and BMMCFG, and the chain is lost. With CONTROL.MSTR still set the
 * PLL locks again, and MSTR_RUNNING comes again, once SYNC is back (TRMs, "Main Node
 * PLL Locked Interrupt").
 */
void Bus::SetSync(bool present) {
  if (present == m_Sync) {
    return;
  }
  m_Sync = present;
  if (!present && m_Running) {
    m_Running = false;
    m_Discovering = false;
    m_DiscoveryPending = false;
    LoseChain();
    m_Main->SoftResetRegisters();
    m_Locking = (m_Main->m_Regs[0][reg::kControl] & reg::kControlMstr) != 0;
  }
  if (present && m_Locking) {
    m_LockAt = m_Sim.NowMs() + m_Sim.GetTiming().pllLockMs;
  }
}

/** True while the main node is in SUSTAIN after a soft reset. */
bool Bus::InSustain() const { return m_Sustaining && !TimeReached(m_Sim.NowMs(), m_SustainEnd); }

/** Lets every event that is due happen: PLL lock (MSTR_RUNNING) and DSCDONE. */
void Bus::Update() {
  const uint32_t now = m_Sim.NowMs();
  if (m_Locking && m_Sync && TimeReached(now, m_LockAt)) {
    m_Locking = false;
    m_Running = true;
    m_Interrupts.push_back({reg::kIntsrcMstint, reg::kInttypeMstrRunning});
  }
  if (m_DiscoveryPending && TimeReached(now, m_DiscoveryDoneAt)) {
    m_DiscoveryPending = false;
    if (m_Running && CanDiscover()) {
      Node& node = *m_Subs[m_Discovered];
      node.ResetRegisters();
      node.m_Regs[0][reg::kNode] =
          static_cast<uint8_t>(reg::kNodeLast | reg::kNodeDiscvd | m_Discovered);
      if (m_Discovered > 0) {  // The node before it is no longer the last.
        m_Subs[m_Discovered - 1]->m_Regs[0][reg::kNode] &= static_cast<uint8_t>(~reg::kNodeLast);
      }
      ++m_Discovered;
      m_Discovering = false;
      m_Main->m_Regs[0][reg::kIntpnd2] |= reg::kIntpnd2Dscdone;
      if ((m_Main->m_Regs[0][reg::kIntmsk2] & reg::kIntmsk2Dscdone) != 0) {
        m_Interrupts.push_back({reg::kIntsrcMstint, reg::kInttypeDscdone});
      }
    }
    // Otherwise discovery stays active with nothing to come, as on the hardware.
  }
}

/**
 * Drives the reset line: asserting it power-cycles the bus and holds it silent; after it
 * is released the bus answers again once Timing::accessAfterResetMs have passed.
 */
void Bus::HoldInReset(bool asserted) {
  if (asserted) {
    PowerCycle();
  } else if (m_InReset) {
    m_AccessAt = m_Sim.NowMs() + m_Sim.GetTiming().accessAfterResetMs;
  }
  m_InReset = asserted;
}

/** A transfer to the main node's address: its registers. NAKs while held in reset and
    for tACCESS after it. */
Result Bus::MainAccess(const uint8_t* writeData, size_t writeLen, uint8_t* readData,
                       size_t readLen) {
  CheckSettle();
  if (m_InReset || !TimeReached(m_Sim.NowMs(), m_AccessAt)) {
    return Result::kI2cError;
  }
  Node& main = *m_Main;
  if (writeLen > 0) {
    main.m_Pointer = writeData[0];
    for (size_t i = 1; i < writeLen; ++i) {
      WriteMain(main.Full(main.m_Pointer), writeData[i]);
      ++main.m_Pointer;
    }
  }
  for (size_t i = 0; i < readLen; ++i) {
    readData[i] = ReadMain(main.Full(main.m_Pointer));
    ++main.m_Pointer;
  }
  return Result::kOk;
}

/**
 * A transfer to the bus address: goes where NODEADR points (a sub-node, every sub-node, or
 * a peripheral at a sub-node's CHIP address). NAKs if that node is not discovered, the
 * peripheral is missing, or a broadcast is read.
 */
Result Bus::BusAccess(const uint8_t* writeData, size_t writeLen, uint8_t* readData,
                      size_t readLen) {
  CheckSettle();
  if (m_InReset || !TimeReached(m_Sim.NowMs(), m_AccessAt)) {
    return Result::kI2cError;
  }
  const uint8_t nodeadr = m_Main->m_Regs[0][reg::kNodeadr];
  if ((nodeadr & reg::kNodeadrBrcst) != 0) {
    // What the hardware does with no sub-node is not documented: a NAK here, so the
    // stack must not rely on it.
    if (readLen > 0 || writeLen == 0 || m_Discovered == 0) {
      return Result::kI2cError;
    }
    for (size_t i = 1; i < writeLen; ++i) {  // Every node, the main node too (TRMs).
      WriteMain(static_cast<uint16_t>(writeData[0] + i - 1), writeData[i]);
    }
    for (size_t i = 0; i < m_Discovered; ++i) {
      m_Subs[i]->WriteBurst(writeData, writeLen);
    }
    CheckPower();
    return Result::kOk;
  }
  const size_t position = nodeadr & reg::kNodeadrNode;
  if (position >= m_Discovered) {
    return Result::kI2cError;  // No such node on the bus yet.
  }
  Node& node = *m_Subs[position];
  if ((nodeadr & reg::kNodeadrPeri) != 0) {
    I2cDevice* device = node.FindDevice(node.m_Regs[0][reg::kChip]);
    if (device == nullptr) {
      return Result::kI2cError;
    }
    const uint32_t now = m_Sim.NowMs();
    if (writeLen > 0) {
      const Result result = device->Write(writeData, writeLen, now);
      if (result != Result::kOk) {
        return result;
      }
    }
    return (readLen > 0) ? device->Read(readData, readLen, now) : Result::kOk;
  }
  if (writeLen > 0) {
    node.WriteBurst(writeData, writeLen);
    CheckPower();
  }
  if (readLen > 0) {
    node.ReadBurst(readData, readLen);
  }
  return Result::kOk;
}

/** A main-node register write, with CONTROL, DISCVRY and the SUSTAIN trap handled. */
void Bus::WriteMain(uint16_t reg, uint8_t value) {
  if (PageOf(reg) == 0) {
    switch (LowByte(reg)) {
      case reg::kControl:
        WriteControl(value);
        return;
      case reg::kDiscvry:
        m_Main->m_Regs[0][reg::kDiscvry] = value;
        StartDiscovery(value);
        return;
      case reg::kI2sgcfg:
        if ((value & reg::kI2sgcfgInv) != 0 && InSustain() &&
            m_Main->Caps().family == ChipFamily::kAd243x) {
          Wedge();
        }
        break;
      case reg::kIntstat:
      case reg::kIntsrc:
      case reg::kInttype:
      case reg::kDiscstat:
        return;  // Read-only.
      default:
        break;
    }
  }
  m_Main->Write(reg, value);
  if (reg == reg::kSwctl) {
    CheckPower();
  }
  if (PageOf(reg) == 0 && Node::IsAutoBroadcastReg(LowByte(reg))) {
    // The main node broadcasts the new value to the sub-nodes discovered by now (TRMs, the
    // registers' descriptions). One discovered later doesn't get it until the register is
    // written again: the bench found no DATCTL.DNS/UPS in a sub-node that came after the
    // start-up writes.
    for (size_t i = 0; i < m_Discovered; ++i) {
      m_Subs[i]->m_Regs[0][LowByte(reg)] = value;
    }
  }
}

/** A main-node register read; INTSRC, INTTYPE (which clears), INTSTAT and DISCSTAT come from the
 * bus. */
uint8_t Bus::ReadMain(uint16_t reg) {
  if (PageOf(reg) == 0) {
    switch (LowByte(reg)) {
      case reg::kIntstat:
        return m_Interrupts.empty() ? 0 : reg::kIntstatIrq;
      case reg::kIntsrc:
        return m_Interrupts.empty() ? 0 : m_Interrupts.front().source;
      case reg::kInttype: {
        if (m_Interrupts.empty()) {
          return 0;
        }
        const uint8_t type = m_Interrupts.front().type;
        m_Interrupts.pop_front();
        return type;
      }
      case reg::kDiscstat:
        return m_Discovering ? static_cast<uint8_t>(reg::kDiscstatDscact |
                                                    (m_Discovered & reg::kDiscstatDnode))
                             : 0;
      default:
        break;
    }
  }
  return m_Main->Read(reg);
}

/**
 * A main-node CONTROL write: SOFTRST resets, ENDDSC ends discovery, and MSTR starts the
 * PLL, or wedges an AD243x main node if it comes during SUSTAIN. Once the PLL is locked,
 * MSTR has no effect, set or cleared (TRMs, "Main Node Bring-Up").
 */
void Bus::WriteControl(uint8_t value) {
  const bool mstr = (value & reg::kControlMstr) != 0;
  if ((value & reg::kControlSoftrst) != 0) {
    SoftReset(mstr);
  }
  if ((value & reg::kControlEnddsc) != 0) {
    m_Discovering = false;
    m_DiscoveryPending = false;
  }
  if ((value & reg::kControlNewstrct) != 0) {
    m_Main->ApplyStructure();
    for (size_t i = 0; i < m_Discovered; ++i) {
      m_Subs[i]->ApplyStructure();
    }
    m_NewStruct = true;
    m_NewStructAt = m_Sim.NowMs();
    ++m_NewStructs;
    if (m_NewStructObserver) {
      m_NewStructObserver();
    }
  }
  uint8_t& control = m_Main->m_Regs[0][reg::kControl];
  const bool wasMstr = (control & reg::kControlMstr) != 0;
  constexpr uint8_t kSelfClearing =
      reg::kControlSoftrst | reg::kControlNewstrct | reg::kControlEnddsc;
  control = static_cast<uint8_t>(value & ~kSelfClearing);
  CheckPower();  // SWBYP opens the switch.
  if (m_Running || m_Wedged) {
    return;  // The PLL is locked, or the node is unusable until power-cycled.
  }
  if (mstr && !wasMstr) {
    if (InSustain() && m_Main->Caps().family == ChipFamily::kAd243x) {
      Wedge();
      return;
    }
    m_Locking = true;
    m_LockAt = m_Sim.NowMs() + m_Sim.GetTiming().pllLockMs;
  } else if (!mstr) {
    m_Locking = false;
  }
}

/**
 * CONTROL.SOFTRST, from the TRMs' "Soft Reset" sections. It has no effect in POWER-UP (a
 * main node not running). Otherwise the registers reset (except CONTROL, BMMCFG and, on the
 * AD2433, PLLCTL) and the chain is lost. With @p mstr set the PLL stays locked. With it
 * clear the PLL unlocks: an AD243x main node spends SUSTAIN (1024 SYNC periods) before it
 * is back in POWER-UP; an AD242x main node never enters SUSTAIN (AD242x TRM).
 */
void Bus::SoftReset(bool mstr) {
  // A reset during SUSTAIN restarts it: stricter than the TRM says, so that the stack
  // must time its wait from the last reset.
  const bool sustaining = InSustain();
  if (!m_Running && !sustaining) {
    return;
  }
  m_Discovering = false;
  m_DiscoveryPending = false;
  m_Interrupts.clear();
  LoseChain();
  m_Main->SoftResetRegisters();
  if (mstr && m_Running) {
    return;
  }
  m_Running = false;
  m_Locking = false;
  if (m_Main->Caps().family == ChipFamily::kAd243x) {
    m_Sustaining = true;
    m_SustainEnd = m_Sim.NowMs() + m_Sim.GetTiming().sustainMs;
  }
}

/** The SUSTAIN trap: the main node stops and stays unusable until a power cycle. */
void Bus::Wedge() {
  m_Wedged = true;
  m_Running = false;
  m_Locking = false;
  m_Discovering = false;
  m_DiscoveryPending = false;
  LoseChain();
}

/** A DISCVRY write: starts discovering the next sub-node, if the main node is running. */
void Bus::StartDiscovery(uint8_t respCycles) {
  if (!m_Running) {
    return;
  }
  m_Discovering = true;
  m_DiscoveryPending = true;
  const uint8_t nodeadr = m_Main->m_Regs[0][reg::kNodeadr];
  m_DiscoveryAddressed =
      (m_Discovered == 0) || ((nodeadr & (reg::kNodeadrBrcst | reg::kNodeadrPeri)) == 0 &&
                              static_cast<size_t>(nodeadr & reg::kNodeadrNode) == m_Discovered - 1);
  m_DiscoveryValue = respCycles;
  m_DiscoveryDoneAt = m_Sim.NowMs() + m_Sim.GetTiming().discoveryMs;
}

/**
 * True if the next sub-node can be discovered: plugged in, powered (upstream ENSW or its
 * own supply), the upstream B side passing frames (its switch closed onto a node that
 * draws the bias current, or SWBYP), upstream XCVRBINV matching the link, DISCVRY in its
 * response-cycle window, and NODEADR on the upstream sub-node when DISCVRY was written.
 */
bool Bus::CanDiscover() const {
  if (m_Discovered >= m_Subs.size()) {
    return false;
  }
  const Node& upstream = (m_Discovered == 0) ? *m_Main : *m_Subs[m_Discovered - 1];
  const Node& node = *m_Subs[m_Discovered];
  const LinkSpec& link = node.m_Link;
  const bool bypass = (upstream.m_Regs[0][reg::kControl] & reg::kControlSwbyp) != 0;
  const bool switchOn = SwitchClosed(upstream);
  const bool onPair = (link.power == LinkPower::kOnPair);
  const bool powered = onPair ? switchOn && upstream.m_DownstreamSupply : node.m_LocalPower;
  const bool frames = bypass || (switchOn && (onPair || !link.isolated));
  const bool inverted = (upstream.m_Regs[0][reg::kControl] & reg::kControlXcvrbinv) != 0;
  return powered && frames && inverted == link.bSideInverted && m_DiscoveryAddressed &&
         m_DiscoveryValue >= link.minRespCycles && m_DiscoveryValue <= link.maxRespCycles;
}

/**
 * True if @p node's switch is closed: SWCTL.ENSW without CONTROL.SWBYP (the TRMs: with
 * SWBYP "the line switch will not be enabled even if A2B_SWCTL.ENSW is set").
 */
bool Bus::SwitchClosed(const Node& node) {
  return (node.m_Regs[0][reg::kSwctl] & reg::kSwctlEnsw) != 0 &&
         (node.m_Regs[0][reg::kControl] & reg::kControlSwbyp) == 0;
}

/**
 * True if the sub-node at @p position has power: over the A2B pair, the upstream node's
 * switch closed and the supply it passes on; on separate wires, its own supply.
 */
bool Bus::Powered(size_t position) const {
  const Node& node = *m_Subs[position];
  if (node.m_Link.power != LinkPower::kOnPair) {
    return node.m_LocalPower;
  }
  const Node& upstream = (position == 0) ? *m_Main : *m_Subs[position - 1];
  return SwitchClosed(upstream) && upstream.m_DownstreamSupply;
}

/** Drops the first discovered sub-node that has lost power, with the nodes after it. */
void Bus::CheckPower() {
  for (size_t i = 0; i < m_Discovered; ++i) {
    if (!Powered(i)) {
      DropFrom(i, false);
      return;
    }
  }
}

/**
 * The discovered sub-nodes from @p position on drop out of the bus: undiscovered, their
 * registers reset. The node before them becomes the last and latches SRFERR, raising
 * INTTYPE 5 if its INTMSK0 enables it; waiting interrupts of the dropped nodes become 0xFD;
 * a discovery under way finds nothing.
 * @param powerFault The upstream node's switch reports a power fault first (INTTYPE 15):
 *        the supply it passes on was removed (SetDownstreamSupply()).
 */
void Bus::DropFrom(size_t position, bool powerFault) {
  if (position >= m_Discovered) {
    return;
  }
  for (size_t i = position; i < m_Discovered; ++i) {
    m_Subs[i]->ResetRegisters();
  }
  m_Discovered = position;
  m_DiscoveryPending = false;
  Node& last = (position == 0) ? *m_Main : *m_Subs[position - 1];
  if (position > 0) {
    last.m_Regs[0][reg::kNode] |= reg::kNodeLast;
  }
  last.m_Regs[0][reg::kIntpnd0] |= reg::kIntpnd0Srferr;
  for (Interrupt& irq : m_Interrupts) {
    if ((irq.source & reg::kIntsrcSlvint) != 0 && (irq.source & reg::kIntsrcInode) >= position) {
      irq.type = reg::kInttypeSubReadErr;
    }
  }
  const uint8_t source =
      (position == 0)
          ? reg::kIntsrcMstint
          : static_cast<uint8_t>(reg::kIntsrcSlvint | ((position - 1) & reg::kIntsrcInode));
  const uint8_t mask = last.m_Regs[0][reg::kIntmsk0];
  if (powerFault) {
    last.m_Regs[0][reg::kIntpnd0] |= reg::kIntmsk0Pwrerr;
    if ((mask & reg::kIntmsk0Pwrerr) != 0) {
      m_Interrupts.push_back({source, reg::kInttypePwrerrCable});
    }
  }
  if ((mask & reg::kIntmsk0Srferr) != 0) {
    m_Interrupts.push_back({source, reg::kInttypeSrferr});
  }
}

/** Counts a transfer in the millisecond after the last CONTROL.NEWSTRCT. */
void Bus::CheckSettle() {
  if (m_NewStruct && m_Sim.NowMs() == m_NewStructAt) {
    ++m_SettleAccesses;
  }
}

/**
 * Follows the audio through the bus's registers, from the TRMs' slot model (the file's
 * comment): the RX frame buffer of each node (the RXMASK channels in order, 32 at most; an
 * AD242x's, its port's RX channels),
 * the downstream slots from the main node's DNSLOTS through each discovered sub-node's
 * DNSLOTS, DNMASK and LDNSLOTS/DNOFFSET, the upstream slots from the last sub-node's
 * LUPSLOTS/UPOFFSET through each UPSLOTS and UPMASK to the main node's UPSLOTS, each node's
 * TX frame buffer (its downstream slots played, then its upstream ones) and its TXXBAR (an
 * AD242x: channel n plays location n), to the TX channels its port gives.
 * The slot registers are the ones in use (the last NEWSTRCT's).
 * @return What each node plays, and an error for registers the model can't follow.
 */
AudioTrace Bus::TraceAudio() const {
  AudioTrace trace;
  const size_t count = m_Discovered;
  trace.sub.resize(count);
  trace.dnSlots.resize(count);
  trace.upSlots.resize(count);
  trace.rate.assign(count, 1);
  const auto nodeAt = [this](size_t index) -> const Node& {
    return (index == 0) ? *m_Main : *m_Subs[index - 1];
  };
  const auto fail = [&trace](const std::string& what) {
    if (trace.error.empty()) {
      trace.error = what;
    }
  };
  // Each sub-node's I2S rate. Only the 1x, 2x and 4x rates (the stack's scope) are known;
  // the main node's I2SRATE does nothing. The TDM channels are the same list at any rate.
  for (size_t i = 1; i <= count; ++i) {
    const uint8_t rate = nodeAt(i).Reg(reg::kI2srate);
    const uint8_t field = rate & reg::kI2srateRate;
    if ((rate & (reg::kI2srateShare | reg::kI2srateReduce)) != 0 ||
        (field != 0 && field != reg::kI2srateX2 && field != reg::kI2srateX4)) {
      fail("a reduced I2S rate (I2SRATE) is not modelled");
    } else {
      trace.rate[i - 1] = (field == reg::kI2srateX2) ? 2 : (field == reg::kI2srateX4) ? 4 : 1;
    }
  }
  // Each node's port: its channels each way.
  std::vector<PortChannels> ports(count + 1);
  for (size_t i = 0; i <= count; ++i) {
    ports[i] = PortChannelsOf(nodeAt(i), (i == 0) ? 1 : trace.rate[i - 1]);
  }
  // RX frame buffers: the RXMASK channels, in channel order; without a crossbar, the port's
  // RX channels.
  std::vector<std::vector<Signal>> rx(count + 1);
  for (size_t i = 0; i <= count; ++i) {
    const Node& node = nodeAt(i);
    if (!node.Caps().hasCrossbar) {
      for (unsigned channel = 0; channel < ports[i].rx; ++channel) {
        rx[i].push_back(Signal::From(static_cast<int8_t>(i) - 1, static_cast<uint8_t>(channel)));
      }
      for (uint16_t r = reg::kRxmask0; r < reg::kTxxbar0 + 32; ++r) {
        if (node.Reg(r) != 0) {
          fail("a crossbar register written on a node without a crossbar");
        }
      }
      continue;
    }
    for (unsigned channel = 0; channel < 64; ++channel) {
      if ((node.Reg(static_cast<uint16_t>(reg::kRxmask0 + channel / 8)) >> (channel % 8) & 1) !=
          0) {
        rx[i].push_back(Signal::From(static_cast<int8_t>(i) - 1, static_cast<uint8_t>(channel)));
      }
    }
    if (rx[i].size() > 32) {
      rx[i].resize(32);  // The frame buffer is full.
    }
  }
  const auto run = [&rx, &fail](size_t index, size_t offset, size_t len) {
    std::vector<Signal> slots;
    for (size_t k = offset; k < offset + len; ++k) {
      if (k >= rx[index].size()) {
        fail("slots added from past the RX frame buffer's channels");
        slots.push_back(Signal());
      } else {
        slots.push_back(rx[index][k]);
      }
    }
    return slots;
  };
  // A node takes part in a direction's data if DATCTL (in use) enables it and, for a
  // sub-node, its slot size and format (SLOTFMT) are the main node's. Otherwise it plays
  // nothing and adds nothing there, though the slots still pass through it.
  const auto carries = [this, &nodeAt](size_t index, bool upstream) {
    const Node& node = nodeAt(index);
    if ((node.AppliedReg(reg::kDatctl) & (upstream ? reg::kDatctlUps : reg::kDatctlDns)) == 0) {
      return false;
    }
    const uint8_t fields = upstream ? 0xF0 : 0x0F;  // UPFMT and UPSIZE, or DNFMT and DNSIZE.
    return (node.AppliedReg(reg::kSlotfmt) & fields) ==
           (m_Main->AppliedReg(reg::kSlotfmt) & fields);
  };
  const auto blank = [](std::vector<Signal>& slots) {
    for (Signal& slot : slots) {
      slot = Signal();
    }
  };
  std::vector<std::vector<Signal>> played(count + 1);  // TX frame buffers.
  // Downstream.
  std::vector<Signal> slots = run(0, 0, m_Main->AppliedReg(reg::kDnslots) & 0x3F);
  if (!carries(0, false)) {
    blank(slots);
  }
  for (size_t i = 1; i <= count; ++i) {
    const Node& node = nodeAt(i);
    trace.dnSlots[i - 1] = slots.size();
    const uint8_t ldnslots = node.AppliedReg(reg::kLdnslots);
    if ((ldnslots & reg::kLdnslotsDnmasken) == 0 && (ldnslots & 0x3F) != 0) {
      fail("LDNSLOTS without DNMASKEN (consumed slots) is not modelled");
    }
    uint32_t mask = 0;
    for (int b = 0; b < 4; ++b) {
      mask |= static_cast<uint32_t>(node.AppliedReg(static_cast<uint16_t>(reg::kDnmask0 + b)))
              << (8 * b);
    }
    for (unsigned k = 0; k < 32; ++k) {
      if ((mask >> k & 1) != 0) {
        if (k >= slots.size()) {
          fail("DNMASK selects a slot past the slots received");
        }
        played[i].push_back((k < slots.size() && carries(i, false)) ? slots[k] : Signal());
      }
    }
    const size_t passed = node.AppliedReg(reg::kDnslots) & 0x3F;
    if (passed > slots.size()) {
      fail("DNSLOTS passes more slots than it receives");
    }
    slots.resize(passed);
    std::vector<Signal> own =
        run(i, node.AppliedReg(reg::kDnoffset) & 0x1F,
            ((ldnslots & reg::kLdnslotsDnmasken) != 0) ? (ldnslots & 0x3F) : 0);
    if (!carries(i, false)) {
      blank(own);
    }
    slots.insert(slots.end(), own.begin(), own.end());
    if (slots.size() > 32) {
      fail("more than 32 downstream slots on a link");
    }
  }
  // Upstream.
  slots.clear();
  for (size_t i = count; i >= 1; --i) {
    const Node& node = nodeAt(i);
    uint32_t mask = 0;
    for (int b = 0; b < 4; ++b) {
      mask |= static_cast<uint32_t>(node.AppliedReg(static_cast<uint16_t>(reg::kUpmask0 + b)))
              << (8 * b);
    }
    for (unsigned k = 0; k < 32; ++k) {
      if ((mask >> k & 1) != 0) {
        if (k >= slots.size()) {
          fail("UPMASK selects a slot past the slots received");
        }
        played[i].push_back((k < slots.size() && carries(i, true)) ? slots[k] : Signal());
      }
    }
    const size_t passed = node.AppliedReg(reg::kUpslots) & 0x3F;
    if (passed > slots.size()) {
      fail("UPSLOTS passes more slots than it receives");
    }
    slots.resize(passed);
    std::vector<Signal> own =
        run(i, node.AppliedReg(reg::kUpoffset) & 0x1F, node.AppliedReg(reg::kLupslots) & 0x3F);
    if (!carries(i, true)) {
      blank(own);
    }
    slots.insert(slots.end(), own.begin(), own.end());
    if (slots.size() > 32) {
      fail("more than 32 upstream slots on a link");
    }
    trace.upSlots[i - 1] = slots.size();
  }
  const size_t received = m_Main->AppliedReg(reg::kUpslots) & 0x3F;
  if (received > slots.size()) {
    fail("the main node's UPSLOTS is more than the slots it receives");
  }
  slots.resize(received);
  if (!carries(0, true)) {
    blank(slots);
  }
  played[0] = slots;
  // TX crossbars; without one, channel n plays location n. Only the port's channels play.
  for (size_t i = 0; i <= count; ++i) {
    if (played[i].size() > 32) {
      fail("more than 32 slots played by a node");
    }
    const Node& node = nodeAt(i);
    std::array<Signal, 32>& out = (i == 0) ? trace.main : trace.sub[i - 1];
    for (unsigned channel = 0; channel < 32; ++channel) {
      const size_t location = node.Caps().hasCrossbar
                                  ? node.Reg(static_cast<uint16_t>(reg::kTxxbar0 + channel)) & 0x1F
                                  : channel;
      out[channel] =
          (channel < ports[i].tx && location < played[i].size()) ? played[i][location] : Signal();
    }
  }
  return trace;
}

/** Every sub-node becomes undiscovered, with its registers reset. */
void Bus::LoseChain() {
  m_Discovered = 0;
  for (auto& node : m_Subs) {
    node->ResetRegisters();
  }
}

// Simulator

/** Makes a simulator with no buses, at time 0. */
Simulator::Simulator(const Timing& timing) : m_Timing(timing) {}

/** Destroys the buses and the devices the nodes own. */
Simulator::~Simulator() = default;

/**
 * Adds a bus: a main node at @p mainAddr, sub-nodes behind @p busAddr.
 * @return The new bus; its index is the number of buses added before it.
 */
Bus& Simulator::AddBus(uint8_t mainAddr, uint8_t busAddr, const ChipSpec& mainChip) {
  m_Buses.push_back(std::make_unique<Bus>(*this, mainAddr, busAddr, mainChip));
  return *m_Buses.back();
}

/** Attaches a device the test owns at @p i2cAddr on the host I2C bus. */
void Simulator::AttachHostDevice(uint8_t i2cAddr, I2cDevice& device) {
  m_HostDevices.emplace_back(i2cAddr, &device);
}

/** Moves time on by @p ms and lets every pending event that is due happen. */
void Simulator::Advance(uint32_t ms) {
  m_NowMs += ms;
  for (auto& bus : m_Buses) {
    bus->Update();
  }
}

/** Makes the next @p count transfers to @p i2cAddr fail. */
void Simulator::FailAddress(uint8_t i2cAddr, int count) {
  m_FailAddr = i2cAddr;
  m_FailAddrCount = count;
}

/**
 * Makes the next @p count write transfers (without a read) to @p i2cAddr whose register
 * address, the first byte, is @p reg fail, e.g. one register of a node's set-up.
 */
void Simulator::FailRegisterWrite(uint8_t i2cAddr, uint8_t reg, int count) {
  m_FailRegAddr = i2cAddr;
  m_FailReg = reg;
  m_FailRegCount = count;
}

/** The transfers so far as I2CT lines, between an I2CT-BEGIN and an I2CT-END line. */
std::string Simulator::Trace() const {
  std::string text;
  char line[64];
  std::snprintf(line, sizeof(line), "I2CT-BEGIN,1,%u,0,0,0\n",
                static_cast<unsigned>(m_Trace.size()));
  text += line;
  static const char* const kOpNames[] = {"W", "R", "WR"};
  for (const TraceEntry& entry : m_Trace) {
    std::snprintf(line, sizeof(line), "I2CT,%u,%llu,0,%02X,%s,%u,%u,",
                  static_cast<unsigned>(entry.seq),
                  static_cast<unsigned long long>(entry.timeMs) * 1000ULL, entry.addr,
                  kOpNames[static_cast<int>(entry.op)], entry.failed ? 1u : 0u,
                  static_cast<unsigned>(entry.written.size()));
    text += line;
    for (uint8_t byte : entry.written) {
      std::snprintf(line, sizeof(line), "%02X", byte);
      text += line;
    }
    std::snprintf(line, sizeof(line), ",%u,", static_cast<unsigned>(entry.read.size()));
    text += line;
    for (uint8_t byte : entry.read) {
      std::snprintf(line, sizeof(line), "%02X", byte);
      text += line;
    }
    text += '\n';
  }
  text += "I2CT-END\n";
  return text;
}

/** Platform: a write transfer to a simulated bus or device. */
Result Simulator::I2cWrite(uint8_t addr, const uint8_t* data, size_t len) {
  return Transfer(Op::kWrite, addr, data, len, nullptr, 0);
}

/** Platform: a read transfer from a simulated bus or device. */
Result Simulator::I2cRead(uint8_t addr, uint8_t* data, size_t len) {
  return Transfer(Op::kRead, addr, nullptr, 0, data, len);
}

/** Platform: a write-read transfer to a simulated bus or device. */
Result Simulator::I2cWriteRead(uint8_t addr, const uint8_t* writeData, size_t writeLen,
                               uint8_t* readData, size_t readLen) {
  return Transfer(Op::kWriteRead, addr, writeData, writeLen, readData, readLen);
}

/** Platform: drives the bus's reset line, if Bus::SetHasResetLine() gave it one. */
Result Simulator::SetMainNodeReset(uint8_t bus, bool asserted) {
  if (bus >= m_Buses.size() || !m_Buses[bus]->HasResetLine()) {
    return Result::kNotSupported;
  }
  m_Buses[bus]->HoldInReset(asserted);
  return Result::kOk;
}

/** Platform: the bus's IRQ line if it has one (Bus::SetHasIrqLine()), else true. */
bool Simulator::IrqPending(uint8_t bus) {
  if (bus >= m_Buses.size() || !m_Buses[bus]->HasIrqLine()) {
    return true;
  }
  m_Buses[bus]->Update();
  return m_Buses[bus]->PendingInterrupts() > 0;
}

/** Platform: the bus's main-node power-good input (Bus::SetPowerGood()). */
bool Simulator::PowerGood(uint8_t bus) {
  return (bus < m_Buses.size()) ? m_Buses[bus]->PowerGood() : true;
}

/** Platform: keeps the message for LogLines(), prefixed "E: ", "W: ", "I: " or "D: ". */
void Simulator::Log(LogLevel level, const char* text) {
  static const char* const kPrefixes[] = {"E: ", "W: ", "I: ", "D: "};
  m_LogLines.push_back(std::string(kPrefixes[static_cast<int>(level)]) + text);
}

/**
 * Runs one transfer: counts it if Lock() is not held, lets due events happen, injects a
 * failure or routes it, zeroes the read data of a failed transfer, and records it.
 */
Result Simulator::Transfer(Op op, uint8_t addr, const uint8_t* writeData, size_t writeLen,
                           uint8_t* readData, size_t readLen) {
  if (m_LockDepth <= 0) {
    ++m_UnlockedTransfers;
  }
  for (auto& bus : m_Buses) {
    bus->Update();
  }
  Result result = InjectFailure(addr, writeData, writeLen, readLen)
                      ? Result::kI2cError
                      : Route(addr, writeData, writeLen, readData, readLen);
  if (result != Result::kOk && readLen > 0) {
    std::memset(readData, 0, readLen);
  }
  TraceEntry entry = {m_Seq, m_NowMs, addr, op, result != Result::kOk, {}, {}};
  if (writeLen > 0) {
    entry.written.assign(writeData, writeData + writeLen);
  }
  if (readLen > 0) {
    entry.read.assign(readData, readData + readLen);
  }
  m_Trace.push_back(std::move(entry));
  ++m_Seq;
  return result;
}

/** Hands a transfer to the bus or host device at @p addr; nothing there is a NAK. */
Result Simulator::Route(uint8_t addr, const uint8_t* writeData, size_t writeLen, uint8_t* readData,
                        size_t readLen) {
  for (auto& bus : m_Buses) {
    if (addr == bus->MainAddr()) {
      return bus->MainAccess(writeData, writeLen, readData, readLen);
    }
    if (addr == bus->BusAddr()) {
      return bus->BusAccess(writeData, writeLen, readData, readLen);
    }
  }
  for (const auto& entry : m_HostDevices) {
    if (entry.first == addr) {
      if (writeLen > 0) {
        const Result result = entry.second->Write(writeData, writeLen, m_NowMs);
        if (result != Result::kOk) {
          return result;
        }
      }
      return (readLen > 0) ? entry.second->Read(readData, readLen, m_NowMs) : Result::kOk;
    }
  }
  return Result::kI2cError;  // Nothing answers: NAK.
}

/** True if this transfer is to fail (FailTransfer(), FailAddress() or FailRegisterWrite()). */
bool Simulator::InjectFailure(uint8_t addr, const uint8_t* writeData, size_t writeLen,
                              size_t readLen) {
  bool fail = false;
  if (m_Seq == m_FailAt) {
    m_FailAt = UINT32_MAX;
    fail = true;
  }
  if (m_FailAddrCount > 0 && addr == m_FailAddr) {
    --m_FailAddrCount;
    fail = true;
  }
  if (m_FailRegCount > 0 && addr == m_FailRegAddr && readLen == 0 && writeLen > 1 &&
      writeData[0] == m_FailReg) {
    --m_FailRegCount;
    fail = true;
  }
  return fail;
}

}  // namespace sim
}  // namespace a2b
