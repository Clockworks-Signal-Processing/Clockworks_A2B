/**
 * @file test_ss_plus_file.cpp
 * @brief SsPlusFile: the repository's SigmaStudio+ exports read at run time give the same
 *        structures, and the same ImportSsPlus() result, as the compiler gives them (so
 *        the loader's tables match the header both use, tools/SS+reference/adi); the C the
 *        loader takes, what it skips and what it refuses.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#include "a2b/board_config.h"
#include "a2b/op_list.h"
#include "ss_plus_file.h"
#include "ss_plus_import.h"
#include "test_harness.h"

extern "C" {
extern ADI_A2B_BCD a2b_export_rp2040;   ///< adi/exports/rp2040/12_a2b_busconfig.c, compiled.
extern ADI_A2B_BCD a2b_export_windows;  ///< adi/exports/windows/12_a2b_busconfig.c, compiled.
/// tools/SS+reference/2x_AB0020_EVM_with_2437/adi_a2b_busconfig.c, compiled.
extern ADI_A2B_BCD a2b_export_ab0020;
/// tools/SS+reference/AB0310_AB0312_EVM_with_2437/adi_a2b_busconfig.c, compiled.
extern ADI_A2B_BCD a2b_export_ab0310;
}

namespace {

/** True if the bytes of @p a and @p b from @p from up to @p to are equal. */
template <typename T>
bool SameBytes(const T& a, const T& b, size_t from, size_t to) {
  return std::memcmp(reinterpret_cast<const char*>(&a) + from,
                     reinterpret_cast<const char*>(&b) + from, to - from) == 0;
}

/** Checks a loaded export against the compiled one, structure by structure. */
void CheckSameStructures(const ADI_A2B_BCD& loaded, const ADI_A2B_BCD& compiled) {
  A2B_CHECK_EQ(loaded.nNumMasterNode, compiled.nNumMasterNode);
  const ADI_A2B_NETWORK_CONFIG& lt = loaded.sTargetProperties;
  const ADI_A2B_NETWORK_CONFIG& ct = compiled.sTargetProperties;
  // Everything before the peripheral pointers.
  A2B_CHECK(SameBytes(lt, ct, 0, offsetof(ADI_A2B_NETWORK_CONFIG, apPeriConfig)));
  for (size_t i = 0; i < ADI_A2B_MAX_DEVICES_PER_NODE; ++i) {
    const A2B_PERIPHERAL_DEVICE_CONFIG* lp = lt.apPeriConfig[i];
    const A2B_PERIPHERAL_DEVICE_CONFIG* cp = ct.apPeriConfig[i];
    A2B_CHECK((lp == nullptr) == (cp == nullptr));
    if (lp == nullptr || cp == nullptr) {
      continue;
    }
    A2B_CHECK(SameBytes(*lp, *cp, 0, offsetof(A2B_PERIPHERAL_DEVICE_CONFIG, paPeriConfigUnit)));
    A2B_CHECK_EQ(lp->nNumPeriConfigUnit, cp->nNumPeriConfigUnit);
    for (size_t u = 0; u < cp->nNumPeriConfigUnit; ++u) {
      const ADI_A2B_PERI_CONFIG_UNIT& lu = lp->paPeriConfigUnit[u];
      const ADI_A2B_PERI_CONFIG_UNIT& cu = cp->paPeriConfigUnit[u];
      A2B_CHECK(SameBytes(lu, cu, 0, offsetof(ADI_A2B_PERI_CONFIG_UNIT, paConfigData)));
      A2B_CHECK_EQ(lu.nAddrIncrement, cu.nAddrIncrement);
      A2B_CHECK((lu.paConfigData == nullptr) == (cu.paConfigData == nullptr));
      if (lu.paConfigData != nullptr && cu.paConfigData != nullptr) {
        A2B_CHECK(std::memcmp(lu.paConfigData, cu.paConfigData, cu.nDataCount) == 0);
      }
    }
  }
  const ADI_A2B_MASTER_SLAVE_CONFIG& lc = *loaded.apNetworkconfig[0];
  const ADI_A2B_MASTER_SLAVE_CONFIG& cc = *compiled.apNetworkconfig[0];
  A2B_CHECK_EQ(lc.nNumSlaveNode, cc.nNumSlaveNode);
  A2B_CHECK(std::memcmp(&lc.sCommonSetting, &cc.sCommonSetting, sizeof(cc.sCommonSetting)) == 0);
  // The main node, apart from its stream pointers (sStreamSettings).
  const ADI_A2B_MASTER_NCD& lm = *lc.pMasterConfig;
  const ADI_A2B_MASTER_NCD& cm = *cc.pMasterConfig;
  A2B_CHECK(SameBytes(lm, cm, 0, offsetof(ADI_A2B_MASTER_NCD, sStreamSettings)));
  A2B_CHECK(SameBytes(lm, cm, offsetof(ADI_A2B_MASTER_NCD, oSpiSettings), sizeof(cm)));
}

