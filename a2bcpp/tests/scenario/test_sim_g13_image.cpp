/**
 * @file test_sim_g13_image.cpp
 * @brief The G13 register image: Network brings up the Pi's main node from its
 *        SigmaStudio+ export (adi/exports/rp2040/12_a2b_busconfig.c, linked in), discovers one
 *        sub-node on the simulator and probes for a second, and the main node's and the
 *        sub-node's registers are checked against the G13 golden
 *        (tests/golden/G13_pi_run1.txt).
 *
 * The golden image is taken when ADI's stack had configured sub-node 0, routed it,
 * and ended its first probe for sub-node 1 through sub-node 0 (golden events 0-352:
 * CONTROL.ENDDSC and the switches restored). Each table entry is the last value
 * ADI's stack wrote to that register. Registers it didn't write must hold their
 * power-on values here too. The bench had four sub-nodes, and that first probe found
 * sub-node 1 and lost it again in ADI's Pi build's response-cycle search; here nothing is
 * plugged in behind sub-node 0, and only the main node and sub-node 0 are compared.
 *
 * G13 ran with the EEPROM ignored. ADI's stack then configured sub-node 0 from the
 * export's sub-node settings (GPIOIEN 0x7F, PINCFG 0x00); here the same settings come from
 * the app's module-info source, as "ignore EEPROM" asks (decisions.md).
 *
 * ADI's Pi app routed 12 channels from the main node to sub-node 0 (main DNSLOTS 12,
 * sub-node 0 LDNSLOTS 0x80 and DNMASK 0xFFF). Here the same is a connection from the main
 * node's RX group 0 (channels 0-11) to sub-node 0's TX group 0 (channels 0-11), routed by
 * the stack's plan, and the slot registers match; the response cycles and the
 * crossbars differ on purpose (the list below).
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdio>
#include <initializer_list>
#include <vector>

#include "a2b/network.h"
#include "detail/regdefs.h"
#include "sim/module_info.h"
#include "sim/simulator.h"
#include "ss_plus_import.h"
#include "test_harness.h"

extern "C" {
extern ADI_A2B_BCD sBusDescription;  // From the export.
}

namespace a2b {
namespace {

struct RegValue {
  uint16_t reg;
  uint8_t value;
};

/** TDM channels 0-11: the 12 slots ADI's Pi app routed. */
constexpr uint32_t kTwelveChannels = 0x0FFF;

