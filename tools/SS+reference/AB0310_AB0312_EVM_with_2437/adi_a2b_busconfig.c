/*******************************************************************************
Copyright (c) 2026 - Analog Devices Inc. All Rights Reserved.
This software is proprietary & confidential to Analog Devices, Inc.
and its licensors.
******************************************************************************
* @file: adi_a2b_busconfig.c
* @brief: This file contains A2B Schematic information
* @date: Monday, 19 October 2026-23:19:59
* BCF Version - 1.0.0
* A2BDLL- 2.0.1.0
* SigmaStudio+ version- 2.3.0
* Developed by: Automotive Software and Systems team, Bangalore, India
* THIS IS A SIGMASTUDIO GENERATED FILE AND SHALL NOT BE MODIFIED OUTSIDE OF SIGMASTUDIO 
*****************************************************************************/

/*! \addtogroup Target_Independent
* @{
*/

/*! \addtogroup Bus_Configuration Bus Configuration
* @{
*/

#include "adi_a2b_datatypes.h"
#include "adi_a2b_graphdata.h"
#include "adi_a2b_busconfig.h"

static ADI_A2B_MASTER_SLAVE_CONFIG sChainConfig0;
static ADI_A2B_MASTER_NCD sMasterNode0;
static ADI_A2B_SLAVE_NCD sChain0_SlaveNode0;

ADI_A2B_MEM_BCF_CONST_DATA
 static A2B_PERIPHERAL_DEVICE_CONFIG  Target_PeriConfig0;

ADI_A2B_MEM_BCF_CONST_DATA
 static A2B_PERIPHERAL_DEVICE_CONFIG  Target_PeriConfig1;

ADI_A2B_MEM_BCF_CONST_DATA
 static ADI_A2B_STREAM  sChain0_Master0_Src_Stream0;

ADI_A2B_MEM_BCF_CONST_DATA
 static ADI_A2B_STREAM  sChain0_Master0_Snk_Stream0;

ADI_A2B_MEM_BCF_CONST_DATA
  static A2B_PERIPHERAL_DEVICE_CONFIG  sChain0_Slave0_PeriConfig0;

ADI_A2B_MEM_BCF_CONST_DATA
  static A2B_PERIPHERAL_DEVICE_CONFIG  sChain0_Slave0_PeriConfig1;

ADI_A2B_MEM_BCF_CONST_DATA
 static ADI_A2B_STREAM  sChain0_Slave0_Src_Stream0;

ADI_A2B_MEM_BCF_CONST_DATA
 static ADI_A2B_STREAM  sChain0_Slave0_Snk_Stream0;


ADI_A2B_MEM_BCF_DATA

/* BUS CONFIGURATION */ 
ADI_A2B_BCD sBusDescription = 
 { 
	/* Number of master nodes/daisy chains in the network */ 
	.nNumMasterNode  = 1u, 
 
	/* Array of Master-slave daisy chains */ 
 	.apNetworkconfig = {  &sChainConfig0 }, 

	/* Network control guidance */ 
	.sTargetProperties    = 
 	{ 
		/* Discovery Mode Selection */ 
		.eDiscoveryMode     = A2B_SIMPLE_DISCOVERY,

		/* PLL Lock Time (tPLK). Delay (in msec) to wait after a software reset and before starting discovery. */ 
		.nDiscoveryStartDelay     = 25,

		/* Wait time between discovery attempts for systems containing AD243x Transceivers. */ 
		.nRediscWaitTime     = 25,

		/*  Enable/Disable  Line diagnostics (Post discovery) */ 
		.bLineDiagnostics   = A2B_ENABLED,

		/*  Enable/Disable automatic discovery upon critical faults (during discovery process) */ 
		.bAutoDiscCriticalFault = A2B_DISABLED,

		/*  Number of automatic discovery attempts (during discovery process) */ 
		.nAttemptsCriticalFault = 0,

		/* Auto-rediscovery upon post discovery line fault */ 
		.bAutoRediscOnFault = A2B_DISABLED,

		/*! Interval (in milliseconds) between re-discovery attempt during line fault */ 
		.nRediscInterval = 100,

		/*! Enable Cross Talk Fix, applicable only for AD2425 series */ 
		/*! Enable partial discovery upon line faults */ 
		.bEnablePartialDisc = A2B_DISABLED,

		.bCrossTalkFix = A2B_DISABLED,

		/*! Enable override bus self discovery */ 
		.bOverrideBusSelfDisc = A2B_ENABLED,

		/*! Network Cable Type */ 
		.eCableType = A2B_CABLETYPE_RJ45,

		/*! Number of peripheral devices connected to Target */
		.nNumPeriDevice = 2,

		/*! Array of peripheral configuration pointers */
 		.apPeriConfig = { 	&Target_PeriConfig0, &Target_PeriConfig1, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR }
 	}, 

 };

 /************************************************* CHAIN 0 DESCRIPTION  ********************************************************************************/

 /* MASTER-SLAVE DAISY CHAIN0 */ 
 
ADI_A2B_MEM_BCF_DATA 
static ADI_A2B_MASTER_SLAVE_CONFIG sChainConfig0= 
 { 
 	/* Pointer to master node */ 
	.pMasterConfig = &sMasterNode0, 
 
	/*! Number of Slave nodes */ 
	.nNumSlaveNode = 1, 
 
	/*! Array of slave node pointers */ 
	.apSlaveConfig = { &sChain0_SlaveNode0, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR}, 

	.sCommonSetting = 
 	{ 
		/*! Master I2C address - 7 bit */ 
		.nMasterI2CAddr = 0x68, 

		/*! Bus I2C address - 7 bit */ 
		.nBusI2CAddr = 0x69, 

		/*! Enable down stream */ 
		.bEnableDwnstream = A2B_ENABLED, 

		/*! Enable down stream */ 
		.bEnableUpstream = A2B_ENABLED, 

		/*! Down slot size */ 
		.nDwnSlotSize = A2B_DNSLOT_SIZE_24, 

 		/*! Up slot size  */ 
		.nUpSlotSize = A2B_UPSLOT_SIZE_24 ,

 		/*! Floating point compression for upstream  */ 
		.bUpstreamCompression  = A2B_DISABLED,

		/*! Floating point compression for downstream  */  
		.bDwnstreamCompression = A2B_DISABLED,

		/*! Reduce Data Rate on A2B Bus   */  
		.bEnableReduceRate = A2B_DISABLED,

		/*! System level reduced rate factor   */  
		.nSysRateDivFactor =1,

		/*! A2B master device interface connected from Target processor */ 
		.eA2bDeviceInterface = I2C,

	} 
 };

 /************************************************* START OF MASTER NODE 0 DESCRIPTION ********************************************************************************/ 

 /*  MASTER NODE DESCRIPTION   */ 

