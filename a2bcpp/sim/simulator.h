/**
 * @file simulator.h
 * @brief A simulated A2B system for host tests: a Platform whose host I2C bus reaches
 *        simulated nodes and peripherals. What it models is listed in simulator.cpp.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_SIM_SIMULATOR_H_
#define A2BCPP_SIM_SIMULATOR_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "a2b/platform.h"
#include "chip_caps.h"
#include "detail/regdefs.h"
#include "sim/eeprom.h"
#include "sim/i2c_device.h"

namespace a2b {
namespace sim {

/** @brief Simulated delays, in milliseconds. */
struct Timing {
  /** Soft reset to the end of SUSTAIN: 1024 SYNC periods at 48 kHz is 21.3 ms. */
  uint32_t sustainMs = 22;
  /** CONTROL.MSTR written to MSTR_RUNNING. The goldens bound it to 2.2-22.6 ms. */
  uint32_t pllLockMs = 5;
  /** DISCVRY written to DSCDONE. The goldens show 15-36 ms. */
  uint32_t discoveryMs = 20;
  /** RESETn released to the first access that is answered: tACCESS, at least 1.5 ms (AD2437
      and AD2431W-35W data sheets), rounded up to the millisecond clock. */
  uint32_t accessAfterResetMs = 2;
};

/** @brief The identity a simulated transceiver reports. */
struct ChipSpec {
  uint8_t vendorId = kAdiVendorId;         ///< VENDOR.
  uint8_t productId = 0x37;                ///< PRODUCT.
  uint8_t version = 0x21;                  ///< VERSION.
  uint8_t capability = 0x03;               ///< CAPABILITY.
  uint8_t chipId[reg::kCountChipid] = {};  ///< CHIPID0-5.
  /** Power configuration the chip detects at reset (SWSTAT2.HPSW_CFG_DET on AD243x): 0
      (CFG-0) or 4 (CFG-4, as on the bench). */
  uint8_t powerConfig = 0;
  bool lowVoltageInput = false;  ///< SWSTAT2.LVI_MODE (set on the bench's AD2437 nodes).
};

/** @brief Where a sub-node's power comes from. */
enum class LinkPower : uint8_t {
  kOnPair,         ///< Over the A2B pair: needs ENSW on the upstream node.
  kSeparateWires,  ///< Its own supply (Node::SetLocalPower()).
  /** An RJ45 or XLR link into a node behind a high-power switch: the node runs from the
      5 V the upstream board puts on the data pair while that board's power-good is high
      (the main node's Bus::SetPowerGood(), a sub-node's GPIO7 input), and gets the 24 V
      (VBUS) once the upstream switch closes. Frames reach it with CONTROL.SWBYP or the
      switch closed; without either it drops out but stays powered. */
  kTwoStep,
};

/** @brief The link from the upstream node to a sub-node. */
struct LinkSpec {
  /** The upstream node must set CONTROL.XCVRBINV, as for an RJ45 link. */
  bool bSideInverted = false;
  LinkPower power = LinkPower::kOnPair;  ///< Power source.
  /** On its own supply, the node's A side carries no DC (galvanically isolated), so the
      upstream switch never finishes closing: only CONTROL.SWBYP passes frames to it. */
  bool isolated = false;
  uint8_t minRespCycles = 0x10;  ///< Lowest DISCVRY value that discovers the node.
  uint8_t maxRespCycles = 0xFF;  ///< Highest DISCVRY value that discovers the node.
};

/** @brief A sub-node to plug in. */
struct SubNodeSpec {
  ChipSpec chip;  ///< Transceiver.
  LinkSpec link;  ///< Link from the upstream node.
};

/** @brief An audio channel's content in Bus::TraceAudio(): the node and RX channel it came
 *         from. */
struct Signal {
  static constexpr int8_t kNone = -2;  ///< #node of a channel that carries nothing.
  int8_t node = kNone;                 ///< -1 for the main node, 0-15 for a sub-node.
  uint8_t channel = 0;                 ///< Its TDM channel on that node's DRXn pins.

  /** @brief A signal from @p from's RX channel @p rxChannel. */
  static Signal From(int8_t from, uint8_t rxChannel) {
    Signal signal;
    signal.node = from;
    signal.channel = rxChannel;
    return signal;
  }
  /** @brief Same node and channel (or both nothing). */
  bool operator==(const Signal& other) const {
    return node == other.node && (node == kNone || channel == other.channel);
  }
  /** @brief Not operator==. */
  bool operator!=(const Signal& other) const { return !(*this == other); }
};

