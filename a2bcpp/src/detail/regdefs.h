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
 * @file regdefs.h
 * @brief A2B register addresses and bit masks used by the C++ stack.
 *
 * A trimmed C++ copy of ADI's a2bstack/a2bstack/inc/a2b/regdefs.h: only what the stack
 * uses, added as it is needed. A register address above 0xFF is on MMR page 1
 * (high byte = page, low byte = register); Transport selects the page.
 *
 * C++ form by ClockWorks Signal Processing LLC.
 */

#ifndef A2BCPP_DETAIL_REGDEFS_H_
#define A2BCPP_DETAIL_REGDEFS_H_

#include <cstdint>

namespace a2b {
namespace reg {

// Registers.
inline constexpr uint16_t kChip = 0x00;     ///< I2C address of a sub-node's peripheral.
inline constexpr uint16_t kNodeadr = 0x01;  ///< Main node only: which node the bus address reaches.
inline constexpr uint16_t kVendor = 0x02;   ///< Vendor ID (0xAD).
inline constexpr uint16_t kProduct = 0x03;  ///< Product ID (0x27, 0x28, 0x33, 0x37, ...).
inline constexpr uint16_t kVersion = 0x04;  ///< Silicon version.
inline constexpr uint16_t kCapability = 0x05;  ///< Capability ID.
inline constexpr uint16_t kSwctl = 0x09;       ///< Switch control.
inline constexpr uint16_t kBcdnslots = 0x0A;   ///< Broadcast downstream slots (sub-nodes).
inline constexpr uint16_t kLdnslots = 0x0B;    ///< Local downstream slots (sub-nodes).
inline constexpr uint16_t kLupslots = 0x0C;    ///< Local upstream slots (sub-nodes).
inline constexpr uint16_t kDnslots = 0x0D;     ///< Downstream slots passed on.
inline constexpr uint16_t kUpslots = 0x0E;     ///< Upstream slots passed on.
inline constexpr uint16_t kRespcycs = 0x0F;    ///< Response cycles.
inline constexpr uint16_t kSlotfmt = 0x10;     ///< Main node only: slot format.
inline constexpr uint16_t kDatctl = 0x11;      ///< Main node only: data control.
inline constexpr uint16_t kControl = 0x12;     ///< Control.
inline constexpr uint16_t kDiscvry = 0x13;  ///< Main node only: start discovery (response cycles).
inline constexpr uint16_t kIntstat = 0x15;  ///< Interrupt status.
inline constexpr uint16_t kIntsrc = 0x16;   ///< Main node only: interrupt source.
inline constexpr uint16_t kInttype =
    0x17;  ///< Main node only: interrupt type; reading it clears it.
inline constexpr uint16_t kIntpnd0 = 0x18;  ///< Interrupt pending 0 (write 1 to clear).
inline constexpr uint16_t kIntpnd1 = 0x19;  ///< Interrupt pending 1 (write 1 to clear).
inline constexpr uint16_t kIntpnd2 =
    0x1A;  ///< Main node only: interrupt pending 2 (write 1 to clear).
inline constexpr uint16_t kIntmsk0 = 0x1B;   ///< Interrupt mask 0.
inline constexpr uint16_t kIntmsk1 = 0x1C;   ///< Interrupt mask 1.
inline constexpr uint16_t kIntmsk2 = 0x1D;   ///< Main node only: interrupt mask 2.
inline constexpr uint16_t kBecctl = 0x1E;    ///< Bit error count control.
inline constexpr uint16_t kBecnt = 0x1F;     ///< Bit error count; any write clears it.
inline constexpr uint16_t kNode = 0x29;      ///< Sub-node position and discovery status.
inline constexpr uint16_t kDiscstat = 0x2B;  ///< Main node only: discovery status.
inline constexpr uint16_t kTxactl = 0x2E;  ///< AD242x: LVDS A-side transmit control; AD243x: TXCTL.
inline constexpr uint16_t kTxbctl = 0x30;  ///< AD242x: LVDS B-side transmit control.
inline constexpr uint16_t kI2ccfg = 0x3F;  ///< I2C configuration.
inline constexpr uint16_t kPllctl = 0x40;  ///< PLL control.
inline constexpr uint16_t kI2sgcfg = 0x41;      ///< I2S global configuration.
inline constexpr uint16_t kI2scfg = 0x42;       ///< I2S configuration.
inline constexpr uint16_t kI2srate = 0x43;      ///< I2S rate (sub-nodes).
inline constexpr uint16_t kI2stxoffset = 0x44;  ///< I2S transmit data offset.
inline constexpr uint16_t kI2srxoffset = 0x45;  ///< I2S receive data offset.
inline constexpr uint16_t kSyncoffset = 0x46;   ///< SYNC offset (sub-nodes).
inline constexpr uint16_t kPdmctl = 0x47;       ///< PDM control.
inline constexpr uint16_t kErrmgmt = 0x48;      ///< Error management.
inline constexpr uint16_t kGpiodat = 0x4A;      ///< GPIO output data.
inline constexpr uint16_t kGpiooen = 0x4D;      ///< GPIO output enable.
inline constexpr uint16_t kGpioien = 0x4E;      ///< GPIO input enable.
inline constexpr uint16_t kGpioin = 0x4F;       ///< GPIO input levels (enabled inputs only).
inline constexpr uint16_t kPinten = 0x50;       ///< Pin interrupt enable.
inline constexpr uint16_t kPintinv = 0x51;      ///< Pin interrupt invert.
inline constexpr uint16_t kPincfg = 0x52;       ///< Pin configuration.
inline constexpr uint16_t kI2srrate = 0x56;     ///< I2S reduced rate.
inline constexpr uint16_t kI2srrctl = 0x57;     ///< I2S reduced-rate control.
inline constexpr uint16_t kClk1cfg = 0x59;      ///< CLKOUT1 configuration.
inline constexpr uint16_t kClk2cfg = 0x5A;      ///< CLKOUT2 configuration.
inline constexpr uint16_t kBmmcfg = 0x5B;       ///< Bus monitor mode configuration.
inline constexpr uint16_t kPdmctl2 = 0x5D;      ///< PDM control 2.
inline constexpr uint16_t kUpmask0 = 0x60;      ///< First of the four upstream slot masks.
inline constexpr uint16_t kUpoffset = 0x64;     ///< RX frame buffer offset of LUPSLOTS.
inline constexpr uint16_t kDnmask0 = 0x65;      ///< First of the four downstream slot masks.
inline constexpr uint16_t kDnoffset = 0x69;     ///< RX frame buffer offset of LDNSLOTS.
inline constexpr uint16_t kChipid0 = 0x6A;      ///< First of the six CHIPID registers.
inline constexpr uint16_t kDtcfg = 0x7C;        ///< AD243x: data tunnel configuration.
inline constexpr uint16_t kDtslots = 0x7D;      ///< AD243x: data tunnel slots.
inline constexpr uint16_t kDtdnoffs = 0x7E;     ///< AD243x: data tunnel downstream offset.
inline constexpr uint16_t kDtupoffs = 0x7F;     ///< AD243x: data tunnel upstream offset.
inline constexpr uint16_t kGpioden = 0x80;      ///< GPIO over distance enable.
inline constexpr uint16_t kGpiod0msk = 0x81;    ///< GPIO over distance mask 0; mask n is at + n.
inline constexpr uint16_t kGpioddat = 0x89;     ///< GPIO over distance data.
inline constexpr uint16_t kGpiodinv = 0x8A;     ///< GPIO over distance invert.
inline constexpr uint16_t kSwctl2 = 0xA0;       ///< AD243x: switch control 2.
inline constexpr uint16_t kSwstat2 = 0xA5;      ///< AD243x: switch status 2.
inline constexpr uint16_t kSpicfg = 0xB0;       ///< AD243x: SPI configuration.
inline constexpr uint16_t kSpickdiv = 0xB2;     ///< AD243x: SPI clock divide.
inline constexpr uint16_t kSpifdsize = 0xB3;    ///< AD243x: SPI full-duplex size.
inline constexpr uint16_t kSpifdtarg = 0xB4;    ///< AD243x: SPI full-duplex target.
inline constexpr uint16_t kSpipincfg = 0xB5;    ///< AD243x: SPI pin configuration.
inline constexpr uint16_t kSpiint = 0xB6;       ///< AD243x: SPI interrupt.
inline constexpr uint16_t kSpimsk = 0xB7;       ///< AD243x: SPI interrupt mask.
inline constexpr uint16_t kRxmask0 = 0xB8;      ///< AD243x: first of the eight RX mask registers.
inline constexpr uint16_t kTxxbar0 = 0xC0;      ///< AD243x: first of the 32 TX crossbar registers.
inline constexpr uint16_t kMmrpage = 0xE0;      ///< MMR page select (AD243x).
inline constexpr uint16_t kVmtrVen = 0x100;     ///< VMTR: voltage input enables (AD243x).
inline constexpr uint16_t kVmtrVltg0 = 0x120;  ///< VMTR: measured voltage 0; input n is at + 3 * n.
inline constexpr uint16_t kVmtrVmax0 = 0x121;  ///< VMTR: upper limit 0; input n is at + 3 * n.
inline constexpr uint16_t kVmtrVmin0 = 0x122;  ///< VMTR: lower limit 0; input n is at + 3 * n.
inline constexpr uint16_t kPwmcfg = 0x140;     ///< PWM configuration (AD243x).
inline constexpr uint16_t kPwmfreq = 0x141;    ///< PWM frequency.
inline constexpr uint16_t kPwmblink1 = 0x142;  ///< PWM blink 1.
inline constexpr uint16_t kPwmblink2 = 0x143;  ///< PWM blink 2.
inline constexpr uint16_t kPwm1vall =
    0x148;  ///< PWM1 value, low byte; PWM1H, PWM2L, PWM2H, PWM3L, PWM3H follow.
inline constexpr uint16_t kPwmoevall = 0x14E;  ///< PWM OE value, low byte; the high byte follows.

// NODEADR bits.
inline constexpr uint8_t kNodeadrBrcst = 0x80;  ///< Bus-address writes go to every sub-node.
inline constexpr uint8_t kNodeadrPeri = 0x20;   ///< Bus-address accesses reach a peripheral.
inline constexpr uint8_t kNodeadrNode = 0x0F;   ///< Sub-node position.

// SWCTL, SWCTL2 and SWSTAT2 bits.
inline constexpr uint8_t kSwctlEnsw = 0x01;  ///< Switch on the downstream (B-side) power.
inline constexpr uint8_t kSwctlCfgDetOv =
    0x02;  ///< AD243x: SWCTL2.HPSW_CFG overrides the detected power configuration.
inline constexpr uint8_t kSwctlMode = 0x30;  ///< External switch mode field.
inline constexpr uint8_t kSwctlModeInrush =
    0x20;  ///< Mode 2: avoids inrush faults while a node starts.
inline constexpr uint8_t kSwctl2HpswCfg = 0x07;      ///< Power configuration to use (0 or 4).
inline constexpr uint8_t kSwstat2HpswCfgDet = 0x38;  ///< Power configuration detected.
inline constexpr uint8_t kSwstat2HpswCfgDetPos = 3;  ///< Position of #kSwstat2HpswCfgDet.
inline constexpr uint8_t kSwstat2LviMode = 0x80;     ///< Low-voltage input mode detected.

// CONTROL bits.
inline constexpr uint8_t kControlNewstrct = 0x01;  ///< Apply new structure (self-clearing).
inline constexpr uint8_t kControlEnddsc = 0x02;    ///< End discovery (self-clearing).
inline constexpr uint8_t kControlSoftrst = 0x04;   ///< Soft reset (self-clearing).
inline constexpr uint8_t kControlSwbyp = 0x08;     ///< B side on without the switch.
inline constexpr uint8_t kControlXcvrbinv = 0x10;  ///< Invert the B side (per link; RJ45).
inline constexpr uint8_t kControlI2smsinv = 0x20;  ///< I2S main/sub invert (AD243x).
inline constexpr uint8_t kControlMstr = 0x80;      ///< Run as the main node.

// INTSTAT, INTSRC, INTPND2, INTMSK2 bits.
inline constexpr uint8_t kIntstatIrq = 0x01;      ///< An interrupt is pending.
inline constexpr uint8_t kIntsrcMstint = 0x80;    ///< The main node raised the interrupt.
inline constexpr uint8_t kIntsrcSlvint = 0x40;    ///< A sub-node raised the interrupt.
inline constexpr uint8_t kIntsrcInode = 0x0F;     ///< Which sub-node.
inline constexpr uint8_t kIntpnd2Dscdone = 0x01;  ///< Discovery done.
inline constexpr uint8_t kIntpnd0Srferr =
    0x40;  ///< SRF missed (SRFMISSERR on AD243x): the next node's SRF didn't come.
inline constexpr uint8_t kIntmsk2Dscdone = 0x01;  ///< Discovery-done interrupt enable.

// INTTYPE values.
inline constexpr uint8_t kInttypeSrferr = 5;            ///< SRF missed (INTPND0.SRFERR).
inline constexpr uint8_t kInttypePwrerrFirst = 9;       ///< First power fault type (9-15).
inline constexpr uint8_t kInttypePwrerrLast = 15;       ///< Last power fault type (9-15).
inline constexpr uint8_t kInttypePwrerrCable = 15;      ///< Power fault: undetermined cable fault.
inline constexpr uint8_t kInttypeDscdone = 24;          ///< Discovery done.
inline constexpr uint8_t kInttypeI2cerr = 25;           ///< I2C error (remote access failed).
inline constexpr uint8_t kInttypePwrerrShortGnd = 41;   ///< Power fault: shorted to GND.
inline constexpr uint8_t kInttypePwrerrShortVbus = 42;  ///< Power fault: shorted to VBUS.
inline constexpr uint8_t kInttypeMsgErr = 0x80;         ///< Interrupt messaging error (main only).
inline constexpr uint8_t kInttypeSubReadErr =
    0xFD;  ///< A sub-node's INTTYPE could not be read: it dropped (main only).
inline constexpr uint8_t kInttypeMstrRunning = 255;  ///< The main node's PLL locked.

// NODE, DISCSTAT, I2SGCFG bits.
inline constexpr uint8_t kNodeLast = 0x80;        ///< The sub-node is the last in line.
inline constexpr uint8_t kNodeDiscvd = 0x20;      ///< The sub-node was discovered.
inline constexpr uint8_t kDiscstatDscact = 0x80;  ///< Discovery in progress.
inline constexpr uint8_t kDiscstatDnode = 0x0F;   ///< Position being discovered.
inline constexpr uint8_t kI2sgcfgInv = 0x80;      ///< Invert the sync pin.

// Routing fields: slot formats and the main node's TDM settings (RESPOFFS).
inline constexpr uint8_t kLdnslotsDnmasken = 0x80;  ///< DNMASK0-3 pick the slots played.
inline constexpr uint8_t kDatctlDns = 0x01;         ///< DATCTL: downstream slots enabled.
inline constexpr uint8_t kDatctlUps = 0x02;         ///< DATCTL: upstream slots enabled.
inline constexpr uint8_t kSlotfmtDnsize = 0x07;     ///< Downstream slot size field.
inline constexpr uint8_t kSlotfmtUpsize = 0x70;     ///< Upstream slot size field.
inline constexpr uint8_t kSlotfmtUpsizePos = 4;     ///< Position of #kSlotfmtUpsize.
inline constexpr uint8_t kI2sgcfgTdmmode = 0x07;    ///< TDM mode field (TDM2/I2S to TDM32).
inline constexpr uint8_t kI2sgcfgTdmss = 0x10;      ///< 16-bit TDM channels (else 32-bit).
inline constexpr uint8_t kI2ccfgDatarate = 0x01;    ///< Sub-node: its I2C port at 400 kHz.
inline constexpr uint8_t kI2ccfgFramerate = 0x04;   ///< Sub-node: superframe rate is 44.1 kHz.
inline constexpr uint8_t kI2srateShare = 0x80;      ///< Sub-node: share bus slots (reduced rate).
inline constexpr uint8_t kI2srateReduce = 0x40;     ///< Sub-node: reduce and duplicate samples.
inline constexpr uint8_t kI2srateRate = 0x07;       ///< Sub-node: I2S rate field.
inline constexpr uint8_t kI2srateX2 = 5;            ///< #kI2srateRate: twice the superframe rate.
inline constexpr uint8_t kI2srateX4 = 6;  ///< #kI2srateRate: four times the superframe rate.

// Register fields that the SigmaStudio+ import sets (import/ss_plus). A mask is the
// field; a k...Pos constant is the bit position of a field wider than one bit.
inline constexpr uint8_t kSlotfmtDnfp = 0x08;       ///< Downstream floating-point compression.
inline constexpr uint8_t kI2ccfgDisable = 0x80;     ///< AD243x: I2C pins used as GPIO.
inline constexpr uint8_t kI2sgcfgSyncdis = 0x08;    ///< AD243x: SYNC pin disabled.
inline constexpr uint8_t kI2sgcfgRxondtx1 = 0x08;   ///< AD242x: receive on DTX1.
inline constexpr uint8_t kI2scfgTx0en = 0x01;       ///< AD242x: DTX0 enabled.
inline constexpr uint8_t kI2scfgTx1en = 0x02;       ///< AD242x: DTX1 enabled.
inline constexpr uint8_t kI2scfgRx0en = 0x10;       ///< AD242x: DRX0 enabled.
inline constexpr uint8_t kI2scfgRx1en = 0x20;       ///< AD242x: DRX1 enabled.
inline constexpr uint8_t kI2scfgTxpins = 0x07;      ///< AD243x: number of TX pins.
inline constexpr uint8_t kI2scfgRxpins = 0x70;      ///< AD243x: number of RX pins.
inline constexpr uint8_t kI2scfgRxpinsPos = 4;      ///< Position of #kI2scfgRxpins.
inline constexpr uint8_t kIntmsk0Pwrerr = 0x10;     ///< Power fault interrupt (INTPND0 bit 4).
inline constexpr uint8_t kIntmsk0Srferr = 0x40;     ///< SRF missed interrupt (INTPND0 bit 6).
inline constexpr uint8_t kPincfgDrvstr = 0x01;      ///< High drive strength.
inline constexpr uint8_t kPincfgGpioselPos = 6;     ///< AD243x: position of the GPIO mode field.
inline constexpr uint8_t kPincfgGpiosel = 0xC0;     ///< AD243x: GPIO mode field (unused on AD242x).
inline constexpr uint8_t kI2srrctlEnstrb = 0x10;    ///< Reduced-rate strobe on IO1/ADR1.
inline constexpr uint8_t kClkcfgDivPos = 0;         ///< CLKnCFG: position of the divider field.
inline constexpr uint8_t kClkcfgPdivPos = 5;        ///< CLKnCFG: position of the pre-divider bit.
inline constexpr uint8_t kClkcfgInvPos = 6;         ///< CLKnCFG: position of the invert bit.
inline constexpr uint8_t kClkcfgEn = 0x80;          ///< CLKnCFG: clock output enabled.
inline constexpr uint8_t kPdmctlPdm0en = 0x01;      ///< PDM0 enabled.
inline constexpr uint8_t kPdmctlPdm0slots = 0x02;   ///< PDM0: two microphones (else one).
inline constexpr uint8_t kPdmctlPdm1en = 0x04;      ///< PDM1 enabled.
inline constexpr uint8_t kPdmctlPdm1slots = 0x08;   ///< PDM1: two microphones (else one).
inline constexpr uint8_t kPdmctl2Pdmaltclk = 0x10;  ///< PDM clock on the alternate pin.
inline constexpr uint8_t kPdmctl2Hpfcorner = 0xC0;  ///< AD243x: HPF corner (unused on AD242x).
inline constexpr uint8_t kSpicfgSpimode = 0x03;     ///< SPI mode field.
inline constexpr uint8_t kSpicfgTnlowner = 0x04;    ///< Data tunnel owner.
inline constexpr uint8_t kSpicfgCphaPos = 3;        ///< Position of the clock phase bit.
inline constexpr uint8_t kSpicfgCpolPos = 4;        ///< Position of the clock polarity bit.
inline constexpr uint8_t kSpicfgEnfdcsPos = 5;   ///< Position of the full-duplex clock stretch bit.
inline constexpr uint8_t kSpicfgSpifdssPos = 6;  ///< Position of the full-duplex SS field.
inline constexpr uint8_t kSpifdtargSselPos = 6;  ///< Position of the full-duplex target SS field.
inline constexpr uint8_t kSpipincfgSpimss0en = 0x10;  ///< SS0 output enabled.
inline constexpr uint8_t kSpipincfgSpimss1en = 0x20;  ///< SS1 output enabled.
inline constexpr uint8_t kSpipincfgSpimss2en = 0x40;  ///< SS2 output enabled.
inline constexpr uint8_t kDtcfgDtfrst = 0x02;         ///< First node of the data tunnel.
inline constexpr uint8_t kDtcfgDtlast = 0x04;         ///< Last node of the data tunnel.
inline constexpr uint8_t kDtslotsUpPos = 4;           ///< Position of the upstream tunnel slots.
inline constexpr uint8_t kPwmcfgPwm1en = 0x01;        ///< PWM1 enabled.
inline constexpr uint8_t kPwmcfgPwm2en = 0x02;        ///< PWM2 enabled.
inline constexpr uint8_t kPwmcfgPwm3en = 0x04;        ///< PWM3 enabled.
inline constexpr uint8_t kPwmcfgPwmoeen = 0x08;       ///< PWM output enable enabled.

// Other constants.
inline constexpr uint8_t kCountChipid = 6;  ///< Number of CHIPID registers.
inline constexpr uint8_t kVmtrInputs = 8;   ///< Number of VMTR voltage inputs.

}  // namespace reg
}  // namespace a2b

#endif  // A2BCPP_DETAIL_REGDEFS_H_
