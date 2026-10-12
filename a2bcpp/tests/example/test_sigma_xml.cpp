/**
 * @file test_sigma_xml.cpp
 * @brief The bench console's SigmaStudio XML reader (examples/bench/sigma_xml.cpp) on the
 *        AB0020's two ADAU1761 programs (main node and sub-node), on SigmaStudio's other
 *        layout, and on bad input.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstdio>
#include <string>
#include <vector>

#include "sigma_xml.h"
#include "test_harness.h"

namespace {

/** The bench's SigmaStudio project folder. */
const std::string kFolder = A2B_REPO_ROOT "/tools/SS+reference/2x_AB0020_EVM_with_2437/";

/** Parses @p text; gives the error, or "" if it was read. */
std::string ErrorOf(const std::string& text) {
  SigmaProgram program;
  std::string error;
  return ParseSigmaXml(text, 0, program, error) ? std::string() : error;
}

/** True if @p actual is @p expected; prints both if not. */
bool Is(const std::string& actual, const std::string& expected) {
  if (actual != expected) {
    std::printf("  got \"%s\", expected \"%s\"\n", actual.c_str(), expected.c_str());
    return false;
  }
  return true;
}

/** An action element with @p attributes and @p content. */
std::string Action(const std::string& attributes, const std::string& content) {
  return "<ROM>\n<action " + attributes + ">" + content + "</action>\n</ROM>\n";
}

A2B_TEST(SubNodeProgram) {
  // The AB0020's sub-node program, in SigmaStudio's layout without i2caddr or addr_width:
  // refused without a default address; with the ADAU1761's 0x39, 38 writes (6076 bytes with
  // their sub-addresses, the largest 1732), the 100 ms wait fifth, and the read of the PLL
  // register skipped.
  const std::string file = kFolder + "AB0020-sub-node-ADAU1761.xml";
  SigmaProgram program;
  std::string error;
  A2B_CHECK(!ReadSigmaXml(file, 0, program, error));
  A2B_CHECK(error.find("line 6: no 7-bit i2caddr") != std::string::npos);
  A2B_CHECK(ReadSigmaXml(file, 0x39, program, error));
  A2B_CHECK_EQ(program.readsSkipped, 1u);
  const std::vector<SigmaAction>& actions = program.actions;
  A2B_CHECK_EQ(actions.size(), 39u);
  size_t writes = 0;
  size_t bytes = 0;
  size_t largest = 0;
  for (const SigmaAction& action : actions) {
    if (!action.wait) {
      ++writes;
      bytes += action.bytes.size();
      largest = (action.bytes.size() > largest) ? action.bytes.size() : largest;
      A2B_CHECK_EQ(action.i2cAddr, 0x39);
    }
  }
  A2B_CHECK_EQ(writes, 38u);
  A2B_CHECK_EQ(bytes, 6076u);
  A2B_CHECK_EQ(largest, 1732u);
  A2B_CHECK(actions.size() > 4 && actions[4].wait && actions[4].waitMs == 100);
  // The first write: sub-address 0x40EB (addr="16619", len 3: two address bytes) then 7F.
  const std::vector<uint8_t> first = {0x40, 0xEB, 0x7F};
  A2B_CHECK(actions[0].bytes == first);
}

A2B_TEST(MainNodeProgramIsAnother) {
  // The main node's program: the same layout, another program (its serial port is the
  // master; the sub-node's, R15 = 00 00, is the slave).
  SigmaProgram mainProgram;
  SigmaProgram subProgram;
  std::string error;
  A2B_CHECK(ReadSigmaXml(kFolder + "AB0020-main-node-adau1761.xml", 0x39, mainProgram, error));
  A2B_CHECK(ReadSigmaXml(kFolder + "AB0020-sub-node-ADAU1761.xml", 0x39, subProgram, error));
  const std::vector<SigmaAction>& main = mainProgram.actions;
  const std::vector<SigmaAction>& sub = subProgram.actions;
  A2B_CHECK_EQ(main.size(), sub.size());
  const std::vector<uint8_t> masterPort = {0x40, 0x15, 0x01, 0x00};
  const std::vector<uint8_t> slavePort = {0x40, 0x15, 0x00, 0x00};
  bool mainMaster = false;
  bool subSlave = false;
  for (size_t i = 0; i < main.size() && i < sub.size(); ++i) {
    mainMaster = mainMaster || main[i].bytes == masterPort;
    subSlave = subSlave || sub[i].bytes == slavePort;
  }
  A2B_CHECK(mainMaster && subSlave);
}