/** @brief The audio a bus carries, by its registers (Bus::TraceAudio()). */
struct AudioTrace {
  /** Registers the model can't follow (a slot register past the slots there, frame
      buffers over 32, LDNSLOTS without DNMASKEN); empty if none. */
  std::string error;
  std::array<Signal, 32> main{};            ///< What the main node plays on TX channel n.
  std::vector<std::array<Signal, 32>> sub;  ///< What each discovered sub-node plays.
  std::vector<size_t> dnSlots;              ///< Downstream data slots into sub-node n.
  std::vector<size_t> upSlots;              ///< Upstream data slots out of sub-node n's A side.
  /** Sub-node n's I2S/TDM rate as a multiple of the superframe rate: 1, 2 or 4 (I2SRATE).
      Its TDM channels run in sequence through the superframe, so at 2x channels 0 to
      2 * (channels per frame) - 1 are two sample frames. */
  std::vector<unsigned> rate;

  /** @brief The rate multiple of node @p node (-1: main, always 1). */
  unsigned Rate(int node) const { return (node < 0) ? 1 : rate[static_cast<size_t>(node)]; }

  /** @brief What node @p node (-1: main) plays on TX channel @p channel. */
  Signal Played(int node, unsigned channel) const {
    return (node < 0) ? main[channel] : sub[static_cast<size_t>(node)][channel];
  }
};

class Bus;
class Simulator;

/// One simulated transceiver: registers, inputs and peripherals. Tests set and check it.
class Node {
 public:
  /** @brief GPIO input levels after power-on: GPIO7 (power-good) high. */
  static constexpr uint8_t kDefaultGpioInputs = 0x80;
  /** @brief VMTR input 1 (VBUS) after power-on: 24 V present (the value a bench node read). */
  static constexpr uint8_t kDefaultSupplyReading = 0xB9;
  /** @brief CHIP after reset: the usual EEPROM address. */
  static constexpr uint8_t kResetChip = 0x50;
  /** @brief RESPCYCS after reset. */
  static constexpr uint8_t kResetRespCycles = 0x40;

  Node(const ChipSpec& chip, const LinkSpec& link);  ///< Powered-on node.
  Node(const Node&) = delete;
  Node& operator=(const Node&) = delete;

  const ChipCaps& Caps() const { return m_Caps; }  ///< Capabilities (none if unsupported).
  LinkSpec& Link() { return m_Link; }              ///< Link from upstream (sub-nodes only).
  uint8_t Reg(uint16_t reg) const;                 ///< Register as a host read sees it.
  uint8_t AppliedReg(uint16_t reg) const;          ///< A structure register as in use (NEWSTRCT).
  uint8_t Page() const { return m_Page; }          ///< Page MMRPAGE selects.
  uint8_t PowerConfig() const;  ///< Power configuration in use: detected, or overridden.

  void SetGpioInput(int pin, bool level);             ///< Sets a GPIO input level.
  void SetVmtrInput(int input, uint8_t reading);      ///< Sets what a VMTR input measures.
  void SetLocalPower(bool on) { m_LocalPower = on; }  ///< Own supply on/off.
  bool LocalPower() const { return m_LocalPower; }    ///< Own supply is on.
  /// The supply its switch passes downstream (VBUS, e.g. 24 V) is there.
  bool DownstreamSupply() const { return m_DownstreamSupply; }
  /// On a LinkPower::kTwoStep link: the 24 V reaches it (the upstream switch is closed).
  bool HighVoltage() const { return m_HighVoltage; }
  /// On a LinkPower::kTwoStep link: how many times the 24 V was switched on to it.
  size_t HighVoltageCount() const { return m_HighVoltageCount; }

  Eeprom& AddEeprom(uint8_t i2cAddr,
                    size_t size = Eeprom::kDefaultSize);  ///< Adds an EEPROM behind it.
  void AttachDevice(uint8_t i2cAddr, I2cDevice& device);  ///< Adds a test's device behind it.
  I2cDevice* FindDevice(uint8_t i2cAddr) const;           ///< Device at an address, or nullptr.

 private:
  friend class Bus;