ADI_A2B_MEM_BCF_DATA
static ADI_A2B_MASTER_NCD sMasterNode0= 
{
	/*! Node ID */
	.nNodeID			= 0x0u ,

	/*! Source node ID : '0xFF' indicates Target processor */
	.nSrcNodeID			= 0xFFu ,

	/*! Transceiver part number */
	.ePartNum			= ADI_A2B_AD2437 ,

	/* Node Power Configuration */
	.bLocalPower		= A2B_ENABLED ,

	/* Expected high power switch Configuration */
	.nHighPwrSwitchCfg	= 0x4u ,

	.sAuthSettings =
	{
		/*! Vendor ID - ADI */
		.nVendorID			= 0xADu ,

		/*! Silicon version  - ADI */
		.nVersionID			= 0x21u ,

		/*! Product ID - AD24xx */
		.nProductID			= 0x37u ,

		/*! Interface Capability - I2C */
		.nCapability		= 0x1u ,

		/*! Enable/Disable Transceiver Authentication */
		.bTransceiverAuth		= A2B_ENABLED ,

	},

	.sCustomNodeAuthSettings =
	{
		/*! Enable/Disable Custom Node ID settings */
		.bCustomNodeIdAuth		= A2B_DISABLED ,

	},

	.sI2SSettings =
	{
		/*! TDM mode  */
		.nTDMMode			= A2B_TDM2 ,

		/*! TDM channel size  */
		.nTDMChSize			= A2B_32BIT_TDM ,

		/*! SYNC mode - Pulse/50% duty cycle   */
		.nSyncMode			= A2B_ALTERNATE_SYNC ,

		/*! SYNC Polarity- Rising/Falling edge */
		.nSyncPolarity		= FALLING_EDGE ,

		/*! Early frame sync status */
		.bEarlySync			= A2B_ENABLED ,

		/*! DTXn change BCLK edge */
		.nBclkTxPolarity	= RAISING_EDGE ,

		/*! DRXn Sampling BCLK edge */
		.nBclkRxPolarity	= RAISING_EDGE ,

		/*! Interleave slots between Tx pins  */
		.bTXInterleave		= A2B_DISABLED ,

		/*! Interleave slots between Rx pins  */
		.bRXInterleave		= A2B_DISABLED ,

		/*! Transmit Channel offset in TDM - 0 to 63 */
		.nTxOffset			= 0x0u ,

		/*! Receive channel offset in TDM - 0 to 63 */
		.nRxOffset			= 0x0u ,

		/*! Tx Pin TriState before driving TDM slots */
		.bTriStateBeforeTx	= A2B_DISABLED ,

		/*! Tx Pin Tristate after driving TDM slots */
		.bTriStateAfterTx	= A2B_DISABLED ,

		.sI2SRateConfig =
		{
			/*! Enable RR valid bit in LSB */
			.bRRValidBitLSB	= A2B_DISABLED ,

			/*! Enable Valid RR bit in Extra bit */
			.bRRValidBitExtraBit	= A2B_DISABLED ,

			/*! Enable Reduced rate strobe in ADR1/IO1 */
			.bRRStrobe	= A2B_DISABLED ,

			/*! Strobe direction High or Low */
			.bRRStrobeDirection	= A2B_LOW ,

		},
		/*! Enable/Disable Sync */
		.bSync	= A2B_ENABLED ,

	},
	.sPDMSettings =
	{
		/*! Number of PDM0 slots  */
		.nNumSlotsPDM0		= A2B_PDM0SLOTS_1 ,

		/*! Number of PDM1 slots  */
		.nNumSlotsPDM1		= A2B_PDM1SLOTS_1 ,

		/*! Use High Pass Filter  */
		.bHPFUse		= A2B_DISABLED ,

		/*! PDM rate  */
		.nPDMRate		= A2B_PDM_RATE_SFF ,

		/*! PDM Inverted Version of Alternate Clock */
		.bPDMInvClk		= A2B_DISABLED ,

		/*! PDM Alternate Clock */
		.bPDMAltClk		= A2B_DISABLED ,

		/*! PDM0 Falling Edge First */
		.bPDM0FallingEdgeFrst		= A2B_DISABLED ,

		/*! PDM1 Falling Edge First */
		.bPDM1FallingEdgeFrst		= A2B_DISABLED ,

		/*! PDM Destination */
		.ePDMDestination		= A2B_BUS_ONLY ,

		/*! HPF Corner Select */
		.ePDMHpfCorner		= HPF_CORNERFREQ_1Hz ,

	},

	.sConfigCtrlSettings =
	{
		/*! Early acknowledge for I2C read/write */
		.bI2CEarlyAck		= A2B_DISABLED ,

		/*! Response cycles  */
		.nRespCycle			= 0x7Du ,

		/*! Pass up slots */
		.nPassUpSlots		= 0x2u ,

		/*! Pass down slots */
		.nPassDwnSlots		= 0x2u ,

		/*! Data control for Master */
		.nDatctrl		= 0x3u ,

		/*! I2C Interface */
		.bDisableI2c	= A2B_DISABLED ,

	},
	.oPinAssignSettings =
	{
		.oSio0=
		{
			/*! Functionality */
			.eFunc			= FUNC_RX0,
			/*! GPIO mapping*/
			.eIoMapping			= NA,
		},
		.oSio1=
		{
			/*! Functionality */
			.eFunc			= FUNC_GPIO,
			/*! GPIO mapping*/
			.eIoMapping			= GPIO1,
		},
		.oSio2=
		{
			/*! Functionality */
			.eFunc			= FUNC_GPIO,
			/*! GPIO mapping*/
			.eIoMapping			= GPIO2,
		},
		.oSio3=
		{
			/*! Functionality */
			.eFunc			= FUNC_GPIO,
			/*! GPIO mapping*/
			.eIoMapping			= GPIO3,
		},
		.oSio4=
		{
			/*! Functionality */
			.eFunc			= FUNC_TX0,
			/*! GPIO mapping*/
			.eIoMapping			= NA,
		},
		.oGPIO7=
		{
			/*! Functionality */
			.eFunc			= FUNC_GPIO,
			/*! GPIO mapping*/
			.eIoMapping			= GPIO7,
		},
		.oSDA=
		{
			/*! Functionality */
			.eFunc			= FUNC_I2C,
			/*! GPIO mapping*/
			.eIoMapping			= NA,
		},
		.oSCL=
		{
			/*! Functionality */
			.eFunc			= FUNC_I2C,
			/*! GPIO mapping*/
			.eIoMapping			= NA,
		},
		.oMISO=
		{
			/*! Functionality */
			.eFunc			= FUNC_SPI_SLAVE,
			/*! GPIO mapping*/
			.eIoMapping			= NA,
		},
		.oMOSI=
		{
			/*! Functionality */
			.eFunc			= FUNC_SPI_SLAVE,
			/*! GPIO mapping*/
			.eIoMapping			= NA,
		},
		.oSCK=
		{
			/*! Functionality */
			.eFunc			= FUNC_SPI_SLAVE,
			/*! GPIO mapping*/
			.eIoMapping			= NA,
		},
		.oADR1=
		{
			/*! Functionality */
			.eFunc			= FUNC_SS0,
			/*! GPIO mapping*/
			.eIoMapping			= NA,
		},
		.oADR2=
		{
			/*! Functionality */
			.eFunc			= FUNC_I2C_CLKOUT2,
			/*! GPIO mapping*/
			.eIoMapping			= NA,
		},
			/*! GPIO mode*/
			.nGpioMode			= 0x0u ,
	},
	.sGPIOSettings =
	{
		.sPinMuxSettings =
		{
			/*! GPIO 0 Pin multiplexing */
			.bGPIO0PinUsage	= A2B_GPIO_0_DISABLE ,	/*SPI(S)*/

			/*! GPIO 1 Pin multiplexing */
			.bGPIO1PinUsage	= A2B_GPIO_1_DISABLE ,	/*DISABLE*/

			/*! GPIO 2 Pin multiplexing */
			.bGPIO2PinUsage	= A2B_GPIO_2_DISABLE ,	/*DISABLE*/

			/*! GPIO 3 Pin multiplexing */
			.bGPIO3PinUsage	= A2B_GPIO_3_DISABLE ,	/*DISABLE*/

			/*! GPIO 4 Pin multiplexing */
			.bGPIO4PinUsage	= A2B_GPIO_4_DISABLE ,	/*SS0*/

			/*! GPIO 5 Pin multiplexing */
			.bGPIO5PinUsage	= A2B_GPIO_5_DISABLE ,	/*SPI(S)*/

			/*! GPIO 6 Pin multiplexing */
			.bGPIO6PinUsage	= A2B_GPIO_6_DISABLE ,	/*SPI(S)*/

			/*! GPIO 7 Pin multiplexing */
			.bGPIO7PinUsage	= A2B_GPIO_7_INPUT ,

		},
		.sPinIntConfig =
		{
			/*! Enable GPIO 0 Input pin interrupt  */
			.bGPIO0Interrupt	= A2B_DISABLED ,

			/*! Interrupt polarity - GPIO 0 Input pin  */
			.bGPIO0IntPolarity	= RAISING_EDGE ,

			/*! Enable GPIO 1 Input pin interrupt  */
			.bGPIO1Interrupt	= A2B_DISABLED ,

			/*! Interrupt polarity - GPIO 1 Input pin  */
			.bGPIO1IntPolarity	= RAISING_EDGE ,

			/*! Enable GPIO 2 Input pin interrupt  */
			.bGPIO2Interrupt	= A2B_DISABLED ,

			/*! Interrupt polarity - GPIO 2 Input pin  */
			.bGPIO2IntPolarity	= RAISING_EDGE ,

			/*! Enable GPIO 3 Input pin interrupt  */
			.bGPIO3Interrupt	= A2B_DISABLED ,

			/*! Interrupt polarity - GPIO 3 Input pin  */
			.bGPIO3IntPolarity	= RAISING_EDGE ,

			/*! Enable GPIO 4 Input pin interrupt  */
			.bGPIO4Interrupt	= A2B_DISABLED ,

			/*! Interrupt polarity - GPIO 4 Input pin  */
			.bGPIO4IntPolarity	= RAISING_EDGE ,

			/*! Enable GPIO 5 Input pin interrupt  */
			.bGPIO5Interrupt	= A2B_DISABLED ,

			/*! Interrupt polarity - GPIO 5 Input pin  */
			.bGPIO5IntPolarity	= RAISING_EDGE ,

			/*! Enable GPIO 6 Input pin interrupt  */
			.bGPIO6Interrupt	= A2B_DISABLED ,

			/*! Interrupt polarity - GPIO 6 Input pin  */
			.bGPIO6IntPolarity	= RAISING_EDGE ,

			/*! Enable GPIO 7 Input pin interrupt  */
			.bGPIO7Interrupt	= A2B_DISABLED ,

			/*! Interrupt polarity - GPIO 7 Input pin  */
			.bGPIO7IntPolarity	= RAISING_EDGE ,

		},
		.sOutPinVal =
		{
			/*! Data value for GPIO 0 output pin  */
			.bGPIO0Val = A2B_LOW ,

			/*! Data value for GPIO 1 output pin  */
			.bGPIO1Val = A2B_LOW ,

			/*! Data value for GPIO 2 output pin  */
			.bGPIO2Val = A2B_LOW ,

			/*! Data value for GPIO 3 output pin  */
			.bGPIO3Val = A2B_LOW ,

			/*! Data value for GPIO 4 output pin  */
			.bGPIO4Val = A2B_LOW ,

			/*! Data value for GPIO 5 output pin  */
			.bGPIO5Val = A2B_LOW ,

			/*! Data value for GPIO 6 output pin  */
			.bGPIO6Val = A2B_LOW ,

			/*! Data value for GPIO 7 output pin  */
			.bGPIO7Val = A2B_LOW ,

		},

		/*! Digital Pin drive strength */
		.bHighDriveStrength = A2B_ENABLED ,

		/*! IRQ Pin Invert */
		.bIRQInv = A2B_DISABLED ,

		/*! Enable tristate when inactive */
		.bIRQTriState = A2B_DISABLED ,

	},

	.sInterruptSettings =
	{
		/*! Report Header count error  */
		.bReportHDCNTErr		= A2B_DISABLED ,

		/*! Report Data decoding error  */
		.bReportDDErr			= A2B_DISABLED ,

		/*! Report Data CRC error  */
		.bReportCRCErr			= A2B_DISABLED ,

		/*! Report Data Parity error  */
		.bReportDataParityErr	= A2B_DISABLED ,

		/*! Report Data Bus Power error  */
		.bReportPwrErr			= A2B_ENABLED ,

		/*! Report bit error count overflow error  */
		.bReportErrCntOverFlow	= A2B_DISABLED ,

		/*! Report SRF miss error  */
		.bReportSRFMissErr	= A2B_DISABLED ,

		/*! Report GPIO  0 Interrupt */
		.bReportGPIO0			= A2B_DISABLED ,

		/*! Report GPIO  1 Interrupt */
		.bReportGPIO1			= A2B_DISABLED ,

		/*! Report GPIO  2 Interrupt */
		.bReportGPIO2			= A2B_DISABLED ,

		/*! Report GPIO  3 Interrupt */
		.bReportGPIO3			= A2B_DISABLED ,

		/*! Report GPIO  4 Interrupt */
		.bReportGPIO4			= A2B_DISABLED ,

		/*! Report GPIO  5 Interrupt */
		.bReportGPIO5			= A2B_DISABLED ,

		/*! Report GPIO  6 Interrupt */
		.bReportGPIO6			= A2B_DISABLED ,

		/*! Report GPIO  7 Interrupt */
		.bReportGPIO7			= A2B_DISABLED ,

		/*! Report I2C failure error  */
		.bReportI2CErr			= A2B_DISABLED ,

		/*! Report Discovery Completion */
		.bDiscComplete			= A2B_ENABLED ,

		/*! Report Interrupt frame error */
		.bIntFrameCRCErr		= A2B_DISABLED ,

		/*! Report Interrupt requests  */
		.bSlaveIntReq			= A2B_ENABLED ,

	},

	.sGPIODSettings =
	{
		.sGPIOD0Config =
		{
			/*! Enable/Disable GPIO over distance   */
			.bGPIODistance	= A2B_DISABLED ,

			/*! Enable/Disable  */
			.bGPIOSignalInv	= A2B_DISABLED ,

			/*! Bus port masks */ 
			.abBusPortMask = { A2B_MASK_BUSFLAG_0, A2B_MASK_BUSFLAG_1, A2B_MASK_BUSFLAG_2, A2B_MASK_BUSFLAG_3, A2B_MASK_BUSFLAG_4, A2B_MASK_BUSFLAG_5, A2B_MASK_BUSFLAG_6, A2B_MASK_BUSFLAG_7},


		},

		.sGPIOD1Config =
		{
			/*! Enable/Disable GPIO over distance   */
			.bGPIODistance	= A2B_DISABLED ,

			/*! Enable/Disable  */
			.bGPIOSignalInv	= A2B_DISABLED ,

			/*! Bus port masks */ 
			.abBusPortMask = { A2B_MASK_BUSFLAG_0, A2B_MASK_BUSFLAG_1, A2B_MASK_BUSFLAG_2, A2B_MASK_BUSFLAG_3, A2B_MASK_BUSFLAG_4, A2B_MASK_BUSFLAG_5, A2B_MASK_BUSFLAG_6, A2B_MASK_BUSFLAG_7},


		},

		.sGPIOD2Config =
		{
			/*! Enable/Disable GPIO over distance   */
			.bGPIODistance	= A2B_DISABLED ,

			/*! Enable/Disable  */
			.bGPIOSignalInv	= A2B_DISABLED ,

			/*! Bus port masks */ 
			.abBusPortMask = { A2B_MASK_BUSFLAG_0, A2B_MASK_BUSFLAG_1, A2B_MASK_BUSFLAG_2, A2B_MASK_BUSFLAG_3, A2B_MASK_BUSFLAG_4, A2B_MASK_BUSFLAG_5, A2B_MASK_BUSFLAG_6, A2B_MASK_BUSFLAG_7},


		},

		.sGPIOD3Config =
		{
			/*! Enable/Disable GPIO over distance   */
			.bGPIODistance	= A2B_DISABLED ,

			/*! Enable/Disable  */
			.bGPIOSignalInv	= A2B_DISABLED ,

			/*! Bus port masks */ 
			.abBusPortMask = { A2B_MASK_BUSFLAG_0, A2B_MASK_BUSFLAG_1, A2B_MASK_BUSFLAG_2, A2B_MASK_BUSFLAG_3, A2B_MASK_BUSFLAG_4, A2B_MASK_BUSFLAG_5, A2B_MASK_BUSFLAG_6, A2B_MASK_BUSFLAG_7},


		},

		.sGPIOD4Config =
		{
			/*! Enable/Disable GPIO over distance   */
			.bGPIODistance	= A2B_DISABLED ,

			/*! Enable/Disable  */
			.bGPIOSignalInv	= A2B_DISABLED ,

			/*! Bus port masks */ 
			.abBusPortMask = { A2B_MASK_BUSFLAG_0, A2B_MASK_BUSFLAG_1, A2B_MASK_BUSFLAG_2, A2B_MASK_BUSFLAG_3, A2B_MASK_BUSFLAG_4, A2B_MASK_BUSFLAG_5, A2B_MASK_BUSFLAG_6, A2B_MASK_BUSFLAG_7},


		},

		.sGPIOD5Config =
		{
			/*! Enable/Disable GPIO over distance   */
			.bGPIODistance	= A2B_DISABLED ,

			/*! Enable/Disable  */
			.bGPIOSignalInv	= A2B_DISABLED ,

			/*! Bus port masks */ 
			.abBusPortMask = { A2B_MASK_BUSFLAG_0, A2B_MASK_BUSFLAG_1, A2B_MASK_BUSFLAG_2, A2B_MASK_BUSFLAG_3, A2B_MASK_BUSFLAG_4, A2B_MASK_BUSFLAG_5, A2B_MASK_BUSFLAG_6, A2B_MASK_BUSFLAG_7},


		},

		.sGPIOD6Config =
		{
			/*! Enable/Disable GPIO over distance   */
			.bGPIODistance	= A2B_DISABLED ,

			/*! Enable/Disable  */
			.bGPIOSignalInv	= A2B_DISABLED ,

			/*! Bus port masks */ 
			.abBusPortMask = { A2B_MASK_BUSFLAG_0, A2B_MASK_BUSFLAG_1, A2B_MASK_BUSFLAG_2, A2B_MASK_BUSFLAG_3, A2B_MASK_BUSFLAG_4, A2B_MASK_BUSFLAG_5, A2B_MASK_BUSFLAG_6, A2B_MASK_BUSFLAG_7},


		},

		.sGPIOD7Config =
		{
			/*! Enable/Disable GPIO over distance   */
			.bGPIODistance	= A2B_DISABLED ,

			/*! Enable/Disable  */
			.bGPIOSignalInv	= A2B_DISABLED ,

			/*! Bus port masks */ 
			.abBusPortMask = { A2B_MASK_BUSFLAG_0, A2B_MASK_BUSFLAG_1, A2B_MASK_BUSFLAG_2, A2B_MASK_BUSFLAG_3, A2B_MASK_BUSFLAG_4, A2B_MASK_BUSFLAG_5, A2B_MASK_BUSFLAG_6, A2B_MASK_BUSFLAG_7},


		},

	},

	.sClkOutSettings =
	{
		/*! Enable Clock1 inversion */
		.bClk1Inv			= A2B_DISABLED ,

		/*! Clk1 pre-division */
		.bClk1PreDiv			= A2B_CLKOUT_PREDIV_02,

		/*! Clk1 division */
		.bClk1Div			= A2B_CLKOUT_DIV_2,

		/*! Enable Clock2 inversion */
		.bClk2Inv			= A2B_DISABLED ,

		/*! Clk2 pre-division */
		.bClk2PreDiv			= A2B_CLKOUT_PREDIV_02,

		/*! Clk2 division */
		.bClk2Div			= A2B_CLKOUT_DIV_4,

	},

	.sRegSettings =
	{
		/*! Switch control register */
		.nSWCTL		= 0x11u ,

		/*! PDM Control register  */
		.nPDMCTL	= 0x0u ,

		/*! Test mode register */
		.nTESTMODE	= 0x0u ,

		/*! Error control register */
		.nBECCTL	= 0xEFu ,

		/*! Error management register  */
		.nERRMGMT	= 0x0u ,

		/*! I2S test register  */
		.nI2STEST	= 0x0u ,

		/*! Generate error  */
		.nGENERR	= 0x0u ,

		/*! Raise interrupt register */
		.nRAISE		= 0x0u ,

		/*! Bus monitor configuration  */
		.nBMMCFG	= 0x0u ,

		/*! PDM Control 2 register  */
		.nPDMCTL2	= 0x0u ,

		/*! PLL Control register  */
		.nPLLCTL	= 0x0u ,

		/*! Control Register */
		.nCONTROL	= 0x10u ,

		/*! Switch Control 2 Register */
		.nSWCTL2	= 0x0u ,

		/*! Switch Control 5 Register */
		.nSWCTL5	= 0x0u ,

		/*!  TX Control Register */
		.nTXACTL	= 0x0u , /* TXACTL is being resued for TXCTL for AD243x only */

	},

	.oSpiSettings =
	{
		/*! SPI mode  */
		.nSPIMode	= 0x0u ,

		/*! Lead Clk Edge(CPOL) */
		.nCPOL	= 0x0u ,

		/*! Sample Clk Edge(CPHA)  */
		.nCPHA	= 0x0u ,

		/*! Clock Div factor */
		.nClkDivFactor	= 0x0u ,

		/*! SPI Mstr Slave Select 2 Enable */
		.nMstrSS2En	= 0x0u ,

		/*! SPI Mstr Slave Select 1 Enable */
		.nMstrSS1En	= 0x0u ,

		/*! SPI Mstr Slave Select 0 Enable */
		.nMstrSS0En	= 0x01u ,

		/*! SPI GPIO Select */
		.nGpioSelect	= 0x0u ,

		/*! SPI GPIO Enable */
		.nGpioEnable	= 0x0u ,

		/*! Full Duplex Slave select  */
		.nFDSize	= 0x0u ,

		/*! Full target Node */
		.nFDTargetNode	= 0x0u ,

		/*! Full Duplex Target Slave select  */
		.nTargetSSel	= 0x0u ,

		/*! Full Duplex clock stretch - enable/dsiable */
		.bFDClkStretchEn	= A2B_DISABLED ,

		/*! SPI Error interrupts */
		.oSpiIntSettings =
		{
			/*! FIFO underflow Error */
			.bFifoUnderflow	= A2B_DISABLED ,

			/*! FIFO overflow Error */
			.bFifoOverflow	= A2B_DISABLED ,

			/*! Bad Command */
			.bBadCommand	= A2B_DISABLED ,

			/*! Data Tunnel Error */
			.bDataTunnel	= A2B_DISABLED ,

			/*! SPI Remote I2C Access Error */
			.bSpiRemoteI2cAccess	= A2B_DISABLED ,

			/*! SPI Remote Reg Access Error */
			.bSpiRemoteRegAccess	= A2B_DISABLED ,

			/*! SPI Done */
			.bSpiDone	= A2B_DISABLED ,

		},
		/*! Data tunnel enable*/
		.bDTEnable	= 0x0u ,

		/*! Tunnel Ownership - Owner/Responder */
		.eTunnelOwnership	= DT_RESPONDER,

		/*! Tunnel Position  */
		.eTunnelPos	= DT_POS_MIDDLE,

		/*! Data tunnel downstream slots */
		.nDTDwnstrmSlots	= 0x0u ,

		/*! Data tunnel downstream offset */
		.nDTDwnstrmOffset	= 0x0u ,

		/*! Data tunnel upstream slots */
		.nDTUpstrmSlots	= 0x0u ,

		/*! Data tunnel upstream offset */
		.nDTUpstrmOffset	= 0x0u ,

	},
	.anTxXbarSettings =
	{
		0x0u, /*TXXBAR 0*/
		0x1u, /*TXXBAR 1*/
		0x2u, /*TXXBAR 2*/
		0x3u, /*TXXBAR 3*/
		0x4u, /*TXXBAR 4*/
		0x5u, /*TXXBAR 5*/
		0x6u, /*TXXBAR 6*/
		0x7u, /*TXXBAR 7*/
		0x8u, /*TXXBAR 8*/
		0x9u, /*TXXBAR 9*/
		0xAu, /*TXXBAR 10*/
		0xBu, /*TXXBAR 11*/
		0xCu, /*TXXBAR 12*/
		0xDu, /*TXXBAR 13*/
		0xEu, /*TXXBAR 14*/
		0xFu, /*TXXBAR 15*/
		0x10u, /*TXXBAR 16*/
		0x11u, /*TXXBAR 17*/
		0x12u, /*TXXBAR 18*/
		0x13u, /*TXXBAR 19*/
		0x14u, /*TXXBAR 20*/
		0x15u, /*TXXBAR 21*/
		0x16u, /*TXXBAR 22*/
		0x17u, /*TXXBAR 23*/
		0x18u, /*TXXBAR 24*/
		0x19u, /*TXXBAR 25*/
		0x1Au, /*TXXBAR 26*/
		0x1Bu, /*TXXBAR 27*/
		0x1Cu, /*TXXBAR 28*/
		0x1Du, /*TXXBAR 29*/
		0x1Eu, /*TXXBAR 30*/
		0x1Fu, /*TXXBAR 31*/
	},
	.anRxXbarSettings =
	{
		0xFFu,	/*RXMASK 0*/
		0xFFu,	/*RXMASK 1*/
		0xFFu,	/*RXMASK 2*/
		0xFFu,	/*RXMASK 3*/
		0xFFu,	/*RXMASK 4*/
		0xFFu,	/*RXMASK 5*/
		0xFFu,	/*RXMASK 6*/
		0xFFu,	/*RXMASK 7*/
	},
	.oVmtrSettings =
	{
		/*! Voltage Enable */
		.bVEN			= A2B_DISABLED ,

		/*! Interrupt Enable */
		.bIntEN			= A2B_DISABLED ,

		/*! VMAX Check Result */
		.nMxStat			= 0x0u ,

		/*! VMIN Check Result */
		.nMinStat			= 0x0u ,

		.oVtg0 =
		{
			/*! VMIN*/
			.nVmin			= 0x0u ,

			/*! VMAX*/
			.nVmax			= 0xFFu ,

		},
		.oVtg1 =
		{
			/*! VMIN*/
			.nVmin			= 0x0u ,

			/*! VMAX*/
			.nVmax			= 0xFFu ,

		},
		.oVtg2 =
		{
			/*! VMIN*/
			.nVmin			= 0x0u ,

			/*! VMAX*/
			.nVmax			= 0xFFu ,

		},
		.oVtg3 =
		{
			/*! VMIN*/
			.nVmin			= 0x0u ,

			/*! VMAX*/
			.nVmax			= 0xFFu ,

		},
		.oVtg4 =
		{
			/*! VMIN*/
			.nVmin			= 0x0u ,

			/*! VMAX*/
			.nVmax			= 0xFFu ,

		},
		.oVtg5 =
		{
			/*! VMIN*/
			.nVmin			= 0x0u ,

			/*! VMAX*/
			.nVmax			= 0xFFu ,

		},
		.oVtg6 =
		{
			/*! VMIN*/
			.nVmin			= 0x0u ,

			/*! VMAX*/
			.nVmax			= 0xFFu ,

		},
	},
	.oPwmSettings =
	{
		/*! PWM config  */
		.nPwmCfg			= 0x0u ,

		/*! PWM Pin Frequency  */
		.nPwmFreq			= 0x0u ,

		/*! PWM blink rate - for PWM1 and PWM2   */
		.nPwmBlink1			= 0x0u ,

		/*! PWM blink rate2 for PWM3 and OE  */
		.nPwmBlink2			= 0x0u ,

		/*! PWM1 value   */
		.nPwm1Val			= 0x0u ,

		/*! PWM2 value  */
		.nPwm2Val			= 0x0u ,

		/*! PWM3 value   */
		.nPwm3Val			= 0x0u ,

		/*! PWM OE value  */
		.nPwmOEVal			= 0x0u ,

	},
	.sStreamSettings =
	{
		/*! No of Streams sourced (added) by the node */
		.nNumSrcStreams = 0x1u ,

		/*! Source Streams Info */
		.pSrcStreams = { 	&sChain0_Master0_Src_Stream0, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR	},

		/*! No of Streams sinked (used) by the node */
		.nNumSnkStreams = 0x1u ,

		/*! Sink Streams Info */
		.pSnkStreams = { 	&sChain0_Master0_Snk_Stream0, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR	},


	}
};