A2B_TEST(LayoutWithAddresses) {
  // SigmaStudio's other layout: i2caddr and addr_width on each write.
  SigmaProgram program;
  std::string error;
  A2B_CHECK(
      ParseSigmaXml("<ROM>\n<action instr=\"writeXbytes\" addr_width=\"2\" len=\"4\" "
                    "addr=\"16405\" i2caddr=\"56\">10 00</action>\n"
                    "<action instr=\"delay\">00 64</action>\n</ROM>\n",
                    0, program, error));
  A2B_CHECK_EQ(program.actions.size(), 2u);
  const std::vector<uint8_t> write = {0x40, 0x15, 0x10, 0x00};
  A2B_CHECK(program.actions[0].i2cAddr == 0x38 && program.actions[0].bytes == write);
  A2B_CHECK(program.actions[1].wait && program.actions[1].waitMs == 100);
}

A2B_TEST(Refusals) {
  const std::string write = "instr=\"writeXbytes\" addr_width=\"2\" len=\"3\" addr=\"16619\" ";
  A2B_CHECK(Is(ErrorOf(Action(write + "i2caddr=\"57\"", "7F")), ""));
  A2B_CHECK(Is(ErrorOf("<ROM></ROM>"), "no <action> elements"));
  A2B_CHECK(Is(ErrorOf(Action(write + "i2caddr=\"57\"", "7F 00")),
               "line 2: len is not addr_width plus the data bytes"));
  A2B_CHECK(Is(ErrorOf(Action(write + "i2caddr=\"128\"", "7F")), "line 2: no 7-bit i2caddr"));
  A2B_CHECK(
      Is(ErrorOf(Action(write + "i2caddr=\"57\"", "7G")), "line 2: content that isn't hex bytes"));
  A2B_CHECK(Is(
      ErrorOf(Action("instr=\"writeXbytes\" addr_width=\"1\" addr=\"256\" i2caddr=\"57\"", "00")),
      "line 2: no addr that fits its 0-4 bytes"));
  A2B_CHECK(Is(ErrorOf(Action("instr=\"writeXbytes\" addr=\"0\" i2caddr=\"57\"", "00")),
               "line 2: neither addr_width nor len, so no sub-address size"));
  A2B_CHECK(Is(ErrorOf(Action("instr=\"read\"", "")), "no <action> elements"));  // Skipped.
  A2B_CHECK(Is(ErrorOf(Action("instr=\"readXbytes\"", "")),
               "line 2: instr \"readXbytes\", which the console doesn't run"));
  A2B_CHECK(Is(ErrorOf(Action("instr=\"delay\"", "10 00")),
               "line 2: a delay that isn't 0-4095 ms in 1 or 2 bytes"));
  A2B_CHECK(Is(ErrorOf("<ROM>\n<action instr=\"delay\">00 64"), "line 2: an unclosed <action>"));
  std::string big = "00";
  for (size_t i = 1; i < kMaxSigmaWrite - 2; ++i) {
    big += " 00";
  }
  const std::string longWrite = "instr=\"writeXbytes\" addr_width=\"2\" addr=\"0\" i2caddr=\"57\"";
  A2B_CHECK(Is(ErrorOf(Action(longWrite, big)), ""));  // 4094 bytes with the sub-address.
  A2B_CHECK(Is(ErrorOf(Action(longWrite, big + " 00")), "line 2: a write longer than 4094 bytes"));
  SigmaProgram program;
  std::string error;
  A2B_CHECK(!ReadSigmaXml("/no/such/file.xml", 0, program, error));
  A2B_CHECK(Is(error, "can't open /no/such/file.xml"));
}

}  // namespace

A2B_TEST_MAIN()