// Generated from G13_pi_run1.txt with trace_decode.py (its load() and Event list): writes
// to M and S0 in events 0-352, the last value of each, by register.
constexpr RegValue kG13Main[] = {
    {0x001, 0x00}, {0x009, 0x11}, {0x00D, 0x0C}, {0x00E, 0x00}, {0x00F, 0xA0}, {0x010, 0x44},
    {0x011, 0x03}, {0x012, 0x92}, {0x013, 0x9C}, {0x01A, 0x01}, {0x01B, 0x10}, {0x01C, 0x00},
    {0x01D, 0x09}, {0x01E, 0xEF}, {0x02E, 0x00}, {0x03F, 0x00}, {0x040, 0x00}, {0x041, 0xE2},
    {0x042, 0x21}, {0x043, 0x00}, {0x044, 0x00}, {0x045, 0x00}, {0x046, 0x00}, {0x047, 0x00},
    {0x048, 0x00}, {0x04A, 0x80}, {0x04D, 0x80}, {0x04E, 0x00}, {0x050, 0x00}, {0x051, 0x00},
    {0x052, 0x01}, {0x057, 0x00}, {0x059, 0x00}, {0x05A, 0x81}, {0x05D, 0x00}, {0x07C, 0x00},
    {0x07D, 0x00}, {0x07E, 0x00}, {0x07F, 0x00}, {0x080, 0x00}, {0x081, 0x00}, {0x082, 0x00},
    {0x083, 0x00}, {0x084, 0x00}, {0x085, 0x00}, {0x086, 0x00}, {0x087, 0x00}, {0x088, 0x00},
    {0x089, 0x00}, {0x08A, 0x00}, {0x0B0, 0x00}, {0x0B2, 0x00}, {0x0B3, 0x00}, {0x0B4, 0x00},
    {0x0B5, 0x10}, {0x0B6, 0x00}, {0x0B7, 0x00}, {0x0B8, 0xFF}, {0x0B9, 0xFF}, {0x0BA, 0xFF},
    {0x0BB, 0xFF}, {0x0BC, 0xFF}, {0x0BD, 0xFF}, {0x0BE, 0xFF}, {0x0BF, 0xFF}, {0x0C0, 0x00},
    {0x0C1, 0x01}, {0x0C2, 0x02}, {0x0C3, 0x03}, {0x0C4, 0x04}, {0x0C5, 0x05}, {0x0C6, 0x06},
    {0x0C7, 0x07}, {0x0C8, 0x08}, {0x0C9, 0x09}, {0x0CA, 0x0A}, {0x0CB, 0x0B}, {0x0CC, 0x0C},
    {0x0CD, 0x0D}, {0x0CE, 0x0E}, {0x0CF, 0x0F}, {0x0D0, 0x10}, {0x0D1, 0x11}, {0x0D2, 0x12},
    {0x0D3, 0x13}, {0x0D4, 0x14}, {0x0D5, 0x15}, {0x0D6, 0x16}, {0x0D7, 0x17}, {0x0D8, 0x18},
    {0x0D9, 0x19}, {0x0DA, 0x1A}, {0x0DB, 0x1B}, {0x0DC, 0x1C}, {0x0DD, 0x1D}, {0x0DE, 0x1E},
    {0x0DF, 0x1F}, {0x0E0, 0x00}, {0x100, 0x00}, {0x101, 0x00}, {0x102, 0x00}, {0x103, 0x00},
    {0x121, 0xFF}, {0x122, 0x00}, {0x124, 0xFF}, {0x125, 0x00}, {0x127, 0xFF}, {0x128, 0x00},
    {0x12A, 0xFF}, {0x12B, 0x00}, {0x12D, 0xFF}, {0x12E, 0x00}, {0x130, 0x00}, {0x131, 0x00},
    {0x133, 0xFF}, {0x134, 0x00}, {0x140, 0x00}, {0x141, 0x00}, {0x142, 0x00}, {0x143, 0x00},
    {0x148, 0x00}, {0x149, 0x00}, {0x14A, 0x00}, {0x14B, 0x00}, {0x14C, 0x00}, {0x14D, 0x00},
    {0x14E, 0x00}, {0x14F, 0x00},
};
constexpr RegValue kG13Sub0[] = {
    {0x009, 0x10}, {0x00B, 0x80}, {0x00C, 0x00}, {0x00D, 0x00}, {0x00E, 0x00}, {0x00F, 0xA0},
    {0x012, 0x10}, {0x01B, 0x10}, {0x01C, 0x00}, {0x04E, 0x7F}, {0x052, 0x00}, {0x060, 0x00},
    {0x061, 0x00}, {0x062, 0x00}, {0x063, 0x00}, {0x064, 0x00}, {0x065, 0xFF}, {0x066, 0x0F},
    {0x067, 0x00}, {0x068, 0x00}, {0x069, 0x00}, {0x07C, 0x00}, {0x07D, 0x00}, {0x07E, 0x00},
    {0x07F, 0x00}, {0x0B8, 0xFF}, {0x0B9, 0xFF}, {0x0BA, 0xFF}, {0x0BB, 0xFF}, {0x0BC, 0xFF},
    {0x0BD, 0xFF}, {0x0BE, 0xFF}, {0x0BF, 0xFF}, {0x0C0, 0x00}, {0x0C1, 0x01}, {0x0C2, 0x02},
    {0x0C3, 0x03}, {0x0C4, 0x04}, {0x0C5, 0x05}, {0x0C6, 0x06}, {0x0C7, 0x07}, {0x0C8, 0x08},
    {0x0C9, 0x09}, {0x0CA, 0x0A}, {0x0CB, 0x0B}, {0x0CC, 0x0C}, {0x0CD, 0x0D}, {0x0CE, 0x0E},
    {0x0CF, 0x0F}, {0x0D0, 0x10}, {0x0D1, 0x11}, {0x0D2, 0x12}, {0x0D3, 0x13}, {0x0D4, 0x14},
    {0x0D5, 0x15}, {0x0D6, 0x16}, {0x0D7, 0x17}, {0x0D8, 0x18}, {0x0D9, 0x19}, {0x0DA, 0x1A},
    {0x0DB, 0x1B}, {0x0DC, 0x1C}, {0x0DD, 0x1D}, {0x0DE, 0x1E}, {0x0DF, 0x1F}, {0x0E0, 0x00},
    {0x100, 0x02},
};

