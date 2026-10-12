/*******************************************************************************
Copyright (c) 2026 - Analog Devices Inc. All Rights Reserved.
This software is proprietary & confidential to Analog Devices, Inc.
and its licensors.
******************************************************************************
* @brief: This file contains I2C/SPI command sequence to be followed for 
*         discovery and configuration of A2B nodes for an A2B schematic
* @date: Monday, 05 October 2026-20:51:12
* I2C/SPI Command File Version - 1.0.0
* A2B DLL- 2.0.1.0
* SigmaStudio+ version- 2.3.0
* Developed by: Automotive Software and Systems team, Bangalore, India
* THIS IS A GENERATED FILE
*****************************************************************************/

/*! \addtogroup ADI_A2B_DISCOVERY_CONFIG ADI_A2B_DISCOVERY_CONFIG 
* @{
*/
#ifndef _ADI_A2B_CMD_LIST_H_ 
#define _ADI_A2B_CMD_LIST_H_ 

/*! \struct ADI_A2B_DISCOVERY_CONFIG 
A2B discovery config unit structure 
*/
typedef struct 
 { 
/*! Device address */
	unsigned char nDeviceAddr;

/*! Operation code */
	unsigned char eOpCode;

/*! SPI Command width (in bytes), not used for I2C */
	unsigned char nSpiCmdWidth;

/*! SPI Commands, not used for I2C */
	unsigned int nSpiCmd;

/*! Reg Sub address width (in bytes) */
	unsigned char nAddrWidth;

/*! Reg Sub address */
	unsigned int nAddr;

/*! Reg data width (in bytes) */
	unsigned char nDataWidth;

/*! Reg data count (in bytes) */
	unsigned short nDataCount;

/*! Config Data */
	unsigned char* paConfigData;

/*! Protocol */
	unsigned char eProtocol;

} ADI_A2B_DISCOVERY_CONFIG;

#define WRITE   ((unsigned char) 0x00u)
#define READ    ((unsigned char) 0x01u)
#define DELAY   ((unsigned char) 0x02u)
#define INVALID ((unsigned char) 0xffu)

#define I2C     ((unsigned char) 0x00u)
#define SPI     ((unsigned char) 0x01u)

#define CONFIG_LEN (54) 


static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data0[1] =
{
	0x04u	
};