/************************************************* END OF MASTER NODE 0 DESCRIPTION ********************************************************************************/

 /************************************************* START OF SLAVE NODE 0 DESCRIPTION ********************************************************************************/

/* SLAVE NODE DESCRIPTION */ 

ADI_A2B_MEM_BCF_DATA
static ADI_A2B_SLAVE_NCD sChain0_SlaveNode0= 
{
	/* Node ID  */
	.nNodeID			= 0x0u ,

	/* Source node ID  */
	.nSrcNodeID			= 0x0u ,

	/*! Transceiver part number */
	.ePartNum			= ADI_A2B_AD2437 ,

	/* Auto-configure Node Enabled */
	.bEnableAutoConfig			= A2B_DISABLED ,

	/* Node Power Configuration */
	.bLocalPower		= A2B_ENABLED ,

	/* Expected high power switch Configuration */
	.nHighPwrSwitchCfg	= 0x4u ,

	.sAuthSettings =
	{
		/*! Vendor ID - ADI */
		.nVendorID			= 0xADu ,

		/*! Silicon version  */
		.nVersionID			= 0x21u ,

		/*! PRODUCT ID -A2bChain */
		.nProductID			= 0x37u ,

		/*! Expected capablity - I2C */
		.nCapability		= 0x1u ,

		/*! Enable/Disable Transceiver Authentication */
		.bTransceiverAuth		= A2B_ENABLED ,

		/*! Enable Two Step discovery */
		.bTwoStepDisc		= A2B_DISABLED ,

	},

	.sCustomNodeAuthSettings =
	{
		/*! Enable/Disable Custom Node ID settings */
		.bCustomNodeIdAuth		= A2B_DISABLED ,

	},

	.sI2SSettings =
	{
		/*! TDM mode  */
		.nTDMMode		= A2B_TDM2 ,

		/*! TDM channel size  */
		.nTDMChSize			= A2B_32BIT_TDM ,

		/*! SYNC mode - Pulse/50% duty cycle   */
		.nSyncMode			= A2B_ALTERNATE_SYNC ,

		/*! SYNC Polarity- Rising/Falling edge */
		.nSyncPolarity		= FALLING_EDGE ,

		/*! Early frame sync status */
		.bEarlySync			= A2B_ENABLED ,

		/*! SYNC offset with Super frame */
		.nSyncOffset		= 0x0 ,

		/*! DTXn change BCLK edge */
		.nBclkTxPolarity	= RAISING_EDGE ,

		/*! DRXn sampling BCLK edge */
		.nBclkRxPolarity	= RAISING_EDGE ,

		/*! Interleave slots between Tx pins  */
		.bTXInterleave		= A2B_DISABLED ,

		/*! Interleave slots between Rx pins  */
		.bRXInterleave		= A2B_DISABLED ,

		.sI2SRateConfig =
		{
			/* Sample rate */
			.nSamplingRate		= A2B_SAMPLERATE_48kHz ,

			/*! Reduce / re-transmit higher frequency samples  */
		.bReduce			= A2B_DISABLED ,

			/*! Share A2B bus slots for reduced sampling */
			.bShareBusSlot	= A2B_DISABLED ,

			/*! Enable Valid RR bit in Extra bit */
			.bRRValidBitLSB 	= A2B_DISABLED ,

			/*! Enable Valid RR bit in Extra Channel */
			.bRRValidBitExtraBit	= A2B_DISABLED ,

			/*! Enable Reduced rate strobe in ADR1/IO1 */
			.bRRStrobe	= A2B_DISABLED ,

			/*! Strobe direction High or Low */
			.bRRStrobeDirection	= A2B_LOW ,

		},
		/*! Enable/Disable Sync */
		.bSync	= A2B_ENABLED ,

	},

	.sPDMSettings =
	{
		/*! Number of PDM0 slots  */
		.nNumSlotsPDM0		= A2B_PDM0SLOTS_1 ,

		/*! Number of PDM1 slots  */
		.nNumSlotsPDM1		= A2B_PDM1SLOTS_1 ,

		/*! Use High Pass Filter  */
		.bHPFUse		= A2B_DISABLED ,

		/*! PDM rate  */
		.nPDMRate		= A2B_PDM_RATE_SFF ,

		/*! PDM Inverted Version of Alternate Clock */
		.bPDMInvClk		= A2B_DISABLED ,

		/*! PDM Alternate Clock */
		.bPDMAltClk		= A2B_DISABLED ,

		/*! PDM0 Falling Edge First */
		.bPDM0FallingEdgeFrst		= A2B_DISABLED ,

		/*! PDM1 Falling Edge First */
		.bPDM1FallingEdgeFrst		= A2B_DISABLED ,

		/*! PDM Destination */
		.ePDMDestination		= A2B_BUS_ONLY ,

		/*! HPF Corner Select */
		.ePDMHpfCorner		= HPF_CORNERFREQ_1Hz ,

	},

	.sConfigCtrlSettings =
	{
		/*! I2C interface frequency */
		.nI2CFrequency		= A2B_I2C_100kHz ,

		/*! Response cycles  */
		.nRespCycle			= 0x7Du ,

		/*! Expected super/audio frame rate */
		.nSuperFrameRate	= A2B_SFF_RATE_48_0kHz ,

		/*! Number of broadcast slots */
		.nBroadCastSlots	= 0x0u ,

		/*! Local down slots */
		.nLocalDwnSlotsConsume		= 0x2u ,

		/*! Local Up slots */
		.nLocalUpSlotsContribute		= 0x2u ,

		/*! Pass up slots */
		.nPassUpSlots		= 0x0u ,

		/*! Pass down slots */
		.nPassDwnSlots		= 0x0u ,

		/*! Number of slots for contribution */
		.nSlotsforDwnstrmContribute		= 0x0u ,

		/*! Number of Upslots consumed */
		.nLocalUpSlotsConsume		= 0x0u ,

		/*! Array of downstream  masks */ 
		.anUpstreamConsumeSlots = { SLOT_0_DISABLED, SLOT_1_DISABLED, SLOT_2_DISABLED, SLOT_3_DISABLED, SLOT_4_DISABLED, SLOT_5_DISABLED, SLOT_6_DISABLED, SLOT_7_DISABLED, 
 									 SLOT_8_DISABLED, SLOT_9_DISABLED, SLOT_10_DISABLED, SLOT_11_DISABLED, SLOT_12_DISABLED, SLOT_13_DISABLED, SLOT_14_DISABLED, SLOT_15_DISABLED, 
 									 SLOT_16_DISABLED, SLOT_17_DISABLED, SLOT_18_DISABLED, SLOT_19_DISABLED, SLOT_20_DISABLED, SLOT_21_DISABLED, SLOT_22_DISABLED, SLOT_23_DISABLED, 
 									 SLOT_24_DISABLED, SLOT_25_DISABLED, SLOT_26_DISABLED, SLOT_27_DISABLED, SLOT_28_DISABLED, SLOT_29_DISABLED, SLOT_30_DISABLED, SLOT_31_DISABLED},
		/*! Offset from the RX Buffer for downstream contribution */
		.nOffsetDwnstrmContribute		= 0x0u ,

		/*! Enable Down slot consume through mask */
		.bUseDwnslotConsumeMasks		= A2B_ENABLED ,

		/*! Array of downstream  masks */ 
		.anDwnstreamConsumeSlots = { SLOT_0_ENABLED, SLOT_1_ENABLED, SLOT_2_DISABLED, SLOT_3_DISABLED, SLOT_4_DISABLED, SLOT_5_DISABLED, SLOT_6_DISABLED, SLOT_7_DISABLED, 
 									 SLOT_8_DISABLED, SLOT_9_DISABLED, SLOT_10_DISABLED, SLOT_11_DISABLED, SLOT_12_DISABLED, SLOT_13_DISABLED, SLOT_14_DISABLED, SLOT_15_DISABLED, 
 									 SLOT_16_DISABLED, SLOT_17_DISABLED, SLOT_18_DISABLED, SLOT_19_DISABLED, SLOT_20_DISABLED, SLOT_21_DISABLED, SLOT_22_DISABLED, SLOT_23_DISABLED, 
 									 SLOT_24_DISABLED, SLOT_25_DISABLED, SLOT_26_DISABLED, SLOT_27_DISABLED, SLOT_28_DISABLED, SLOT_29_DISABLED, SLOT_30_DISABLED, SLOT_31_DISABLED},
		/*! Offset from the RX Buffer for Upstream contribution*/
		.nOffsetUpstrmContribute		= 0x0u ,

		/*! I2C Interface */
		.bDisableI2c	= A2B_DISABLED ,

		/*! I2C Fast Mode Plus */
		.bEnI2cFstModePlus	= A2B_DISABLED ,

	},
	.oPinAssignSettings =
	{
		.oSio0=
		{
			/*! Functionality */
			.eFunc			= FUNC_RX0,
			/*! GPIO mapping*/
			.eIoMapping			= NA,
		},
		.oSio1=
		{
			/*! Functionality */
			.eFunc			= FUNC_GPIO,
			/*! GPIO mapping*/
			.eIoMapping			= GPIO1,
		},
		.oSio2=
		{
			/*! Functionality */
			.eFunc			= FUNC_GPIO,
			/*! GPIO mapping*/
			.eIoMapping			= GPIO2,
		},
		.oSio3=
		{
			/*! Functionality */
			.eFunc			= FUNC_GPIO,
			/*! GPIO mapping*/
			.eIoMapping			= GPIO3,
		},
		.oSio4=
		{
			/*! Functionality */
			.eFunc			= FUNC_TX0,
			/*! GPIO mapping*/
			.eIoMapping			= NA,
		},
		.oGPIO7=
		{
			/*! Functionality */
			.eFunc			= FUNC_GPIO,
			/*! GPIO mapping*/
			.eIoMapping			= GPIO7,
		},
		.oSDA=
		{
			/*! Functionality */
			.eFunc			= FUNC_I2C,
			/*! GPIO mapping*/
			.eIoMapping			= NA,
		},
		.oSCL=
		{
			/*! Functionality */
			.eFunc			= FUNC_I2C,
			/*! GPIO mapping*/
			.eIoMapping			= NA,
		},
		.oMISO=
		{
			/*! Functionality */
			.eFunc			= FUNC_GPIO,
			/*! GPIO mapping*/
			.eIoMapping			= GPIO5,
		},
		.oMOSI=
		{
			/*! Functionality */
			.eFunc			= FUNC_GPIO,
			/*! GPIO mapping*/
			.eIoMapping			= GPIO6,
		},
		.oSCK=
		{
			/*! Functionality */
			.eFunc			= FUNC_GPIO,
			/*! GPIO mapping*/
			.eIoMapping			= GPIO0,
		},
		.oADR1=
		{
			/*! Functionality */
			.eFunc			= FUNC_GPIO,
			/*! GPIO mapping*/
			.eIoMapping			= GPIO4,
		},
		.oADR2=
		{
			/*! Functionality */
			.eFunc			= FUNC_I2C_CLKOUT2,
			/*! GPIO mapping*/
			.eIoMapping			= NA,
		},
			/*! GPIO mode*/
			.nGpioMode			= 0x0u ,
	},
	.sGPIOSettings =
	{
		.sPinMuxSettings =
		{
			/*! GPIO 0 Pin multiplexing */
			.bGPIO0PinUsage	= A2B_GPIO_0_DISABLE ,	/*A2B_DISABLED*/

			/*! GPIO 1 Pin multiplexing */
			.bGPIO1PinUsage	= A2B_GPIO_1_DISABLE ,	/*A2B_DISABLED*/

			/*! GPIO 2 Pin multiplexing */
			.bGPIO2PinUsage	= A2B_GPIO_2_DISABLE ,	/*A2B_DISABLED*/

			/*! GPIO 3 Pin multiplexing */
			.bGPIO3PinUsage	= A2B_GPIO_3_DISABLE ,	/*A2B_DISABLED*/

			/*! GPIO 4 Pin multiplexing */
			.bGPIO4PinUsage	= A2B_GPIO_4_DISABLE ,	/*A2B_DISABLED*/

			/*! GPIO 5 Pin multiplexing */
			.bGPIO5PinUsage	= A2B_GPIO_5_DISABLE ,	/*A2B_DISABLED*/

			/*! GPIO 6 Pin multiplexing */
			.bGPIO6PinUsage	= A2B_GPIO_6_DISABLE ,	/*A2B_DISABLED*/

			/*! GPIO 7 Pin multiplexing */
			.bGPIO7PinUsage	= A2B_GPIO_7_INPUT ,
		},
		.sPinIntConfig =
		{
			/*! Enable GPIO 0 Input pin interrupt  */
			.bGPIO0Interrupt	= A2B_DISABLED ,

			/*! Interrupt polarity - GPIO 0 Input pin  */
			.bGPIO0IntPolarity	= RAISING_EDGE ,

			/*! Enable GPIO 1 Input pin interrupt  */
			.bGPIO1Interrupt	= A2B_DISABLED ,

			/*! Interrupt polarity - GPIO 1 Input pin  */
			.bGPIO1IntPolarity	= RAISING_EDGE ,

			/*! Enable GPIO 2 Input pin interrupt  */
			.bGPIO2Interrupt	= A2B_DISABLED ,

			/*! Interrupt polarity - GPIO 2 Input pin  */
			.bGPIO2IntPolarity	= RAISING_EDGE ,

			/*! Enable GPIO 3 Input pin interrupt  */
			.bGPIO3Interrupt	= A2B_DISABLED ,

			/*! Interrupt polarity - GPIO 3 Input pin  */
			.bGPIO3IntPolarity	= RAISING_EDGE ,

			/*! Enable GPIO 4 Input pin interrupt  */
			.bGPIO4Interrupt	= A2B_DISABLED ,

			/*! Interrupt polarity - GPIO 4 Input pin  */
			.bGPIO4IntPolarity	= RAISING_EDGE ,

			/*! Enable GPIO 5 Input pin interrupt  */
			.bGPIO5Interrupt	= A2B_DISABLED ,

			/*! Interrupt polarity - GPIO 5 Input pin  */
			.bGPIO5IntPolarity	= RAISING_EDGE ,

			/*! Enable GPIO 6 Input pin interrupt  */
			.bGPIO6Interrupt	= A2B_DISABLED ,

			/*! Interrupt polarity - GPIO 6 Input pin  */
			.bGPIO6IntPolarity	= RAISING_EDGE ,

			/*! Enable GPIO 7 Input pin interrupt  */
			.bGPIO7Interrupt	= A2B_DISABLED ,

			/*! Interrupt polarity - GPIO 7 Input pin  */
			.bGPIO7IntPolarity	= RAISING_EDGE ,

		},
		.sOutPinVal =
		{
			/*! Data value for GPIO 0 output pin  */
			.bGPIO0Val = A2B_LOW ,

			/*! Data value for GPIO 1 output pin  */
			.bGPIO1Val = A2B_LOW ,

			/*! Data value for GPIO 2 output pin  */
			.bGPIO2Val = A2B_LOW ,

			/*! Data value for GPIO 3 output pin  */
			.bGPIO3Val = A2B_LOW ,

			/*! Data value for GPIO 4 output pin  */
			.bGPIO4Val = A2B_LOW ,

			/*! Data value for GPIO 5 output pin  */
			.bGPIO5Val = A2B_LOW ,

			/*! Data value for GPIO 6 output pin  */
			.bGPIO6Val = A2B_LOW ,

			/*! Data value for GPIO 7 output pin  */
			.bGPIO7Val = A2B_LOW ,

		},

		/*! Digital Pin drive strength */
		.bHighDriveStrength = A2B_ENABLED ,

		/*! IRQ Pin Invert */
		.bIRQInv = A2B_DISABLED ,

		/*! Enable tristate when inactive */
		.bIRQTriState = A2B_DISABLED ,

	},

	.sInterruptSettings =
	{
		/*! Report Header count error  */
		.bReportHDCNTErr		= A2B_DISABLED ,

		/*! Report Data decoding error  */
		.bReportDDErr			= A2B_DISABLED ,

		/*! Report Data CRC error  */
		.bReportCRCErr			= A2B_DISABLED ,

		/*! Report Data Parity error  */
		.bReportDataParityErr	= A2B_DISABLED ,

		/*! Report Data Bus Power error  */
		.bReportPwrErr			= A2B_ENABLED ,

		/*! Report bit error count overflow error  */
		.bReportErrCntOverFlow	= A2B_DISABLED ,

		/*! Report SRF miss error  */
		.bReportSRFMissErr	= A2B_DISABLED ,

		/*! Report SRF crc error  */
		.bReportSRFCrcErr		= A2B_DISABLED ,

		/*! Report GPIO  0 Interrupt */
		.bReportGPIO0			= A2B_DISABLED ,

		/*! Report GPIO  1 Interrupt */
		.bReportGPIO1			= A2B_DISABLED ,

		/*! Report GPIO  2 Interrupt */
		.bReportGPIO2			= A2B_DISABLED ,

		/*! Report GPIO  3 Interrupt */
		.bReportGPIO3			= A2B_DISABLED ,

		/*! Report GPIO  4 Interrupt */
		.bReportGPIO4			= A2B_DISABLED ,

		/*! Report GPIO  5 Interrupt */
		.bReportGPIO5			= A2B_DISABLED ,

		/*! Report GPIO  6 Interrupt */
		.bReportGPIO6			= A2B_DISABLED ,

		/*! Report GPIO  7 Interrupt */
		.bReportGPIO7			= A2B_DISABLED ,

	},

	.sGPIODSettings =
	{
		.sGPIOD0Config =
		{
			/*! Enable/Disable GPIO over distance   */
			.bGPIODistance	= A2B_DISABLED ,

			/*! Enable/Disable  */
			.bGPIOSignalInv	= A2B_DISABLED ,

			/*! Bus port masks */ 
			.abBusPortMask = { A2B_MASK_BUSFLAG_0, A2B_MASK_BUSFLAG_1, A2B_MASK_BUSFLAG_2, A2B_MASK_BUSFLAG_3, A2B_MASK_BUSFLAG_4, A2B_MASK_BUSFLAG_5, A2B_MASK_BUSFLAG_6, A2B_MASK_BUSFLAG_7},


		},
		.sGPIOD1Config =
		{
			/*! Enable/Disable GPIO over distance   */
			.bGPIODistance	= A2B_DISABLED ,

			/*! Enable/Disable  */
			.bGPIOSignalInv	= A2B_DISABLED ,

			/*! Bus port masks */ 
			.abBusPortMask = { A2B_MASK_BUSFLAG_0, A2B_MASK_BUSFLAG_1, A2B_MASK_BUSFLAG_2, A2B_MASK_BUSFLAG_3, A2B_MASK_BUSFLAG_4, A2B_MASK_BUSFLAG_5, A2B_MASK_BUSFLAG_6, A2B_MASK_BUSFLAG_7},


		},
		.sGPIOD2Config =
		{
			/*! Enable/Disable GPIO over distance   */
			.bGPIODistance	= A2B_DISABLED ,

			/*! Enable/Disable  */
			.bGPIOSignalInv	= A2B_DISABLED ,

			/*! Bus port masks */ 
			.abBusPortMask = { A2B_MASK_BUSFLAG_0, A2B_MASK_BUSFLAG_1, A2B_MASK_BUSFLAG_2, A2B_MASK_BUSFLAG_3, A2B_MASK_BUSFLAG_4, A2B_MASK_BUSFLAG_5, A2B_MASK_BUSFLAG_6, A2B_MASK_BUSFLAG_7},


		},
		.sGPIOD3Config =
		{
			/*! Enable/Disable GPIO over distance   */
			.bGPIODistance	= A2B_DISABLED ,

			/*! Enable/Disable  */
			.bGPIOSignalInv	= A2B_DISABLED ,

			/*! Bus port masks */ 
			.abBusPortMask = { A2B_MASK_BUSFLAG_0, A2B_MASK_BUSFLAG_1, A2B_MASK_BUSFLAG_2, A2B_MASK_BUSFLAG_3, A2B_MASK_BUSFLAG_4, A2B_MASK_BUSFLAG_5, A2B_MASK_BUSFLAG_6, A2B_MASK_BUSFLAG_7},


		},
		.sGPIOD4Config =
		{
			/*! Enable/Disable GPIO over distance   */
			.bGPIODistance	= A2B_DISABLED ,

			/*! Enable/Disable  */
			.bGPIOSignalInv	= A2B_DISABLED ,

			/*! Bus port masks */ 
			.abBusPortMask = { A2B_MASK_BUSFLAG_0, A2B_MASK_BUSFLAG_1, A2B_MASK_BUSFLAG_2, A2B_MASK_BUSFLAG_3, A2B_MASK_BUSFLAG_4, A2B_MASK_BUSFLAG_5, A2B_MASK_BUSFLAG_6, A2B_MASK_BUSFLAG_7},


		},
		.sGPIOD5Config =
		{
			/*! Enable/Disable GPIO over distance   */
			.bGPIODistance	= A2B_DISABLED ,

			/*! Enable/Disable  */
			.bGPIOSignalInv	= A2B_DISABLED ,

			/*! Bus port masks */ 
			.abBusPortMask = { A2B_MASK_BUSFLAG_0, A2B_MASK_BUSFLAG_1, A2B_MASK_BUSFLAG_2, A2B_MASK_BUSFLAG_3, A2B_MASK_BUSFLAG_4, A2B_MASK_BUSFLAG_5, A2B_MASK_BUSFLAG_6, A2B_MASK_BUSFLAG_7},


		},
		.sGPIOD6Config =
		{
			/*! Enable/Disable GPIO over distance   */
			.bGPIODistance	= A2B_DISABLED ,

			/*! Enable/Disable  */
			.bGPIOSignalInv	= A2B_DISABLED ,

			/*! Bus port masks */ 
			.abBusPortMask = { A2B_MASK_BUSFLAG_0, A2B_MASK_BUSFLAG_1, A2B_MASK_BUSFLAG_2, A2B_MASK_BUSFLAG_3, A2B_MASK_BUSFLAG_4, A2B_MASK_BUSFLAG_5, A2B_MASK_BUSFLAG_6, A2B_MASK_BUSFLAG_7},


		},
		.sGPIOD7Config =
		{
			/*! Enable/Disable GPIO over distance   */
			.bGPIODistance	= A2B_DISABLED ,

			/*! Enable/Disable  */
			.bGPIOSignalInv	= A2B_DISABLED ,

			/*! Bus port masks */ 
			.abBusPortMask = { A2B_MASK_BUSFLAG_0, A2B_MASK_BUSFLAG_1, A2B_MASK_BUSFLAG_2, A2B_MASK_BUSFLAG_3, A2B_MASK_BUSFLAG_4, A2B_MASK_BUSFLAG_5, A2B_MASK_BUSFLAG_6, A2B_MASK_BUSFLAG_7},


		},
	},

	.sClkOutSettings =
	{
		/*! Enable Clock1 inversion */
		.bClk1Inv			= A2B_DISABLED ,

		/*! Clk1 pre-division */
		.bClk1PreDiv		= A2B_CLKOUT_PREDIV_02,

		/*! Clk1 division */
		.bClk1Div			= A2B_CLKOUT_DIV_2,

		/*! Enable Clock2 inversion */
		.bClk2Inv			= A2B_DISABLED ,

		/*! Clk2 pre-division */
		.bClk2PreDiv		= A2B_CLKOUT_PREDIV_02,

		/*! Clk2 division */
		.bClk2Div			= A2B_CLKOUT_DIV_4,

	},
	.sRegSettings =
	{
		/*! Switch control register */
		.nSWCTL		= 0x11u ,

		/*! Test mode register */
		.nTESTMODE	= 0x0u ,

		/*! Error control register */
		.nBECCTL	= 0xEFu ,

		/*! Error management register  */
		.nERRMGMT	= 0x0u ,

		/*! I2S test register  */
		.nI2STEST	= 0x0u ,

		/*! Generate error  */
		.nGENERR	= 0x0u ,

		/*! Raise interrupt register */
		.nRAISE		= 0x0u ,

		/*! Bus monitor configuration */
		.nBMMCFG		= 0x0u ,

		/*! Clock sustain configuration   */
		.nSUSCFG	= 0x0u ,

		/*!  Mailbox 0 control */
		.nMBOX0CTL		= 0x0u ,

		/*!  Mailbox 1 control */
		.nMBOX1CTL		= 0x2u ,

		/*! PLL Control register  */
		.nPLLCTL	= 0x0u ,

		/*! Control Register */
		.nCONTROL	= 0x10u ,

		/*! Switch Control 2 Register */
		.nSWCTL2	= 0x0u ,

		/*! Switch Control 5 Register */
		.nSWCTL5	= 0x0u ,

		/*!  TX Control Register */
		.nTXACTL	= 0x0u ,

	},

	.oSpiSettings =
	{
		/*! SPI mode  */
		.nSPIMode	= 0x2u ,

		/*! Lead Clk Edge(CPOL) */
		.nCPOL	= 0x0u ,

		/*! Sample Clk Edge(CPHA)  */
		.nCPHA	= 0x0u ,

		/*! Clock Div factor */
		.nClkDivFactor	= 0x0u ,

		/*! SPI Mstr Slave Select 2 Enable */
		.nMstrSS2En	= 0x0u ,

		/*! SPI Mstr Slave Select 1 Enable */
		.nMstrSS1En	= 0x0u ,

		/*! SPI Mstr Slave Select 0 Enable */
		.nMstrSS0En	= 0x0u ,

		/*! SPI GPIO Select */
		.nGpioSelect	= 0x0u ,

		/*! SPI GPIO Enable */
		.nGpioEnable	= 0x0u ,

		/*! Full Duplex Slave select  */
		.nFDSize	= 0x0u ,

		/*! Full target Node */
		.nFDTargetNode	= 0x0u ,

		/*! Full Duplex Target Slave select  */
		.nTargetSSel	= 0x0u ,

		/*! Full Duplex clock stretch - enable/dsiable */
		.bFDClkStretchEn	= A2B_DISABLED ,

		/*! SPI Error interrupts */
		.oSpiIntSettings =
		{
			/*! FIFO underflow Error */
			.bFifoUnderflow	= A2B_DISABLED ,

			/*! FIFO overflow Error */
			.bFifoOverflow	= A2B_DISABLED ,

			/*! Bad Command */
			.bBadCommand	= A2B_DISABLED ,

			/*! Data Tunnel Error */
			.bDataTunnel	= A2B_DISABLED ,

			/*! SPI Remote I2C Access Error */
			.bSpiRemoteI2cAccess	= A2B_DISABLED ,

			/*! SPI Remote Reg Access Error */
			.bSpiRemoteRegAccess	= A2B_DISABLED ,

			/*! SPI Done */
			.bSpiDone	= A2B_DISABLED ,

		},
		/*! Data tunnel enable*/
		.bDTEnable	= 0x0u ,

		/*! Tunnel Ownership - Owner/Responder */
		.eTunnelOwnership	= DT_RESPONDER,

		/*! Tunnel Position  */
		.eTunnelPos	= DT_POS_MIDDLE,

		/*! Data tunnel downstream slots */
		.nDTDwnstrmSlots	= 0x0u ,

		/*! Data tunnel downstream offset */
		.nDTDwnstrmOffset	= 0x0u ,

		/*! Data tunnel upstream slots */
		.nDTUpstrmSlots	= 0x0u ,

		/*! Data tunnel upstream offset */
		.nDTUpstrmOffset	= 0x0u ,

	},
	.anTxXbarSettings =
	{
		0x0u, /*TXXBAR 0*/
		0x1u, /*TXXBAR 1*/
		0x2u, /*TXXBAR 2*/
		0x3u, /*TXXBAR 3*/
		0x4u, /*TXXBAR 4*/
		0x5u, /*TXXBAR 5*/
		0x6u, /*TXXBAR 6*/
		0x7u, /*TXXBAR 7*/
		0x8u, /*TXXBAR 8*/
		0x9u, /*TXXBAR 9*/
		0xAu, /*TXXBAR 10*/
		0xBu, /*TXXBAR 11*/
		0xCu, /*TXXBAR 12*/
		0xDu, /*TXXBAR 13*/
		0xEu, /*TXXBAR 14*/
		0xFu, /*TXXBAR 15*/
		0x10u, /*TXXBAR 16*/
		0x11u, /*TXXBAR 17*/
		0x12u, /*TXXBAR 18*/
		0x13u, /*TXXBAR 19*/
		0x14u, /*TXXBAR 20*/
		0x15u, /*TXXBAR 21*/
		0x16u, /*TXXBAR 22*/
		0x17u, /*TXXBAR 23*/
		0x18u, /*TXXBAR 24*/
		0x19u, /*TXXBAR 25*/
		0x1Au, /*TXXBAR 26*/
		0x1Bu, /*TXXBAR 27*/
		0x1Cu, /*TXXBAR 28*/
		0x1Du, /*TXXBAR 29*/
		0x1Eu, /*TXXBAR 30*/
		0x1Fu, /*TXXBAR 31*/
	},
	.anRxXbarSettings =
	{
		0xFFu,	/*RXMASK 0*/
		0xFFu,	/*RXMASK 1*/
		0xFFu,	/*RXMASK 2*/
		0xFFu,	/*RXMASK 3*/
		0xFFu,	/*RXMASK 4*/
		0xFFu,	/*RXMASK 5*/
		0xFFu,	/*RXMASK 6*/
		0xFFu,	/*RXMASK 7*/
	},
	.oVmtrSettings =
	{
		/*! Voltage Enable */
		.bVEN			= A2B_DISABLED ,

		/*! Interrupt Enable */
		.bIntEN			= A2B_DISABLED ,

		/*! VMAX Check Result */
		.nMxStat			= 0x0u ,

		/*! VMIN Check Result */
		.nMinStat			= 0x0u ,

		.oVtg0 =
		{
			/*! VMIN*/
			.nVmin			= 0x0u ,

			/*! VMAX*/
			.nVmax			= 0xFFu ,

		},
		.oVtg1 =
		{
			/*! VMIN*/
			.nVmin			= 0x0u ,

			/*! VMAX*/
			.nVmax			= 0xFFu ,

		},
		.oVtg2 =
		{
			/*! VMIN*/
			.nVmin			= 0x0u ,

			/*! VMAX*/
			.nVmax			= 0xFFu ,

		},
		.oVtg3 =
		{
			/*! VMIN*/
			.nVmin			= 0x0u ,

			/*! VMAX*/
			.nVmax			= 0xFFu ,

		},
		.oVtg4 =
		{
			/*! VMIN*/
			.nVmin			= 0x0u ,

			/*! VMAX*/
			.nVmax			= 0xFFu ,

		},
		.oVtg5 =
		{
			/*! VMIN*/
			.nVmin			= 0x0u ,

			/*! VMAX*/
			.nVmax			= 0xFFu ,

		},
		.oVtg6 =
		{
			/*! VMIN*/
			.nVmin			= 0x0u ,

			/*! VMAX*/
			.nVmax			= 0xFFu ,

		},
	},
	.oPwmSettings =
	{
		/*! PWM config  */
		.nPwmCfg			= 0x0u ,

		/*! PWM Pin Frequency  */
		.nPwmFreq			= 0x0u ,

		/*! PWM blink rate - for PWM1 and PWM2   */
		.nPwmBlink1			= 0x0u ,

		/*! PWM blink rate2 for PWM3 and OE  */
		.nPwmBlink2			= 0x0u ,

		/*! PWM1 value   */
		.nPwm1Val			= 0x0u ,

		/*! PWM2 value  */
		.nPwm2Val			= 0x0u ,

		/*! PWM3 value   */
		.nPwm3Val			= 0x0u ,

		/*! PWM OE value  */
		.nPwmOEVal			= 0x0u ,

	},
		 /* Number of peripheral devices connected  */
		.nNumPeriDevice = 2,

		 /*Array of peripheral configuration pointers */ 
		.apPeriConfig = { 	 &sChain0_Slave0_PeriConfig0,  &sChain0_Slave0_PeriConfig1, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR	},

	.sStreamSettings =
	{
		/*! No of Streams sourced (added) by the node */
		.nNumSrcStreams = 0x1u ,

		/*! Source Streams Info */
		.pSrcStreams = { 	&sChain0_Slave0_Src_Stream0, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR	},

		/*! No of Streams sinked (used) by the node */
		.nNumSnkStreams = 0x1u ,

		/*! Sink Streams Info */
		.pSnkStreams = { 	&sChain0_Slave0_Snk_Stream0, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR	},

		/*! No of Streams passing through this node */
		.nNumPassThruStreams = 0x0u ,

		/*! Pass Through Streams Info */
		.pPassThruStreams = { 	NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR, NULL_PTR	},


	}
};