/** Checks two imports of the same export for the same BoardConfig and host script. */
void CheckSameImport(const ADI_A2B_BCD& loaded, const ADI_A2B_BCD& compiled) {
  a2b::BoardConfig lb;
  a2b::BoardConfig cb;
  a2b::FixedOpList<64> ls;
  a2b::FixedOpList<64> cs;
  A2B_CHECK(a2b::ImportSsPlus(loaded, 0, lb, ls) == a2b::Result::kOk);
  A2B_CHECK(a2b::ImportSsPlus(compiled, 0, cb, cs) == a2b::Result::kOk);
  A2B_CHECK_EQ(lb.mainAddr, cb.mainAddr);
  A2B_CHECK_EQ(lb.busAddr, cb.busAddr);
  A2B_CHECK_EQ(lb.mainProduct, cb.mainProduct);
  A2B_CHECK_EQ(lb.mainLocalPower, cb.mainLocalPower);
  A2B_CHECK_EQ(lb.highPowerSwitchCfg, cb.highPowerSwitchCfg);
  A2B_CHECK_EQ(lb.defaultLink.bSideInverted, cb.defaultLink.bSideInverted);
  A2B_CHECK_EQ(lb.timings.discoveryStartDelayMs, cb.timings.discoveryStartDelayMs);
  A2B_CHECK_EQ(lb.timings.rediscoveryWaitMs, cb.timings.rediscoveryWaitMs);
  A2B_CHECK_EQ(lb.mainNode.Size(), cb.mainNode.Size());
  for (size_t i = 0; i < cb.mainNode.Size() && i < lb.mainNode.Size(); ++i) {
    A2B_CHECK_EQ(lb.mainNode.RegAt(i), cb.mainNode.RegAt(i));
    A2B_CHECK_EQ(lb.mainNode.ValueAt(i), cb.mainNode.ValueAt(i));
  }
  A2B_CHECK_EQ(ls.Size(), cs.Size());
  for (size_t i = 0; i < cs.Size() && i < ls.Size(); ++i) {
    const a2b::Op& l = ls[i];
    const a2b::Op& c = cs[i];
    A2B_CHECK(l.type == c.type);
    A2B_CHECK_EQ(l.i2cAddr, c.i2cAddr);
    A2B_CHECK_EQ(l.addrWidth, c.addrWidth);
    A2B_CHECK_EQ(l.addrIncrement, c.addrIncrement);
    A2B_CHECK_EQ(l.addr, c.addr);
    A2B_CHECK_EQ(l.len, c.len);
    if (l.data != nullptr && c.data != nullptr) {
      A2B_CHECK(std::memcmp(l.data, c.data, c.len) == 0);
      A2B_CHECK(l.data != c.data);  // the loaded one's own copy
    }
  }
}