static unsigned char gaConfig_AD243xMain_0_A2BDelay_Data1[1] =
{
	0x19u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data2[1] =
{
	0x19u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data3[3] =
{
	0xADu,	0x37u,	0x21u	
};

static unsigned char gaConfig_AD243xMain_0_REGAD243X0_Data4[1] =
{
	0xB0u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data5[1] =
{
	0x04u	
};

static unsigned char gaConfig_AD243xMain_0_A2BDelay_Data6[1] =
{
	0x19u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data7[1] =
{
	0x19u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data8[1] =
{
	0x80u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data9[1] =
{
	0x80u	
};

static unsigned char gaConfig_AD243xMain_0_A2BDelay_Data10[1] =
{
	0x19u	
};

static unsigned char gaConfig_AD243xMain_0_A2BDelay_Data11[1] =
{
	0x19u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data12[3] =
{
	0xADu,	0x37u,	0x21u	
};

static unsigned char gaConfig_AD243xMain_0_REGAD243X0_Data13[1] =
{
	0x00u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data14[3] =
{
	0x10u,	0x00u,	0x09u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data15[1] =
{
	0xEFu	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data16[1] =
{
	0x01u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data17[1] =
{
	0x7Du	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data18[1] =
{
	0xCCu	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data19[1] =
{
	0x99u	
};

static unsigned char gaConfig_AD243xMain_0_A2BDelay_Data20[1] =
{
	0x01u	
};

static unsigned char gaConfig_AD243xMain_0_A2BDelay_Data21[1] =
{
	0x01u	
};

static unsigned char gaConfig_AD243xMain_0_REGAD243X0_Data22[1] =
{
	0x00u	
};

static unsigned char gaConfig_AD243xMain_0_REGAD243X0_Data23[1] =
{
	0x00u	
};

static unsigned char gaConfig_AD243xMain_0_REGAD243X0_Data24[1] =
{
	0x20u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data25[1] =
{
	0xE0u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data26[1] =
{
	0x11u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data27[1] =
{
	0x7Du	
};

static unsigned char gaConfig_AD243xMain_0_A2BDelay_Data28[1] =
{
	0x64u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data29[1] =
{
	0x64u	
};

static unsigned char gaConfig_AD243xMain_0_A2BDelay_Data30[1] =
{
	0x32u	
};

static unsigned char gaConfig_AD243xMain_0_A2BDelay_Data31[1] =
{
	0x32u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data32[1] =
{
	0x00u	
};

static unsigned char gaConfig_AD243xSubNode_1_REGA2B0_Data0[1] =
{
	0x7Du	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data33[1] =
{
	0x21u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data34[1] =
{
	0x00u	
};

static unsigned char gaConfig_AD243xSubNode_1_REGA2B0_Data1[4] =
{
	0xADu,	0x37u,	0x21u,	0x01u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data35[1] =
{
	0x00u	
};

static unsigned char gaConfig_AD243xSubNode_1_REGA2B0_Data2[1] =
{
	0x50u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data36[1] =
{
	0x20u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data37[1] =
{
	0x00u	
};

static unsigned char gaConfig_AD243xSubNode_1_REGA2B0_Data3[1] =
{
	0x51u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data38[1] =
{
	0x20u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data39[1] =
{
	0x00u	
};

static unsigned char gaConfig_AD243xSubNode_1_REGA2B0_Data4[1] =
{
	0x52u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data40[1] =
{
	0x20u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data41[1] =
{
	0x00u	
};

static unsigned char gaConfig_AD243xSubNode_1_REGA2B0_Data5[1] =
{
	0x53u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data42[1] =
{
	0x20u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data43[1] =
{
	0x00u	
};

static unsigned char gaConfig_AD243xSubNode_1_REGA2B0_Data6[1] =
{
	0x54u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data44[1] =
{
	0x20u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data45[1] =
{
	0x10u	
};

static unsigned char gaConfig_AD243xMain_0_REGA2B0_Data46[1] =
{
	0x92u	
};

ADI_A2B_DISCOVERY_CONFIG gaA2BConfig[CONFIG_LEN] =
{

	/*-- COMMANDS FOR DEVICE - AD243xMain_0 --*/
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000012u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data0[0],	I2C},	/* REG_A2B0_CONTROL       */
	{0x00u,	DELAY,	0x00u,	0x00000000u,	0x01u,	0x00000012u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_A2BDelay_Data1[0],	I2C},	/* A2B_Delay */
	{0x68u,	READ,	0x00u,	0x00000000u,	0x01u,	0x00000017u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data2[0],	I2C},	/* REG_A2B0_INTTYPE       */
	{0x68u,	READ,	0x00u,	0x00000000u,	0x01u,	0x00000002u,	0x01u,	0x0003u,	&gaConfig_AD243xMain_0_REGA2B0_Data3[0],	I2C},	/* REG_A2B0_VENDOR        */
	{0x68u,	READ,	0x00u,	0x00000000u,	0x01u,	0x0000005Eu,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGAD243X0_Data4[0],	I2C},	/* REG_AD243X0_BSDSTAT     */
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000012u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data5[0],	I2C},	/* REG_A2B0_CONTROL       */
	{0x00u,	DELAY,	0x00u,	0x00000000u,	0x01u,	0x00000012u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_A2BDelay_Data6[0],	I2C},	/* A2B_Delay */
	{0x68u,	READ,	0x00u,	0x00000000u,	0x01u,	0x00000017u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data7[0],	I2C},	/* REG_A2B0_INTTYPE       */
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000041u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data8[0],	I2C},	/* REG_A2B0_I2SGCFG       */
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000012u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data9[0],	I2C},	/* REG_A2B0_CONTROL       */
	{0x00u,	DELAY,	0x00u,	0x00000000u,	0x00u,	0x00000000u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_A2BDelay_Data10[0],	I2C},	/* A2B_Delay */
	{0x68u,	READ,	0x00u,	0x00000000u,	0x01u,	0x00000017u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_A2BDelay_Data11[0],	I2C},	/* A2B_Delay */
	{0x68u,	READ,	0x00u,	0x00000000u,	0x01u,	0x00000002u,	0x01u,	0x0003u,	&gaConfig_AD243xMain_0_REGA2B0_Data12[0],	I2C},	/* REG_A2B0_VENDOR        */
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x000000B7u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGAD243X0_Data13[0],	I2C},	/* REG_AD243X0_SPIMSK      */
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x0000001Bu,	0x01u,	0x0003u,	&gaConfig_AD243xMain_0_REGA2B0_Data14[0],	I2C},	/* REG_A2B0_INTMSK0       */
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x0000001Eu,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data15[0],	I2C},	/* REG_A2B0_BECCTL        */
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x0000001Au,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data16[0],	I2C},	/* REG_A2B0_INTPND2       */
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x0000000Fu,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data17[0],	I2C},	/* REG_A2B0_RESPCYCS      */
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x0000004Eu,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data18[0],	I2C},	/* REG_A2B0_GPIOIEN       */
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000012u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data19[0],	I2C},	/* REG_A2B0_CONTROL       */
	{0x00u,	DELAY,	0x00u,	0x00000000u,	0x00u,	0x00000000u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_A2BDelay_Data20[0],	I2C},	/* A2B_Delay */
	{0x68u,	READ,	0x00u,	0x00000000u,	0x01u,	0x00000017u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_A2BDelay_Data21[0],	I2C},	/* A2B_Delay */
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x000000A0u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGAD243X0_Data22[0],	I2C},	/* REG_AD243X0_SWCTL2      */
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x000000A3u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGAD243X0_Data23[0],	I2C},	/* REG_AD243X0_SWCTL5      */
	{0x68u,	READ,	0x00u,	0x00000000u,	0x01u,	0x000000A5u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGAD243X0_Data24[0],	I2C},	/* REG_AD243X0_SWSTAT2     */
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000041u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data25[0],	I2C},	/* REG_A2B0_I2SGCFG       */
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000009u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data26[0],	I2C},	/* REG_A2B0_SWCTL         */
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000013u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data27[0],	I2C},	/* REG_A2B0_DISCVRY       */
	{0x00u,	DELAY,	0x00u,	0x00000000u,	0x01u,	0x00000013u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_A2BDelay_Data28[0],	I2C},	/* A2B_Delay */
	{0x68u,	READ,	0x00u,	0x00000000u,	0x01u,	0x00000017u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data29[0],	I2C},	/* REG_A2B0_INTTYPE       */
	{0x00u,	DELAY,	0x00u,	0x00000000u,	0x00u,	0x00000000u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_A2BDelay_Data30[0],	I2C},	/* A2B_Delay */
	{0x68u,	READ,	0x00u,	0x00000000u,	0x01u,	0x00000017u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_A2BDelay_Data31[0],	I2C},	/* A2B_Delay */
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000001u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data32[0],	I2C},	/* REG_A2B0_NODEADR       */

	/*-- COMMANDS FOR DEVICE - AD243xSubNode_1 --*/
	{0x69u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x0000000Fu,	0x01u,	0x0001u,	&gaConfig_AD243xSubNode_1_REGA2B0_Data0[0],	I2C},	/* REG_A2B0_RESPCYCS      */

	/*-- COMMANDS FOR DEVICE - AD243xMain_0 --*/
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000009u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data33[0],	I2C},	/* REG_A2B0_SWCTL         */
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000001u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data34[0],	I2C},	/* REG_A2B0_NODEADR       */

	/*-- COMMANDS FOR DEVICE - AD243xSubNode_1 --*/
	{0x69u,	READ,	0x00u,	0x00000000u,	0x01u,	0x00000002u,	0x01u,	0x0004u,	&gaConfig_AD243xSubNode_1_REGA2B0_Data1[0],	I2C},	/* REG_A2B0_VENDOR        */

	/*-- COMMANDS FOR DEVICE - AD243xMain_0 --*/
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000001u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data35[0],	I2C},	/* REG_A2B0_NODEADR       */

	/*-- COMMANDS FOR DEVICE - AD243xSubNode_1 --*/
	{0x69u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000000u,	0x01u,	0x0001u,	&gaConfig_AD243xSubNode_1_REGA2B0_Data2[0],	I2C},	/* REG_A2B0_CHIP          */

	/*-- COMMANDS FOR DEVICE - AD243xMain_0 --*/
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000001u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data36[0],	I2C},	/* REG_A2B0_NODEADR       */
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000001u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data37[0],	I2C},	/* REG_A2B0_NODEADR       */

	/*-- COMMANDS FOR DEVICE - AD243xSubNode_1 --*/
	{0x69u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000000u,	0x01u,	0x0001u,	&gaConfig_AD243xSubNode_1_REGA2B0_Data3[0],	I2C},	/* REG_A2B0_CHIP          */

	/*-- COMMANDS FOR DEVICE - AD243xMain_0 --*/
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000001u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data38[0],	I2C},	/* REG_A2B0_NODEADR       */
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000001u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data39[0],	I2C},	/* REG_A2B0_NODEADR       */

	/*-- COMMANDS FOR DEVICE - AD243xSubNode_1 --*/
	{0x69u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000000u,	0x01u,	0x0001u,	&gaConfig_AD243xSubNode_1_REGA2B0_Data4[0],	I2C},	/* REG_A2B0_CHIP          */

	/*-- COMMANDS FOR DEVICE - AD243xMain_0 --*/
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000001u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data40[0],	I2C},	/* REG_A2B0_NODEADR       */
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000001u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data41[0],	I2C},	/* REG_A2B0_NODEADR       */

	/*-- COMMANDS FOR DEVICE - AD243xSubNode_1 --*/
	{0x69u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000000u,	0x01u,	0x0001u,	&gaConfig_AD243xSubNode_1_REGA2B0_Data5[0],	I2C},	/* REG_A2B0_CHIP          */

	/*-- COMMANDS FOR DEVICE - AD243xMain_0 --*/
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000001u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data42[0],	I2C},	/* REG_A2B0_NODEADR       */
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000001u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data43[0],	I2C},	/* REG_A2B0_NODEADR       */

	/*-- COMMANDS FOR DEVICE - AD243xSubNode_1 --*/
	{0x69u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000000u,	0x01u,	0x0001u,	&gaConfig_AD243xSubNode_1_REGA2B0_Data6[0],	I2C},	/* REG_A2B0_CHIP          */

	/*-- COMMANDS FOR DEVICE - AD243xMain_0 --*/
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000001u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data44[0],	I2C},	/* REG_A2B0_NODEADR       */
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000009u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data45[0],	I2C},	/* REG_A2B0_SWCTL         */
	{0x68u,	WRITE,	0x00u,	0x00000000u,	0x01u,	0x00000012u,	0x01u,	0x0001u,	&gaConfig_AD243xMain_0_REGA2B0_Data46[0],	I2C},	/* REG_A2B0_CONTROL       */
};

#endif /* _ADI_A2B_CMD_LIST_H_ */