/************************************************* END OF SLAVE NODE 0 DESCRIPTION ********************************************************************************/ 
 
/************************************************** END OF A2bChain DESCRIPTION ********************************************************************************  */

/************************************************** START OF PERIPHERAL DESCRIPTION ********************************************************************************  */

/************************************************** START OF PERIPHERALS CONNECTED TO TARGET PROCESSOR  ********************************************************************************  */

/* Peripheral - 0*/

ADI_A2B_MEM_BCF_CONST_DATA
static A2B_PERIPHERAL_DEVICE_CONFIG  Target_PeriConfig0=
{
 /*! I2C interface status  */ 
	 .bI2CInterfaceUse = A2B_ENABLED, 

 /*! SPI interface status  */ 
	 .bSpiInterfaceUse = A2B_DISABLED, 

	/* SPI Slave select */ 
	.nSpiSS = 0x39, 

	/*7 bit I2C address */ 
	.nI2Caddr = 0x39, 

	/* Device type -audio source/sink/host  */ 
	.eDeviceType = A2B_AUDIO_UNKNOWN, 

	/* Tx0 Pin in use */
	.bUseTx0 = A2B_DISABLED, 

	/* Rx0 Pin in use */
	.bUseRx0 = A2B_DISABLED, 

	/* Tx1 Pin in use */
	.bUseTx1 = A2B_DISABLED, 

	/* Rx1 Pin in use */
	.bUseRx1 = A2B_DISABLED, 

	/* No of Tx0 channels  */ 
	.nChTx0 = 0, 

	/* No of Rx0 channels  */ 
	.nChRx0 = 0, 

	/* No of Tx1 channels  */ 
	.nChTx1 = 0, 

	/* No of Rx1 channels  */ 
	.nChRx1 = 0, 

	/* Device Connection Interface   */ 
	.ePeriDeviceInterface = I2C, 

	/* Flag to decide when to configure peripheral connected to target   */ 
	.bPostDiscCfg = A2B_DISABLED, 

	/* Number of configuration units  */ 
	.nNumPeriConfigUnit = 0, 

	.paPeriConfigUnit = NULL_PTR 
 };