/** Checks that @p text is refused with @p expected. */
void CheckError(const char* text, const char* expected) {
  a2b::SsPlusFile file;
  A2B_CHECK(file.Parse(text, std::strlen(text)) == a2b::Result::kInvalidData);
  A2B_CHECK(file.Bus() == nullptr);
  const std::string error = file.Error();
  A2B_CHECK_STREQ(error.c_str(), expected);
}

/** A minimal export: the bus, one chain and its main node, with @p extra before them. */
std::string MinimalExport(const char* extra, const char* masterBody) {
  return std::string(extra) +
         "static ADI_A2B_MASTER_NCD sMasterNode0;\n"
         "static ADI_A2B_MASTER_SLAVE_CONFIG sChainConfig0;\n"
         "ADI_A2B_BCD sBusDescription = { .nNumMasterNode = 1, .apNetworkconfig = { "
         "&sChainConfig0 } };\n"
         "static ADI_A2B_MASTER_SLAVE_CONFIG sChainConfig0 = { .pMasterConfig = &sMasterNode0 };\n"
         "static ADI_A2B_MASTER_NCD sMasterNode0 = {" +
         masterBody + "};\n";
}

}  // namespace

A2B_TEST(PiExportLoadsAsCompiled) {
  a2b::SsPlusFile file;
  A2B_CHECK(file.Load(A2B_REPO_ROOT "/adi/exports/rp2040/12_a2b_busconfig.c") == a2b::Result::kOk);
  A2B_CHECK_STREQ(file.Error().c_str(), "");
  A2B_CHECK_EQ(file.Warnings().size(), 0);
  A2B_CHECK(file.Bus() != nullptr);
  if (file.Bus() != nullptr) {
    CheckSameStructures(*file.Bus(), a2b_export_rp2040);
    CheckSameImport(*file.Bus(), a2b_export_rp2040);
  }
}

A2B_TEST(WindowsExportLoadsAsCompiled) {
  a2b::SsPlusFile file;
  A2B_CHECK(file.Load(A2B_REPO_ROOT "/adi/exports/windows/12_a2b_busconfig.c") == a2b::Result::kOk);
  A2B_CHECK_STREQ(file.Error().c_str(), "");
  A2B_CHECK_EQ(file.Warnings().size(), 0);
  if (file.Bus() != nullptr) {
    CheckSameStructures(*file.Bus(), a2b_export_windows);
    CheckSameImport(*file.Bus(), a2b_export_windows);
    // The AB0020 codec: 40 units at 0x39, as compiled.
    A2B_CHECK_EQ(file.Bus()->sTargetProperties.apPeriConfig[0]->nNumPeriConfigUnit, 40);
    A2B_CHECK_EQ(file.Bus()->sTargetProperties.apPeriConfig[0]->nI2Caddr, 0x39);
  }
}

namespace {

/**
 * Checks a tools/SS+reference export with one sub-node, read at run time, against it
 * compiled: no warnings, the structures, the sub-node and the import.
 */
void CheckReferenceExport(const char* path, const ADI_A2B_BCD& compiled) {
  a2b::SsPlusFile file;
  A2B_CHECK(file.Load(path) == a2b::Result::kOk);
  A2B_CHECK_STREQ(file.Error().c_str(), "");
  A2B_CHECK_EQ(file.Warnings().size(), 0);
  if (file.Bus() == nullptr) {
    return;
  }
  CheckSameStructures(*file.Bus(), compiled);
  CheckSameImport(*file.Bus(), compiled);
  const ADI_A2B_MASTER_SLAVE_CONFIG& lc = *file.Bus()->apNetworkconfig[0];
  const ADI_A2B_MASTER_SLAVE_CONFIG& cc = *compiled.apNetworkconfig[0];
  A2B_CHECK_EQ(cc.nNumSlaveNode, 1);
  if (lc.nNumSlaveNode == 1 && cc.nNumSlaveNode == 1) {
    // The sub-node, apart from its peripheral and stream pointers.
    const ADI_A2B_SLAVE_NCD& ls = *lc.apSlaveConfig[0];
    const ADI_A2B_SLAVE_NCD& cs = *cc.apSlaveConfig[0];
    A2B_CHECK(SameBytes(ls, cs, 0, offsetof(ADI_A2B_SLAVE_NCD, apPeriConfig)));
    A2B_CHECK(SameBytes(ls, cs, offsetof(ADI_A2B_SLAVE_NCD, oSpiSettings), sizeof(cs)));
  }
}

}  // namespace