  static bool IsStructureReg(uint8_t low);      ///< Takes effect at NEWSTRCT.
  static bool IsAutoBroadcastReg(uint8_t low);  ///< A main-node write reaches the sub-nodes.
  void ApplyStructure();                        ///< NEWSTRCT: the written values take effect.
  void ResetRegisters();                        ///< Power-on register values.
  void SoftResetRegisters();                    ///< Register values after a soft reset.
  /// Register address on the current page.
  uint16_t Full(uint8_t low) const { return static_cast<uint16_t>((m_Page << 8) | low); }
  void Write(uint16_t reg, uint8_t value);               ///< Host write of one register.
  uint8_t Read(uint16_t reg) const { return Reg(reg); }  ///< Host read of one register.
  void WriteBurst(const uint8_t* data, size_t len);      ///< Register address, then values.
  void ReadBurst(uint8_t* data, size_t len);             ///< Reads from the pointer.

  ChipSpec m_Chip;
  ChipCaps m_Caps;
  LinkSpec m_Link;
  uint8_t m_Regs[2][256] = {};
  uint8_t m_Applied[256] = {};  ///< Page 0's structure registers as in use.
  uint8_t m_Page = 0;
  uint8_t m_Pointer = 0;  ///< Register pointer for reads without a register address.
  uint8_t m_GpioInputs = kDefaultGpioInputs;
  uint8_t m_Vmtr[reg::kVmtrInputs] = {};
  bool m_LocalPower = true;
  bool m_DownstreamSupply = true;
  bool m_HighVoltage = false;     ///< kTwoStep link: the 24 V reaches it.
  int m_FailAccesses = 0;         ///< Register accesses still to fail (FailNodeAccesses()).
  size_t m_HighVoltageCount = 0;  ///< kTwoStep link: times the 24 V came on.
  std::vector<std::unique_ptr<Eeprom>> m_Eeproms;
  std::vector<std::pair<uint8_t, I2cDevice*>> m_Devices;
};

/// One simulated A2B bus: a main node and a chain of plugged sub-nodes, powered on.
class Bus {
 public:
  Bus(Simulator& sim, uint8_t mainAddr, uint8_t busAddr,
      const ChipSpec& mainChip);  ///< Powered on, main node stopped.
  Bus(const Bus&) = delete;
  Bus& operator=(const Bus&) = delete;

  Node& Plug(const SubNodeSpec& spec);  ///< Plugs a sub-node in at the end of the chain.
  void Unplug(size_t position);         ///< Unplugs a sub-node and everything beyond it.
  void SetDownstreamSupply(size_t position, bool on);  ///< Its downstream supply (24 V) off or on.
  void ResetSub(size_t position);  ///< A sub-node resets (PLL unlock): it drops out.
  void RaiseInterrupt(size_t position, uint8_t type);  ///< A sub-node raises an interrupt.
  /// A node (-1: the main node) sees @p count bit errors of the types its BECCTL counts.
  void InjectBitErrors(int position, uint8_t count);
  /// The next @p count register accesses to a sub-node fail; it stays in the chain.
  void FailNodeAccesses(size_t position, int count);
  void PowerCycle();    ///< Power-cycles every node.
  void StartRunning();  ///< Main node left running by a previous session.

  void SetSync(bool present);                       ///< SYNC on the main node's pin.
  void SetPowerGood(bool on) { m_PowerGood = on; }  ///< Main node's power-good input.
  void SetHasResetLine(bool present) { m_HasResetLine = present; }  ///< Default: none.
  void SetHasIrqLine(bool present) { m_HasIrqLine = present; }      ///< Default: none.

  Node& Main() { return *m_Main; }                                  ///< The main node.
  Node& Sub(size_t position) { return *m_Subs[position]; }          ///< A plugged sub-node.
  size_t PluggedCount() const { return m_Subs.size(); }             ///< Sub-nodes plugged in.
  size_t DiscoveredCount() const { return m_Discovered; }           ///< Sub-nodes discovered.
  uint8_t MainAddr() const { return m_MainAddr; }                   ///< Main node's I2C address.
  uint8_t BusAddr() const { return m_BusAddr; }                     ///< Bus I2C address.
  bool PowerGood() const { return m_PowerGood; }                    ///< Main node's power-good.
  bool HasResetLine() const { return m_HasResetLine; }              ///< Has a reset line.
  bool HasIrqLine() const { return m_HasIrqLine; }                  ///< Has an IRQ line.
  bool Running() const { return m_Running; }                        ///< MSTR_RUNNING happened.
  bool Wedged() const { return m_Wedged; }                          ///< Stuck until power-cycled.
  bool InSustain() const;                                           ///< In SUSTAIN after a reset.
  bool DiscoveryActive() const { return m_Discovering; }            ///< DISCSTAT.DSCACT.
  size_t PendingInterrupts() const { return m_Interrupts.size(); }  ///< Waiting in INTTYPE.
  /// Transfers to this bus within 1 ms after a CONTROL.NEWSTRCT (the settle).
  size_t AccessesInNewStructSettle() const { return m_SettleAccesses; }
  size_t NewStructCount() const { return m_NewStructs; }  ///< CONTROL.NEWSTRCT writes.
  /** CONTROL writes that changed XCVRBINV on a node whose SWCTL.ENSW was set: the TRMs want
      XCVRBINV before ENSW, and kept by later CONTROL writes (scenario 10). */
  size_t OrderErrors() const { return m_OrderErrors; }
  AudioTrace TraceAudio() const;  ///< The audio its registers route.
  /// Calls @p observer right after each CONTROL.NEWSTRCT has taken effect, so a test can look
  /// at the audio at the moment a new structure starts (a mute is there to cover it).
  void SetNewStructObserver(std::function<void()> observer) {
    m_NewStructObserver = std::move(observer);
  }