/** Where this stack differs from the golden on purpose, and why: registers first to last. */
struct Difference {
  bool subNode;     ///< False: the main node.
  uint16_t first;   ///< The first register.
  uint16_t last;    ///< The last.
  const char* why;  ///< The reason, and the phase that closes it.
};

constexpr Difference kIntendedDifferences[] = {
    {false, reg::kRespcycs, reg::kRespcycs,
     "0xA0 from ADI's calculation; 0xA2 from the TRMs' Formula B"},
    {false, reg::kDiscvry, reg::kDiscvry,
     "0x9C: ADI's routing's RESPCYCS 0xA0 less 4; here position 1's TRM value from the "
     "image's 0xB9 (0xB4), as the plan is written once the chain is up"},
    {true, reg::kRespcycs, reg::kRespcycs, "0xA0 from ADI's calculation; 0xA2 here"},
    {false, reg::kRxmask0 + 1, reg::kRxmask0 + 7,
     "RXMASK1-7: the plan takes the channels sent (0-11); ADI's stack kept all 64"},
    {false, reg::kTxxbar0, reg::kTxxbar0 + 30,
     "TXXBAR0-30: the main node plays nothing, so its channels point at location 31; the "
     "ADI's stack left them at their power-on values"},
    {true, reg::kRxmask0, reg::kRxmask0 + 7,
     "RXMASK0-7: sub-node 0 sends nothing, so it takes no channel; ADI's stack left "
     "them at their power-on values"},
    {true, reg::kTxxbar0 + 12, reg::kTxxbar0 + 30,
     "TXXBAR12-30: channels outside its group point at location 31"},
    {true, reg::kVmtrVen, reg::kVmtrVen,
     "0x02: ADI's AD2437 24 V check (two-step links are not supported yet)"},
};

/** Registers whose value is state, not configuration: not compared. */
bool IsVolatile(uint16_t reg) {
  switch (reg) {
    case reg::kNodeadr:  // Addressing; Transport's cache decides the last value.
    case reg::kMmrpage:  // Likewise.
    case reg::kIntstat:  // Interrupt state.
    case reg::kIntsrc:
    case reg::kInttype:
    case reg::kIntpnd0:  // Write 1 to clear.
    case reg::kIntpnd1:
    case reg::kIntpnd2:
    case reg::kNode:      // Set by discovery.
    case reg::kDiscstat:  // Discovery state.
      return true;
    default:
      return false;
  }
}

/** True if register @p reg of the main node, or of a sub-node (@p subNode), is one of
    kIntendedDifferences. */
bool IsIntended(bool subNode, uint16_t reg) {
  for (const Difference& difference : kIntendedDifferences) {
    if (difference.subNode == subNode && reg >= difference.first && reg <= difference.last) {
      return true;
    }
  }
  return false;
}

/** The module-info source of the G13 bench: the export's sub-node settings, and a TX group
    of channels 0-11 for ADI's Pi app's routing. */