A2B_TEST(Ab0020ExportLoadsAsCompiled) {
  // SigmaStudio+ 2.3.0, with the fields ADI's 2023 header lacks (the main node's PDM
  // settings, the SPI fields): all read.
  CheckReferenceExport(A2B_REPO_ROOT
                       "/tools/SS+reference/2x_AB0020_EVM_with_2437/adi_a2b_busconfig.c",
                       a2b_export_ab0020);
}

A2B_TEST(Ab0310ExportLoadsAsCompiled) {
  // The AB0310 and AB0312 project: RJ45 links, host peripherals without a configuration.
  CheckReferenceExport(A2B_REPO_ROOT
                       "/tools/SS+reference/AB0310_AB0312_EVM_with_2437/adi_a2b_busconfig.c",
                       a2b_export_ab0310);
}

A2B_TEST(FullExportWithSubNodesLoads) {
  // The Windows export as SigmaStudio+ wrote it: its 12 sub-node descriptions are kept
  // under #if 0 in the repository, since they don't compile with ADI's 2023 header. The
  // reference header has their five SPI fields, so they read with no warnings.
  std::FILE* in = std::fopen(A2B_REPO_ROOT "/adi/exports/windows/12_a2b_busconfig.c", "rb");
  A2B_CHECK(in != nullptr);
  if (in == nullptr) {
    return;
  }
  std::string text;
  char buffer[4096];
  size_t got = 0;
  while ((got = std::fread(buffer, 1, sizeof(buffer), in)) > 0) {
    text.append(buffer, got);
  }
  std::fclose(in);
  // The #if 0 before the first sub-node (CRLF line ends in a Windows checkout).
  const size_t slave = text.find("START OF SLAVE NODE 0");
  const size_t at = (slave == std::string::npos) ? slave : text.rfind("#if 0", slave);
  A2B_CHECK(at != std::string::npos);
  if (at == std::string::npos) {
    return;
  }
  text.replace(at, 5, "#if 1");
  a2b::SsPlusFile file;
  A2B_CHECK(file.Parse(text.data(), text.size()) == a2b::Result::kOk);
  A2B_CHECK_STREQ(file.Error().c_str(), "");
  A2B_CHECK_EQ(file.Warnings().size(), 0);
  if (file.Bus() != nullptr) {
    const ADI_A2B_MASTER_SLAVE_CONFIG& chain = *file.Bus()->apNetworkconfig[0];
    A2B_CHECK_EQ(chain.nNumSlaveNode, 12);
    A2B_CHECK(chain.apSlaveConfig[11] != nullptr);
    if (chain.apSlaveConfig[11] != nullptr) {
      A2B_CHECK(chain.apSlaveConfig[11]->ePartNum == ADI_A2B_AD2437);
    }
    CheckSameImport(*file.Bus(), a2b_export_windows);
  }
}

A2B_TEST(CrlfLineEnds) {
  // A Windows checkout has CRLF line ends: the #if blocks must still close.
  const std::string text = MinimalExport("#if 0\r\nint x = ;\r\n#endif\r\n#define A \\\r\n 1\r\n",
                                         "\r\n#if 0\r\n.nNodeID = 1,\r\n#else\r\n.nNodeID = 2,\r\n"
                                         "#endif\r\n");
  a2b::SsPlusFile file;
  A2B_CHECK(file.Parse(text.data(), text.size()) == a2b::Result::kOk);
  A2B_CHECK_STREQ(file.Error().c_str(), "");
  if (file.Bus() != nullptr) {
    A2B_CHECK_EQ(file.Bus()->apNetworkconfig[0]->pMasterConfig->nNodeID, 2);
  }
}