/* Peripheral - 1*/

ADI_A2B_MEM_BCF_CONST_DATA
static A2B_PERIPHERAL_DEVICE_CONFIG  Target_PeriConfig1=
{
 /*! I2C interface status  */ 
	 .bI2CInterfaceUse = A2B_ENABLED, 

 /*! SPI interface status  */ 
	 .bSpiInterfaceUse = A2B_DISABLED, 

	/* SPI Slave select */ 
	.nSpiSS = 0x50, 

	/*7 bit I2C address */ 
	.nI2Caddr = 0x50, 

	/* Device type -audio source/sink/host  */ 
	.eDeviceType = A2B_AUDIO_UNKNOWN, 

	/* Tx0 Pin in use */
	.bUseTx0 = A2B_DISABLED, 

	/* Rx0 Pin in use */
	.bUseRx0 = A2B_DISABLED, 

	/* Tx1 Pin in use */
	.bUseTx1 = A2B_DISABLED, 

	/* Rx1 Pin in use */
	.bUseRx1 = A2B_DISABLED, 

	/* No of Tx0 channels  */ 
	.nChTx0 = 0, 

	/* No of Rx0 channels  */ 
	.nChRx0 = 0, 

	/* No of Tx1 channels  */ 
	.nChTx1 = 0, 

	/* No of Rx1 channels  */ 
	.nChRx1 = 0, 

	/* Device Connection Interface   */ 
	.ePeriDeviceInterface = I2C, 

	/* Flag to decide when to configure peripheral connected to target   */ 
	.bPostDiscCfg = A2B_DISABLED, 

	/* Number of configuration units  */ 
	.nNumPeriConfigUnit = 0, 

	.paPeriConfigUnit = NULL_PTR 
 };