class G13Source : public ModuleInfoSource {
 public:
  G13Source() {
    sim::ModuleInfoHeader header;
    header.powerConfig = sim::kPowerConfigHighPower;  // CFG-4, as the bench modules detect.
    sim::ModuleInfoRegs regs;
    regs.gpioien = 0x7F;
    regs.pincfg = 0x00;
    m_Image = sim::MakeModuleInfoImage(header, regs);
    ChannelGroups groups;
    groups.AddTx(0, kTwelveChannels);
    sim::AddChannelGroups(m_Image, groups);
  }
  size_t GetModuleInfo(const SubNodeIdentity& node, uint8_t* buf, size_t bufLen) override {
    (void)node;
    const size_t len = (kModuleInfoLen < bufLen) ? kModuleInfoLen : bufLen;
    for (size_t i = 0; i < len; ++i) {
      buf[i] = m_Image[i];
    }
    return len;
  }
  Result ReadModuleInfo(const SubNodeIdentity& node, uint32_t offset, uint8_t* buf,
                        size_t len) override {
    (void)node;
    if (offset + len > m_Image.size()) {
      return Result::kInvalidArgument;
    }
    for (size_t i = 0; i < len; ++i) {
      buf[i] = m_Image[offset + i];
    }
    return Result::kOk;
  }

 private:
  std::vector<uint8_t> m_Image;
};

/** The golden's last write to @p reg, or -1 if it wrote none. */
template <size_t N>
int GoldenValue(const RegValue (&golden)[N], uint16_t reg) {
  int value = -1;
  for (const RegValue& entry : golden) {
    if (entry.reg == reg) {
      value = entry.value;
    }
  }
  return value;
}

/**
 * Compares a node's registers with the golden: every register on pages 0 and 1 holds the
 * golden's last write, or the power-on value if the golden didn't write it. A sub-node's
 * SLOTFMT, DATCTL and I2SRRATE are the exception: the main node broadcasts its writes of
 * them to the sub-nodes discovered by then (the TRMs' "Auto-Broadcast"), which the trace
 * of the host's writes doesn't show, so they hold the main node's last write.
 * @return The number of registers that differ, intended differences aside.
 */
template <size_t N>
int Compare(const char* name, bool subNode, const sim::Node& node, const sim::ChipSpec& chip,
            const RegValue (&golden)[N]) {
  const sim::Node fresh(chip, sim::LinkSpec());
  int differences = 0;
  for (uint16_t reg = 0; reg < 0x200; ++reg) {
    if (IsVolatile(reg & 0xFF) || IsIntended(subNode, reg)) {
      continue;
    }
    int written = GoldenValue(golden, reg);
    if (subNode && written < 0 &&
        (reg == reg::kSlotfmt || reg == reg::kDatctl || reg == reg::kI2srrate)) {
      written = GoldenValue(kG13Main, reg);  // -1 if the main node's is at its power-on value.
    }
    int expected = (written >= 0) ? written : fresh.Reg(reg);
    if (reg == reg::kControl) {  // NEWSTRCT, ENDDSC and SOFTRST clear themselves.
      expected &= ~(reg::kControlNewstrct | reg::kControlEnddsc | reg::kControlSoftrst);
    }
    if (node.Reg(reg) != expected) {
      std::printf("  %s register 0x%03X: 0x%02X, golden 0x%02X\n", name, reg, node.Reg(reg),
                  expected);
      ++differences;
    }
  }
  return differences;
}

/** ADI's Pi app routed 12 channels to a sub-node whose settings (the export's) leave
    its I2S port off, so the golden has I2SCFG 0 there. The stack refuses that
    (RoutingError::kNoSuchChannel); this policy says the sub-node's port gives them, as an
    app that sets its ports up itself would, so the image is still compared whole. */
class G13PortPolicy : public RoutingPolicy {
 public:
  ChannelMap MapGroups(const PortSettings& port) const override {
    ChannelMap map = ComputeChannelMap(port);
    if (port.node != kMainNode) {
      map.txChannels = 32;
    }
    return map;
  }
};

/** The G13 bench: the Pi's main node left running, one AD2437 sub-node behind an
    inverted link, as on the bench. Both chips detect CFG-4 in LVI mode (SWSTAT2 0xA0 in
    the golden), which the export and the module info expect: nothing is overridden. */
struct G13Bench {
  sim::ChipSpec mainChip;
  sim::ChipSpec subChip;
  sim::Simulator sim;
  sim::Bus* bus = nullptr;
  BoardConfig board;
  FixedOpList<4> hostScript;
  G13Source source;
  G13PortPolicy policy;