A2B_TEST(DisabledFieldsKeptOutLoadWithAWarning) {
  // A field from a SigmaStudio+ newer than the header (nNewerField, made up).
  const std::string text = MinimalExport(
      "",
      ".ePartNum = ADI_A2B_AD2428, .oSpiSettings = { .nSPIMode = 2u, .nNewerField = 0x0u, "
      ".nFDSize = 1 }, .sAuthSettings.nProductID = 0x28,");
  a2b::SsPlusFile file;
  A2B_CHECK(file.Parse(text.data(), text.size()) == a2b::Result::kOk);
  A2B_CHECK_EQ(file.Warnings().size(), 1);
  if (file.Warnings().size() == 1) {
    A2B_CHECK_STREQ(file.Warnings()[0].c_str(),
                    "line 5: A2B_SPI_SETTINGS has no field nNewerField: skipped");
  }
  if (file.Bus() != nullptr) {
    const ADI_A2B_MASTER_NCD& m = *file.Bus()->apNetworkconfig[0]->pMasterConfig;
    A2B_CHECK(m.ePartNum == ADI_A2B_AD2428);
    A2B_CHECK_EQ(m.oSpiSettings.nSPIMode, 2);
    A2B_CHECK_EQ(m.oSpiSettings.nFDSize, 1);         // the item after the unknown one, by name
    A2B_CHECK_EQ(m.sAuthSettings.nProductID, 0x28);  // a chained designator
    A2B_CHECK_EQ(m.sAuthSettings.nVendorID, 0);      // not set: zero
  }
}

A2B_TEST(PreprocessorConditions) {
  const std::string text = MinimalExport("#include \"adi_a2b_busconfig.h\"\n",
                                         "\n#if 0 // off\n.nNodeID = 1,\n#else\n.nNodeID = 2,\n"
                                         "#endif\n#ifdef A2B_ENABLED\n.nSrcNodeID = 3,\n#endif\n"
                                         "#ifndef NOT_A_SYMBOL\n.bLocalPower = 1,\n#elif 1\n"
                                         ".bLocalPower = 7,\n#endif\n/* #if 1 */\n");
  a2b::SsPlusFile file;
  A2B_CHECK(file.Parse(text.data(), text.size()) == a2b::Result::kOk);
  if (file.Bus() != nullptr) {
    const ADI_A2B_MASTER_NCD& m = *file.Bus()->apNetworkconfig[0]->pMasterConfig;
    A2B_CHECK_EQ(m.nNodeID, 2);
    A2B_CHECK_EQ(m.nSrcNodeID, 3);
    A2B_CHECK_EQ(m.bLocalPower, 1);
  }
  CheckError("#if 0\nint x;\n", "line 3: #if without #endif");
  CheckError("#endif\n", "line 1: #endif without #if");
  CheckError("#if A + B\n#endif\n", "line 1: can't evaluate #if A + B");
}