/************************************************** END OF PERIPHERALS CONNECTED TO TARGET PROCESSOR  ********************************************************************************  */

/************************************************** START OF CHAIN 0 MASTER 0 STREAM DESCRIPTION ********************************************************************************  */

/* Src(Added) Streams description */

ADI_A2B_MEM_BCF_CONST_DATA
static ADI_A2B_STREAM  sChain0_Master0_Src_Stream0 =
{
	/*! Stream ID */ 
	 .nStreamID = 0, 

	/*! Stream Name (Just for reference). Leave it commented!!! (will result in build errors if uncommented) */ 
	/*.nStreamName = Down2, */

	/*! Data width (in Bits) of the stream */ 
	.nDataWidth = 24, 

	/*! Number of channels in the stream */ 
	.nNumChannels = 2, 

	/*! Number of slots used for the stream */ 
	.nNumSlots = 2, 

	/*! Sampling rate of the stream (in kHz) */ 
	.nSampleRate = 48, 

	/*! Bus slot number (starting index) to which the stream is mapped */ 
	.nBusSlotIndex = 0, 

	/*! Stream Direction (0 - Downstream, 1 - Upstream) */ 
	.eDirection = A2B_DOWNSTREAM
};

/* Snk(Used) Streams description */

ADI_A2B_MEM_BCF_CONST_DATA
static ADI_A2B_STREAM  sChain0_Master0_Snk_Stream0 =
{
	/*! Stream ID */ 
	 .nStreamID = 1, 

	/*! Stream Name (Just for reference). Leave it commented!!! (will result in build errors if uncommented) */ 
	/*.nStreamName = Up2, */

	/*! Data width (in Bits) of the stream */ 
	.nDataWidth = 24, 

	/*! Number of channels in the stream */ 
	.nNumChannels = 2, 

	/*! Number of slots used for the stream */ 
	.nNumSlots = 2, 

	/*! Sampling rate of the stream (in kHz) */ 
	.nSampleRate = 48, 

	/*! Bus slot number (starting index) to which the stream is mapped */ 
	.nBusSlotIndex = 0, 

	/*! Stream Direction (0 - Downstream, 1 - Upstream) */ 
	.eDirection = A2B_UPSTREAM
};