  G13Bench() {
    const uint8_t mainId[6] = {0x8E, 0x66, 0x56, 0x94, 0x25, 0x46};
    const uint8_t subId[6] = {0x18, 0xAC, 0xEE, 0x04, 0x23, 0x16};
    for (int i = 0; i < 6; ++i) {
      mainChip.chipId[i] = mainId[i];
      subChip.chipId[i] = subId[i];
    }
    for (sim::ChipSpec* chip : {&mainChip, &subChip}) {
      chip->powerConfig = sim::kPowerConfigHighPower;
      chip->lowVoltageInput = true;
    }
    bus = &sim.AddBus(0x68, 0x69, mainChip);
    bus->StartRunning();  // The Pi has no RESETn pulse at start-up.
    sim::SubNodeSpec spec;
    spec.chip = subChip;
    spec.link.bSideInverted = true;
    bus->Plug(spec);
    A2B_CHECK(ImportSsPlus(sBusDescription, 0, board, hostScript) == Result::kOk);
    board.routing.groups.AddRx(0, kTwelveChannels);
    board.routing.policy = &policy;
  }

  /** Starts the bus with the connection ADI's Pi app routed, until the plan is written. */
  void Run(Network& network) {
    network.SetModuleInfoSource(&source);
    // Every link the export's (XCVRBINV), as ADI's stack set them, not the image's B port.
    for (int position = 0; position < kMaxSubNodes; ++position) {
      A2B_CHECK(network.SetLinkProfile(static_cast<NodeAddr>(position), board.defaultLink) ==
                Result::kOk);
    }
    A2B_CHECK(network.Start() == Result::kOk);
    A2B_CHECK(network.ApplyRouting() == RoutingError::kNone);  // Routed once the chain is up.
    for (int i = 0; i < 2000 && !network.Routing().valid; ++i) {
      sim.Advance(1);
      network.Poll(sim.NowMs());
      if (network.SubNodeCount() == 1 && network.ConnectionCount() == 0) {
        A2B_CHECK(network.AddConnection(kMainNode, 0, 0, 0) == Result::kOk);
        A2B_CHECK(network.ApplyRouting() == RoutingError::kNone);
      }
    }
  }
};

A2B_TEST(RegisterImageMatchesG13) {
  G13Bench bench;
  Network network(bench.sim, bench.board);
  bench.Run(network);
  A2B_CHECK(network.State() == NetworkState::kRunning);
  A2B_CHECK(network.Routing().valid);
  A2B_CHECK_EQ(network.SubNodeCount(), 1);
  A2B_CHECK(!bench.bus->Wedged());
  A2B_CHECK_EQ(Compare("main node", false, bench.bus->Main(), bench.mainChip, kG13Main), 0);
  A2B_CHECK_EQ(Compare("sub-node 0", true, bench.bus->Sub(0), bench.subChip, kG13Sub0), 0);
  const SubNodeInfo* node = network.SubNodeInfoAt(0);
  A2B_CHECK(node != nullptr && node->identity.chipId[0] == 0x18);
}

A2B_TEST(IntendedDifferencesAreRealDifferences) {
  // Each listed difference is a register the golden set to something other than what
  // this stack leaves there, so the list can't hide a match.
  G13Bench bench;
  Network network(bench.sim, bench.board);
  bench.Run(network);
  for (const Difference& difference : kIntendedDifferences) {
    const sim::Node& node = difference.subNode ? bench.bus->Sub(0) : bench.bus->Main();
    for (uint16_t reg = difference.first; reg <= difference.last; ++reg) {
      const int golden =
          difference.subNode ? GoldenValue(kG13Sub0, reg) : GoldenValue(kG13Main, reg);
      A2B_CHECK(golden >= 0);
      if (golden == node.Reg(reg)) {
        std::printf("  not a difference at 0x%03X: %s\n", reg, difference.why);
        A2B_CHECK(false);
      }
    }
  }
}

}  // namespace
}  // namespace a2b

A2B_TEST_MAIN()