A2B_TEST(PositionalUnitsDataArraysAndCasts) {
  const std::string text = MinimalExport(
      "static uint8 gaData[] = { 0x01u, 2, (uint8)3, 0x4 | 0x10 };\n"
      "static ADI_A2B_PERI_CONFIG_UNIT gaUnits[2] = {\n"
      "  {A2B_WRITE_OP, 0x0u, 0x0u, 0x2u, 0x4017u, 0x1u, 0x4u, &gaData[0], 0x0u },\n"
      "  [1] = {2u /* delay */, .nDataCount = 1, .paConfigData = gaData },\n"
      "};\n"
      "static A2B_PERIPHERAL_DEVICE_CONFIG sPeri = { .nI2Caddr = 0x39, .nNumPeriConfigUnit = 2,\n"
      "  .paPeriConfigUnit = &gaUnits[0] };\n",
      ".anTxXbarSettings = { [2] = 5, 6 },");
  a2b::SsPlusFile file;
  A2B_CHECK(file.Parse(text.data(), text.size()) == a2b::Result::kOk);
  A2B_CHECK_STREQ(file.Error().c_str(), "");
  if (file.Bus() != nullptr) {
    const ADI_A2B_MASTER_NCD& m = *file.Bus()->apNetworkconfig[0]->pMasterConfig;
    A2B_CHECK_EQ(m.anTxXbarSettings[1], 0);
    A2B_CHECK_EQ(m.anTxXbarSettings[2], 5);
    A2B_CHECK_EQ(m.anTxXbarSettings[3], 6);
  }
}

A2B_TEST(PeripheralReachedThroughTheBus) {
  const char* text =
      "static uint8 gaData[4] = { 1, 2, 3, 4 };\n"
      "static ADI_A2B_PERI_CONFIG_UNIT gaUnits[1] = {\n"
      "  {A2B_WRITE_OP, 0, 0, 2, 0x4017u, 1, 4, &gaData[1], 0 } };\n"
      "static A2B_PERIPHERAL_DEVICE_CONFIG sPeri = { .nI2Caddr = 0x39, .nNumPeriConfigUnit = 1,\n"
      "  .paPeriConfigUnit = gaUnits };\n"
      "ADI_A2B_BCD sBusDescription = { .sTargetProperties = { .nNumPeriDevice = 1,\n"
      "  .apPeriConfig = { &sPeri, NULL_PTR } } };\n";
  a2b::SsPlusFile file;
  A2B_CHECK(file.Parse(text, std::strlen(text)) == a2b::Result::kOk);
  A2B_CHECK_STREQ(file.Error().c_str(), "");
  if (file.Bus() != nullptr) {
    const A2B_PERIPHERAL_DEVICE_CONFIG* peri = file.Bus()->sTargetProperties.apPeriConfig[0];
    A2B_CHECK(peri != nullptr);
    A2B_CHECK(file.Bus()->sTargetProperties.apPeriConfig[1] == nullptr);
    if (peri != nullptr) {
      A2B_CHECK_EQ(peri->nI2Caddr, 0x39);
      A2B_CHECK_EQ(peri->paPeriConfigUnit[0].nAddr, 0x4017);
      A2B_CHECK_EQ(peri->paPeriConfigUnit[0].nDataCount, 4);
      A2B_CHECK_EQ(peri->paPeriConfigUnit[0].paConfigData[0], 2);  // &gaData[1]
    }
  }
}