/************************************************** END OF CHAIN 0 MASTER 0 STREAM DESCRIPTION ********************************************************************************  */

/************************************************** START OF CHAIN 0 SLAVE0 PERIPHERAL DESCRIPTION ********************************************************************************  */

/* Peripheral - 0*/

ADI_A2B_MEM_BCF_CONST_DATA
static A2B_PERIPHERAL_DEVICE_CONFIG  sChain0_Slave0_PeriConfig0=
{
 /*! I2C interface status  */ 
	.bI2CInterfaceUse = A2B_ENABLED, 

 /*! SPI interface status  */ 
	.bSpiInterfaceUse = A2B_DISABLED, 

	/* SPI Slave select */ 
	.nSpiSS = 0x39, 

	/*7 bit I2C address */ 
	.nI2Caddr = 0x39, 

	/* Device type -audio source/sink/host  */ 
	.eDeviceType = A2B_AUDIO_UNKNOWN, 

	/* Tx0 Pin in use */
	.bUseTx0 = A2B_DISABLED, 

	/* Rx0 Pin in use */
	.bUseRx0 = A2B_DISABLED, 

	/* Tx1 Pin in use */
	.bUseTx1 = A2B_DISABLED, 

	/* Rx1 Pin in use */
	.bUseRx1 = A2B_DISABLED, 

	/* No of Tx0 channels  */ 
	.nChTx0 = 0, 

	/* No of Rx0 channels  */ 
	.nChRx0 = 0, 

	/* No of Tx1 channels  */ 
	.nChTx1 = 0, 

	/* No of Rx1 channels  */ 
	.nChRx1 = 0, 

	/* Device Connection Interface   */ 
	.ePeriDeviceInterface = I2C, 

	/* SPI to SPI transaction type over A2B   */ 
	.eSpiMode = A2B_SPI_ATOMIC, 

	/* Number of configuration units  */ 
	.nNumPeriConfigUnit = 0, 

	/* Pointer to configuration units  */ 
	.paPeriConfigUnit = NULL_PTR 
 };