 private:
  friend class Simulator;

  struct Interrupt {
    uint8_t source;  ///< INTSRC value.
    uint8_t type;    ///< INTTYPE value.
  };

  void Update();                    ///< Lets due events happen.
  void HoldInReset(bool asserted);  ///< Drives the reset line.
  Result MainAccess(const uint8_t* writeData, size_t writeLen, uint8_t* readData,
                    size_t readLen);  ///< Transfer to the main address.
  Result BusAccess(const uint8_t* writeData, size_t writeLen, uint8_t* readData,
                   size_t readLen);             ///< Transfer to the bus address.
  void WriteMain(uint16_t reg, uint8_t value);  ///< Main-node register write.
  uint8_t ReadMain(uint16_t reg);               ///< Main-node register read.
  void WriteControl(uint8_t value);             ///< Main-node CONTROL write.
  void SoftReset(bool mstr);                    ///< CONTROL.SOFTRST, with MSTR as written.
  void Wedge();                                 ///< The SUSTAIN trap.
  void StartDiscovery(uint8_t respCycles);      ///< DISCVRY write.
  bool CanDiscover() const;                     ///< Next sub-node's link is right.
  bool Powered(size_t position) const;          ///< A sub-node has power.
  bool FiveVolts(size_t position) const;        ///< kTwoStep: the upstream board's 5 V.
  bool Frames(size_t position) const;           ///< kTwoStep: frames reach it.
  void CheckOrder(const Node& node, uint8_t controlBefore);  ///< XCVRBINV changed under ENSW.
  static bool SwitchClosed(const Node& node);                ///< ENSW without SWBYP.
  void CheckPower();                                         ///< Drops sub-nodes that lost power.
  void DropFrom(size_t position, bool powerFault);           ///< Sub-nodes from here drop out.
  void LoseChain();                                          ///< Every sub-node undiscovered.
  void CheckSettle();                                        ///< Counts an access in the settle.

  Simulator& m_Sim;
  const uint8_t m_MainAddr;
  const uint8_t m_BusAddr;
  std::unique_ptr<Node> m_Main;
  std::vector<std::unique_ptr<Node>> m_Subs;
  size_t m_Discovered = 0;
  std::deque<Interrupt> m_Interrupts;
  bool m_Sync = true;
  bool m_PowerGood = true;
  bool m_HasResetLine = false;
  bool m_HasIrqLine = false;
  bool m_InReset = false;
  bool m_Running = false;
  bool m_Locking = false;
  uint32_t m_LockAt = 0;
  bool m_Sustaining = false;
  uint32_t m_SustainEnd = 0;
  bool m_Wedged = false;
  bool m_Discovering = false;
  bool m_DiscoveryPending = false;
  bool m_DiscoveryAddressed = false;  ///< NODEADR pointed at the upstream node at DISCVRY.
  uint8_t m_DiscoveryValue = 0;
  uint32_t m_DiscoveryDoneAt = 0;
  uint32_t m_AccessAt = 0;                    ///< First time an access is answered after RESETn.
  bool m_NewStruct = false;                   ///< A CONTROL.NEWSTRCT was written ...
  uint32_t m_NewStructAt = 0;                 ///< ... at this time.
  size_t m_SettleAccesses = 0;                ///< Transfers in the millisecond after it.
  size_t m_NewStructs = 0;                    ///< NEWSTRCT writes.
  size_t m_OrderErrors = 0;                   ///< See OrderErrors().
  std::function<void()> m_NewStructObserver;  ///< Called after each one took effect.
};