A2B_TEST(UnknownConstantOnlyWhereTheImportReads) {
  // A constant from a SigmaStudio+ newer than the header (A2B_GPIO_2_AS_NEWER, made up) in
  // a sub-node description: the import doesn't read it, so a warning and 0.
  const std::string sub = MinimalExport(
      "static ADI_A2B_SLAVE_NCD sSub = { .sGPIOSettings = { .sPinMuxSettings = {\n"
      "  .bGPIO2PinUsage = A2B_GPIO_2_AS_NEWER } } };\n",
      ".nNodeID = 0");
  const std::string withSub = sub +
                              "static ADI_A2B_MASTER_SLAVE_CONFIG sUnusedChain = { "
                              ".apSlaveConfig = { &sSub } };\n";
  a2b::SsPlusFile file;
  A2B_CHECK(file.Parse(withSub.data(), withSub.size()) == a2b::Result::kOk);
  A2B_CHECK_EQ(file.Warnings().size(), 1);
  if (!file.Warnings().empty()) {
    A2B_CHECK_STREQ(file.Warnings()[0].c_str(),
                    "line 2: unknown constant A2B_GPIO_2_AS_NEWER in sSub, which the import "
                    "doesn't use: taken as 0");
  }
  // The same in the main node, which the import reads: refused.
  const std::string main = MinimalExport(
      "", ".sGPIOSettings = { .sPinMuxSettings = { .bGPIO2PinUsage = A2B_GPIO_2_AS_NEWER } }");
  a2b::SsPlusFile refused;
  A2B_CHECK(refused.Parse(main.data(), main.size()) == a2b::Result::kInvalidData);
  A2B_CHECK_STREQ(refused.Error().c_str(), "line 5: unknown constant A2B_GPIO_2_AS_NEWER");
  // And through the chain's apSlaveConfig: still not read by the import.
  const std::string viaChain =
      std::string("static ADI_A2B_SLAVE_NCD sSub = { .nNodeID = UNKNOWN_ONE };\n") +
      "static ADI_A2B_MASTER_NCD sMasterNode0;\n"
      "static ADI_A2B_MASTER_SLAVE_CONFIG sChainConfig0 = { .pMasterConfig = &sMasterNode0,\n"
      "  .apSlaveConfig = { &sSub } };\n"
      "ADI_A2B_BCD sBusDescription = { .nNumMasterNode = 1, .apNetworkconfig = { &sChainConfig0 } "
      "};\n";
  a2b::SsPlusFile chained;
  A2B_CHECK(chained.Parse(viaChain.data(), viaChain.size()) == a2b::Result::kOk);
  A2B_CHECK_EQ(chained.Warnings().size(), 1);
}

A2B_TEST(ErrorsGiveTheLine) {
  a2b::SsPlusFile file;
  CheckError("", "line 1: no ADI_A2B_BCD sBusDescription: not a SigmaStudio+ export");
  CheckError("ADI_A2B_BCD sBusDescription = {\n .nNumMasterNode = NOT_KNOWN };\n",
             "line 2: unknown constant NOT_KNOWN");
  CheckError(
      "static uint8 x[2];\nADI_A2B_BCD sBusDescription = {\n"
      " .apNetworkconfig = { &x } };\n",
      "line 3: apNetworkconfig can't point to x (uint8)");
  CheckError("ADI_A2B_BCD sBusDescription = { .apNetworkconfig = { &nothing } };\n",
             "line 1: unknown variable nothing");
  CheckError("static uint8 x[2] = { 1, 2, 3 };\n", "line 1: too many initializers for x");
  CheckError("static uint8 x[1] = { 256 };\n", "line 1: x: value out of range");
  CheckError("ADI_A2B_BCD sBusDescription = { .nNumMasterNode = 1\n",
             "line 2: expected ',' or '}'");
  CheckError("static uint8 x = 1.5;\n", "line 1: malformed number");
  CheckError("/* open\n", "line 1: comment not closed");
  A2B_CHECK(file.Load(A2B_REPO_ROOT "/no/such/export.c") == a2b::Result::kInvalidArgument);
  A2B_CHECK(file.Error().find("can't read") == 0);
}

A2B_TEST(SkipsWhatItDoesNotRead) {
  const std::string text = MinimalExport(
      "typedef struct { int a; } LOCAL_T;\nextern int gValue;\nint Helper(void);\n"
      "int Helper(void) { return 1; }\nstatic LOCAL_T sLocal = { 1 };\n#define X 1\n",
      ".nNodeID = 4");
  a2b::SsPlusFile file;
  A2B_CHECK(file.Parse(text.data(), text.size()) == a2b::Result::kOk);
  A2B_CHECK_STREQ(file.Error().c_str(), "");
  A2B_CHECK_EQ(file.Warnings().size(), 2);  // sLocal's type, and the #define
  if (file.Bus() != nullptr) {
    A2B_CHECK_EQ(file.Bus()->apNetworkconfig[0]->pMasterConfig->nNodeID, 4);
  }
}

A2B_TEST_MAIN()
