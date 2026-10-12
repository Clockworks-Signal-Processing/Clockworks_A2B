// clang-format off
/*=============================================================================
 *
 * Project: a2bcpp
 *
 * Copyright (c) 2023 - Analog Devices Inc. All Rights Reserved.
 * See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
 *
 *=============================================================================
 */
// clang-format on

/**
 * @file ss_plus_import.cpp
 * @brief ImportSsPlus(): a SigmaStudio+ bus configuration export to a BoardConfig.
 *
 * Ports the main-node half of ADI's BCF-to-BDD conversion
 * (a2bstack/a2bstack-protobuf/src/a2b_bdd_helper.c: adi_a2b_ParseMasterNCD,
 * adi_a2b_ParseMasterNCD_242x/_243x, their pin-mux helpers and
 * adi_a2b_SetforAllReg_242x/243x_master) and its peripheral table
 * (adi_a2b_ParsePeriCfgTable, run as adi_a2b_AudioHostConfig in the platform PALs).
 *
 * What it produces:
 * - BoardConfig::mainNode: the main node's register values, in the order ADI's
 *   stack wrote them (a2b_dscvryNodeComplete in a2bplugin-master/src/discovery.c), then
 *   the registers it wrote elsewhere during discovery (interrupt masks, PLLCTL, SLOTFMT,
 *   DATCTL, RESPCYCS, CONTROL, SWCTL). Each chip gets the registers ADI's stack
 *   marked for it: AD243x adds the crossbar, SPI, data tunnel, PWM and VMTR blocks.
 * - hostScript: the export's peripherals on the host I2C bus (e.g. the AB0020's
 *   ADAU1761), one Op per SigmaStudio+ config unit. SigmaStudio+ marks each peripheral
 *   to be configured before or after the A2B transceiver (bPostDiscCfg): those before
 *   come first, and BoardConfig::hostOpsBeforeMainNode counts their operations, which
 *   Network runs before it touches the main node (the AB0020's ADAU1761 clocks it); the
 *   others run once the main node is configured. ADI's stack ran every host
 *   peripheral before the main node (adi_a2b_ParsePeriCfgTable doesn't copy the flag).
 * - addresses, the main-node chip, its power settings and the two discovery timings.
 * - the default link: single-step, powered over the A2B pair (LinkProfile::Utp()'s
 *   settings), with its B side inverted only if the export's main-node CONTROL sets
 *   XCVRBINV, as an export for RJ45 links does (UTP links are straight; two-step RJ45
 *   discovery is not supported yet). The CONTROL value stays in the image too; the stack takes
 *   XCVRBINV from the link.
 *
 * Sub-node settings in the export are ignored: sub-nodes are configured from their own
 * EEPROM. So are the discovery mode, cable type and fault policies, which the new stack
 * doesn't take from the export.
 *
 * Differences from ADI's conversion:
 * - GPIO0 as an input or output uses the export's GPIO0 interrupt-enable and output
 *   value; ADI's adi_a2b_ParseMasterPinMux0 read GPIO1's. The SigmaStudio+ structure
 *   has no GPIO0 interrupt polarity for the main node that ADI used, so GPIO1's is kept.
 * - AD2430/AD2438 and AD241x main nodes are not supported (not in the stack's chip list).
 *
 * C++ form by ClockWorks Signal Processing LLC.
 */

#include "ss_plus_import.h"

#include "chip_caps.h"
#include "detail/regdefs.h"