/// A Platform backed by simulated buses and devices on one host I2C bus.
class Simulator : public Platform {
 public:
  explicit Simulator(const Timing& timing = Timing());  ///< No buses yet; time 0.
  ~Simulator() override;
  Simulator(const Simulator&) = delete;
  Simulator& operator=(const Simulator&) = delete;

  Bus& AddBus(uint8_t mainAddr, uint8_t busAddr,
              const ChipSpec& mainChip = ChipSpec());         ///< Adds bus n (n = buses so far).
  Bus& GetBus(size_t index) { return *m_Buses[index]; }       ///< Bus by index.
  size_t BusCount() const { return m_Buses.size(); }          ///< Buses added.
  const Timing& GetTiming() const { return m_Timing; }        ///< Simulated delays.
  void AttachHostDevice(uint8_t i2cAddr, I2cDevice& device);  ///< Device on the host bus.

  void Advance(uint32_t ms);                          ///< Moves time on; due events happen.
  void SetNowMs(uint32_t nowMs) { m_NowMs = nowMs; }  ///< Sets the clock (nothing pending).
  /// Makes the transfer @p fromNow transfers ahead fail (0 = the next one).
  void FailTransfer(int fromNow) { m_FailAt = m_Seq + static_cast<uint32_t>(fromNow); }
  void FailAddress(uint8_t i2cAddr, int count);  ///< Fails the next transfers to an address.
  void FailRegisterWrite(uint8_t i2cAddr, uint8_t reg,
                         int count);  ///< Fails the next writes of a register at an address.

  std::string Trace() const;                               ///< Transfers as I2CT lines.
  size_t TransferCount() const { return m_Trace.size(); }  ///< Transfers recorded.
  void ClearTrace() { m_Trace.clear(); }                   ///< Forgets the transfers.
  const std::vector<std::string>& LogLines() const { return m_LogLines; }  ///< Stack's log.
  int UnlockedTransfers() const { return m_UnlockedTransfers; }  ///< Transfers without Lock().

  // Platform.
  Result I2cWrite(uint8_t addr, const uint8_t* data, size_t len) override;
  Result I2cRead(uint8_t addr, uint8_t* data, size_t len) override;
  Result I2cWriteRead(uint8_t addr, const uint8_t* writeData, size_t writeLen, uint8_t* readData,
                      size_t readLen) override;
  uint32_t NowMs() override { return m_NowMs; }
  Result SetMainNodeReset(uint8_t bus, bool asserted) override;
  bool IrqPending(uint8_t bus) override;
  bool PowerGood(uint8_t bus) override;
  void Log(LogLevel level, const char* text) override;
  void Lock() override { ++m_LockDepth; }
  void Unlock() override { --m_LockDepth; }

 private:
  enum class Op : uint8_t { kWrite, kRead, kWriteRead };

  struct TraceEntry {
    uint32_t seq;
    uint32_t timeMs;
    uint8_t addr;
    Op op;
    bool failed;
    std::vector<uint8_t> written;
    std::vector<uint8_t> read;
  };

  Result Transfer(Op op, uint8_t addr, const uint8_t* writeData, size_t writeLen, uint8_t* readData,
                  size_t readLen);  ///< Records, fails or routes one transfer.
  Result Route(uint8_t addr, const uint8_t* writeData, size_t writeLen, uint8_t* readData,
               size_t readLen);  ///< Hands a transfer to its target.
  bool InjectFailure(uint8_t addr, const uint8_t* writeData, size_t writeLen,
                     size_t readLen);  ///< This transfer is to fail.

  Timing m_Timing;
  uint32_t m_NowMs = 0;
  std::vector<std::unique_ptr<Bus>> m_Buses;
  std::vector<std::pair<uint8_t, I2cDevice*>> m_HostDevices;
  std::vector<TraceEntry> m_Trace;
  uint32_t m_Seq = 0;
  uint32_t m_FailAt = UINT32_MAX;
  uint8_t m_FailAddr = 0;
  int m_FailAddrCount = 0;
  uint8_t m_FailRegAddr = 0;
  uint8_t m_FailReg = 0;
  int m_FailRegCount = 0;
  std::vector<std::string> m_LogLines;
  int m_LockDepth = 0;
  int m_UnlockedTransfers = 0;
};

}  // namespace sim
}  // namespace a2b

#endif  // A2BCPP_SIM_SIMULATOR_H_