/* Peripheral - 1*/

ADI_A2B_MEM_BCF_CONST_DATA
static A2B_PERIPHERAL_DEVICE_CONFIG  sChain0_Slave0_PeriConfig1=
{
 /*! I2C interface status  */ 
	.bI2CInterfaceUse = A2B_ENABLED, 

 /*! SPI interface status  */ 
	.bSpiInterfaceUse = A2B_DISABLED, 

	/* SPI Slave select */ 
	.nSpiSS = 0x50, 

	/*7 bit I2C address */ 
	.nI2Caddr = 0x50, 

	/* Device type -audio source/sink/host  */ 
	.eDeviceType = A2B_AUDIO_UNKNOWN, 

	/* Tx0 Pin in use */
	.bUseTx0 = A2B_DISABLED, 

	/* Rx0 Pin in use */
	.bUseRx0 = A2B_DISABLED, 

	/* Tx1 Pin in use */
	.bUseTx1 = A2B_DISABLED, 

	/* Rx1 Pin in use */
	.bUseRx1 = A2B_DISABLED, 

	/* No of Tx0 channels  */ 
	.nChTx0 = 0, 

	/* No of Rx0 channels  */ 
	.nChRx0 = 0, 

	/* No of Tx1 channels  */ 
	.nChTx1 = 0, 

	/* No of Rx1 channels  */ 
	.nChRx1 = 0, 

	/* Device Connection Interface   */ 
	.ePeriDeviceInterface = I2C, 

	/* SPI to SPI transaction type over A2B   */ 
	.eSpiMode = A2B_SPI_ATOMIC, 

	/* Number of configuration units  */ 
	.nNumPeriConfigUnit = 0, 

	/* Pointer to configuration units  */ 
	.paPeriConfigUnit = NULL_PTR 
 };

/************************************************** END OF CHAIN 0 SLAVE0 PERIPHERAL DESCRIPTION ********************************************************************************  */

/************************************************** START OF CHAIN 0 SLAVE 0 STREAM DESCRIPTION ********************************************************************************  */

/* Src(Added) Streams description */

ADI_A2B_MEM_BCF_CONST_DATA
static ADI_A2B_STREAM  sChain0_Slave0_Src_Stream0 =
{
	/*! Stream ID */ 
	 .nStreamID = 1, 

	/*! Stream Name (Just for reference). Leave it commented!!! (will result in build errors if uncommented) */ 
	/*.nStreamName = Up2, */

	/*! Data width (in Bits) of the stream */ 
	.nDataWidth = 24, 

	/*! Number of channels in the stream */ 
	.nNumChannels = 2, 

	/*! Number of slots used for the stream */ 
	.nNumSlots = 2, 

	/*! Sampling rate of the stream (in kHz) */ 
	.nSampleRate = 48, 

	/*! Bus slot number (starting index) to which the stream is mapped */ 
	.nBusSlotIndex = 0, 

	/*! Stream Direction (0 - Downstream, 1 - Upstream) */ 
	.eDirection = A2B_UPSTREAM
};

/* Snk(Used) Streams description */

ADI_A2B_MEM_BCF_CONST_DATA
static ADI_A2B_STREAM  sChain0_Slave0_Snk_Stream0 =
{
	/*! Stream ID */ 
	 .nStreamID = 0, 

	/*! Stream Name (Just for reference). Leave it commented!!! (will result in build errors if uncommented) */ 
	/*.nStreamName = Down2, */

	/*! Data width (in Bits) of the stream */ 
	.nDataWidth = 24, 

	/*! Number of channels in the stream */ 
	.nNumChannels = 2, 

	/*! Number of slots used for the stream */ 
	.nNumSlots = 2, 

	/*! Sampling rate of the stream (in kHz) */ 
	.nSampleRate = 48, 

	/*! Bus slot number (starting index) to which the stream is mapped */ 
	.nBusSlotIndex = 0, 

	/*! Stream Direction (0 - Downstream, 1 - Upstream) */ 
	.eDirection = A2B_DOWNSTREAM
};

/************************************************** END OF CHAIN 0 SLAVE 0 STREAM DESCRIPTION ********************************************************************************  */