namespace a2b {
namespace {

/** Main-node register values, before they go into the image. */
struct MainRegs {
  uint8_t dnslots = 0;
  uint8_t upslots = 0;
  uint8_t respcycs = 0;
  uint8_t slotfmt = 0;
  uint8_t datctl = 0;
  uint8_t control = 0;
  uint8_t swctl = 0;
  uint8_t becctl = 0;
  uint8_t intmsk0 = 0;
  uint8_t intmsk1 = 0;
  uint8_t intmsk2 = 0;
  uint8_t i2ccfg = 0;
  uint8_t pllctl = 0;
  uint8_t i2sgcfg = 0;
  uint8_t i2scfg = 0;
  uint8_t i2stxoffset = 0;
  uint8_t i2srxoffset = 0;
  uint8_t pdmctl = 0;
  uint8_t pdmctl2 = 0;
  uint8_t errmgmt = 0;
  uint8_t gpiodat = 0;
  uint8_t gpiooen = 0;
  uint8_t gpioien = 0;
  uint8_t pinten = 0;
  uint8_t pintinv = 0;
  uint8_t pincfg = 0;
  uint8_t clk1cfg = 0;
  uint8_t clk2cfg = 0;
  uint8_t i2srrctl = 0;
  uint8_t txactl = 0;  ///< TXACTL on AD242x, TXCTL on AD243x (same address).
  uint8_t txbctl = 0;
  uint8_t gpioden = 0;
  uint8_t gpiodinv = 0;
  uint8_t gpiodmsk[8] = {};
  uint8_t spicfg = 0;
  uint8_t spickdiv = 0;
  uint8_t spifdsize = 0;
  uint8_t spifdtarg = 0;
  uint8_t spipincfg = 0;
  uint8_t spimsk = 0;
  uint8_t dtcfg = 0;
  uint8_t dtslots = 0;
  uint8_t dtdnoffs = 0;
  uint8_t dtupoffs = 0;
  uint8_t pwmcfg = 0;
  uint8_t pwmfreq = 0;
  uint8_t pwmblink1 = 0;
  uint8_t pwmblink2 = 0;
  uint8_t pwmval[6] = {};  ///< PWM1VALL, PWM1VALH, PWM2VALL, PWM2VALH, PWM3VALL, PWM3VALH.
  uint8_t pwmoe[2] = {};   ///< PWMOEVALL, PWMOEVALH.
  uint8_t vmtr[4] = {};    ///< VMTR_VEN, VMTR_INTEN, VMTR_MXSTAT, VMTR_MNSTAT.
  uint8_t vmax[7] = {};
  uint8_t vmin[7] = {};
  uint8_t txxbar[A2B_TOTAL_TXBAR_REGS] = {};
  uint8_t rxmask[A2B_TOTAL_RXMASK_REGS] = {};
};

/** Sets a register field as ADI's setRegVal() does: clears @p mask, ORs in the value. */
void SetField(uint8_t& reg, unsigned value, uint8_t mask, unsigned pos) {
  reg = static_cast<uint8_t>((reg & ~mask) | (value << pos));
}

/** One bit from an export flag (A2B_ENABLED is 1). */
uint8_t Bit(unsigned flag, unsigned pos) { return static_cast<uint8_t>((flag & 1u) << pos); }

/** GPIO pin @p pin used as an input or output (ADI's ParseMasterPinMux*). */
void GpioInOut(MainRegs& r, unsigned usage, unsigned pin, unsigned interrupt, unsigned polarity,
               unsigned outValue) {
  if (usage == A2B_GPIO_0_INPUT) {
    r.gpioien |= Bit(1, pin);
    r.pinten |= Bit(interrupt, pin);
    r.pintinv |= Bit(polarity, pin);
  } else if (usage == A2B_GPIO_0_OUTPUT) {
    r.gpiooen |= Bit(1, pin);
    r.gpiodat |= Bit(outValue, pin);
  }
}

/** Settings every chip has (ADI's adi_a2b_ParseMasterNCD). */
void ParseCommon(const ADI_A2B_MASTER_NCD& m, const ADI_A2B_COMMON_CONFIG& common, MainRegs& r) {
  const auto& ctrl = m.sConfigCtrlSettings;
  const auto& i2s = m.sI2SSettings;
  const auto& irq = m.sInterruptSettings;
  const auto& gpio = m.sGPIOSettings;

  r.dnslots = ctrl.nPassDwnSlots;
  r.upslots = ctrl.nPassUpSlots;
  r.respcycs = ctrl.nRespCycle;
  r.slotfmt = static_cast<uint8_t>(common.nDwnSlotSize | common.nUpSlotSize |
                                   Bit(common.bDwnstreamCompression, 3) |
                                   Bit(common.bUpstreamCompression, 7));
  r.datctl = ctrl.nDatctrl;
  r.i2ccfg = Bit(ctrl.bI2CEarlyAck, 1);
  r.i2sgcfg = static_cast<uint8_t>(Bit(i2s.bEarlySync, 6) | i2s.nTDMMode | i2s.nTDMChSize |
                                   i2s.nSyncMode | Bit(i2s.nSyncPolarity, 7));
  r.i2scfg = static_cast<uint8_t>(Bit(i2s.bRXInterleave, 6) | Bit(i2s.bTXInterleave, 2) |
                                  Bit(i2s.nBclkRxPolarity, 7) | Bit(i2s.nBclkTxPolarity, 3));
  r.i2stxoffset = static_cast<uint8_t>(i2s.nTxOffset | Bit(i2s.bTriStateAfterTx, 6) |
                                       Bit(i2s.bTriStateBeforeTx, 7));
  r.i2srxoffset = i2s.nRxOffset;
  r.pdmctl = m.sRegSettings.nPDMCTL;
  r.errmgmt = m.sRegSettings.nERRMGMT;
  r.becctl = m.sRegSettings.nBECCTL;
  r.control = m.sRegSettings.nCONTROL;
  r.intmsk0 = static_cast<uint8_t>(Bit(irq.bReportHDCNTErr, 0) | Bit(irq.bReportDDErr, 1) |
                                   Bit(irq.bReportCRCErr, 2) | Bit(irq.bReportDataParityErr, 3) |
                                   Bit(irq.bReportPwrErr, 4) | Bit(irq.bReportErrCntOverFlow, 5) |
                                   Bit(irq.bReportSRFMissErr, 6));
  r.intmsk2 = static_cast<uint8_t>(Bit(irq.bSlaveIntReq, 3) | Bit(irq.bReportI2CErr, 1) |
                                   Bit(irq.bDiscComplete, 0) | Bit(irq.bIntFrameCRCErr, 2));

  // Pin multiplexing of GPIO3-6 (ADI's ParseMasterPinMux34/56).
  const auto& mux = gpio.sPinMuxSettings;
  const auto& pint = gpio.sPinIntConfig;
  const auto& out = gpio.sOutPinVal;
  GpioInOut(r, mux.bGPIO3PinUsage, 3, pint.bGPIO3Interrupt, pint.bGPIO3IntPolarity, out.bGPIO3Val);
  if (mux.bGPIO3PinUsage == A2B_GPIO_3_AS_DTX0) {
    r.i2scfg |= reg::kI2scfgTx0en;
  }
  GpioInOut(r, mux.bGPIO4PinUsage, 4, pint.bGPIO4Interrupt, pint.bGPIO4IntPolarity, out.bGPIO4Val);
  if (mux.bGPIO4PinUsage == A2B_GPIO_4_AS_DTX1) {
    r.i2scfg |= reg::kI2scfgTx1en;
  }
  GpioInOut(r, mux.bGPIO5PinUsage, 5, pint.bGPIO5Interrupt, pint.bGPIO5IntPolarity, out.bGPIO5Val);
  if (mux.bGPIO5PinUsage == A2B_GPIO_5_AS_DRX0) {
    r.i2scfg |= reg::kI2scfgRx0en;
  }
  GpioInOut(r, mux.bGPIO6PinUsage, 6, pint.bGPIO6Interrupt, pint.bGPIO6IntPolarity, out.bGPIO6Val);
  if (mux.bGPIO6PinUsage == A2B_GPIO_6_AS_DRX1) {
    r.i2scfg |= reg::kI2scfgRx1en;
  }
}

/** Settings of AD242x and later chips (ADI's adi_a2b_ParseMasterNCD_242x). */
void Parse242x(const ADI_A2B_MASTER_NCD& m, ChipFamily family, MainRegs& r) {
  const auto& gpiod = m.sGPIODSettings;
  const A2B_GPIOD_PIN_CONFIG* pins[8] = {nullptr,
                                         &gpiod.sGPIOD1Config,
                                         &gpiod.sGPIOD2Config,
                                         &gpiod.sGPIOD3Config,
                                         &gpiod.sGPIOD4Config,
                                         &gpiod.sGPIOD5Config,
                                         &gpiod.sGPIOD6Config,
                                         &gpiod.sGPIOD7Config};
  for (unsigned pin = 1; pin < 8; ++pin) {
    r.gpiodinv |= Bit(pins[pin]->bGPIOSignalInv, pin);
    r.gpioden |= Bit(pins[pin]->bGPIODistance, pin);
    for (unsigned bus = 0; bus < 8; ++bus) {
      r.gpiodmsk[pin] |= Bit(pins[pin]->abBusPortMask[bus], bus);
    }
  }
  r.swctl = m.sRegSettings.nSWCTL;

  const auto& i2s = m.sI2SSettings;
  const auto& rr = i2s.sI2SRateConfig;
  r.i2srrctl = static_cast<uint8_t>(Bit(rr.bRRStrobe, 4) | Bit(rr.bRRStrobeDirection, 5) |
                                    Bit(rr.bRRValidBitExtraBit, 1) | Bit(rr.bRRValidBitLSB, 0) |
                                    Bit(rr.bRRValidBitExtraCh, 6));

  const auto& irq = m.sInterruptSettings;
  r.intmsk1 = static_cast<uint8_t>(Bit(irq.bReportGPIO1, 1) | Bit(irq.bReportGPIO2, 2) |
                                   Bit(irq.bReportGPIO3, 3) | Bit(irq.bReportGPIO4, 4) |
                                   Bit(irq.bReportGPIO5, 5) | Bit(irq.bReportGPIO6, 6) |
                                   Bit(irq.bReportGPIO7, 7));

  const auto& gpio = m.sGPIOSettings;
  r.pincfg = static_cast<uint8_t>(Bit(gpio.bHighDriveStrength, 0) | Bit(gpio.bIRQInv, 4) |
                                  Bit(gpio.bIRQTriState, 5));

  const auto& clk = m.sClkOutSettings;
  r.clk1cfg |= static_cast<uint8_t>((clk.bClk1Div << reg::kClkcfgDivPos) |
                                    (clk.bClk1PreDiv << reg::kClkcfgPdivPos) |
                                    (clk.bClk1Inv << reg::kClkcfgInvPos));
  r.clk2cfg |= static_cast<uint8_t>((clk.bClk2Div << reg::kClkcfgDivPos) |
                                    (clk.bClk2PreDiv << reg::kClkcfgPdivPos) |
                                    (clk.bClk2Inv << reg::kClkcfgInvPos));

  if (family == ChipFamily::kAd242x) {
    r.i2sgcfg |= Bit(i2s.bSerialRxOnDTx1, 3);
    r.txactl = m.sRegSettings.nTXACTL;
    r.txbctl = m.sRegSettings.nTXBCTL;
  }
  r.pdmctl2 = m.sRegSettings.nPDMCTL2;
  r.pllctl = m.sRegSettings.nPLLCTL;

  // Pin multiplexing of GPIO1, GPIO2 and GPIO7 (ADI's ParseMasterPinMux12/7).
  const auto& mux = gpio.sPinMuxSettings;
  const auto& pint = gpio.sPinIntConfig;
  const auto& out = gpio.sOutPinVal;
  GpioInOut(r, mux.bGPIO1PinUsage, 1, pint.bGPIO1Interrupt, pint.bGPIO1IntPolarity, out.bGPIO1Val);
  if (mux.bGPIO1PinUsage == A2B_GPIO_1_AS_CLKOUT) {
    r.clk1cfg |= reg::kClkcfgEn;
  }
  GpioInOut(r, mux.bGPIO2PinUsage, 2, pint.bGPIO2Interrupt, pint.bGPIO2IntPolarity, out.bGPIO2Val);
  if (mux.bGPIO2PinUsage == A2B_GPIO_2_AS_CLKOUT) {
    r.clk2cfg |= reg::kClkcfgEn;
  }
  GpioInOut(r, mux.bGPIO7PinUsage, 7, pint.bGPIO7Interrupt, pint.bGPIO7IntPolarity, out.bGPIO7Val);
  if (mux.bGPIO7PinUsage == A2B_GPIO_7_PDMCLK) {
    r.pdmctl2 |= reg::kPdmctl2Pdmaltclk;
  }
}

/** SPI and data tunnel settings (ADI's adi_a2b_ParseSpiDT). */
void ParseSpiDt(const A2B_SPI_SETTINGS& spi, MainRegs& r) {
  r.spicfg |= static_cast<uint8_t>(
      (spi.nFDSlaveSel << reg::kSpicfgSpifdssPos) | (spi.bFDClkStretchEn << reg::kSpicfgEnfdcsPos) |
      (spi.nCPOL << reg::kSpicfgCpolPos) | (spi.nCPHA << reg::kSpicfgCphaPos) | spi.nSPIMode |
      ((spi.eTunnelOwnership == DT_OWNER) ? reg::kSpicfgTnlowner : 0));
  r.spickdiv = spi.nClkDivFactor;
  r.spifdsize = spi.nFDSize;
  r.spifdtarg =
      static_cast<uint8_t>(spi.nFDTargetNode | (spi.nTargetSSel << reg::kSpifdtargSselPos));
  const auto& irq = spi.oSpiIntSettings;
  r.spimsk = static_cast<uint8_t>(Bit(irq.bFifoUnderflow, 6) | Bit(irq.bFifoOverflow, 5) |
                                  Bit(irq.bBadCommand, 4) | Bit(irq.bDataTunnel, 3) |
                                  Bit(irq.bSpiRemoteI2cAccess, 2) |
                                  Bit(irq.bSpiRemoteRegAccess, 1) | Bit(irq.bSpiDone, 0));
  r.dtcfg |= Bit(spi.bDTEnable, 0);
  r.dtcfg &= static_cast<uint8_t>(~(reg::kDtcfgDtfrst | reg::kDtcfgDtlast));
  if (spi.eTunnelPos == DT_POS_FIRST) {
    r.dtcfg |= reg::kDtcfgDtfrst;
  } else if (spi.eTunnelPos == DT_POS_LAST) {
    r.dtcfg |= reg::kDtcfgDtlast;
  }
  r.dtslots =
      static_cast<uint8_t>((spi.nDTUpstrmSlots << reg::kDtslotsUpPos) | spi.nDTDwnstrmSlots);
  r.dtdnoffs = spi.nDTDwnstrmOffset;
  r.dtupoffs = spi.nDTUpstrmOffset;
}

/** Serial pins: pin counts, PDM and SS1 (ADI's adi_a2b_ParseSioPinCfg, AD243x branch). */
void ParseSioPins(const A2B_PIN_ASSIGN_CONFIG& pins, MainRegs& r) {
  unsigned rxPins = 0;
  unsigned txPins = 0;
  switch (pins.oSio0.eFunc) {
    case FUNC_RX0:
      ++rxPins;
      break;
    case FUNC_PDM0:
      r.pdmctl |= reg::kPdmctlPdm0en;
      break;
    default:
      break;
  }
  switch (pins.oSio1.eFunc) {
    case FUNC_RX0:
    case FUNC_RX1:
      ++rxPins;
      break;
    case FUNC_PDM1:
      r.pdmctl |= reg::kPdmctlPdm1en;
      break;
    case FUNC_TX3:
      ++txPins;
      break;
    default:
      break;
  }
  switch (pins.oSio2.eFunc) {
    case FUNC_RX0:
    case FUNC_RX1:
    case FUNC_RX2:
      ++rxPins;
      break;
    case FUNC_SS1:
      r.spipincfg |= reg::kSpipincfgSpimss1en;
      break;
    case FUNC_TX2:
      ++txPins;
      break;
    default:
      break;
  }
  switch (pins.oSio3.eFunc) {
    case FUNC_RX1:
    case FUNC_RX2:
    case FUNC_RX3:
      ++rxPins;
      break;
    case FUNC_TX1:
      ++txPins;
      break;
    default:
      break;
  }
  if (pins.oSio4.eFunc == FUNC_TX0) {
    ++txPins;
  }
  SetField(r.i2scfg, rxPins, reg::kI2scfgRxpins, reg::kI2scfgRxpinsPos);
  SetField(r.i2scfg, txPins, reg::kI2scfgTxpins, 0);
}

/**
 * GPIO7, I2C, SPI and ADR pins (ADI's adi_a2b_ParseNonSioPinCfg and its helpers,
 * AD243x). The ADR cases for PWM_CH1, RR_STRB and PWM_CH2 sit inside an AD2430/38 branch
 * in ADI's code but are reached for every chip, as here.
 */
void ParseOtherPins(const A2B_PIN_ASSIGN_CONFIG& pins, MainRegs& r) {
  switch (pins.oGPIO7.eFunc) {
    case FUNC_RR_STRB:
      SetField(r.i2srrctl, 1, reg::kI2srrctlEnstrb, 4);
      SetField(r.pdmctl2, 0, reg::kPdmctl2Pdmaltclk, 4);
      break;
    case FUNC_PDM_CLK:
      SetField(r.i2srrctl, 0, reg::kI2srrctlEnstrb, 4);
      SetField(r.pdmctl2, 1, reg::kPdmctl2Pdmaltclk, 4);
      break;
    default:
      SetField(r.i2srrctl, 0, reg::kI2srrctlEnstrb, 4);
      SetField(r.pdmctl2, 0, reg::kPdmctl2Pdmaltclk, 4);
      break;
  }

  SetField(r.i2ccfg, (pins.oSCL.eFunc == FUNC_I2C) ? 0 : 1, reg::kI2ccfgDisable, 7);

  switch (pins.oMISO.eFunc) {
    case FUNC_SPI_SLAVE:
      SetField(r.spicfg, 0, reg::kSpicfgSpimode, 0);
      SetField(r.clk1cfg, 0, reg::kClkcfgEn, 7);
      SetField(r.pwmcfg, 0, reg::kPwmcfgPwmoeen, 3);
      break;
    case FUNC_SPI_MASTER:
      SetField(r.spicfg, 1, reg::kSpicfgSpimode, 0);
      break;
    default:
      SetField(r.pwmcfg, (pins.oMISO.eFunc == FUNC_PWM_CH1) ? 1 : 0, reg::kPwmcfgPwm1en, 0);
      SetField(r.pwmcfg, (pins.oMOSI.eFunc == FUNC_PWM_CH2) ? 1 : 0, reg::kPwmcfgPwm2en, 1);
      SetField(r.pwmcfg, (pins.oSCK.eFunc == FUNC_PWM_CH3) ? 1 : 0, reg::kPwmcfgPwm3en, 2);
      break;
  }

  switch (pins.oADR1.eFunc) {
    case FUNC_I2C_CLKOUT1:
      SetField(r.clk1cfg, 1, reg::kClkcfgEn, 7);
      break;
    case FUNC_SS0:
      SetField(r.spipincfg, 1, reg::kSpipincfgSpimss0en, 4);
      break;
    case FUNC_PWM_OE:
      SetField(r.pwmcfg, 1, reg::kPwmcfgPwmoeen, 3);
      break;
    case FUNC_PWM_CH1:
      SetField(r.pwmcfg, 1, reg::kPwmcfgPwm1en, 0);
      break;
    case FUNC_RR_STRB:
      SetField(r.i2srrctl, 1, reg::kI2srrctlEnstrb, 4);
      SetField(r.pdmctl2, 0, reg::kPdmctl2Pdmaltclk, 4);
      break;
    default:
      SetField(r.pwmcfg, 0, reg::kPwmcfgPwmoeen, 3);
      break;
  }

  switch (pins.oADR2.eFunc) {
    case FUNC_I2C_CLKOUT2:
      SetField(r.clk2cfg, 1, reg::kClkcfgEn, 7);
      break;
    case FUNC_SS2:
      SetField(r.spipincfg, 1, reg::kSpipincfgSpimss2en, 6);
      break;
    case FUNC_PWM_CH2:
      SetField(r.pwmcfg, 1, reg::kPwmcfgPwm2en, 1);
      break;
    default:
      SetField(r.spipincfg, 0, reg::kSpipincfgSpimss2en, 6);
      break;
  }
}

/** Settings AD243x chips add (ADI's adi_a2b_ParseMasterNCD_243x). */
void Parse243x(const ADI_A2B_MASTER_NCD& m, MainRegs& r) {
  const auto& i2s = m.sI2SSettings;
  if (i2s.bSync != A2B_ENABLED) {
    r.i2sgcfg |= reg::kI2sgcfgSyncdis;
  }
  r.txactl = m.sRegSettings.nTXACTL;
  ParseSpiDt(m.oSpiSettings, r);
  for (unsigned i = 0; i < A2B_TOTAL_TXBAR_REGS; ++i) {
    r.txxbar[i] = m.anTxXbarSettings[i];
  }
  for (unsigned i = 0; i < A2B_TOTAL_RXMASK_REGS; ++i) {
    r.rxmask[i] = m.anRxXbarSettings[i];
  }

  const auto& vmtr = m.oVmtrSettings;
  r.vmtr[0] = vmtr.bVEN;
  r.vmtr[1] = vmtr.bIntEN;
  r.vmtr[2] = vmtr.nMxStat;
  r.vmtr[3] = vmtr.nMinStat;
  const A2B_VMTR_VTG* vtg[7] = {&vmtr.oVtg0, &vmtr.oVtg1, &vmtr.oVtg2, &vmtr.oVtg3,
                                &vmtr.oVtg4, &vmtr.oVtg5, &vmtr.oVtg6};
  for (unsigned i = 0; i < 7; ++i) {
    r.vmax[i] = vtg[i]->nVmax;
    r.vmin[i] = vtg[i]->nVmin;
  }

  const auto& pwm = m.oPwmSettings;
  r.pwmcfg = pwm.nPwmCfg;
  r.pwmfreq = pwm.nPwmFreq;
  r.pwmblink1 = pwm.nPwmBlink1;
  r.pwmblink2 = pwm.nPwmBlink2;
  const unsigned values[3] = {pwm.nPwm1Val, pwm.nPwm2Val, pwm.nPwm3Val};
  for (unsigned i = 0; i < 3; ++i) {
    r.pwmval[2 * i] = static_cast<uint8_t>(values[i] & 0xFF);
    r.pwmval[2 * i + 1] = static_cast<uint8_t>(values[i] >> 8);
  }
  r.pwmoe[0] = static_cast<uint8_t>(pwm.nPwmOEVal & 0xFF);
  r.pwmoe[1] = static_cast<uint8_t>(pwm.nPwmOEVal >> 8);

  const auto& pins = m.oPinAssignSettings;
  r.pincfg |= static_cast<uint8_t>(pins.nGpioMode << reg::kPincfgGpioselPos);
  ParseSioPins(pins, r);
  ParseOtherPins(pins, r);
  if (i2s.bRXInterleave) {
    SetField(r.i2scfg, 7, reg::kI2scfgRxpins, reg::kI2scfgRxpinsPos);
  }
  if (i2s.bTXInterleave) {
    SetField(r.i2scfg, 7, reg::kI2scfgTxpins, 0);
  }

  // GPIO0 (ADI's adi_a2b_ParseMasterPinMux0, which used GPIO1's settings).
  const auto& gpio = m.sGPIOSettings;
  GpioInOut(r, gpio.sPinMuxSettings.bGPIO0PinUsage, 0, gpio.sPinIntConfig.bGPIO0Interrupt,
            gpio.sPinIntConfig.bGPIO1IntPolarity, gpio.sOutPinVal.bGPIO0Val);
}

/** Puts the registers into @p image in ADI's write order (see the file comment). */
Result Emit(const MainRegs& r, ChipFamily family, RegisterImage& image) {
  Result result = Result::kOk;
  const auto set = [&image, &result](uint16_t reg, uint8_t value) {
    if (result == Result::kOk) {
      result = image.Set(reg, value);
    }
  };
  const bool is243x = (family == ChipFamily::kAd243x);

  // Control, I2C/I2S and pin I/O registers.
  set(reg::kDnslots, r.dnslots);
  set(reg::kUpslots, r.upslots);
  set(reg::kI2ccfg, r.i2ccfg);
  set(reg::kSyncoffset, 0);
  set(reg::kI2stxoffset, r.i2stxoffset);
  set(reg::kI2srxoffset, r.i2srxoffset);
  set(reg::kI2sgcfg, r.i2sgcfg);
  set(reg::kI2scfg, r.i2scfg);
  set(reg::kI2srate, 0);
  set(reg::kPdmctl, r.pdmctl);
  set(reg::kPdmctl2, r.pdmctl2);
  set(reg::kErrmgmt, r.errmgmt);
  set(reg::kGpiodat, r.gpiodat);
  set(reg::kGpiooen, r.gpiooen);
  set(reg::kGpioien, r.gpioien);
  set(reg::kPinten, r.pinten);
  set(reg::kPintinv, r.pintinv);
  set(reg::kPincfg, r.pincfg);
  set(reg::kClk1cfg, r.clk1cfg);
  set(reg::kClk2cfg, r.clk2cfg);

  // GPIO over distance.
  for (unsigned i = 0; i < 8; ++i) {
    set(static_cast<uint16_t>(reg::kGpiod0msk + i), r.gpiodmsk[i]);
  }
  set(reg::kGpioddat, 0);
  set(reg::kGpiodinv, r.gpiodinv);
  set(reg::kGpioden, r.gpioden);

  if (is243x) {
    for (unsigned i = 0; i < A2B_TOTAL_RXMASK_REGS; ++i) {
      set(static_cast<uint16_t>(reg::kRxmask0 + i), r.rxmask[i]);
    }
    for (unsigned i = 0; i < A2B_TOTAL_TXBAR_REGS; ++i) {
      set(static_cast<uint16_t>(reg::kTxxbar0 + i), r.txxbar[i]);
    }
    set(reg::kSpicfg, r.spicfg);
    set(reg::kSpickdiv, r.spickdiv);
    set(reg::kSpifdsize, r.spifdsize);
    set(reg::kSpifdtarg, r.spifdtarg);
    set(reg::kSpipincfg, r.spipincfg);
    set(reg::kSpiint, 0);
    set(reg::kSpimsk, r.spimsk);
    set(reg::kDtcfg, r.dtcfg);
    set(reg::kDtslots, r.dtslots);
    set(reg::kDtdnoffs, r.dtdnoffs);
    set(reg::kDtupoffs, r.dtupoffs);
    set(reg::kPwmcfg, r.pwmcfg);
    set(reg::kPwmfreq, r.pwmfreq);
    set(reg::kPwmblink1, r.pwmblink1);
    set(reg::kPwmblink2, r.pwmblink2);
    for (unsigned i = 0; i < 6; ++i) {
      set(static_cast<uint16_t>(reg::kPwm1vall + i), r.pwmval[i]);
    }
    set(reg::kPwmoevall, r.pwmoe[0]);
    set(reg::kPwmoevall + 1, r.pwmoe[1]);
    for (unsigned i = 0; i < 4; ++i) {
      set(static_cast<uint16_t>(reg::kVmtrVen + i), r.vmtr[i]);
    }
    for (unsigned i = 0; i < 7; ++i) {
      set(static_cast<uint16_t>(reg::kVmtrVmax0 + 3 * i), r.vmax[i]);
      set(static_cast<uint16_t>(reg::kVmtrVmin0 + 3 * i), r.vmin[i]);
    }
  }

  set(reg::kI2srrctl, r.i2srrctl);
  set(reg::kTxactl, r.txactl);
  if (!is243x) {
    set(reg::kTxbctl, r.txbctl);
  }

  // Registers ADI's stack wrote outside a2b_dscvryNodeComplete.
  set(reg::kIntmsk0, r.intmsk0);
  set(reg::kIntmsk1, r.intmsk1);
  set(reg::kIntmsk2, r.intmsk2);
  set(reg::kBecctl, r.becctl);
  set(reg::kPllctl, r.pllctl);
  set(reg::kSlotfmt, r.slotfmt);
  set(reg::kDatctl, r.datctl);
  set(reg::kRespcycs, r.respcycs);
  set(reg::kControl, r.control);
  set(reg::kSwctl, r.swctl);
  return result;
}

/** Adds one SigmaStudio+ config unit of a host peripheral to @p script. */
Result AddUnit(const ADI_A2B_PERI_CONFIG_UNIT& unit, uint8_t i2cAddr, OpList& script) {
  const uint32_t bytes = unit.nDataCount * (unit.nDataWidth > 0 ? unit.nDataWidth : 1);
  switch (unit.eOpCode) {
    case A2B_WRITE_OP:
      if (unit.nAddrWidth > 4 || bytes == 0 || bytes > 0xFFFF || unit.paConfigData == nullptr) {
        return Result::kInvalidArgument;
      }
      return script.Add(Op::PeripheralWrite(
          kMainNode, i2cAddr, static_cast<uint8_t>(unit.nAddrWidth), unit.nAddr, unit.paConfigData,
          static_cast<uint16_t>(bytes), unit.nAddrIncrement));
    case A2B_READ_OP:
      if (unit.nAddrWidth > 4 || bytes == 0 || bytes > 0xFFFF) {
        return Result::kInvalidArgument;
      }
      return script.Add(Op::PeripheralRead(kMainNode, i2cAddr,
                                           static_cast<uint8_t>(unit.nAddrWidth), unit.nAddr,
                                           static_cast<uint16_t>(bytes)));
    case A2B_DEALY_OP: {
      // The delay in milliseconds, least significant byte first (adi_a2b_AudioHostConfig).
      if (unit.nDataCount > 4 || unit.paConfigData == nullptr) {
        return Result::kInvalidArgument;
      }
      uint32_t ms = 0;
      for (uint32_t i = 0; i < unit.nDataCount; ++i) {
        ms |= static_cast<uint32_t>(unit.paConfigData[i]) << (8 * i);
      }
      return script.Add(Op::Wait(ms));
    }
    default:
      return Result::kInvalidArgument;
  }
}

/**
 * Adds the config units of the host peripherals configured before the main node
 * (@p postDiscovery false: bPostDiscCfg not set) or after it (true) to @p script.
 */
Result ImportHostPeripherals(const ADI_A2B_NETWORK_CONFIG& target, bool postDiscovery,
                             OpList& script) {
  for (unsigned device = 0; device < target.nNumPeriDevice; ++device) {
    const A2B_PERIPHERAL_DEVICE_CONFIG* config = target.apPeriConfig[device];
    if (config == nullptr || config->paPeriConfigUnit == nullptr) {
      continue;  // Declared without a configuration (e.g. the AB0020's EEPROM).
    }
    if ((config->bPostDiscCfg == A2B_ENABLED) != postDiscovery) {
      continue;  // In the other part of the script.
    }
    if (config->ePeriDeviceInterface != I2C || config->nI2Caddr > 0x7F) {
      return Result::kNotSupported;  // SPI peripherals are not supported yet.
    }
    for (uint32_t i = 0; i < config->nNumPeriConfigUnit; ++i) {
      const Result result =
          AddUnit(config->paPeriConfigUnit[i], static_cast<uint8_t>(config->nI2Caddr), script);
      if (result != Result::kOk) {
        return result;
      }
    }
  }
  return Result::kOk;
}

}  // namespace

/**
 * Fills a BoardConfig from one chain of a SigmaStudio+ export.
 *
 * Sets the addresses, main-node chip and power settings, the timings, the main-node
 * register image (replacing what was there), the host peripheral script, and the default
 * link: single-step, powered over the A2B pair, with the B side inverted only if the
 * export's main-node CONTROL sets XCVRBINV, as for RJ45 links (UTP links are straight; the
 * export has no two-step setting the stack takes). The bus index, policy, hooks and
 * the other timings are left as they are.
 *
 * @param bcd The export's bus description (its sBusDescription).
 * @param chain Which chain (main node) of the export, from 0.
 * @param board Filled in. Its hostScript is set to @p hostScript, and its
 *        hostOpsBeforeMainNode to the operations of the peripherals configured first.
 * @param hostScript Emptied, then filled with the host peripheral operations: those
 *        configured before the main node, then the others. Their data
 *        points into the export, which must stay linked in.
 * @return Result::kOk; Result::kInvalidArgument if @p chain isn't in the export or a
 *         config unit is malformed; Result::kNotSupported if the main node isn't an
 *         AD2427/28/33/37, the host reaches it over SPI, or a host peripheral is on SPI;
 *         Result::kNoSpace if @p hostScript is too small.
 */
Result ImportSsPlus(const ADI_A2B_BCD& bcd, uint8_t chain, BoardConfig& board, OpList& hostScript) {
  if (chain >= bcd.nNumMasterNode || chain >= A2B_CONF_MAX_NUM_MASTER_NODES ||
      bcd.apNetworkconfig[chain] == nullptr ||
      bcd.apNetworkconfig[chain]->pMasterConfig == nullptr) {
    return Result::kInvalidArgument;
  }
  const ADI_A2B_MASTER_SLAVE_CONFIG& config = *bcd.apNetworkconfig[chain];
  const ADI_A2B_MASTER_NCD& main = *config.pMasterConfig;
  const ADI_A2B_COMMON_CONFIG& common = config.sCommonSetting;

  const ChipCaps* caps =
      LookUpChipCaps(main.sAuthSettings.nVendorID, main.sAuthSettings.nProductID);
  if (caps == nullptr || common.eA2bDeviceInterface != I2C) {
    return Result::kNotSupported;
  }

  MainRegs regs;
  ParseCommon(main, common, regs);
  Parse242x(main, caps->family, regs);
  if (caps->family == ChipFamily::kAd243x) {
    Parse243x(main, regs);
  }
  board.mainNode.Clear();
  Result result = Emit(regs, caps->family, board.mainNode);
  if (result != Result::kOk) {
    return result;
  }

  hostScript.Clear();
  result = ImportHostPeripherals(bcd.sTargetProperties, false, hostScript);
  if (result != Result::kOk) {
    return result;
  }
  board.hostOpsBeforeMainNode = hostScript.Size();
  result = ImportHostPeripherals(bcd.sTargetProperties, true, hostScript);
  if (result != Result::kOk) {
    return result;
  }

  board.mainAddr = common.nMasterI2CAddr;
  board.busAddr = common.nBusI2CAddr;
  board.mainProduct = caps->productId;
  board.mainLocalPower = (main.bLocalPower == A2B_ENABLED);
  board.highPowerSwitchCfg = static_cast<uint8_t>(main.nHighPwrSwitchCfg);
  board.hostScript = &hostScript;
  board.defaultLink = LinkProfile::Utp();
  board.defaultLink.bSideInverted = (regs.control & reg::kControlXcvrbinv) != 0;
  const ADI_A2B_NETWORK_CONFIG& target = bcd.sTargetProperties;
  board.timings.discoveryStartDelayMs =
      (target.nDiscoveryStartDelay > 0) ? target.nDiscoveryStartDelay : 25;
  board.timings.rediscoveryWaitMs = target.nRediscWaitTime;
  return Result::kOk;
}

}  // namespace a2b
