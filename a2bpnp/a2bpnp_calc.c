/*******************************************************************************
Copyright (c) 2022 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
*******************************************************************************

   Name       : a2bpnp_calc.c

   Description: This file is responsible for handling all the Plug and Play level
                functions

   Developed by: Automotive Software and Systems team, Bangalore, India

******************************************************************************/
/*============= I N C L U D E S =============*/
#include <stdlib.h>
#include "a2b/pal.h"
#include "assert.h"
#include "a2b/ctypes.h"
#include <a2bpnp.h>
#include <a2bpnp_local.h>
#include <a2bpnp_stack_interface.h>
#include <math.h>
#include <stdlib.h>

//Response cycle constants
const int32_t TURNAROUNDTIME = 8;
const int32_t NUM_OF_SCFBITS = 64;                     //number of bits in SCF
const int32_t NUM_OF_SRFBITS = 64;                     //number of bits in SRF
const int32_t SF_CLOCKCYCLES = 1024;                   //number of clock cyles in superframe
const double  SF_BITTIME = 20.35;                      //one super frame transmission time
const double  TRX_ATOB_AVGDELAY = 7.5;                 //avg delay from A to B
const double  TRX_BTOA_AVGDELAY = 8.3;                 //avg delay from B to A
const double  CABLE_PROP_DELAY = 6.5;                  //cable propogation delay
const int32_t RESPONSE_CYCLE_BIT = 4;
const int32_t RESPONSE_CYCLE_OFFSET = 7;
const int32_t STDBY_SCF_LEN = 19;       //super frame bits in standby mode

static a2b_UInt8 Readbuf[4096];

/* TODO: All below PnP structure need to be updated to take care of multi-main use cases */
static void a2b_PrunePassDNList(a2b_App_t *pApp_Info, a2b_Int8 nNode);
static void a2b_PrunePassUPList(a2b_App_t *pApp_Info, a2b_Int8 nNode);


SlotInfo getSlotInfo(a2b_App_t *pApp_Info, a2b_Int32 nodeId);
a2b_UInt32 getDownstreamSlotSize(a2b_UInt8 slotFmt);
a2b_UInt32 getUpstreamSlotSize(a2b_UInt8 slotFmt);
int32_t getmaxMaskDn(a2b_App_t *pApp_Info, a2b_Int32 nodeId);
int32_t getmaxMaskUp(a2b_App_t *pApp_Info, a2b_Int32 nodeId);
a2b_UInt32 getResponseCycleOffset(a2b_App_t *pApp_Info, a2b_Int32 nodeId);
a2b_Bool isCrossbarDefault(a2b_UInt32 crossbarReg[]);
void CrossbarLocalMute(a2b_App_t *pApp_Info, a2b_Int8 node);

#ifndef max
#define max(a,b)            (((a) > (b)) ? (a) : (b))
#endif

#ifndef min
#define min(a,b)            (((a) < (b)) ? (a) : (b))
#endif

/* nodeAddr is the sub node address which is dropped */
void UpdateAudioRt(a2b_App_t *pApp_Info, a2b_Int16 nodeAddr)
{
	a2b_UInt8 StreamIdx, OldStreamIdx;
	a2b_UInt8 Idx;
	a2b_UInt8 Idx1;
	a2b_Int16 NodeIdx;
	a2bpnp_NetworkInfo *PnpNWInfo;

	PnpNWInfo = &pApp_Info->PnpNWInfo;

	for(StreamIdx=0u;StreamIdx<PnpNWInfo->nStreamCount;StreamIdx++)
	{	/* Identify the streams with dropped nodes as source */
		if(PnpNWInfo->NWStream[StreamIdx].SourceNodeIdx >= nodeAddr)
		{
			for(Idx=0u; Idx<PnpNWInfo->NWStream[StreamIdx].nDestNodes;Idx++)
			{
				pApp_Info->SubNodeTypeInfo[PnpNWInfo->NWStream[StreamIdx].DestNodeIdx[Idx]].IsStreamDefined = A2B_FALSE;
			}
			PnpNWInfo->NWStream[StreamIdx].nDestNodes = 0u;
		}
	}

	for(NodeIdx=nodeAddr; NodeIdx<(int8_t)A2B_CONF_MAX_NUM_SLAVE_NODES; NodeIdx++)
	{
		pApp_Info->SubNodeTypeInfo[NodeIdx].IsStreamDefined = A2B_FALSE;
	}

	for(StreamIdx=0u; StreamIdx<PnpNWInfo->nStreamCount; StreamIdx++)
	{
		Idx=0u;
		/* Remove all the destination nodes which are dropped */
		while(Idx<PnpNWInfo->NWStream[StreamIdx].nDestNodes)
		{
			if(PnpNWInfo->NWStream[StreamIdx].DestNodeIdx[Idx] >= nodeAddr)
			{
				for(Idx1 = Idx; Idx1<(PnpNWInfo->NWStream[StreamIdx].nDestNodes-1u);Idx1++)
				{
					PnpNWInfo->NWStream[StreamIdx].DestNodeIdx[Idx1] = PnpNWInfo->NWStream[StreamIdx].DestNodeIdx[Idx1+1u];
				}

				PnpNWInfo->NWStream[StreamIdx].nDestNodes--;
				pApp_Info->SubNodeTypeInfo[PnpNWInfo->NWStream[StreamIdx].DestNodeIdx[Idx]].IsStreamDefined = A2B_FALSE;
			}
			else
			{
				Idx++;
			}
		}
	}

	StreamIdx=0u;
	/* Removed all the stream which have zero destination nodes */
	while(StreamIdx<PnpNWInfo->nStreamCount)
	{
		if(PnpNWInfo->NWStream[StreamIdx].nDestNodes == 0u)
		{
			PnpNWInfo->nStreamCount--;
			/* Source node stream is reset */
			pApp_Info->SubNodeTypeInfo[PnpNWInfo->NWStream[StreamIdx].SourceNodeIdx].IsStreamDefined = A2B_FALSE;
			/* Adjust the streams */
			for(Idx1 = StreamIdx; Idx1<PnpNWInfo->nStreamCount;Idx1++)
			{
				OldStreamIdx = PnpNWInfo->NWStream[StreamIdx].streamIdx;
				(void)memcpy(&PnpNWInfo->NWStream[StreamIdx], &PnpNWInfo->NWStream[StreamIdx+1u], sizeof(a2bpnp_StreamInfo));
				PnpNWInfo->NWStream[StreamIdx].streamIdx = OldStreamIdx;
			}
		}
		else
		{
			/* Check the next stream */
			StreamIdx++;
		}
	}
}


a2b_Int8 a2b_ReadVerify_ModuleInfo(a2b_App_t *pApp_Info, a2b_Int16 nodeAddr)
{
    a2b_UInt8 wBuf[4u];
    a2b_UInt32 ReadCounter=0u;
    a2b_UInt8 WriteCounter=0u;
    a2b_UInt8 NumPeri = 0;
    a2b_UInt8 NumPeriBlocks;
    a2b_UInt16 BlockSize;
//A2B_PNP_DEVICETYPE DeviceID;
    a2b_UInt32 DeviceID;
    a2b_UInt8 DeviceName[60];
    a2b_UInt8 GroupName[32] = {0};
    a2b_UInt32 PeriConifigBlockAddr[8]; /* TODO: Use macro */
    a2b_UInt8 PeriCount;
    a2b_UInt16 FuncCount;
    a2b_UInt8 I2SCFG_Val;
    a2b_UInt16 FunctionAdd[16];
    a2b_UInt8 I2SGCFG_Val;
    a2b_UInt8 SPICFG_Val;
    a2b_UInt8 SPICKDIV_Val;
    a2b_UInt8 SPIPINCFG_Val;
    a2b_UInt8 PDMCTL_Val;
    a2b_UInt8 PDMCTL2_Val;
    a2b_UInt8 CLKOUT1_Val;
    a2b_UInt8 CLKOUT2_Val;
    a2b_UInt16 EepromI2CAddr=A2B_PNP_EEPROM_ADRESS_START;
    a2b_UInt32 Result;
    a2b_Int8 nodeIdx;
    a2b_UInt8 chCnt=0u;
    a2b_UInt8 devCnt=0u;
    a2b_UInt16 TxGroupMap = 0;
    a2b_UInt16 RxGroupMap = 0;
    nodeIdx = nodeAddr-1;
	a2bpnp_NetworkInfo *PnpNWInfo;

	PnpNWInfo = &pApp_Info->PnpNWInfo;

	if(pApp_Info->ePnPModuleState == A2B_PNP_BLIND_DISC_IN_PROGRESS)
	{
		memset(&PnpNWInfo->DevInfo[nodeIdx], 0,sizeof(a2bpnp_DeviceInfo));
		{
			memset(Readbuf, 0,sizeof(Readbuf));
			/* Iterate through all the possible EEPROMs for locating the PnP content */
			do
			{
				/* Verify valid module information */
				wBuf[0]= 0x00;
				wBuf[1]= A2B_PNP_MODINFO_ID_ADDR;
				Result = a2b_periphWriteRead(pApp_Info->ctx, nodeIdx, EepromI2CAddr, (a2b_UInt16)0x02, &wBuf[0], (a2b_UInt16)0x01, (void*)&Readbuf[A2B_PNP_MODINFO_ID_ADDR]);
				if( (Result != 0u) || (Readbuf[0] != A2B_PNP_MODINFO_ID_VAL))
				{
					EepromI2CAddr = EepromI2CAddr + 1u;
					if(EepromI2CAddr > A2B_PNP_EEPROM_ADRESS_END)
					{
						return 1;
					}
				}
				else
				{
					if(pApp_Info->bDebug)
					{
						A2B_APP_DBG_LOG("\n********Found EEPROM with Module info*********\n");
					}	
					break;
				}
			}while(1);


			PnpNWInfo->DevInfo[nodeIdx].eepromAddr = EepromI2CAddr;
			/* Read Vendor ID, Product ID etc to authenticate */
			wBuf[0]= 0x00;
			wBuf[1]= A2B_PNP_MODINFO_VENDORID_ADDR;
			Result = a2b_periphWriteRead(pApp_Info->ctx, nodeIdx, EepromI2CAddr, 0x02, &wBuf[0], 0x06, &Readbuf[A2B_PNP_MODINFO_VENDORID_ADDR]);
			if(pApp_Info->bDebug)
			{
				A2B_APP_DBG_LOG("********Vendor ID: 0x%X, Product ID: 0x%X, Silicon Version: 0x%X *********\n", Readbuf[1], Readbuf[3], Readbuf[5]);
			}
			if( (Result != A2B_RESULT_SUCCESS) || (Readbuf[A2B_PNP_MODINFO_VENDORID_ADDR] != A2B_PNP_MODINFO_VENDORID_VAL) || (Readbuf[A2B_PNP_MODINFO_PRODUCTID_ADDR] != A2B_PNP_MODINFO_PRODUCTID_VAL))
			{
				/* Reject the device */
				if(pApp_Info->bDebug)
				{
					A2B_APP_DBG_LOG("\n********Device Authentication failed*********\n");
				}
				return 1;
			}

			/*SW Version*/
			wBuf[0]= 0x00;
			wBuf[1]= A2B_PNP_MODINFO_SWVER_ADDR;
			Result = a2b_periphWriteRead(pApp_Info->ctx, nodeIdx, EepromI2CAddr, 0x02, &wBuf[0], 0x04, &Readbuf[A2B_PNP_MODINFO_SWVER_ADDR]);
			snprintf(PnpNWInfo->DevInfo[nodeIdx].swVersion, sizeof(PnpNWInfo->DevInfo[nodeIdx].swVersion), "%d.%d.%d.%d", Readbuf[A2B_PNP_MODINFO_SWVER_ADDR], Readbuf[A2B_PNP_MODINFO_SWVER_ADDR+1], Readbuf[A2B_PNP_MODINFO_SWVER_ADDR+2], Readbuf[A2B_PNP_MODINFO_SWVER_ADDR+3]);

			/*Primary Function address*/
			wBuf[0]= 0x00;
			wBuf[1]= A2B_PNP_MODINFO_PRIM_FUNC_ADDR;
			Result = a2b_periphWriteRead(pApp_Info->ctx, nodeIdx, EepromI2CAddr, 0x02, &wBuf[0], 0x04, &Readbuf[A2B_PNP_MODINFO_PRIM_FUNC_ADDR]);
			a2b_UInt32 primaryFuncAddr = (a2b_UInt32)(Readbuf[A2B_PNP_MODINFO_PRIM_FUNC_ADDR] | Readbuf[A2B_PNP_MODINFO_PRIM_FUNC_ADDR+1]<<8 | Readbuf[A2B_PNP_MODINFO_PRIM_FUNC_ADDR+2]<<16 | Readbuf[A2B_PNP_MODINFO_PRIM_FUNC_ADDR+3]<<24);

			if (primaryFuncAddr != A2B_PNP_BLOCK_MODINFO_ADRR)
			{
				/* Reject the device */
				if(pApp_Info->bDebug)
				{
					A2B_APP_DBG_LOG("\n********Incorrect Primary Function Address *********\n");
				}
				return 1;
			}
			/* Reading Module Information Block */
			wBuf[0]= 0x00;
			wBuf[1]= A2B_PNP_BLOCK_MODINFO_ADRR;
			Result = a2b_periphWriteRead(pApp_Info->ctx, nodeIdx, EepromI2CAddr, 0x02, &wBuf[0], A2B_PNP_BLOCK_HEADER_SIZE, &Readbuf[A2B_PNP_BLOCK_MODINFO_ADRR]);

			/* Check the module information block */
			if (((Result != A2B_RESULT_SUCCESS) || (Readbuf[A2B_PNP_BLOCK_MODINFO_ADRR] >> 4u) != A2B_PNP_BLOCK_MODINFO_TYPE))
			{
				/* Reject the device */
				if(pApp_Info->bDebug)
				{
					A2B_APP_DBG_LOG("\n********Incorrect Module Info Block *********\n");
				}
				return 1;
			}

			/* Size of Module Info Block: */
			BlockSize = (((a2b_UInt16)Readbuf[A2B_PNP_BLOCK_MODINFO_ADRR] & 0x0Fu) << 8u) | Readbuf[A2B_PNP_BLOCK_MODINFO_ADRR+1u];
			if(pApp_Info->bDebug)
			{
				A2B_APP_DBG_LOG("\n********Size of Module Info Block: 0x%X *********\n", BlockSize);
			}


			/* Read complete Module Information block */
			/* Reading Module Information Block */
			wBuf[0]= 0x00;
			wBuf[1]= A2B_PNP_BLOCK_MODINFO_ADRR+A2B_PNP_BLOCK_HEADER_SIZE;
			Result = a2b_periphWriteRead(pApp_Info->ctx, nodeIdx, EepromI2CAddr, 0x02, &wBuf[0], BlockSize, &Readbuf[A2B_PNP_BLOCK_MODINFO_ADRR+A2B_PNP_BLOCK_HEADER_SIZE]);

			/* Check the power capability */
			if ((Result != A2B_RESULT_SUCCESS) || (Readbuf[A2B_PNP_BLOCK_MODINFO_POWERCONFIG_ADDR] >> 4u) != A2B_PNP_BLOCK_MODINFO_PWRCONFIG)
			{
				/* Reject the device */
				if(pApp_Info->bDebug)
				{
					A2B_APP_DBG_LOG("\n********Incorrect Power configuration *********\n");
				}
				return 1;
			}

			/* Bus powered/ Local Pwered ? */
			if ((Readbuf[A2B_PNP_BLOCK_MODINFO_POWERCONFIG_ADDR] &0xFu) == 0u)
			{
				A2B_APP_DBG_LOG("\n********Bus powered device detected*********\n");
				PnpNWInfo->DevInfo[nodeIdx].pwrInfo.bLocalPowered = A2B_FALSE;
			}
			else
			{
				A2B_APP_DBG_LOG("\n********Local powered device detected*********\n");
				PnpNWInfo->DevInfo[nodeIdx].pwrInfo.bLocalPowered = A2B_TRUE;
			}
			PnpNWInfo->DevInfo[nodeIdx].pwrInfo.maxPwr = ((Readbuf[A2B_PNP_BLOCK_MODINFO_MAX_PWR_ADDR] ) | (Readbuf[A2B_PNP_BLOCK_MODINFO_MAX_PWR_ADDR + 1]) << 8);
			PnpNWInfo->DevInfo[nodeIdx].pwrInfo.minPwr = ((Readbuf[A2B_PNP_BLOCK_MODINFO_MIN_PWR_ADDR] ) | (Readbuf[A2B_PNP_BLOCK_MODINFO_MIN_PWR_ADDR + 1]) << 8);
			PnpNWInfo->DevInfo[nodeIdx].pwrInfo.maxCurrent = ((Readbuf[A2B_PNP_BLOCK_MODINFO_MAX_CURRENT_ADDR] ) | (Readbuf[A2B_PNP_BLOCK_MODINFO_MAX_CURRENT_ADDR + 1]) << 8);
			//Cable Type

			/*Update DeviceID type, device name, device ID location*/
			DeviceID = (a2b_UInt32)(Readbuf[A2B_PNP_BLOCK_MODINFO_DEVICEID_ADDR] | (Readbuf[A2B_PNP_BLOCK_MODINFO_DEVICEID_ADDR+1u] << 8u) | (Readbuf[A2B_PNP_BLOCK_MODINFO_DEVICEID_ADDR+2u] << 16u) | (Readbuf[A2B_PNP_BLOCK_MODINFO_DEVICEID_ADDR+3u] << 24u));

			if(DeviceID)
			{
				for(a2b_UInt8 i = 0; i<A2B_PNP_CFG_MAX_NUM_DTYPES; i++)
				{
					if(DeviceID & (1<<i))
					{
						PnpNWInfo->DevInfo[nodeIdx].nDeviceType++;
						PnpNWInfo->DevInfo[nodeIdx].DeviceType[devCnt++] = (a2bpnp_DeviceType)i;
					}
				}
			}

			ReadCounter = A2B_PNP_BLOCK_MODINFO_DEVICENAME_ADDR;
			while(Readbuf[ReadCounter] != 0u)
			{
				DeviceName[WriteCounter] = Readbuf[ReadCounter];
				ReadCounter++;
				WriteCounter++;
			};

			DeviceName[WriteCounter] = 0x0u;

			(void)memcpy(PnpNWInfo->DevInfo[nodeIdx].DeviceName, &Readbuf[A2B_PNP_BLOCK_MODINFO_DEVICENAME_ADDR], (strlen((const char*)DeviceName)+1u));

			/*Peripheral Info Block ADDR*/
			a2b_UInt32 periBlockAddr = (a2b_UInt32)(Readbuf[A2B_PNP_BLOCK_MODINFO_PERIPHBLOCK_ADDR] | Readbuf[A2B_PNP_BLOCK_MODINFO_PERIPHBLOCK_ADDR+1]<<8 | Readbuf[A2B_PNP_BLOCK_MODINFO_PERIPHBLOCK_ADDR+2]<<16 | Readbuf[A2B_PNP_BLOCK_MODINFO_PERIPHBLOCK_ADDR+3]<<24);
			if (periBlockAddr != A2B_PNP_BLOCK_PERIINFO_ADDR)
			{
				/* Reject the device */
				if(pApp_Info->bDebug)
				{
					A2B_APP_DBG_LOG("\n********Incorrect Peripheral Block Address *********\n");
				}
//				return 1;
			}


			/* Audio Device and Channel configuration*/

			PDMCTL_Val = Readbuf[A2B_PNP_BLOCK_MODINFO_PDMCTL_ADDR];

			if(PDMCTL_Val != 0x0u)
			{
				NumPeri = ((PDMCTL_Val & 0x05u)==0x05)?2:1;
				for(ReadCounter = 0; ReadCounter<NumPeri; ReadCounter++)
				{
					switch((PDMCTL_Val >> ReadCounter*2u) & 0x03u)
					{
					case 0x03u:
						PnpNWInfo->DevInfo[nodeIdx].PeripInfo[ReadCounter].nSourceChannels = 2u;
						PnpNWInfo->DevInfo[nodeIdx].PeripInfo[ReadCounter].nRxPins = 1u;
						PnpNWInfo->DevInfo[nodeIdx].PeripInfo[ReadCounter].RxPinMap = 0x01u + (a2b_UInt8)ReadCounter;
						break;

					case 0x01u:
						PnpNWInfo->DevInfo[nodeIdx].PeripInfo[ReadCounter].nSourceChannels = 1u;
						PnpNWInfo->DevInfo[nodeIdx].PeripInfo[ReadCounter].nRxPins = 1u;
						PnpNWInfo->DevInfo[nodeIdx].PeripInfo[ReadCounter].RxPinMap = 0x01u + (a2b_UInt8)ReadCounter;
						break;
					}
				}
			}

			/* Total number of Peripheral configuration blocks */
		    NumPeriBlocks = Readbuf[A2B_PNP_BLOCK_MODINFO_NUMPERIHBLOCK_ADDR];

			PnpNWInfo->DevInfo[nodeIdx].dnSlotSize = Readbuf[A2B_PNP_BLOCK_MODINFO_DNSLOTSIZE_ADDR];
			PnpNWInfo->DevInfo[nodeIdx].upSlotSize = Readbuf[A2B_PNP_BLOCK_MODINFO_UPSLOTSIZE_ADDR];

			I2SCFG_Val =  Readbuf[A2B_PNP_BLOCK_MODINFO_I2SCFG_ADDR];
			I2SGCFG_Val = Readbuf[A2B_PNP_BLOCK_MODINFO_I2SGCFG_ADDR];
			SPICFG_Val = Readbuf[A2B_PNP_BLOCK_MODINFO_SPICFG_ADDR];
			SPICKDIV_Val = Readbuf[A2B_PNP_BLOCK_MODINFO_SPICKDIV_ADDR];
			SPIPINCFG_Val = Readbuf[A2B_PNP_BLOCK_MODINFO_SPIPINCFG_ADDR];
			PDMCTL_Val =  Readbuf[A2B_PNP_BLOCK_MODINFO_PDMCTL_ADDR];
			PDMCTL2_Val = Readbuf[A2B_PNP_BLOCK_MODINFO_PDMCTL2_ADDR];
			CLKOUT1_Val = Readbuf[A2B_PNP_BLOCK_MODINFO_CLKOUT1_ADDR];
			CLKOUT2_Val = Readbuf[A2B_PNP_BLOCK_MODINFO_CLKOUT2_ADDR];

			/* Update the required BDD registers */
			pApp_Info->bdd.nodes[nodeAddr].i2cI2sRegs.i2scfg = I2SCFG_Val;
			pApp_Info->bdd.nodes[nodeAddr].i2cI2sRegs.i2sgcfg = I2SGCFG_Val;
			pApp_Info->bdd.nodes[nodeAddr].i2cI2sRegs.pdmctl =  PDMCTL_Val;
			pApp_Info->bdd.nodes[nodeAddr].i2cI2sRegs.pdmctl2 = PDMCTL2_Val;
			pApp_Info->bdd.nodes[nodeAddr].pinIoRegs.clk1cfg = CLKOUT1_Val;
			pApp_Info->bdd.nodes[nodeAddr].pinIoRegs.clk2cfg = CLKOUT2_Val;
			pApp_Info->bdd.nodes[nodeAddr].spiRegs.spicfg = SPICFG_Val;
			pApp_Info->bdd.nodes[nodeAddr].spiRegs.spickdiv = SPICKDIV_Val;
			pApp_Info->bdd.nodes[nodeAddr].spiRegs.spipicfg = SPIPINCFG_Val;

			pApp_Info->bdd.nodes[nodeAddr].i2cI2sRegs.has_i2scfg = I2SCFG_Val ? 1: 0;
			pApp_Info->bdd.nodes[nodeAddr].i2cI2sRegs.has_i2sgcfg = I2SGCFG_Val ? 1:0;
			pApp_Info->bdd.nodes[nodeAddr].i2cI2sRegs.has_pdmctl =  PDMCTL_Val ?1 :0;
			pApp_Info->bdd.nodes[nodeAddr].i2cI2sRegs.has_pdmctl2 = PDMCTL2_Val ?1:0;
			pApp_Info->bdd.nodes[nodeAddr].pinIoRegs.has_clk1cfg = CLKOUT1_Val ? 1:0;
			pApp_Info->bdd.nodes[nodeAddr].pinIoRegs.has_clk2cfg = CLKOUT2_Val ? 1:0;
			pApp_Info->bdd.nodes[nodeAddr].spiRegs.has_spicfg = SPICFG_Val ? 1: 0;
			pApp_Info->bdd.nodes[nodeAddr].spiRegs.has_spickdiv = SPICKDIV_Val ? 1 :0;
			pApp_Info->bdd.nodes[nodeAddr].spiRegs.has_spipicfg = SPIPINCFG_Val ? 1 :0;
			pApp_Info->bdd.nodes[nodeAddr].has_spiRegs = 1;

			//gpio registers
			pApp_Info->bdd.nodes[nodeAddr].pinIoRegs.gpiooen = Readbuf[A2B_PNP_BLOCK_MODINFO_GPIOOEN_ADDR];
			pApp_Info->bdd.nodes[nodeAddr].pinIoRegs.gpioien =Readbuf[A2B_PNP_BLOCK_MODINFO_GPIOIEN_ADDR];
			pApp_Info->bdd.nodes[nodeAddr].pinIoRegs.pinten = Readbuf[A2B_PNP_BLOCK_MODINFO_PINTEN_ADDR];
			pApp_Info->bdd.nodes[nodeAddr].pinIoRegs.pintinv = Readbuf[A2B_PNP_BLOCK_MODINFO_PINTINV_ADDR];
			pApp_Info->bdd.nodes[nodeAddr].pinIoRegs.pincfg = Readbuf[A2B_PNP_BLOCK_MODINFO_PINCFG_ADDR];
			pApp_Info->bdd.nodes[nodeAddr].pinIoRegs.gpiodat = Readbuf[A2B_PNP_BLOCK_MODINFO_GPIODAT_ADDR];

			pApp_Info->bdd.nodes[nodeAddr].pinIoRegs.has_gpiooen = pApp_Info->bdd.nodes[nodeAddr].pinIoRegs.gpiooen ? 1:0;
			pApp_Info->bdd.nodes[nodeAddr].pinIoRegs.has_gpioien = pApp_Info->bdd.nodes[nodeAddr].pinIoRegs.gpioien ? 1:0;
			pApp_Info->bdd.nodes[nodeAddr].pinIoRegs.has_pinten = pApp_Info->bdd.nodes[nodeAddr].pinIoRegs.pinten ? 1:0;
			pApp_Info->bdd.nodes[nodeAddr].pinIoRegs.has_pintinv = pApp_Info->bdd.nodes[nodeAddr].pinIoRegs.pintinv ? 1:0;
			pApp_Info->bdd.nodes[nodeAddr].pinIoRegs.has_pincfg = (pApp_Info->bdd.nodes[nodeAddr].pinIoRegs.pincfg !=1) ? 1:0;
			pApp_Info->bdd.nodes[nodeAddr].pinIoRegs.has_gpiodat = pApp_Info->bdd.nodes[nodeAddr].pinIoRegs.gpiodat ? 1:0;

			/*DT config*/
			PnpNWInfo->DevInfo[nodeIdx].dtEnabled = Readbuf[A2B_PNP_BLOCK_MODINFO_DTREQ_ADDR];
			PnpNWInfo->DevInfo[nodeIdx].dtRole = (1<<2) & (pApp_Info->bdd.nodes[nodeAddr].spiRegs.spicfg);//Bit 2 is TNLOWNER bit
			PnpNWInfo->DevInfo[nodeIdx].nDtDnSlots = Readbuf[A2B_PNP_BLOCK_MODINFO_DTDNRNG_ADDR];
			PnpNWInfo->DevInfo[nodeIdx].nDtUpSlots = Readbuf[A2B_PNP_BLOCK_MODINFO_DTUPRNG_ADDR];

			//Read the TxGroup details
			PnpNWInfo->DevInfo[nodeIdx].nTxGroups = Readbuf[A2B_PNP_BLOCK_MODINFO_NTXGRP_ADDR];
			for(ReadCounter = 0u; ReadCounter < PnpNWInfo->DevInfo[nodeIdx].nTxGroups; ReadCounter++)
			{
				PnpNWInfo->DevInfo[nodeIdx].TxGroups[ReadCounter].GroupID = Readbuf[A2B_PNP_BLOCK_MODINFO_GRPID_ADDR + ReadCounter*A2B_PNP_NEXT_GRPID_OFFSET]; //ID+Name+bitmap = 37bytes

				a2b_UInt32 ReadCnt = A2B_PNP_BLOCK_MODINFO_GRPID_ADDR + ReadCounter*A2B_PNP_NEXT_GRPID_OFFSET + 1u;
				WriteCounter = 0u;
				while(Readbuf[ReadCnt] != 0u)
				{
					GroupName[WriteCounter] = Readbuf[ReadCnt];
					ReadCnt++;
					WriteCounter++;
				};
				(void)memcpy(PnpNWInfo->DevInfo[nodeIdx].TxGroups[ReadCounter].GroupName, &Readbuf[A2B_PNP_BLOCK_MODINFO_GRPID_ADDR + ReadCounter*A2B_PNP_NEXT_GRPID_OFFSET + 1u], (strlen((const char*)GroupName)+1u));

				chCnt = 0u;
				ReadCnt = A2B_PNP_BLOCK_MODINFO_GRPID_ADDR + ReadCounter*A2B_PNP_NEXT_GRPID_OFFSET + A2B_PNP_NEXT_GRPMAP_OFFSET;
				for(a2b_UInt8 i=0; i<4; i++)
				{
					if(Readbuf[ReadCnt])
					{
						a2b_UInt8 byte = Readbuf[ReadCnt];
						for(a2b_UInt8 j=0; j<8; j++)
						{
							if(byte & (1<<j))
							{
								PnpNWInfo->DevInfo[nodeIdx].TxGroups[ReadCounter].nGroupChannels++;
								PnpNWInfo->DevInfo[nodeIdx].TxGroups[ReadCounter].GroupChannels[chCnt++] = j + 8*i;
							}
						}
					}
					ReadCnt++;
				}

			}

			//Read the RxGroup details - TODO
			uint16_t nRxGrpAddr = A2B_PNP_BLOCK_MODINFO_NTXGRP_ADDR + 1u + PnpNWInfo->DevInfo[nodeIdx].nTxGroups * A2B_PNP_NEXT_GRPID_OFFSET;
			uint16_t nRxGIDAddr = nRxGrpAddr + 1u;
			PnpNWInfo->DevInfo[nodeIdx].nRxGroups = Readbuf[nRxGrpAddr];
			for(ReadCounter = 0u; ReadCounter < PnpNWInfo->DevInfo[nodeIdx].nRxGroups; ReadCounter++)
			{
				PnpNWInfo->DevInfo[nodeIdx].RxGroups[ReadCounter].GroupID = Readbuf[nRxGIDAddr + ReadCounter*A2B_PNP_NEXT_GRPID_OFFSET]; //ID+Name+bitmap = 37bytes

				a2b_UInt32 ReadCnt = nRxGIDAddr + ReadCounter*A2B_PNP_NEXT_GRPID_OFFSET + 1u;
				WriteCounter = 0u;
				while(Readbuf[ReadCnt] != 0u)
				{
					GroupName[WriteCounter] = Readbuf[ReadCnt];
					ReadCnt++;
					WriteCounter++;
				};
				(void)memcpy(PnpNWInfo->DevInfo[nodeIdx].RxGroups[ReadCounter].GroupName, &Readbuf[nRxGIDAddr + ReadCounter*A2B_PNP_NEXT_GRPID_OFFSET + 1u], (strlen((const char*)GroupName)+1u));

				chCnt = 0u;
				ReadCnt = nRxGIDAddr + ReadCounter*A2B_PNP_NEXT_GRPID_OFFSET + A2B_PNP_NEXT_GRPMAP_OFFSET;
				for(a2b_UInt8 i=0; i<4; i++)
				{
					if(Readbuf[ReadCnt])
					{
						a2b_UInt8 byte = Readbuf[ReadCnt];
						for(a2b_UInt8 j=0; j<8; j++)
						{
							if(byte & (1<<j))
							{
								PnpNWInfo->DevInfo[nodeIdx].RxGroups[ReadCounter].nGroupChannels++;
								PnpNWInfo->DevInfo[nodeIdx].RxGroups[ReadCounter].GroupChannels[chCnt++] = j + 8*i;
							}
						}
					}
					ReadCnt++;
				}

			}
			a2b_UInt32 devInfoBlockAddr = (a2b_UInt32)(Readbuf[A2B_PNP_BLOCK_MODINFO_DEVINFOBLOCK_ADDR] | Readbuf[A2B_PNP_BLOCK_MODINFO_DEVINFOBLOCK_ADDR+1]<<8 | Readbuf[A2B_PNP_BLOCK_MODINFO_DEVINFOBLOCK_ADDR+2]<<16 | Readbuf[A2B_PNP_BLOCK_MODINFO_DEVINFOBLOCK_ADDR+3]<<24);
			if (devInfoBlockAddr != A2B_PNP_BLOCK_DEVINFO_ADDR)
			{
				/* Reject the device */
				if(pApp_Info->bDebug)
				{
					A2B_APP_DBG_LOG("\n********Incorrect Device Information Block Address *********\n");
				}
				return 1;
			}

			/*TODO - Device Function Information Block*/
			/* Reading Peripheral Information Block header  */
			wBuf[0]= (a2b_UInt8)(A2B_PNP_BLOCK_DEVINFO_ADDR >> 8);
			wBuf[1]= (a2b_UInt8)(A2B_PNP_BLOCK_DEVINFO_ADDR & 0xFFu);
			Result = a2b_periphWriteRead(pApp_Info->ctx, nodeIdx, EepromI2CAddr, 0x02u, &wBuf[0], A2B_PNP_BLOCK_HEADER_SIZE, &Readbuf[A2B_PNP_BLOCK_DEVINFO_ADDR]);

			/* Check the peripheral information block */
			if ((Result != A2B_RESULT_SUCCESS) || (Readbuf[A2B_PNP_BLOCK_DEVINFO_ADDR] >> 4u) != A2B_PNP_BLOCK_DEVINFO_TYPE)
			{
				/* Reject the device */
				if(pApp_Info->bDebug)
				{
					A2B_APP_DBG_LOG("\n********Incorrect Device Info Block *********\n");
				}
				return 1;
			}

			/* Size of Device Info Block: */
			BlockSize = (((a2b_UInt16)Readbuf[A2B_PNP_BLOCK_DEVINFO_ADDR] & 0x0Fu) << 8u) | Readbuf[A2B_PNP_BLOCK_DEVINFO_ADDR+1u];

			/* Reading Device Information Block payload */
			if(BlockSize != 0)
			{
				wBuf[0]= (a2b_UInt8)((A2B_PNP_BLOCK_DEVINFO_ADDR+A2B_PNP_BLOCK_HEADER_SIZE) >> 8u);
				wBuf[1]= (a2b_UInt8)((A2B_PNP_BLOCK_DEVINFO_ADDR+A2B_PNP_BLOCK_HEADER_SIZE) & 0xFFu);
				Result = a2b_periphWriteRead(pApp_Info->ctx, nodeIdx, EepromI2CAddr, 0x02u, &wBuf[0], BlockSize, &Readbuf[A2B_PNP_BLOCK_DEVINFO_ADDR+A2B_PNP_BLOCK_HEADER_SIZE]);

				if((Result != A2B_RESULT_SUCCESS))
				{
					/* Reject the device */
					return 1;
				}
			}
			PnpNWInfo->DevInfo[nodeIdx].nDeviceFunc = (uint16_t)((Readbuf[A2B_PNP_BLOCK_DEVINFO_ADDR+A2B_PNP_BLOCK_HEADER_SIZE] << 8) | Readbuf[A2B_PNP_BLOCK_DEVINFO_ADDR+A2B_PNP_BLOCK_HEADER_SIZE+1u]);

			ReadCounter = A2B_PNP_BLOCK_DEVINFO_ADDR+A2B_PNP_BLOCK_HEADER_SIZE+2u;

			for(FuncCount=0; FuncCount<PnpNWInfo->DevInfo[nodeIdx].nDeviceFunc; FuncCount++)
			{
				//Extract function lengths and addresses and update the function info
				PnpNWInfo->DevInfo[nodeIdx].DevFunc[FuncCount].FunctionLength = (uint16_t)((Readbuf[ReadCounter] << 8) | Readbuf[ReadCounter + 1u]);
				ReadCounter += 2u;

				FunctionAdd[FuncCount] = (uint16_t)((Readbuf[ReadCounter] << 8) | Readbuf[ReadCounter + 1u]);
				ReadCounter += 2u;

				PnpNWInfo->DevInfo[nodeIdx].DevFunc[FuncCount].FunctionType = Readbuf[FunctionAdd[FuncCount]];

				(void)memcpy(PnpNWInfo->DevInfo[nodeIdx].DevFunc[FuncCount].FunctionInfo, &Readbuf[FunctionAdd[FuncCount]], PnpNWInfo->DevInfo[nodeIdx].DevFunc[FuncCount].FunctionLength);
			}

			wBuf[0]= (a2b_UInt8)(A2B_PNP_BLOCK_SUPPLIERINFO_ADDR >> 8);
			wBuf[1]= (a2b_UInt8)(A2B_PNP_BLOCK_SUPPLIERINFO_ADDR & 0xFFu);
			Result = a2b_periphWriteRead(pApp_Info->ctx, nodeIdx, EepromI2CAddr, 0x02u, &wBuf[0], A2B_PNP_BLOCK_HEADER_SIZE, &Readbuf[A2B_PNP_BLOCK_SUPPLIERINFO_ADDR]);

			/* Check the supplier information block */
			if ((Result != A2B_RESULT_SUCCESS) || (Readbuf[A2B_PNP_BLOCK_SUPPLIERINFO_ADDR] >> 4u) != A2B_PNP_BLOCK_SUPPLIERINFO_TYPE)
			{
				/* Reject the device */
				if(pApp_Info->bDebug)
				{
					A2B_APP_DBG_LOG("\n********Incorrect Supplier Info Block *********\n");
				}
				return 1;
			}

			/* Size of Device Info Block: */
			BlockSize = (((a2b_UInt16)Readbuf[A2B_PNP_BLOCK_SUPPLIERINFO_ADDR] & 0x0Fu) << 8u) | Readbuf[A2B_PNP_BLOCK_SUPPLIERINFO_ADDR+1u];

			/* Reading Device Information Block payload */
			if(BlockSize != 0)
			{
				wBuf[0]= (a2b_UInt8)((A2B_PNP_BLOCK_SUPPLIERINFO_ADDR+A2B_PNP_BLOCK_HEADER_SIZE) >> 8u);
				wBuf[1]= (a2b_UInt8)((A2B_PNP_BLOCK_SUPPLIERINFO_ADDR+A2B_PNP_BLOCK_HEADER_SIZE) & 0xFFu);
				Result = a2b_periphWriteRead(pApp_Info->ctx, nodeIdx, EepromI2CAddr, 0x02u, &wBuf[0], BlockSize, &Readbuf[A2B_PNP_BLOCK_SUPPLIERINFO_ADDR+A2B_PNP_BLOCK_HEADER_SIZE]);

				if((Result != A2B_RESULT_SUCCESS))
				{
					/* Reject the device */
					return 1;
				}
			}

			PnpNWInfo->DevInfo[nodeIdx].vendorDataLen = BlockSize - (A2B_PNP_BLOCK_HEADER_SIZE + A2B_PNP_VENDOR_ID_LEN + 32u);
			(void)memcpy(PnpNWInfo->DevInfo[nodeIdx].vendorID, &Readbuf[A2B_PNP_BLOCK_SUPPLIERINFO_ADDR+A2B_PNP_BLOCK_HEADER_SIZE], A2B_PNP_VENDOR_ID_LEN);

			if(PnpNWInfo->DevInfo[nodeIdx].vendorDataLen > 0)
			{
				(void)memcpy(PnpNWInfo->DevInfo[nodeIdx].vendorData, &Readbuf[A2B_PNP_BLOCK_SUPPLIERINFO_ADDR + A2B_PNP_BLOCK_HEADER_SIZE + A2B_PNP_VENDOR_ID_LEN + 32u], PnpNWInfo->DevInfo[nodeIdx].vendorDataLen);
			}

		    if(NumPeriBlocks != 0u)
		    {
		    	pApp_Info->SubNodeTypeInfo[nodeIdx].NodeAddr = nodeIdx;

				/* TODO Read Module Information block need to overwritten here and use less buffer size in Readbuf */

				/* Reading Peripheral Information Block header  */
				wBuf[0]= (a2b_UInt8)(A2B_PNP_BLOCK_PERIINFO_ADDR >> 8u);//(periBlockAddr >> 8);
				wBuf[1]= (a2b_UInt8)(A2B_PNP_BLOCK_PERIINFO_ADDR & 0xFFu);//(periBlockAddr & 0xFFu);
				Result = a2b_periphWriteRead(pApp_Info->ctx, nodeIdx, EepromI2CAddr, 0x02u, &wBuf[0], A2B_PNP_BLOCK_HEADER_SIZE, &Readbuf[A2B_PNP_BLOCK_PERIINFO_ADDR]);

				/* Check the peripheral information block */
				if ((Result != A2B_RESULT_SUCCESS) || (Readbuf[A2B_PNP_BLOCK_PERIINFO_ADDR] >> 4u) != A2B_PNP_BLOCK_PERIINFO_TYPE)
				{
					/* Reject the device */
					if(pApp_Info->bDebug)
					{
						A2B_APP_DBG_LOG("\n********Incorrect Peripheral Info Block *********\n");
					}
//					return 1;
				}

				/* Size of Peripheral Info Block: */
				BlockSize = (((a2b_UInt16)Readbuf[A2B_PNP_BLOCK_PERIINFO_ADDR] & 0x0Fu) << 8u) | Readbuf[A2B_PNP_BLOCK_PERIINFO_ADDR+1u];

				/* Reading Peripheral Information Block payload */
				wBuf[0]= (a2b_UInt8)((A2B_PNP_BLOCK_PERIINFO_ADDR+A2B_PNP_BLOCK_HEADER_SIZE) >> 8u);
				wBuf[1]= (a2b_UInt8)((A2B_PNP_BLOCK_PERIINFO_ADDR+A2B_PNP_BLOCK_HEADER_SIZE) & 0xFFu);
				Result = a2b_periphWriteRead(pApp_Info->ctx, nodeIdx, EepromI2CAddr, 0x02u, &wBuf[0], BlockSize, &Readbuf[A2B_PNP_BLOCK_PERIINFO_ADDR+A2B_PNP_BLOCK_HEADER_SIZE]);

				if((Result != A2B_RESULT_SUCCESS))
				{
					/* Reject the device */
					return 1;
				}
				/* Store the Peripheral Configuration block address */
				for(PeriCount=0; PeriCount<NumPeriBlocks ; PeriCount++)
				{
					PeriConifigBlockAddr[PeriCount] = Readbuf[A2B_PNP_BLOCK_PERIINFO_ADDR+A2B_PNP_BLOCK_HEADER_SIZE+(a2b_UInt32)(PeriCount*4u)] + ((a2b_UInt32)Readbuf[A2B_PNP_BLOCK_PERIINFO_ADDR+A2B_PNP_BLOCK_HEADER_SIZE+(a2b_UInt32)(PeriCount*4u)+1u] << 8u) + ((a2b_UInt32)Readbuf[A2B_PNP_BLOCK_PERIINFO_ADDR+A2B_PNP_BLOCK_HEADER_SIZE+(a2b_UInt32)(PeriCount*4u)+2u]<<16u) + ((a2b_UInt32)Readbuf[A2B_PNP_BLOCK_PERIINFO_ADDR+A2B_PNP_BLOCK_HEADER_SIZE+(a2b_UInt32)(PeriCount*4u)+3u] << 24u);
				}

				for(PeriCount=0; PeriCount<NumPeriBlocks ; PeriCount++)
				{
					wBuf[0]= (a2b_UInt8)(PeriConifigBlockAddr[PeriCount] >> 8u);
					wBuf[1]= (a2b_UInt8)(PeriConifigBlockAddr[PeriCount] & 0xFFu);
					Result = a2b_periphWriteRead(pApp_Info->ctx, nodeIdx, EepromI2CAddr, 0x02u, &wBuf[0], A2B_PNP_BLOCK_HEADER_SIZE, &Readbuf[PeriConifigBlockAddr[PeriCount]]);

					/* Check the peripheral Configuration block */
					if ((Result != A2B_RESULT_SUCCESS) || (Readbuf[PeriConifigBlockAddr[PeriCount]] >> 4u) != A2B_PNP_BLOCK_PERICONFIG_TYPE)
					{
						/* Reject the device */
						if(pApp_Info->bDebug)
						{
							A2B_APP_DBG_LOG("\n********Incorrect Peripheral Config Block *********\n");
						}
						return 1;
					}

					/* Size of Peripheral Configuration Block: */
					BlockSize = (a2b_UInt16)Readbuf[(a2b_UInt16)PeriConifigBlockAddr[PeriCount]] & 0x0Fu;
					BlockSize = (BlockSize << 8u) | Readbuf[(a2b_UInt16)PeriConifigBlockAddr[PeriCount]+1u];

					wBuf[0]= (a2b_UInt8)(PeriConifigBlockAddr[PeriCount] >> 8u);
					wBuf[1]= (a2b_UInt8)((PeriConifigBlockAddr[PeriCount]+A2B_PNP_BLOCK_HEADER_SIZE) & 0xFFu);
					Result = a2b_periphWriteRead(pApp_Info->ctx, nodeIdx, EepromI2CAddr, 0x02, &wBuf[0], BlockSize, &Readbuf[PeriConifigBlockAddr[PeriCount]+A2B_PNP_BLOCK_HEADER_SIZE]);
					if((Result != A2B_RESULT_SUCCESS))
					{
						/* Reject the device */
						return 1;
					}

					ReadCounter = PeriConifigBlockAddr[PeriCount]+A2B_PNP_BLOCK_HEADER_SIZE;
					PnpNWInfo->DevInfo[nodeIdx].PeripInfo[NumPeri].InterfaceType = (ADI_A2B_DEVICE_INTERFACE)Readbuf[ReadCounter];

					/* Read Peripheral name */
					ReadCounter++;
					WriteCounter = 0u;
					while(Readbuf[ReadCounter] != 0u)
					{
						DeviceName[WriteCounter] = Readbuf[ReadCounter];
						ReadCounter++;
						WriteCounter++;
					};
					DeviceName[WriteCounter] = 0u;
					(void)memcpy(PnpNWInfo->DevInfo[nodeIdx].PeripInfo[NumPeri].PeriphStreamName, &DeviceName[0], strlen((const char*)DeviceName));

					/* SPI/I2C Address */
					ReadCounter = (a2b_UInt32)PeriConifigBlockAddr[PeriCount] + A2B_PNP_BLOCK_HEADER_SIZE + 1u + A2B_PNP_DEVICE_NAME_SIZE;
					PnpNWInfo->DevInfo[nodeIdx].PeripInfo[NumPeri].Address = Readbuf[ReadCounter];

					ReadCounter++;
					TxGroupMap = (a2b_UInt16)Readbuf[ReadCounter] | (a2b_UInt16)(Readbuf[ReadCounter+1]<<8u);
					for(a2b_UInt32 i=0; i<16u ;i++)
					{
						if(((1<<i) & TxGroupMap) != 0)
						{
							PnpNWInfo->DevInfo[nodeIdx].PeripInfo[NumPeri].TxGroupID[PnpNWInfo->DevInfo[nodeIdx].PeripInfo[NumPeri].nTxGroups] = i;
							PnpNWInfo->DevInfo[nodeIdx].PeripInfo[NumPeri].nTxGroups++;
						}
					}

					ReadCounter += 2;
					RxGroupMap = (a2b_UInt16)Readbuf[ReadCounter] | (a2b_UInt16)(Readbuf[ReadCounter+1]<<8u);
					for(a2b_UInt32 i=0; i<16u ;i++)
					{
						if(((1<<i) & RxGroupMap) != 0)
						{
							PnpNWInfo->DevInfo[nodeIdx].PeripInfo[NumPeri].RxGroupID[PnpNWInfo->DevInfo[nodeIdx].PeripInfo[NumPeri].nRxGroups] = i;
							PnpNWInfo->DevInfo[nodeIdx].PeripInfo[NumPeri].nRxGroups++;
						}
					}


					/* RX Interface */
					ReadCounter += 2;
					PnpNWInfo->DevInfo[nodeIdx].PeripInfo[NumPeri].nRxPins = (Readbuf[ReadCounter] & 0xE0u) >> 5u;
					PnpNWInfo->DevInfo[nodeIdx].PeripInfo[NumPeri].RxPinMap = (Readbuf[ReadCounter] & 0x1Fu);

					/* TX Interface */
					ReadCounter += 2;
					PnpNWInfo->DevInfo[nodeIdx].PeripInfo[NumPeri].nTxPins = (Readbuf[ReadCounter] & 0xE0u) >> 5u;
					PnpNWInfo->DevInfo[nodeIdx].PeripInfo[NumPeri].TxPinMap = (Readbuf[ReadCounter] & 0x1Fu);


					/* Address Width */
					ReadCounter += 2;
//					PnpNWInfo.DevInfo[nodeIdx].PeripInfo[NumPeri].nAddrWidth = Readbuf[ReadCounter];

					/* Data Width */
					ReadCounter++;
//					PnpNWInfo.DevInfo[nodeIdx].PeripInfo[NumPeri].nDataWidth = Readbuf[ReadCounter];

					/* Command Width */
					ReadCounter++;
//					PnpNWInfo.DevInfo[nodeIdx].PeripInfo[NumPeri].nCommandWidth = Readbuf[ReadCounter];

					/* Number of Peripheral Configuration units */
					ReadCounter++;
					PnpNWInfo->DevInfo[nodeIdx].PeripInfo[NumPeri].nPeriConfigUnits = (((uint16_t)Readbuf[ReadCounter] & 0x0Fu) << 8u) | Readbuf[ReadCounter+1u];

					/* *CRC Address */
					ReadCounter = ReadCounter + 2u;

					/* Skip CRC and go to Peripheral Configuration Units */
					ReadCounter++;

					/* Store the address of Peripheral configuration units */
					PnpNWInfo->DevInfo[nodeIdx].PeripInfo[NumPeri].pConifUnitAddr = ReadCounter;

					/* Update the Network table to include the Peripheral information from EEPROM */
					pApp_Info->aPeriNetworkTable[nodeAddr].aDeviceConfig[NumPeri].nNumPeriConfigUnit = PnpNWInfo->DevInfo[nodeIdx].PeripInfo[NumPeri].nPeriConfigUnits;
					pApp_Info->aPeriNetworkTable[nodeAddr].aDeviceConfig[NumPeri].paPeriConfigUnit = (ADI_A2B_PERI_CONFIG_UNIT*)(a2b_UIntPtr)PnpNWInfo->DevInfo[nodeIdx].PeripInfo[NumPeri].pConifUnitAddr;
					pApp_Info->aPeriNetworkTable[nodeAddr].aDeviceConfig[NumPeri].bActive = 1u;
					pApp_Info->aPeriNetworkTable[nodeAddr].aDeviceConfig[NumPeri].nConnectedNodeID = (a2b_UInt32)nodeIdx; /* Since this is for Sub node configuration, nodeIdx will always be >=0 */
					pApp_Info->aPeriNetworkTable[nodeAddr].aDeviceConfig[NumPeri].ePeriDeviceInterface = PnpNWInfo->DevInfo[nodeIdx].PeripInfo[NumPeri].InterfaceType;
					pApp_Info->aPeriNetworkTable[nodeAddr].aDeviceConfig[NumPeri].nSpiSs = PnpNWInfo->DevInfo[nodeIdx].PeripInfo[NumPeri].Address;
					pApp_Info->aPeriNetworkTable[nodeAddr].aDeviceConfig[NumPeri].nDeviceAddress = EepromI2CAddr; /* I2V device Address is retreived from EEPROM, here address of the EEPROM is provided which has PnP content */
					NumPeri++;
				}

				pApp_Info->aPeriNetworkTable[nodeAddr].nNumConfig = NumPeriBlocks;

				for(NumPeri = NumPeriBlocks; NumPeri < (a2b_UInt8)ADI_A2B_MAX_DEVICES_PER_NODE; NumPeri++)
				{
					pApp_Info->aPeriNetworkTable[nodeAddr].aDeviceConfig[NumPeri].bActive = 0u;
				}
		    }

			PnpNWInfo->nDeviceCount++;
		}
	}


	return 0;
}

//Function to update the DNMask and UPMask by inclusion of DT
static a2b_UInt32 adjust_mask(a2b_UInt32 mask, a2b_UInt8 offset, a2b_UInt8 dtSlots)
{
	a2b_UInt32 x = mask;
	a2b_UInt32 y = mask;

	for(a2b_UInt32 i = offset; i < offset+dtSlots; i++)
	{
		x = x << 1; //left shift once
		x &= ~((a2b_UInt32)1 << i);
		x &= ((~(a2b_UInt32)0) << i);
		y &= ~((~(a2b_UInt32)0) << i);

		x |= y;
	}

	return x;
}


static void swap_stream_ptr(a2bpnp_StreamInfo** px, a2bpnp_StreamInfo** py)
{
	a2bpnp_StreamInfo *temp = *px;
    *px = *py;
    *py = temp;
}
static void swap_stream(a2bpnp_StreamInfo* px, a2bpnp_StreamInfo* py)
{
	a2bpnp_StreamInfo temp = *px;
    *px = *py;
    *py = temp;
}

//selection sort
static void sort_streamForDown_Ptr(a2bpnp_StreamInfo *info[], a2b_UInt32 size)
{
	a2b_UInt32 i, j, min_idx;
	if(size == 0)
	{
		return;
	}
    for(i = 0; i < (size-1); i++)
    {
    	min_idx = i;
    	for(j = i+1; j < size; j++)
    	{
    		if((*info[j]).SourceNodeIdx < (*info[min_idx]).SourceNodeIdx)
    		{
    			min_idx = j;
    		}
    		if((*info[j]).SourceNodeIdx == (*info[min_idx]).SourceNodeIdx)
    		{
    			//Now decide based on src group
    			if((*info[j]).srcRxGroup < (*info[min_idx]).srcRxGroup)
    			{
    				min_idx = j;
    			}
    		}
    		if(min_idx != i)
    		{
    			swap_stream_ptr(&info[i], &info[min_idx]);
    		}
    	}
    }

}
static void sort_streamForUp_Ptr(a2bpnp_StreamInfo *info[], a2b_UInt32 size)
{
	a2b_UInt32 i, j, min_idx;
	if(size == 0)
	{
		return;
	}
    for(i = 0; i < (size-1); i++)
    {
    	min_idx = i;
    	for(j = i+1; j < size; j++)
    	{
    		if((*info[j]).SourceNodeIdx > (*info[min_idx]).SourceNodeIdx)
    		{
    			min_idx = j;
    		}
    		if((*info[j]).SourceNodeIdx == (*info[min_idx]).SourceNodeIdx)
    		{
    			//Now decide based on src group
    			if((*info[j]).srcRxGroup < (*info[min_idx]).srcRxGroup)
    			{
    				min_idx = j;
    			}
    		}
    		if(min_idx != i)
    		{
    			swap_stream_ptr(&info[i], &info[min_idx]);
    		}
    	}
    }

}
static void sort_streamForGroup(a2bpnp_StreamInfo info[], a2b_UInt32 size)
{
	a2b_UInt32 i, j, min_idx;
	if(size == 0)
	{
		return;
	}
    for(i = 0; i < (size-1); i++)
    {
    	min_idx = i;
    	for(j = i+1; j < size; j++)
    	{
			//Now decide based on src group
			if(info[j].srcRxGroup < info[min_idx].srcRxGroup)
			{
				min_idx = j;
			}

    		if(min_idx != i)
    		{
    			swap_stream(&info[i], &info[min_idx]);
    		}
    	}
    }

}


static void sort_streamForGroup_Ptr(a2bpnp_StreamInfo *info[], a2b_UInt32 size)
{
	a2b_UInt32 i, j, min_idx;
	if(size == 0)
	{
		return;
	}
    for(i = 0; i < (size-1); i++)
    {
    	min_idx = i;
    	for(j = i+1; j < size; j++)
    	{
			//Now decide based on src group
			if((*info[j]).srcRxGroup < (*info[min_idx]).srcRxGroup)
			{
				min_idx = j;
			}

    		if(min_idx != i)
    		{
    			swap_stream_ptr(&info[i], &info[min_idx]);
    		}
    	}
    }

}

static void crossbarCalc(a2b_App_t *pApp_Info)
{

	a2b_Int8  nNumSubNodesDscvrd;
	a2b_Int8  DestNodeIdx;
	a2b_Int8  nNode;
	a2b_UInt8 nDestNodeCnt;
	a2b_UInt8 nListCnt;
	a2b_UInt8 nStreamIdx=0u;
	a2b_UInt8 bFlag, i;
	a2b_UInt8 GroupID = 0u;
	a2b_UInt8 GroupChannels = 0u;
	a2b_UInt8 DNFrameOffset = 0u;
	a2b_UInt8 UPFrameOffset = 0u;
	a2b_UInt8 xbarIdx = 0u;
	a2b_UInt8 TxGrpOffset[A2B_PNP_CFG_MAX_NUM_GROUPS] = {0xFF};
	a2bpnp_NetworkInfo *PnpNWInfo = &pApp_Info->PnpNWInfo;
	a2bpnp_StreamInfo* NWStream = &PnpNWInfo->NWStream[0];
	a2b_UInt8 StreamCount = PnpNWInfo->nStreamCount;
	a2b_UInt8 nSubListCnt;


	nNumSubNodesDscvrd =  (a2b_Int8)pApp_Info->bdd.nodes_count - 1u; /* master excluded */

	/* */
	(void)memset(&pApp_Info->MainStreamInfo, 0, sizeof(a2bpnp_MainnodeStreamInfo));

	for( i = 0; i < 32; i++)
	{
		PnpNWInfo->MainNodeInfo.txxbar[i]= i;
	}
	for( i = 0; i < 8; i++)
	{
		PnpNWInfo->MainNodeInfo.rxmask[i]= 0xFF;
	}
	for(nNode = 0; nNode <nNumSubNodesDscvrd; nNode++)
	{
		pApp_Info->NodeStreamInfo[nNode].SourceStreamListCnt=0u;
		pApp_Info->NodeStreamInfo[nNode].SinkStreamListCnt=0u;
		pApp_Info->NodeStreamInfo[nNode].DNSinkStreamListCnt=0u;
		pApp_Info->NodeStreamInfo[nNode].UPSinkStreamListCnt=0u;
		pApp_Info->NodeStreamInfo[nNode].DNSourceStreamListCnt=0u;
		pApp_Info->NodeStreamInfo[nNode].UPSourceStreamListCnt=0u;
		pApp_Info->NodeStreamInfo[nNode].PassDNStreamListCnt=0u;
		pApp_Info->NodeStreamInfo[nNode].PassUPStreamListCnt=0u;
		(void)memset(&pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo, 0, sizeof(a2bpnp_SubnodeSlotInfo));
		(void)memset(PnpNWInfo->DevInfo[nNode].txFrameUP, 0, sizeof(PnpNWInfo->DevInfo[nNode].txFrameUP));
		(void)memset(PnpNWInfo->DevInfo[nNode].txFrameDN, 0, sizeof(PnpNWInfo->DevInfo[nNode].txFrameDN));
		for( i = 0; i < 32; i++)
		{
			PnpNWInfo->DevInfo[nNode].txxbar[i]= i;
		}
		for( i = 0; i < 8; i++)
		{
			PnpNWInfo->DevInfo[nNode].rxmask[i]= 0xFF;
		}
	}
	for(nStreamIdx=0; nStreamIdx<StreamCount; nStreamIdx++)
	{
		/* Source Stream list */
		nNode = NWStream[nStreamIdx].SourceNodeIdx;
		if(nNode >= 0)
		{
			nListCnt = pApp_Info->NodeStreamInfo[nNode].SourceStreamListCnt;
			pApp_Info->NodeStreamInfo[nNode].SourceStreamList[nListCnt] = &NWStream[nStreamIdx];
			//(void)memcpy(&pApp_Info->NodeStreamInfo[nNode].SourceStreamList[nListCnt], &NWStream[nStreamIdx], sizeof(a2bpnp_StreamInfo));
			pApp_Info->NodeStreamInfo[nNode].SourceStreamListCnt++;
		}
		else
		{
			nListCnt = pApp_Info->MainStreamInfo.DNSourceStreamListCnt;
			pApp_Info->MainStreamInfo.DNSourceStreamList[nListCnt] = &NWStream[nStreamIdx];
			// (void)memcpy(&pApp_Info->MainStreamInfo.DNSourceStreamList[nListCnt], &NWStream[nStreamIdx], sizeof(a2bpnp_StreamInfo));
			pApp_Info->MainStreamInfo.DNSourceStreamListCnt++;
		}
	}
	//SortSourceStreamList
	sort_streamForGroup_Ptr(pApp_Info->MainStreamInfo.DNSourceStreamList, pApp_Info->MainStreamInfo.DNSourceStreamListCnt);


	for(nStreamIdx=0u; nStreamIdx<StreamCount; nStreamIdx++)
	{
		/* Sink Stream list */
		for(nDestNodeCnt = 0u; nDestNodeCnt < NWStream[nStreamIdx].nDestNodes; nDestNodeCnt++)
		{
			nNode = NWStream[nStreamIdx].DestNodeIdx[nDestNodeCnt];
			if(nNode >= 0)
			{
				nListCnt = pApp_Info->NodeStreamInfo[nNode].SinkStreamListCnt;
				pApp_Info->NodeStreamInfo[nNode].SinkStreamList[nListCnt] = &NWStream[nStreamIdx];
				// (void)memcpy(&pApp_Info->NodeStreamInfo[nNode].SinkStreamList[nListCnt], &NWStream[nStreamIdx], sizeof(a2bpnp_StreamInfo));
				pApp_Info->NodeStreamInfo[nNode].SinkStreamListCnt++;
			}
			else
			{
				nListCnt = pApp_Info->MainStreamInfo.UPSinkStreamListCnt;
				pApp_Info->MainStreamInfo.UPSinkStreamList[nListCnt] = &NWStream[nStreamIdx];
				// (void)memcpy(&pApp_Info->MainStreamInfo.UPSinkStreamList[nListCnt], &NWStream[nStreamIdx], sizeof(a2bpnp_StreamInfo));
				pApp_Info->MainStreamInfo.UPSinkStreamListCnt++;
			}
		}
	}




	/*Update Main node slot info and XBAR regs*/
	sort_streamForUp_Ptr(pApp_Info->MainStreamInfo.UPSinkStreamList, pApp_Info->MainStreamInfo.UPSinkStreamListCnt);
	nListCnt = pApp_Info->MainStreamInfo.UPSinkStreamListCnt;

	for(nStreamIdx=0u; nStreamIdx<nListCnt; nStreamIdx++)
	{
		for(nDestNodeCnt = 0u; nDestNodeCnt < pApp_Info->MainStreamInfo.UPSinkStreamList[nStreamIdx]->nDestNodes; nDestNodeCnt++)
		{
			if(pApp_Info->MainStreamInfo.UPSinkStreamList[nStreamIdx]->DestNodeIdx[nDestNodeCnt] == A2B_NODEADDR_MASTER)
			{
				GroupID = pApp_Info->MainStreamInfo.UPSinkStreamList[nStreamIdx]->destTxGroup[nDestNodeCnt];
				break;
			}
		}
		GroupChannels = PnpNWInfo->MainNodeInfo.TxGroups[GroupID].nGroupChannels;

		pApp_Info->MainStreamInfo.nConsumeUPSlots += GroupChannels;
		TxGrpOffset[GroupID] = UPFrameOffset;
		for(a2b_UInt32 i = 0; i<GroupChannels; i++)
		{
			PnpNWInfo->MainNodeInfo.txFrameUP[UPFrameOffset++] = GroupID;
		}
	}

	nListCnt = pApp_Info->MainStreamInfo.DNSourceStreamListCnt;
	for(nStreamIdx=0u; nStreamIdx<nListCnt; nStreamIdx++)
	{
		GroupID = pApp_Info->MainStreamInfo.DNSourceStreamList[nStreamIdx]->srcRxGroup;
		GroupChannels = PnpNWInfo->MainNodeInfo.RxGroups[GroupID].nGroupChannels;

		pApp_Info->MainStreamInfo.nContributeDNSlots += GroupChannels;

		//Update Rx (Mask) Crossbar field
		for(a2b_UInt32 nCh=0; nCh<GroupChannels; nCh++)
		{
			PnpNWInfo->MainNodeInfo.rxmask[PnpNWInfo->MainNodeInfo.RxGroups[GroupID].GroupChannels[nCh]/A2B_PNP_RXMASKn_SIZE] |= (a2b_UInt8)(1<<(PnpNWInfo->MainNodeInfo.RxGroups[GroupID].GroupChannels[nCh] % A2B_PNP_RXMASKn_SIZE));
		}
	}

	nListCnt = pApp_Info->MainStreamInfo.UPSinkStreamListCnt;
	for(a2b_UInt8 TxGrp = 0; TxGrp < nListCnt; TxGrp++)
	{
		GroupChannels = PnpNWInfo->MainNodeInfo.TxGroups[TxGrp].nGroupChannels;
		for(a2b_UInt8 i = 0; i < GroupChannels; i++)
		{
			PnpNWInfo->MainNodeInfo.txxbar[xbarIdx++] = TxGrpOffset[TxGrp] + i;
		}
	}

//	(void *)memset(TxGrpOffset, '\0', sizeof(TxGrpOffset));
	UPFrameOffset = 0u;
	xbarIdx = 0u;

	/*Update Sub node slots info and XBAR regs*/
	for(nNode = 0; nNode <nNumSubNodesDscvrd; nNode++)
	{
		nListCnt = pApp_Info->NodeStreamInfo[nNode].SinkStreamListCnt;
		for(nStreamIdx=0u; nStreamIdx<nListCnt; nStreamIdx++)
		{
			/* Sink Down Stream list */
			if(pApp_Info->NodeStreamInfo[nNode].SinkStreamList[nStreamIdx]->SourceNodeIdx < nNode)
			{
				nSubListCnt = pApp_Info->NodeStreamInfo[nNode].DNSinkStreamListCnt;
				pApp_Info->NodeStreamInfo[nNode].DNSinkStreamList[nSubListCnt] = pApp_Info->NodeStreamInfo[nNode].SinkStreamList[nStreamIdx];
				// (void)memcpy(&pApp_Info->NodeStreamInfo[nNode].DNSinkStreamList[nSubListCnt], &pApp_Info->NodeStreamInfo[nNode].SinkStreamList[nStreamIdx], sizeof(a2bpnp_StreamInfo));
				nSubListCnt = pApp_Info->NodeStreamInfo[nNode].DNSinkStreamListCnt++;
			}
			else
			{
				/* Sink Up Stream list */
				nSubListCnt = pApp_Info->NodeStreamInfo[nNode].UPSinkStreamListCnt;
				pApp_Info->NodeStreamInfo[nNode].UPSinkStreamList[nSubListCnt] = pApp_Info->NodeStreamInfo[nNode].SinkStreamList[nStreamIdx];
				// (void)memcpy(&pApp_Info->NodeStreamInfo[nNode].UPSinkStreamList[nSubListCnt], &pApp_Info->NodeStreamInfo[nNode].SinkStreamList[nStreamIdx], sizeof(a2bpnp_StreamInfo));
				nSubListCnt = pApp_Info->NodeStreamInfo[nNode].UPSinkStreamListCnt++;
			}
		}
		sort_streamForDown_Ptr(pApp_Info->NodeStreamInfo[nNode].DNSinkStreamList, pApp_Info->NodeStreamInfo[nNode].DNSinkStreamListCnt);
		sort_streamForUp_Ptr(pApp_Info->NodeStreamInfo[nNode].UPSinkStreamList, pApp_Info->NodeStreamInfo[nNode].UPSinkStreamListCnt);
	}

	for(nNode = 0; nNode <nNumSubNodesDscvrd; nNode++)
	{
		nListCnt = pApp_Info->NodeStreamInfo[nNode].DNSinkStreamListCnt;
		(void *)memset(TxGrpOffset, 0xFF, sizeof(TxGrpOffset));
		for(nStreamIdx=0u; nStreamIdx<nListCnt; nStreamIdx++)
		{
			for(nDestNodeCnt = 0u; nDestNodeCnt < pApp_Info->NodeStreamInfo[nNode].DNSinkStreamList[nStreamIdx]->nDestNodes; nDestNodeCnt++)
			{
				if(pApp_Info->NodeStreamInfo[nNode].DNSinkStreamList[nStreamIdx]->DestNodeIdx[nDestNodeCnt] == nNode)
				{
					GroupID = pApp_Info->NodeStreamInfo[nNode].DNSinkStreamList[nStreamIdx]->destTxGroup[nDestNodeCnt];
					break;
				}
			}
			GroupChannels = PnpNWInfo->DevInfo[nNode].TxGroups[GroupID].nGroupChannels;
			/* Sink Down Stream list */
			pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.nConsumeDNSlots += GroupChannels;
			TxGrpOffset[GroupID] = DNFrameOffset;
			for(a2b_UInt32 i = 0; i<GroupChannels; i++)
			{
				PnpNWInfo->DevInfo[nNode].txFrameDN[DNFrameOffset++] = GroupID;
			}


		}

		nListCnt = pApp_Info->NodeStreamInfo[nNode].UPSinkStreamListCnt;

		for(nStreamIdx=0u; nStreamIdx<nListCnt; nStreamIdx++)
		{
			for(nDestNodeCnt = 0u; nDestNodeCnt < pApp_Info->NodeStreamInfo[nNode].UPSinkStreamList[nStreamIdx]->nDestNodes; nDestNodeCnt++)
			{
				if(pApp_Info->NodeStreamInfo[nNode].UPSinkStreamList[nStreamIdx]->DestNodeIdx[nDestNodeCnt] == nNode)
				{
					GroupID = pApp_Info->NodeStreamInfo[nNode].UPSinkStreamList[nStreamIdx]->destTxGroup[nDestNodeCnt];
					break;
				}
			}
			GroupChannels = PnpNWInfo->DevInfo[nNode].TxGroups[GroupID].nGroupChannels;
			/* Sink Down Stream list */
			pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.nConsumeUPSlots += GroupChannels;
			TxGrpOffset[GroupID] = DNFrameOffset + UPFrameOffset;
			for(a2b_UInt32 i = 0; i<GroupChannels; i++)
			{
				PnpNWInfo->DevInfo[nNode].txFrameUP[UPFrameOffset++] = GroupID;
			}

		}

		if(pApp_Info->NodeStreamInfo[nNode].SinkStreamListCnt)
		{
			for(a2b_UInt8 TxGrp = 0; TxGrp < PnpNWInfo->DevInfo[nNode].nTxGroups; TxGrp++)
			{
				if(TxGrpOffset[TxGrp] != 0xFF)
				{
					GroupChannels = PnpNWInfo->DevInfo[nNode].TxGroups[TxGrp].nGroupChannels;
					for(a2b_UInt8 i = 0; i < GroupChannels; i++)
					{
						PnpNWInfo->DevInfo[nNode].txxbar[xbarIdx++] = TxGrpOffset[TxGrp] + i;
					}
				}
			}
		}
	}
	
	for(nNode = 0; nNode <nNumSubNodesDscvrd; nNode++)
	{
		nListCnt = pApp_Info->NodeStreamInfo[nNode].SourceStreamListCnt;
		sort_streamForGroup_Ptr(pApp_Info->NodeStreamInfo[nNode].SourceStreamList, nListCnt);
		for(nStreamIdx=0u; nStreamIdx<nListCnt; nStreamIdx++)
		{
			GroupID = pApp_Info->NodeStreamInfo[nNode].SourceStreamList[nStreamIdx]->srcRxGroup;
			GroupChannels = PnpNWInfo->DevInfo[nNode].RxGroups[GroupID].nGroupChannels;
			/* DNSourceStreamList */
			bFlag = A2B_FALSE;
			for(nDestNodeCnt = 0u; nDestNodeCnt < pApp_Info->NodeStreamInfo[nNode].SourceStreamList[nStreamIdx]->nDestNodes; nDestNodeCnt++)
			{
				DestNodeIdx = pApp_Info->NodeStreamInfo[nNode].SourceStreamList[nStreamIdx]->DestNodeIdx[nDestNodeCnt];
				if(DestNodeIdx > nNode)
				{
					pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.nContributeDNSlots += GroupChannels;
					bFlag = A2B_TRUE;
					break;
				}
			}

			if(bFlag == A2B_TRUE)
			{
				nSubListCnt = pApp_Info->NodeStreamInfo[nNode].DNSourceStreamListCnt;
				pApp_Info->NodeStreamInfo[nNode].DNSourceStreamList[nSubListCnt] = pApp_Info->NodeStreamInfo[nNode].SourceStreamList[nStreamIdx];
				// (void)memcpy(&pApp_Info->NodeStreamInfo[nNode].DNSourceStreamList[nSubListCnt], &pApp_Info->NodeStreamInfo[nNode].SourceStreamList[nStreamIdx], sizeof(a2bpnp_StreamInfo));
				nSubListCnt = pApp_Info->NodeStreamInfo[nNode].DNSourceStreamListCnt++;
			}

			/* UPSourceStreamList */
			bFlag = A2B_FALSE;
			for(nDestNodeCnt = 0u; nDestNodeCnt < pApp_Info->NodeStreamInfo[nNode].SourceStreamList[nStreamIdx]->nDestNodes; nDestNodeCnt++)
			{
				DestNodeIdx = pApp_Info->NodeStreamInfo[nNode].SourceStreamList[nStreamIdx]->DestNodeIdx[nDestNodeCnt];
				if(DestNodeIdx < nNode)
				{
					pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.nContributeUPSlots += GroupChannels;
					bFlag = A2B_TRUE;
					break;
				}
			}

			if(bFlag == A2B_TRUE)
			{
				nSubListCnt = pApp_Info->NodeStreamInfo[nNode].UPSourceStreamListCnt;
				pApp_Info->NodeStreamInfo[nNode].UPSourceStreamList[nSubListCnt] = pApp_Info->NodeStreamInfo[nNode].SourceStreamList[nStreamIdx];
				// (void)memcpy(&pApp_Info->NodeStreamInfo[nNode].UPSourceStreamList[nSubListCnt], &pApp_Info->NodeStreamInfo[nNode].SourceStreamList[nStreamIdx], sizeof(a2bpnp_StreamInfo));
				nSubListCnt = pApp_Info->NodeStreamInfo[nNode].UPSourceStreamListCnt++;
			}

			//Update Rx (Mask) Crossbar field
			for(a2b_UInt32 nCh=0; nCh<GroupChannels; nCh++)
			{
				PnpNWInfo->DevInfo[nNode].rxmask[PnpNWInfo->DevInfo[nNode].RxGroups[GroupID].GroupChannels[nCh]/A2B_PNP_RXMASKn_SIZE] |= (a2b_UInt8)(1<<(PnpNWInfo->DevInfo[nNode].RxGroups[GroupID].GroupChannels[nCh] % A2B_PNP_RXMASKn_SIZE));
			}
		}

	}

	DNFrameOffset = 0u;
	UPFrameOffset = 0u;
	xbarIdx = 0u;

}

void AutoCalcSlot(a2b_App_t *pApp_Info)
{

	a2b_Int8  nNumSubNodesDscvrd;
	a2b_Int8  nNode, NodeIdx;
	a2b_UInt8 nDestNodeCnt;
	a2b_UInt8 nListCnt;
	a2b_UInt8 nSubListCnt;
	a2b_UInt8 nStreamIdx=0u;
	a2b_UInt8 nSubStreamIdx=0u;
	a2b_UInt8 nOffsetUP=0u;
	a2b_UInt8 nOffsetDN=0u;
	a2bpnp_NetworkInfo *PnpNWInfo = &pApp_Info->PnpNWInfo;

	nNumSubNodesDscvrd =  (a2b_Int8)pApp_Info->bdd.nodes_count - 1u; /* master excluded */

	crossbarCalc(pApp_Info);

	pApp_Info->NodeStreamInfo[0].PassDNStreamListCnt = 0u;
	/*Update S0 Pass DN list separately*/
	/*Copy all source DNsource streams from Main to passdown of S0*/
	for(nStreamIdx = 0u; nStreamIdx < pApp_Info->MainStreamInfo.DNSourceStreamListCnt; nStreamIdx++)
	{
		nSubListCnt = pApp_Info->NodeStreamInfo[0].PassDNStreamListCnt;
		pApp_Info->NodeStreamInfo[0].PassDNStreamList[nSubListCnt] = pApp_Info->MainStreamInfo.DNSourceStreamList[nStreamIdx];
		//(void)memcpy(&pApp_Info->NodeStreamInfo[0].PassDNStreamList[nSubListCnt], &pApp_Info->MainStreamInfo.DNSourceStreamList[nStreamIdx], sizeof(a2bpnp_StreamInfo));
		nSubListCnt = pApp_Info->NodeStreamInfo[0].PassDNStreamListCnt++;
	}
	/*Prune PassDN list of S0*/
	a2b_PrunePassDNList(pApp_Info, 0);

	/*Update rest sub node Pass DN list*/
	for(nNode = 1; nNode <nNumSubNodesDscvrd; nNode++)
	{
		/* Copy the Pass DN stream list of previous node to current node */
		nListCnt = pApp_Info->NodeStreamInfo[nNode-1].PassDNStreamListCnt;
		pApp_Info->NodeStreamInfo[nNode].PassDNStreamListCnt = 0u;

		for(nStreamIdx = 0u; nStreamIdx < nListCnt; nStreamIdx++)
		{
			nSubListCnt = pApp_Info->NodeStreamInfo[nNode].PassDNStreamListCnt;
			pApp_Info->NodeStreamInfo[nNode].PassDNStreamList[nSubListCnt] = pApp_Info->NodeStreamInfo[nNode-1].PassDNStreamList[nStreamIdx];
			//(void)memcpy(&pApp_Info->NodeStreamInfo[nNode].PassDNStreamList[nSubListCnt], &pApp_Info->NodeStreamInfo[nNode-1].PassDNStreamList[nStreamIdx], sizeof(a2bpnp_StreamInfo));
			nSubListCnt = pApp_Info->NodeStreamInfo[nNode].PassDNStreamListCnt++;
		}

		/* Copy the Source streams of previous node to current node if current node ID < dest node */
		nListCnt = pApp_Info->NodeStreamInfo[nNode-1].SourceStreamListCnt;

		for(nStreamIdx = 0u; nStreamIdx < nListCnt; nStreamIdx++)
		{
			nSubListCnt = pApp_Info->NodeStreamInfo[nNode].PassDNStreamListCnt;
			for(nDestNodeCnt = 0u; nDestNodeCnt < pApp_Info->NodeStreamInfo[nNode-1].SourceStreamList[nStreamIdx]->nDestNodes; nDestNodeCnt++)
			{
				if(pApp_Info->NodeStreamInfo[nNode-1].SourceStreamList[nStreamIdx]->DestNodeIdx[nDestNodeCnt] > nNode-1)
				{
					pApp_Info->NodeStreamInfo[nNode].PassDNStreamList[nSubListCnt] = pApp_Info->NodeStreamInfo[nNode-1].SourceStreamList[nStreamIdx];
					// (void)memcpy(&pApp_Info->NodeStreamInfo[nNode].PassDNStreamList[nSubListCnt], &pApp_Info->NodeStreamInfo[nNode-1].SourceStreamList[nStreamIdx], sizeof(a2bpnp_StreamInfo));
					nSubListCnt = pApp_Info->NodeStreamInfo[nNode].PassDNStreamListCnt++;
					break;
				}
			}
		}

		a2b_PrunePassDNList(pApp_Info, nNode);
	}

	/* TODO There are no streams passed from last node */
	pApp_Info->NodeStreamInfo[nNumSubNodesDscvrd-1].PassUPStreamListCnt = 0u;

	/* Upstream */
	for(nNode = nNumSubNodesDscvrd-2; nNode >= 0; nNode--)
	{
		/* Copy the Pass UP stream list of next downstream node to current node */
		nListCnt = pApp_Info->NodeStreamInfo[nNode+1].PassUPStreamListCnt;
		pApp_Info->NodeStreamInfo[nNode].PassUPStreamListCnt  =0u;

		for(nStreamIdx = 0u; nStreamIdx < nListCnt; nStreamIdx++)
		{
			nSubListCnt = pApp_Info->NodeStreamInfo[nNode].PassUPStreamListCnt;
			pApp_Info->NodeStreamInfo[nNode].PassUPStreamList[nSubListCnt] = pApp_Info->NodeStreamInfo[nNode+1].PassUPStreamList[nStreamIdx];
			nSubListCnt = pApp_Info->NodeStreamInfo[nNode].PassUPStreamListCnt++;
		}

		/* Copy the Source streams of previous node to current node if current node ID > dest node */
		nListCnt = pApp_Info->NodeStreamInfo[nNode+1].SourceStreamListCnt;

		for(nStreamIdx = 0u; nStreamIdx < nListCnt; nStreamIdx++)
		{
			nSubListCnt = pApp_Info->NodeStreamInfo[nNode].PassUPStreamListCnt;
			for(nDestNodeCnt = 0u; nDestNodeCnt < pApp_Info->NodeStreamInfo[nNode+1].SourceStreamList[nStreamIdx]->nDestNodes; nDestNodeCnt++)
			{
				if(pApp_Info->NodeStreamInfo[nNode+1].SourceStreamList[nStreamIdx]->DestNodeIdx[nDestNodeCnt] < nNode+1)
				{
					pApp_Info->NodeStreamInfo[nNode].PassUPStreamList[nSubListCnt] = pApp_Info->NodeStreamInfo[nNode+1].SourceStreamList[nStreamIdx];
					nSubListCnt = pApp_Info->NodeStreamInfo[nNode].PassUPStreamListCnt++;
					break;
				}
			}
		}

		a2b_PrunePassUPList(pApp_Info, nNode);
	}

	/* Calculate PassDNSlots and PassUPSlots */
	for(nNode = 0; nNode <nNumSubNodesDscvrd; nNode++)
	{
		pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.nPassDNSlots = 0u;
		nListCnt = pApp_Info->NodeStreamInfo[nNode].PassDNStreamListCnt;
		for(nStreamIdx = 0u; nStreamIdx < nListCnt; nStreamIdx++)
		{
			pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.nPassDNSlots = pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.nPassDNSlots + pApp_Info->NodeStreamInfo[nNode].PassDNStreamList[nStreamIdx]->nNumChannels;
		}

		pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.nPassUPSlots = 0u;
		nListCnt = pApp_Info->NodeStreamInfo[nNode].PassUPStreamListCnt;
		for(nStreamIdx = 0u; nStreamIdx < nListCnt; nStreamIdx++)
		{
			pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.nPassUPSlots = pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.nPassUPSlots + pApp_Info->NodeStreamInfo[nNode].PassUPStreamList[nStreamIdx]->nNumChannels;
		}
	}

	/*Update Main Node REGS*/
	pApp_Info->bdd.nodes[0].ctrlRegs.dnslots = pApp_Info->MainStreamInfo.nContributeDNSlots;
	if(pApp_Info->bdd.nodes[0].ctrlRegs.dnslots != 0u)
	{
		pApp_Info->bdd.nodes[0].ctrlRegs.has_dnslots = A2B_TRUE;
	}
	pApp_Info->bdd.nodes[0].ctrlRegs.upslots = pApp_Info->MainStreamInfo.nConsumeUPSlots;
	if(pApp_Info->bdd.nodes[0].ctrlRegs.upslots != 0u)
	{
		pApp_Info->bdd.nodes[0].ctrlRegs.has_upslots = A2B_TRUE;
	}

	if(pApp_Info->MainStreamInfo.DNSourceStreamListCnt)
	{
		for(a2b_UInt32 i=0; i<A2B_PNP_RXMASKn_SIZE; i++)
		{
			pApp_Info->bdd.nodes[0].i2sCrossbarRegs.rxmask[i] = PnpNWInfo->MainNodeInfo.rxmask[i];
		}
		pApp_Info->bdd.nodes[0].i2sCrossbarRegs.has_rxmask = A2B_TRUE;
	}

	if(pApp_Info->MainStreamInfo.UPSinkStreamListCnt)
	{
		for(a2b_UInt32 i=0; i<32; i++)
		{
			pApp_Info->bdd.nodes[0].i2sCrossbarRegs.txcrossbar[i] = PnpNWInfo->MainNodeInfo.txxbar[i];
		}
		pApp_Info->bdd.nodes[0].i2sCrossbarRegs.has_txcrossbar = A2B_TRUE;

		/*See if txxbar has changed*/
		if(memcmp(pApp_Info->txxbarPrevMain, pApp_Info->bdd.nodes[0].i2sCrossbarRegs.txcrossbar, sizeof(a2b_UInt32)*32))
		{
			PnpNWInfo->MainNodeInfo.bLocalMute = A2B_TRUE;
		}
		else
		{
			PnpNWInfo->MainNodeInfo.bLocalMute = A2B_FALSE;
		}
		memcpy(pApp_Info->txxbarPrevMain, pApp_Info->bdd.nodes[0].i2sCrossbarRegs.txcrossbar, sizeof(a2b_UInt32)*32);
	}

	/* Calculate UPOffset and DNOffset */
	for(nNode = 0; nNode <nNumSubNodesDscvrd; nNode++)
	{
		nListCnt = pApp_Info->NodeStreamInfo[nNode].UPSourceStreamListCnt;
		if(nListCnt != 0u)
		{
			nSubListCnt = pApp_Info->NodeStreamInfo[nNode].SourceStreamListCnt;
			for(nStreamIdx = 0u; nStreamIdx < nSubListCnt; nStreamIdx++)
			{
				if(pApp_Info->NodeStreamInfo[nNode].SourceStreamList[nStreamIdx]->streamIdx == pApp_Info->NodeStreamInfo[nNode].UPSourceStreamList[nStreamIdx]->streamIdx)
				{
					for(nSubStreamIdx=0u; nSubStreamIdx<nStreamIdx; nSubStreamIdx++)
					{
						/*TODO*/
						nOffsetUP += pApp_Info->NodeStreamInfo[nNode].SourceStreamList[nStreamIdx]->nNumChannels;
					}
				}
			}

		}

		pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.UPOffset = nOffsetUP;

		nListCnt = pApp_Info->NodeStreamInfo[nNode].DNSourceStreamListCnt;
		if(nListCnt != 0u )
		{
			nSubListCnt = pApp_Info->NodeStreamInfo[nNode].SourceStreamListCnt;
			for(nStreamIdx = 0u; nStreamIdx < nSubListCnt; nStreamIdx++)
			{
				if(pApp_Info->NodeStreamInfo[nNode].SourceStreamList[nStreamIdx]->streamIdx == pApp_Info->NodeStreamInfo[nNode].DNSourceStreamList[nStreamIdx]->streamIdx)
				{
					for(nSubStreamIdx=0u; nSubStreamIdx<nStreamIdx; nSubStreamIdx++)
					{
						/*TODO*/
						nOffsetDN += pApp_Info->NodeStreamInfo[nNode].SourceStreamList[nStreamIdx]->nNumChannels;
					}
				}
			}

		}

		pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.DNOffset = nOffsetDN;

		NodeIdx = nNode + 1;

		pApp_Info->bdd.nodes[NodeIdx].ctrlRegs.ldnslots = (a2b_UInt32)0x80u | pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.nContributeDNSlots;
		if(pApp_Info->bdd.nodes[NodeIdx].ctrlRegs.ldnslots != 0u)
		{
			pApp_Info->bdd.nodes[NodeIdx].ctrlRegs.has_ldnslots = A2B_TRUE;
		}

		pApp_Info->bdd.nodes[NodeIdx].ctrlRegs.lupslots = pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.nContributeUPSlots;
		if(pApp_Info->bdd.nodes[NodeIdx].ctrlRegs.lupslots != 0u)
		{
			pApp_Info->bdd.nodes[NodeIdx].ctrlRegs.has_lupslots = A2B_TRUE;
		}

		pApp_Info->bdd.nodes[NodeIdx].ctrlRegs.dnslots = pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.nPassDNSlots;
		if(pApp_Info->bdd.nodes[NodeIdx].ctrlRegs.dnslots != 0u)
		{
			pApp_Info->bdd.nodes[NodeIdx].ctrlRegs.has_dnslots = A2B_TRUE;
		}

		pApp_Info->bdd.nodes[NodeIdx].ctrlRegs.upslots = pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.nPassUPSlots;
		if(pApp_Info->bdd.nodes[NodeIdx].ctrlRegs.upslots != 0u)
		{
			pApp_Info->bdd.nodes[NodeIdx].ctrlRegs.has_upslots = A2B_TRUE;
		}

		pApp_Info->bdd.nodes[NodeIdx].slotEnh.upmask0 = (pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.UPMask & 0x000000FFu);
		if(pApp_Info->bdd.nodes[NodeIdx].slotEnh.upmask0 != 0u)
		{
			pApp_Info->bdd.nodes[NodeIdx].slotEnh.has_upmask0 = A2B_TRUE;
		}

		pApp_Info->bdd.nodes[NodeIdx].slotEnh.upmask1 = (pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.UPMask & 0x0000FF00u) >> 8;
		if(pApp_Info->bdd.nodes[NodeIdx].slotEnh.upmask1 != 0u)
		{
			pApp_Info->bdd.nodes[NodeIdx].slotEnh.has_upmask1 = A2B_TRUE;
		}

		pApp_Info->bdd.nodes[NodeIdx].slotEnh.upmask2 = (pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.UPMask & 0x00FF0000u) >> 16;
		if(pApp_Info->bdd.nodes[NodeIdx].slotEnh.upmask2 != 0u)
		{
			pApp_Info->bdd.nodes[NodeIdx].slotEnh.has_upmask2 = A2B_TRUE;
		}

		pApp_Info->bdd.nodes[NodeIdx].slotEnh.upmask3 = (pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.UPMask & 0xFF000000u) >> 24;
		if(pApp_Info->bdd.nodes[NodeIdx].slotEnh.upmask3 != 0u)
		{
			pApp_Info->bdd.nodes[NodeIdx].slotEnh.has_upmask3 = A2B_TRUE;
		}

		pApp_Info->bdd.nodes[NodeIdx].slotEnh.dnmask0 = (pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.DNMask & 0x000000FFu);
		if(pApp_Info->bdd.nodes[NodeIdx].slotEnh.dnmask0 != 0u)
		{
			pApp_Info->bdd.nodes[NodeIdx].slotEnh.has_dnmask0 = A2B_TRUE;
		}

		pApp_Info->bdd.nodes[NodeIdx].slotEnh.dnmask1 = (pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.DNMask & 0x0000FF00u) >> 8;
		if(pApp_Info->bdd.nodes[NodeIdx].slotEnh.dnmask1 != 0u)
		{
			pApp_Info->bdd.nodes[NodeIdx].slotEnh.has_dnmask1 = A2B_TRUE;
		}

		pApp_Info->bdd.nodes[NodeIdx].slotEnh.dnmask2 = (pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.DNMask & 0x00FF0000u) >> 16;
		if(pApp_Info->bdd.nodes[NodeIdx].slotEnh.dnmask2 != 0u)
		{
			pApp_Info->bdd.nodes[NodeIdx].slotEnh.has_dnmask2 = A2B_TRUE;
		}

		pApp_Info->bdd.nodes[NodeIdx].slotEnh.dnmask3 = (pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.DNMask & 0xFF000000u) >> 24;
		if(pApp_Info->bdd.nodes[NodeIdx].slotEnh.dnmask3 != 0u)
		{
			pApp_Info->bdd.nodes[NodeIdx].slotEnh.has_dnmask3 = A2B_TRUE;
		}

		pApp_Info->bdd.nodes[NodeIdx].slotEnh.upoffset = pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.DNOffset;
		if(pApp_Info->bdd.nodes[NodeIdx].slotEnh.upoffset != 0u)
		{
			pApp_Info->bdd.nodes[NodeIdx].slotEnh.has_upoffset = A2B_TRUE;
		}

		pApp_Info->bdd.nodes[NodeIdx].slotEnh.dnoffset = pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.UPOffset;
		if(pApp_Info->bdd.nodes[NodeIdx].slotEnh.dnoffset != 0u)
		{
			pApp_Info->bdd.nodes[NodeIdx].slotEnh.has_dnoffset = A2B_TRUE;
		}

		for(a2b_UInt32 i=0; i<A2B_PNP_RXMASKn_SIZE; i++)
		{
			pApp_Info->bdd.nodes[NodeIdx].i2sCrossbarRegs.rxmask[i] = (a2b_UInt32)PnpNWInfo->DevInfo[nNode].rxmask[i];
		}
		pApp_Info->bdd.nodes[NodeIdx].i2sCrossbarRegs.has_rxmask = A2B_TRUE;

		memcpy(pApp_Info->bdd.nodes[NodeIdx].i2sCrossbarRegs.txcrossbar, PnpNWInfo->DevInfo[nNode].txxbar, sizeof(a2b_UInt32)*32);
		pApp_Info->bdd.nodes[NodeIdx].i2sCrossbarRegs.has_txcrossbar = A2B_TRUE;

		pApp_Info->bdd.nodes[NodeIdx].has_slotEnh = A2B_TRUE;

		/*See if txxbar has changed*/
		if(memcmp(pApp_Info->txxbarPrev[NodeIdx], PnpNWInfo->DevInfo[nNode].txxbar, sizeof(a2b_UInt32)*32))
		{
			PnpNWInfo->DevInfo[nNode].bLocalMute = A2B_TRUE;
		}
		else
		{
			PnpNWInfo->DevInfo[nNode].bLocalMute = A2B_FALSE;
		}
		memcpy(pApp_Info->txxbarPrev[NodeIdx], PnpNWInfo->DevInfo[nNode].txxbar, sizeof(a2b_UInt32)*32);

	}

#if 0
	/* Register update */
	for(nNode = 0; nNode <nNumSubNodesDscvrd; nNode++)
	{
		if(pApp_Info->bDebug)
		{
			A2B_APP_DBG_LOG("\nNode: %d\n", nNode);
			A2B_APP_DBG_LOG("nPassDNSlots: %d ", NodeStreamInfo[nNode].SubNodeSlotInfo.nPassDNSlots); /* straight register write */
			A2B_APP_DBG_LOG("nPassUPSlots: %d ", NodeStreamInfo[nNode].SubNodeSlotInfo.nPassUPSlots); /* straight register write */
			A2B_APP_DBG_LOG("ContributeDNSlots: %d ", NodeStreamInfo[nNode].SubNodeSlotInfo.nContributeDNSlots); /* Bitwise with 0x80 */
			A2B_APP_DBG_LOG("ContributeUPSlots: %d ", NodeStreamInfo[nNode].SubNodeSlotInfo.nContributeUPSlots); /* straight register write */
			A2B_APP_DBG_LOG("nConsumeDNSlots: %d ", NodeStreamInfo[nNode].SubNodeSlotInfo.nConsumeDNSlots);
			A2B_APP_DBG_LOG("nConsumeUPSlots: %d ", NodeStreamInfo[nNode].SubNodeSlotInfo.nConsumeUPSlots);
			A2B_APP_DBG_LOG("DNMask: %d ", NodeStreamInfo[nNode].SubNodeSlotInfo.DNMask); /* straight register write */
			A2B_APP_DBG_LOG("UPMask: %d ", NodeStreamInfo[nNode].SubNodeSlotInfo.UPMask); /* straight register write */
			A2B_APP_DBG_LOG("DNOffset: %d ", NodeStreamInfo[nNode].SubNodeSlotInfo.DNOffset); /* straight register write */
			A2B_APP_DBG_LOG("UPOffset: %d\n\n ", NodeStreamInfo[nNode].SubNodeSlotInfo.UPOffset); /* straight register write */
		}

	}
#endif
}

void AutoCalcSlotDT(a2b_App_t *pApp_Info)
{
	a2b_Int8  nNumSubNodesDscvrd;
	a2b_Int8  nNode;
	a2bpnp_NetworkInfo *PnpNWInfo = &pApp_Info->PnpNWInfo;
	a2bpnp_DTStreamInfo* DTStream = &PnpNWInfo->DTStream[0];
	a2b_UInt8 DTStreamCount = PnpNWInfo->nDTStreamCount;
	a2b_UInt8 nStreamIdx=0u;
	a2b_UInt32 DNMask = 0;
	a2b_UInt32 UPMask = 0;
	a2b_UInt8 dtFirst = 0;
	a2b_UInt8 dtLast = 0;
	a2b_UInt8 strictBoundDown = true;
	a2b_UInt8 strictBoundUp = true;
	a2b_UInt8 dtDNOffset = 0;
	a2b_UInt8 dtUPOffset = 0;
	a2b_UInt8 dtDNOffMin = 0;
	a2b_UInt8 dtDNOffMax = 0;
	a2b_UInt8 dtUPOffMin = 0;
	a2b_UInt8 dtUPOffMax = 0;
	a2b_UInt8 dtFirstN = 0;
	a2b_UInt8 dtLastN = 0;

	a2b_UInt8 dtNode = 0;
	bool bFlag = false;
	a2b_UInt8 idx;


	nNumSubNodesDscvrd =  (a2b_Int8)pApp_Info->bdd.nodes_count - 1u; /* master excluded */

	A2B_UNUSED(strictBoundDown);
	A2B_UNUSED(strictBoundUp);
	A2B_UNUSED(nNumSubNodesDscvrd);

	//Clear DT calculations
	for(idx = 0u; idx < pApp_Info->bdd.nodes_count; idx++)
	{
		 pApp_Info->bdd.nodes[idx].dataTunnelRegs.dtcfg = 0;
		 pApp_Info->bdd.nodes[idx].dataTunnelRegs.dtslots = 0;
		 pApp_Info->bdd.nodes[idx].dataTunnelRegs.dtndnoffs = 0;
		 pApp_Info->bdd.nodes[idx].dataTunnelRegs.dtnupoffs =0;
	}

	for(nStreamIdx = 0u; nStreamIdx < DTStreamCount; nStreamIdx++)
	{
		dtFirst = DTStream[nStreamIdx].dtNodes[0];
		dtLast = DTStream[nStreamIdx].dtNodes[DTStream[nStreamIdx].nNodes - 1];
		dtFirstN = dtFirst + 1;
		dtLastN = dtLast + 1;

		//First registers
		pApp_Info->bdd.nodes[dtFirstN].dataTunnelRegs.dtcfg = (a2b_UInt32)((1<<0)|(1<<1)|(0<<2)); //DTEN and DTFRST
		if(pApp_Info->bdd.nodes[dtFirstN].dataTunnelRegs.dtcfg != 0u)
		{
			pApp_Info->bdd.nodes[dtFirstN].dataTunnelRegs.has_dtcfg = true;
		}

		pApp_Info->bdd.nodes[dtFirstN].dataTunnelRegs.dtslots = ((a2b_UInt32)DTStream[nStreamIdx].nDtUpSlots << 4) | ((a2b_UInt32)DTStream[nStreamIdx].nDtDnSlots);
		if(pApp_Info->bdd.nodes[dtFirstN].dataTunnelRegs.dtslots != 0u)
		{
			pApp_Info->bdd.nodes[dtFirstN].dataTunnelRegs.has_dtslots = true;
		}

		pApp_Info->bdd.nodes[dtFirstN].ctrlRegs.ldnslots |= (a2b_UInt32)0x80u;

		//Middle Node REGs
		for(nNode = dtFirstN+1; nNode < dtLastN; nNode++)//middle nodes
		{
			for(dtNode = 0; dtNode < DTStream[nStreamIdx].nNodes; dtNode++)
			{
				if(nNode == DTStream[nStreamIdx].dtNodes[dtNode] + 1)
				{
					bFlag = true;
					break;
				}
			}

			if(bFlag == true)
			{
				pApp_Info->bdd.nodes[nNode].dataTunnelRegs.dtcfg = (a2b_UInt32)((1<<0)|(0<<1)|(0<<2)); //DTEN
				if(pApp_Info->bdd.nodes[nNode].dataTunnelRegs.dtcfg != 0)
				{
					pApp_Info->bdd.nodes[nNode].dataTunnelRegs.has_dtcfg = true;
				}

				pApp_Info->bdd.nodes[nNode].dataTunnelRegs.dtslots = ((a2b_UInt32)DTStream[nStreamIdx].nDtUpSlots << 4) | ((a2b_UInt32)DTStream[nStreamIdx].nDtDnSlots);
				if(pApp_Info->bdd.nodes[nNode].dataTunnelRegs.dtslots != 0)
				{
					pApp_Info->bdd.nodes[nNode].dataTunnelRegs.has_dtslots = true;
				}
				bFlag = false;
			}
		}

		//Last node registers
		pApp_Info->bdd.nodes[dtLastN].dataTunnelRegs.dtcfg = (a2b_UInt32)((1<<0)|(0<<1)|(1<<2)); //DTEN and DTLAST
		if(pApp_Info->bdd.nodes[dtLastN].dataTunnelRegs.dtcfg != 0)
		{
			pApp_Info->bdd.nodes[dtLastN].dataTunnelRegs.has_dtcfg = true;
		}

		pApp_Info->bdd.nodes[dtLastN].dataTunnelRegs.dtslots = ((a2b_UInt32)DTStream[nStreamIdx].nDtUpSlots << 4) | ((a2b_UInt32)DTStream[nStreamIdx].nDtDnSlots);
		if(pApp_Info->bdd.nodes[dtLastN].dataTunnelRegs.dtslots != 0)
		{
			pApp_Info->bdd.nodes[dtLastN].dataTunnelRegs.has_dtslots = true;
		}


		if(dtFirst == 0xFF)//if main node is first node
		{
			dtDNOffMin = 0;
			dtUPOffMin = 0;
			dtDNOffMax = pApp_Info->bdd.nodes[0].ctrlRegs.dnslots;
			dtUPOffMax = pApp_Info->bdd.nodes[0].ctrlRegs.upslots;
		}
		else//if a slave node is first
		{
			if(dtDNOffMin <= pApp_Info->bdd.nodes[dtFirstN].ctrlRegs.dnslots)
			{
				dtDNOffMin = pApp_Info->bdd.nodes[dtFirstN].ctrlRegs.dnslots;
			}
			if(dtUPOffMin <= pApp_Info->bdd.nodes[dtFirstN].ctrlRegs.upslots)
			{
				dtUPOffMin = pApp_Info->bdd.nodes[dtFirstN].ctrlRegs.upslots;
			}
			if(dtDNOffMax >= (pApp_Info->bdd.nodes[dtFirstN].ctrlRegs.dnslots + (pApp_Info->bdd.nodes[dtFirstN].ctrlRegs.ldnslots & 0x7F)))
			{
				dtDNOffMax = pApp_Info->bdd.nodes[dtFirstN].ctrlRegs.dnslots + (pApp_Info->bdd.nodes[dtFirstN].ctrlRegs.ldnslots & 0x7F);
			}

			//UPSLOTS<----DTUPOFFS----->MAX(UPSLOTS,upmaskrx)
			uint32_t upmask = (uint32_t)(pApp_Info->bdd.nodes[dtFirstN].slotEnh.upmask3 << 24) |  (uint32_t)(pApp_Info->bdd.nodes[dtFirstN].slotEnh.upmask2 << 16) | (uint32_t)(pApp_Info->bdd.nodes[dtFirstN].slotEnh.upmask1 << 8) | (uint32_t)(pApp_Info->bdd.nodes[dtFirstN].slotEnh.upmask0);
			uint8_t upmaskrx = 0;
			if(upmask != 0)

			{
				for(a2b_UInt32 i = 0; i<32; i++)
				{
					if((1<<i) & upmask)
					{
						upmaskrx = i;
					}
				}
			}
			if(dtUPOffMax >= max(pApp_Info->bdd.nodes[dtFirstN].ctrlRegs.upslots, upmaskrx))
			{
				dtUPOffMax = max(pApp_Info->bdd.nodes[dtFirstN].ctrlRegs.upslots, upmaskrx);
			}

		}
		for(nNode = dtFirstN+1; nNode < dtLastN; nNode++)//middle nodes
		{
			if(dtUPOffMax >= pApp_Info->bdd.nodes[nNode].ctrlRegs.upslots)
			{
				dtUPOffMax = pApp_Info->bdd.nodes[nNode].ctrlRegs.upslots;
			}
			if(dtDNOffMax >= pApp_Info->bdd.nodes[nNode].ctrlRegs.dnslots)
			{
				dtDNOffMax = pApp_Info->bdd.nodes[nNode].ctrlRegs.dnslots;
			}
		}

		//Last node
		if(dtDNOffMin <= pApp_Info->bdd.nodes[dtLastN].ctrlRegs.dnslots)
		{
			dtDNOffMin = pApp_Info->bdd.nodes[dtLastN].ctrlRegs.dnslots;
		}

		if(dtUPOffMin <= pApp_Info->bdd.nodes[dtLastN].ctrlRegs.upslots)
		{
			dtUPOffMin = pApp_Info->bdd.nodes[dtLastN].ctrlRegs.upslots;
		}

		//DNSLOTS<----DTDNOFFS----->MAX(DNSLOTS,dnmaskrx)
		uint32_t dnmask = (uint32_t)(pApp_Info->bdd.nodes[dtLastN].slotEnh.dnmask3 << 24) |  (uint32_t)(pApp_Info->bdd.nodes[dtLastN].slotEnh.dnmask2 << 16) | (uint32_t)(pApp_Info->bdd.nodes[dtLastN].slotEnh.dnmask1 << 8) | (uint32_t)(pApp_Info->bdd.nodes[dtLastN].slotEnh.dnmask0);

		uint8_t dnmaskrx = 0;
		if(dnmask != 0)

		{
			for(a2b_UInt32 i = 0; i<32; i++)
			{
				if((1<<i) & dnmask)
				{
					dnmaskrx = i;
				}
			}
		}

		if(dtDNOffMax >= max(pApp_Info->bdd.nodes[dtLastN].ctrlRegs.dnslots, dnmaskrx))
		{
			dtDNOffMax = max(pApp_Info->bdd.nodes[dtLastN].ctrlRegs.dnslots, dnmaskrx);
		}


		if(dtUPOffMax >= (pApp_Info->bdd.nodes[dtLastN].ctrlRegs.upslots + pApp_Info->bdd.nodes[dtLastN].ctrlRegs.lupslots))
		{
			dtUPOffMax = pApp_Info->bdd.nodes[dtLastN].ctrlRegs.upslots + pApp_Info->bdd.nodes[dtLastN].ctrlRegs.lupslots;
		}

		//decide on OFFSET values
		if(dtUPOffMin <= dtUPOffMax)
		{
			dtUPOffset = dtUPOffMin;
		}
		else if(dtUPOffMax >= pApp_Info->bdd.nodes[dtLastN].ctrlRegs.upslots)
		{
			dtUPOffset = dtUPOffMax;
			strictBoundUp = false;
		}

		for(a2b_UInt32 i=0; i<DTStream[nStreamIdx].nNodes; i++)
		{
			pApp_Info->bdd.nodes[DTStream[nStreamIdx].dtNodes[i] + 1].dataTunnelRegs.dtnupoffs = dtUPOffset;
			pApp_Info->bdd.nodes[DTStream[nStreamIdx].dtNodes[i] + 1].dataTunnelRegs.has_dtnupoffs = true;
		}

		if(dtDNOffMin <= dtDNOffMax)
		{
			dtDNOffset = dtDNOffMin;
		}
		else if(dtDNOffMax >= pApp_Info->bdd.nodes[dtFirstN].ctrlRegs.dnslots)
		{
			dtDNOffset = dtDNOffMax;
			strictBoundDown = false;
		}
		else
		{
			/* Data tunnel is not feasible */


		}

		for(a2b_UInt32 i=0; i<DTStream[nStreamIdx].nNodes; i++)
		{
			pApp_Info->bdd.nodes[DTStream[nStreamIdx].dtNodes[i] + 1].dataTunnelRegs.dtndnoffs = dtDNOffset;
			pApp_Info->bdd.nodes[DTStream[nStreamIdx].dtNodes[i] + 1].dataTunnelRegs.has_dtndnoffs = true;
		}

		//Update registers of each affected nodes

		/*Update UPMASK*/
		for(nNode = dtFirst; nNode < dtLastN; nNode++)//last node's UPMASK isn't affected as DT originates there
		{
			if(nNode != A2B_NODEADDR_MASTER)
			{
				UPMask = pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.UPMask;
				pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.UPMask = adjust_mask(UPMask, dtUPOffset, DTStream[nStreamIdx].nDtUpSlots);

				pApp_Info->bdd.nodes[nNode + 1].slotEnh.upmask0 = (pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.UPMask & 0x000000FFu);
				if(pApp_Info->bdd.nodes[nNode + 1].slotEnh.upmask0 != 0u)
				{
					pApp_Info->bdd.nodes[nNode + 1].slotEnh.has_upmask0 = true;
				}

				pApp_Info->bdd.nodes[nNode + 1].slotEnh.upmask1 = (pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.UPMask & 0x0000FF00u) >> 8;
				if(pApp_Info->bdd.nodes[nNode + 1].slotEnh.upmask1 != 0u)
				{
					pApp_Info->bdd.nodes[nNode + 1].slotEnh.has_upmask1 = true;
				}

				pApp_Info->bdd.nodes[nNode + 1].slotEnh.upmask2 = (pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.UPMask & 0x00FF0000u) >> 16;
				if(pApp_Info->bdd.nodes[nNode + 1].slotEnh.upmask2 != 0u)
				{
					pApp_Info->bdd.nodes[nNode + 1].slotEnh.has_upmask2 = true;
				}

				pApp_Info->bdd.nodes[nNode + 1].slotEnh.upmask3 = (pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.UPMask & 0xFF000000u) >> 24;
				if(pApp_Info->bdd.nodes[nNode + 1].slotEnh.upmask3 != 0u)
				{
					pApp_Info->bdd.nodes[nNode + 1].slotEnh.has_upmask3 = true;
				}
			}
		}


		/*Update DNMASK*/
		for(nNode = dtFirst; nNode < dtLastN; nNode++)
		{
			if(nNode != A2B_NODEADDR_MASTER)
			{
				DNMask = pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.DNMask;
				pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.DNMask = adjust_mask(DNMask, dtDNOffset, DTStream[nStreamIdx].nDtDnSlots);

				pApp_Info->bdd.nodes[nNode + 1].slotEnh.dnmask0 = (pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.DNMask & 0x000000FFu);
				if(pApp_Info->bdd.nodes[nNode + 1].slotEnh.dnmask0 != 0u)
				{
					pApp_Info->bdd.nodes[nNode + 1].slotEnh.has_dnmask0 = true;
				}

				pApp_Info->bdd.nodes[nNode + 1].slotEnh.dnmask1 = (pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.DNMask & 0x0000FF00u) >> 8;
				if(pApp_Info->bdd.nodes[nNode + 1].slotEnh.dnmask1 != 0u)
				{
					pApp_Info->bdd.nodes[nNode + 1].slotEnh.has_dnmask1 = true;
				}

				pApp_Info->bdd.nodes[nNode + 1].slotEnh.dnmask2 = (pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.DNMask & 0x00FF0000u) >> 16;
				if(pApp_Info->bdd.nodes[nNode + 1].slotEnh.dnmask2 != 0u)
				{
					pApp_Info->bdd.nodes[nNode + 1].slotEnh.has_dnmask2 = true;
				}

				pApp_Info->bdd.nodes[nNode + 1].slotEnh.dnmask3 = (pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.DNMask & 0xFF000000u) >> 24;
				if(pApp_Info->bdd.nodes[nNode + 1].slotEnh.dnmask3 != 0u)
				{
					pApp_Info->bdd.nodes[nNode + 1].slotEnh.has_dnmask3 = true;
				}
			}
		}


		//Update DNSLOTS and UPSLOTS regs of first and last node
		if((pApp_Info->bdd.nodes[dtFirstN].ctrlRegs.upslots > dtUPOffset) && (dtFirstN > 0))
		{
			pApp_Info->bdd.nodes[dtFirstN].ctrlRegs.upslots += DTStream[nStreamIdx].nDtUpSlots;
		}

		if(pApp_Info->bdd.nodes[dtLastN].ctrlRegs.dnslots > dtDNOffset)
		{
			pApp_Info->bdd.nodes[dtLastN].ctrlRegs.dnslots += DTStream[nStreamIdx].nDtDnSlots;
		}

		//Update DNMASK, UPMASK, DNSLOTS and UPSLOTS regs of middle nodes
		for(nNode = dtFirstN+1; nNode < dtLastN; nNode++)//middle nodes
		{
			/*Update DNSLOTS and UPSLOTS*/
			if(pApp_Info->bdd.nodes[nNode].ctrlRegs.dnslots > dtDNOffset)
			{
				pApp_Info->bdd.nodes[nNode].ctrlRegs.dnslots += DTStream[nStreamIdx].nDtDnSlots;
			}

			if(pApp_Info->bdd.nodes[nNode].ctrlRegs.upslots > dtUPOffset)
			{
				pApp_Info->bdd.nodes[nNode].ctrlRegs.upslots += DTStream[nStreamIdx].nDtUpSlots;
			}
		}

	}
}

static void a2b_PrunePassDNList(a2b_App_t *pApp_Info, a2b_Int8 nNode)
{
	a2b_UInt32 Mask =0;
	a2b_UInt8 nListCnt =0;
	a2b_UInt8 nStreamIdx=0;
	a2b_UInt8 nSubListCnt=0;
	a2b_UInt8 nSubListIdx=0;
	a2b_UInt8 nIndex=0;
	a2b_UInt8 nSlotIndex=0;
	a2b_UInt8 nDestNodeCnt=0;
	a2b_UInt8 bFlag = A2B_FALSE;

	nListCnt = pApp_Info->NodeStreamInfo[nNode].SinkStreamListCnt;

	/* Down Mask calculation */
	for(nStreamIdx = 0; nStreamIdx < nListCnt; nStreamIdx++)
	{
		nSubListCnt = pApp_Info->NodeStreamInfo[nNode].PassDNStreamListCnt;
		for(nSubListIdx=0; nSubListIdx<nSubListCnt; nSubListIdx++)
		{
			if(pApp_Info->NodeStreamInfo[nNode].SinkStreamList[nStreamIdx]->streamIdx == pApp_Info->NodeStreamInfo[nNode].PassDNStreamList[nSubListIdx]->streamIdx)
			{
				for(nIndex=0; nIndex < nSubListIdx; nIndex++)
				{
					nSlotIndex += pApp_Info->NodeStreamInfo[nNode].PassDNStreamList[nIndex]->nNumChannels;
				}

				for(nIndex=0; nIndex < pApp_Info->NodeStreamInfo[nNode].PassDNStreamList[nSubListIdx]->nNumChannels; nIndex++)
				{
					Mask |= (a2b_UInt32)(1<<(nSlotIndex + nIndex));
				}
				nSlotIndex = 0u;

			}
		}

	}

	pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.DNMask = Mask;

	/* See if the Pass down list can be pruned */
	bFlag = A2B_FALSE;
	while(pApp_Info->NodeStreamInfo[nNode].PassDNStreamListCnt != 0u)
	{

		nStreamIdx = pApp_Info->NodeStreamInfo[nNode].PassDNStreamListCnt-1u;

		for(nDestNodeCnt =0; nDestNodeCnt<pApp_Info->NodeStreamInfo[nNode].PassDNStreamList[nStreamIdx]->nDestNodes; nDestNodeCnt++)
		{
			if(pApp_Info->NodeStreamInfo[nNode].PassDNStreamList[nStreamIdx]->DestNodeIdx[nDestNodeCnt] > nNode)
			{
				bFlag = A2B_TRUE;
				break;
			}
		}

		if(bFlag != 0u)
		{
			break;
		}
		else
		{
			pApp_Info->NodeStreamInfo[nNode].PassDNStreamListCnt--;
		}
	}
}

static void a2b_PrunePassUPList(a2b_App_t *pApp_Info, a2b_Int8 nNode)
{
	a2b_UInt32 Mask =0;
	a2b_UInt8 nListCnt =0;
	a2b_UInt8 nStreamIdx=0;
	a2b_UInt8 nSubListCnt=0;
	a2b_UInt8 nSubListIdx=0;
	a2b_UInt8 nIndex=0;
	a2b_UInt8 nSlotIndex=0;


	a2b_UInt8 nDestNodeCnt=0;
	a2b_UInt8 bFlag = A2B_FALSE;

	nListCnt = pApp_Info->NodeStreamInfo[nNode].SinkStreamListCnt;

	/* Down Mask calculation */
	for(nStreamIdx = 0; nStreamIdx < nListCnt; nStreamIdx++)
	{
		nSubListCnt = pApp_Info->NodeStreamInfo[nNode].PassUPStreamListCnt;
		for(nSubListIdx=0; nSubListIdx<nSubListCnt; nSubListIdx++)
		{
			if(pApp_Info->NodeStreamInfo[nNode].SinkStreamList[nStreamIdx]->streamIdx == pApp_Info->NodeStreamInfo[nNode].PassUPStreamList[nSubListIdx]->streamIdx)
			{
				for(nIndex=0; nIndex < nSubListIdx; nIndex++)
				{
					nSlotIndex += pApp_Info->NodeStreamInfo[nNode].PassUPStreamList[nIndex]->nNumChannels;
				}

				for(nIndex=0; nIndex < pApp_Info->NodeStreamInfo[nNode].PassUPStreamList[nSubListIdx]->nNumChannels; nIndex++)
				{
					Mask |= (a2b_UInt32)(1<<(nSlotIndex + nIndex));
				}
				nSlotIndex = 0u;

			}
		}

	}

	pApp_Info->NodeStreamInfo[nNode].SubNodeSlotInfo.UPMask = Mask;

	/* See if the Pass Up list can be pruned */
	bFlag = A2B_FALSE;
	while(pApp_Info->NodeStreamInfo[nNode].PassUPStreamListCnt != 0u)
	{

		nStreamIdx = pApp_Info->NodeStreamInfo[nNode].PassUPStreamListCnt-1u;

		for(nDestNodeCnt =0u; nDestNodeCnt<pApp_Info->NodeStreamInfo[nNode].PassUPStreamList[nStreamIdx]->nDestNodes; nDestNodeCnt++)
		{
			if(pApp_Info->NodeStreamInfo[nNode].PassUPStreamList[nStreamIdx]->DestNodeIdx[nDestNodeCnt] < nNode)
			{
				bFlag = A2B_TRUE;
				break;
			}
		}

		if(bFlag != 0u)
		{
			break;
		}
		else
		{
			pApp_Info->NodeStreamInfo[nNode].PassUPStreamListCnt--;
		}
	}
}

/* Update the SLOT configuration and New STRUCT */
void UpdateSlots(a2b_App_t *pApp_Info)
{
    a2b_UInt8 	rBuf,idx;
	a2b_Int16 nodeAddr = 0;  
	a2b_Bool crossBarDefaultChkForGMute = A2B_TRUE;
	a2bpnp_NetworkInfo *PnpNWInfo = &pApp_Info->PnpNWInfo;
	a2b_pnp_muteStatus muteRes;

	for(nodeAddr = (a2b_Int16)pApp_Info->bdd.nodes_count-2; nodeAddr>=0; nodeAddr--)
	{
		(void)a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, A2B_REG_LDNSLOTS, pApp_Info->bdd.nodes[nodeAddr+1].ctrlRegs.ldnslots);
		(void)a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, A2B_REG_LUPSLOTS, pApp_Info->bdd.nodes[nodeAddr+1].ctrlRegs.lupslots);
		(void)a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, A2B_REG_DNSLOTS, pApp_Info->bdd.nodes[nodeAddr+1].ctrlRegs.dnslots);
		(void)a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, A2B_REG_UPSLOTS, pApp_Info->bdd.nodes[nodeAddr+1].ctrlRegs.upslots);

		(void)a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, A2B_REG_UPMASK0, pApp_Info->bdd.nodes[nodeAddr+1].slotEnh.upmask0);
		(void)a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, A2B_REG_UPMASK1, pApp_Info->bdd.nodes[nodeAddr+1].slotEnh.upmask1);
		(void)a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, A2B_REG_UPMASK2, pApp_Info->bdd.nodes[nodeAddr+1].slotEnh.upmask2);
		(void)a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, A2B_REG_UPMASK3, pApp_Info->bdd.nodes[nodeAddr+1].slotEnh.upmask3);

		(void)a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, A2B_REG_DNMASK0, pApp_Info->bdd.nodes[nodeAddr+1].slotEnh.dnmask0);
		(void)a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, A2B_REG_DNMASK1, pApp_Info->bdd.nodes[nodeAddr+1].slotEnh.dnmask1);
		(void)a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, A2B_REG_DNMASK2, pApp_Info->bdd.nodes[nodeAddr+1].slotEnh.dnmask2);
		(void)a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, A2B_REG_DNMASK3, pApp_Info->bdd.nodes[nodeAddr+1].slotEnh.dnmask3);

		(void)a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, A2B_REG_UPOFFSET, pApp_Info->bdd.nodes[nodeAddr+1].slotEnh.upoffset);
		(void)a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, A2B_REG_DNOFFSET, pApp_Info->bdd.nodes[nodeAddr+1].slotEnh.dnoffset);

		if(PnpNWInfo->nDTStreamCount)
		{
			(void)a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, A2B_REG_DTCFG, pApp_Info->bdd.nodes[nodeAddr+1].dataTunnelRegs.dtcfg);
			(void)a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, A2B_REG_DTSLOTS, pApp_Info->bdd.nodes[nodeAddr+1].dataTunnelRegs.dtslots);
			(void)a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, A2B_REG_DTDNOFFS, pApp_Info->bdd.nodes[nodeAddr+1].dataTunnelRegs.dtndnoffs);
			(void)a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, A2B_REG_DTUPOFFS, pApp_Info->bdd.nodes[nodeAddr+1].dataTunnelRegs.dtnupoffs);
		}
		else
		{
			(void)a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, A2B_REG_DTCFG, 0x00);
			(void)a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, A2B_REG_DTSLOTS, 0x00);
			(void)a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, A2B_REG_DTDNOFFS, 0x00);
			(void)a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, A2B_REG_DTUPOFFS, 0x00);
		}
	}

	/*Update Main node registers*/
	if(PnpNWInfo->nDTStreamCount)
	{
		(void)a2b_AppWriteReg(pApp_Info->ctx, A2B_NODEADDR_MASTER, A2B_REG_DTCFG, pApp_Info->bdd.nodes[nodeAddr+1].dataTunnelRegs.dtcfg);
		(void)a2b_AppWriteReg(pApp_Info->ctx, A2B_NODEADDR_MASTER, A2B_REG_DTSLOTS, pApp_Info->bdd.nodes[nodeAddr+1].dataTunnelRegs.dtslots);
		(void)a2b_AppWriteReg(pApp_Info->ctx, A2B_NODEADDR_MASTER, A2B_REG_DTDNOFFS, pApp_Info->bdd.nodes[nodeAddr+1].dataTunnelRegs.dtndnoffs);
		(void)a2b_AppWriteReg(pApp_Info->ctx, A2B_NODEADDR_MASTER, A2B_REG_DTUPOFFS, pApp_Info->bdd.nodes[nodeAddr+1].dataTunnelRegs.dtnupoffs);
	}
	else
	{
		(void)a2b_AppWriteReg(pApp_Info->ctx, A2B_NODEADDR_MASTER, A2B_REG_DTCFG, 0x00);
		(void)a2b_AppWriteReg(pApp_Info->ctx, A2B_NODEADDR_MASTER, A2B_REG_DTSLOTS, 0x00);
		(void)a2b_AppWriteReg(pApp_Info->ctx, A2B_NODEADDR_MASTER, A2B_REG_DTDNOFFS, 0x00);
		(void)a2b_AppWriteReg(pApp_Info->ctx, A2B_NODEADDR_MASTER, A2B_REG_DTUPOFFS, 0x00);
	}
	(void)a2b_AppWriteReg(pApp_Info->ctx, A2B_NODEADDR_MASTER, A2B_REG_DNSLOTS, pApp_Info->bdd.nodes[0].ctrlRegs.dnslots);
	(void)a2b_AppWriteReg(pApp_Info->ctx, A2B_NODEADDR_MASTER, A2B_REG_UPSLOTS, pApp_Info->bdd.nodes[0].ctrlRegs.upslots);

    /*Checking whether global mute is required during the crossbar register update*/
	if(pApp_Info->bGlobalMuteDuringCrossbar == A2B_TRUE)
	{
		for(idx = 0;idx <pApp_Info->bdd.nodes_count;idx++)
		{
			if(isCrossbarDefault(pApp_Info->bdd.nodes[idx].i2sCrossbarRegs.txcrossbar) == A2B_FALSE)
			{
				crossBarDefaultChkForGMute = A2B_FALSE;
				break;
			}
		}
		muteRes.globalNetworkMute = crossBarDefaultChkForGMute;
		pApp_Info->pfCallback(pApp_Info, (a2b_UInt32)A2B_PNP_AUDIO_MUTE, (void*)&muteRes);
		if(crossBarDefaultChkForGMute == A2B_FALSE)
		{
			(void)networkMute(pApp_Info, A2B_TRUE);
		}	
	}
	else
	{
        muteRes.globalNetworkMute = A2B_FALSE;
		muteRes.mainNodeMute = pApp_Info->PnpNWInfo.MainNodeInfo.bLocalMute;
		for(idx = 1;idx <pApp_Info->bdd.nodes_count;idx++)
		{
			muteRes.subNodeMute[idx - 1] = pApp_Info->PnpNWInfo.DevInfo[idx - 1].bLocalMute;
		}
		pApp_Info->pfCallback(pApp_Info, (a2b_UInt32)A2B_PNP_AUDIO_MUTE, (void*)&muteRes);

		if(pApp_Info->PnpNWInfo.MainNodeInfo.bLocalMute == A2B_TRUE)
		{
			//MUTE Main
			CrossbarLocalMute(pApp_Info, A2B_NODEADDR_MASTER);
			pApp_Info->PnpNWInfo.MainNodeInfo.bLocalMute = A2B_FALSE;
		}
		for(idx = 1;idx <pApp_Info->bdd.nodes_count;idx++)
		{
			if(pApp_Info->PnpNWInfo.DevInfo[idx - 1].bLocalMute == A2B_TRUE)
			{
				//Mute Sub node
				CrossbarLocalMute(pApp_Info, idx - 1);
				pApp_Info->PnpNWInfo.DevInfo[idx - 1].bLocalMute = A2B_FALSE;
			}
		}
	}

	/* Write the new struct , Mute has to be done before applying */
	(void)a2b_AppReadReg(pApp_Info->ctx, A2B_NODEADDR_MASTER, A2B_REG_CONTROL, (a2b_UInt32*)&rBuf);
	rBuf |= A2B_ENUM_CONTROL_START_NS;
	(void)a2b_AppWriteReg(pApp_Info->ctx, A2B_NODEADDR_MASTER, A2B_REG_CONTROL, rBuf);

	/*Update XBAR registers*/
	if(pApp_Info->bdd.nodes[0].i2sCrossbarRegs.has_txcrossbar == A2B_TRUE)
	{
		for(a2b_UInt32 i=0; i<32; i++)
		{
			(void)a2b_AppWriteReg(pApp_Info->ctx, A2B_NODEADDR_MASTER, A2B_REG_TXXBAR0 + i, pApp_Info->bdd.nodes[0].i2sCrossbarRegs.txcrossbar[i]);
		}
	}

	if(pApp_Info->bdd.nodes[0].i2sCrossbarRegs.has_rxmask == A2B_TRUE)
	{
		for(a2b_UInt32 i=0; i<8; i++)
		{
			(void)a2b_AppWriteReg(pApp_Info->ctx, A2B_NODEADDR_MASTER, A2B_REG_RXMASK0 + i, pApp_Info->bdd.nodes[0].i2sCrossbarRegs.rxmask[i]);
		}
	}

	for(nodeAddr = (a2b_Int16)pApp_Info->bdd.nodes_count-2; nodeAddr>=0; nodeAddr--)
	{
		for(a2b_UInt32 i=0; i<32; i++)
		{
			(void)a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, A2B_REG_TXXBAR0 + i, pApp_Info->bdd.nodes[nodeAddr+1].i2sCrossbarRegs.txcrossbar[i]);
		}
		for(a2b_UInt32 i=0; i<8; i++)
		{
			(void)a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, A2B_REG_RXMASK0 + i, pApp_Info->bdd.nodes[nodeAddr+1].i2sCrossbarRegs.rxmask[i]);
		}
	}

	if(crossBarDefaultChkForGMute == A2B_FALSE)
	{
		networkMute(pApp_Info, A2B_FALSE);
	}
	/* Inform the application that we have unmuted the audio */
	pApp_Info->pfCallback(pApp_Info, (a2b_UInt32)A2B_PNP_AUDIO_UNMUTE_ROUTING_DONE, (void*)&muteRes);

}

a2b_UInt32 calcMstrRespCycs(a2b_App_t *pApp_Info)
{

    double totalCablelen;
    struct _bdd_Network* bdd = &pApp_Info->bdd;
    a2b_UInt8 idx = 0;
    A2B_PNP_BW_CALC_PARM bwParam;
    uint16_t downStreamRespCycles[16];
    uint16_t upStreamRespCycles[16];
    uint16_t tmpMaxDS = 0, tmpMinUS = 0x100;
	a2b_UInt32 upstremBits, dwnstremBits;
	PNP_CALLBACK pfCallback;

    totalCablelen = 4*(bdd->nodes_count-1);
	bwParam.bitsPerNs = (double)((double)(48000 * SF_CLOCKCYCLES) / 1000000000);
	bwParam.sltfmt = bdd->nodes[0].ctrlRegs.slotfmt;
	bwParam.mainTosubNode0Cablelen = 4.0;
    bwParam.downStreamPropDelay = (bdd->nodes_count - 2) * TRX_ATOB_AVGDELAY;
    bwParam.upStreamPropDelay = (bdd->nodes_count - 1) * TRX_BTOA_AVGDELAY;
    bwParam.cableDelay = ceil((((2 * totalCablelen) - bwParam.mainTosubNode0Cablelen) * CABLE_PROP_DELAY * bwParam.bitsPerNs));


    for (idx = 0; idx < (bdd->nodes_count); idx++)
    {
			
		getNodeBwInfo(pApp_Info, idx, &dwnstremBits, &upstremBits,bwParam );
        double tmpDwn = ceil(dwnstremBits);
        double tmpUp = ((getResponseCycleOffset(pApp_Info, idx)) * 4) - upstremBits;

        downStreamRespCycles[idx] = (ceil((tmpDwn - 7) / 4.0));
        upStreamRespCycles[idx] = ((tmpUp - 7) / 4.0);
        tmpMaxDS = max(tmpMaxDS,downStreamRespCycles[idx]);
        tmpMinUS = min(tmpMinUS,upStreamRespCycles[idx] );

    }


    if (tmpMinUS < tmpMaxDS)
    {
		//Send the error message
		pfCallback = (PNP_CALLBACK)pApp_Info->pfCallback;
		pfCallback(pApp_Info, (a2b_UInt32)A2B_PNP_NO_VALID_RESP_CYCS, NULL);
		
       // TODO: Bandwidth overshoot, no valid resp cycles, Add error code and communicate to APP.
		if(pApp_Info->bDebug)
		{
			A2B_APP_DBG_LOG("\n\rNo valid range found for response cycles, possible bandwidth overshoot \n\r");
		}
    }

    a2b_UInt32 respCycle = (a2b_UInt32)(ceil((tmpMaxDS + tmpMinUS) / 2.0));

	if(pApp_Info->bDebug)
	{
		A2B_APP_DBG_LOG("\n\rRESPCYC = 0x%02x \n\r", respCycle);
	}
    return respCycle;

}

 void getNodeBwInfo(a2b_App_t *pApp_Info, a2b_Int32 idx, a2b_UInt32* dwnBits,  a2b_UInt32* upBits, A2B_PNP_BW_CALC_PARM bwParam)
 {
	    a2b_UInt32 dnSlotActivity = 0;
        a2b_UInt32 upSlotActivity = 0;
		a2b_UInt8 sltfmt = bwParam.sltfmt;
		a2b_UInt32 downStreamBusSlotWidth = getDownstreamSlotSize(sltfmt)+1;
		a2b_UInt32 upStreamBusSlotWidth =  getUpstreamSlotSize(sltfmt)+1;

        SlotInfo subNodeSlotInfo = getSlotInfo(pApp_Info, idx-1);
        dnSlotActivity = max(subNodeSlotInfo.a_totalDNSlots, subNodeSlotInfo.b_totalDNSlots);
        dnSlotActivity = (dnSlotActivity * downStreamBusSlotWidth);

        upSlotActivity = max(subNodeSlotInfo.a_totalUPSlots, subNodeSlotInfo.b_totalUPSlots);
        upSlotActivity = (upSlotActivity * upStreamBusSlotWidth);

		*upBits = (64 + upSlotActivity + TURNAROUNDTIME + (bwParam.mainTosubNode0Cablelen * CABLE_PROP_DELAY * bwParam.bitsPerNs));
		*dwnBits = (64 + dnSlotActivity + TURNAROUNDTIME + bwParam.downStreamPropDelay + bwParam.upStreamPropDelay + bwParam.cableDelay);
		
 }


void getNwBw(a2b_App_t *pApp_Info, a2b_Int32* pNode, a2b_UInt32* pUsage)
{
	double totalCablelen;
	struct _bdd_Network* bdd = &pApp_Info->bdd;
    a2b_UInt8 idx = 0;
    A2B_PNP_BW_CALC_PARM bwParam;
	a2b_UInt32 upstremBits, dwnstremBits;
	a2b_UInt16 maxBits = 0;
	a2b_UInt16 maxNodeIdx=0;
	a2b_UInt16 currNodecnt = bdd->nodes_count;

     /*During the blind discovery node count would be increased, so reduce it  */
	if(pApp_Info->ePnPModuleState ==  A2B_PNP_BLIND_DISC_IN_PROGRESS)
	{
		currNodecnt--;
	}

    totalCablelen = A2B_AVG_CABLE_LENGTH_FOR_BW_CALC*(currNodecnt-1);
	bwParam.bitsPerNs = (double)((double)(48000 * SF_CLOCKCYCLES) / 1000000000);
	bwParam.sltfmt = bdd->nodes[0].ctrlRegs.slotfmt;
	bwParam.mainTosubNode0Cablelen = A2B_AVG_CABLE_LENGTH_FOR_BW_CALC;
    bwParam.downStreamPropDelay = (currNodecnt - 2) * TRX_ATOB_AVGDELAY;
    bwParam.upStreamPropDelay = (currNodecnt - 1) * TRX_BTOA_AVGDELAY;
    bwParam.cableDelay = ceil((((2 * totalCablelen) - bwParam.mainTosubNode0Cablelen) * CABLE_PROP_DELAY * bwParam.bitsPerNs));

    for (idx = 0; idx < (currNodecnt); idx++)
    {			
		getNodeBwInfo(pApp_Info, idx, &dwnstremBits, &upstremBits, bwParam);
		maxNodeIdx = (upstremBits + dwnstremBits) > maxBits ? idx : maxNodeIdx;
 		maxBits = (upstremBits + dwnstremBits) > maxBits ? (upstremBits + dwnstremBits) : maxBits;		
	}
    *pUsage = (maxBits*100/1024);
	*pNode = maxNodeIdx - 1;

}

a2b_UInt32 getResponseCycleOffset(a2b_App_t *pApp_Info, a2b_Int32 nodeId)
{
	int32_t offset = 238;		//default offset
	struct _bdd_Network* bdd = &pApp_Info->bdd;

	int32_t tdm = bdd->nodes[nodeId + 1].i2cI2sRegs.i2sgcfg & 0x07;
	int32_t tdmWidth = (bdd->nodes[nodeId + 1].i2cI2sRegs.i2sgcfg >> 4) & 0x01;

	switch (tdm)
	{
	case 0:
		tdm = 2;
		(tdmWidth == 0) ? (offset = 245) : (offset = 238);
		break;
	case 1:
		tdm = 4;
		(tdmWidth == 0) ? (offset = 248) : (offset = 245);
		break;
	case 2:
		tdm = 8;
		offset = 248;
		break;
	case 3:
		tdm = 12;
		offset = 248;
		break;
	case 4:
		tdm = 16;
		offset = 248;
		break;
	case 5:
		tdm = 20;
		offset = 248;
		break;
	case 6:
		tdm = 24;
		offset = 248;
		break;
	case 7:
		tdm = 32;
		offset = 248;
		break;
	default:
		break;
	}
	return offset;
}

a2b_UInt32 getDownstreamSlotSize(a2b_UInt8 slotFmt)
{
    a2b_UInt8 val = 0;
    switch (slotFmt & 0x07)
    {
    case 0:
        val = 8;
        break;
    case 1:
        val = 12;
        break;
    case 2:
        val = 16;
        break;
    case 3:
        val = 20;
        break;
    case 4:
        val = 24;
        break;
    case 5:
        val = 28;
        break;
    case 6:
        val = 32;
        break;
    default:
        break;
    }
    return val;
}

a2b_UInt32 getUpstreamSlotSize(a2b_UInt8 slotFmt)
{
    a2b_UInt8 val;
    switch ((slotFmt >> 4) & 0x7)
    {
    case 0:
        val = 8;
        break;
    case 1:
        val = 12;
        break;
    case 2:
        val = 16;
        break;
    case 3:
        val = 20;
        break;
    case 4:
        val = 24;
        break;
    case 5:
        val = 28;
        break;
    case 6:
        val = 32;
        break;
    default:
    	val = 0;
        break;
    }
    return val;
}


void applyRespCys(a2b_UInt32 respCycleNew, a2b_App_t *pApp_Info)
{
	struct _bdd_Network* bdd = &pApp_Info->bdd;
	int32_t offset =  respCycleNew - bdd->nodes[0].ctrlRegs.respcycs;
	a2b_UInt8 idx;

	for(idx = 0; idx < bdd->nodes_count;idx++)
	{
		bdd->nodes[idx].ctrlRegs.respcycs += offset;
		a2b_AppWriteReg(pApp_Info->ctx, (idx-1), A2B_REG_RESPCYCS, bdd->nodes[idx].ctrlRegs.respcycs);
	}

	for(idx = bdd->nodes_count; idx < (A2B_CONF_MAX_NUM_SLAVE_NODES + 1) ;idx++)
	{
		bdd->nodes[idx].ctrlRegs.respcycs += offset;
	}

}


SlotInfo getSlotInfo(a2b_App_t *pApp_Info, a2b_Int32 nodeId)
{

	struct _bdd_Network* bdd = &pApp_Info->bdd;
	a2b_UInt8 nodeIdx = nodeId + 1; /* -1 for the master */
	a2b_UInt8 tunnelEn, tunnelPos, dtupslots, dtdnslots, dtupOffset, dtdnOffset;
	SlotInfo slotInfo;

	a2b_UInt8 dtcfg = (a2b_UInt8)bdd->nodes[nodeIdx].dataTunnelRegs.dtcfg;
	a2b_UInt8 dtslots = (a2b_UInt8)bdd->nodes[nodeIdx].dataTunnelRegs.dtslots;

	tunnelEn = dtcfg & 0x01;
	tunnelPos = (dtcfg>>1);

	dtupslots = (dtslots >> 4) & 0xF;
	dtdnslots = (dtslots) & 0xF;
	dtupOffset = (a2b_UInt8)bdd->nodes[nodeIdx].dataTunnelRegs.dtnupoffs;
	dtdnOffset = (a2b_UInt8)bdd->nodes[nodeIdx].dataTunnelRegs.dtndnoffs;

	if (nodeId == -1)
	{
		slotInfo.a_totalDNSlots = 0;
		slotInfo.b_totalDNSlots = bdd->nodes[0].ctrlRegs.dnslots + tunnelEn ? dtdnslots : 0;
		slotInfo.a_totalUPSlots = 0;
		slotInfo.b_totalUPSlots = bdd->nodes[0].ctrlRegs.upslots + tunnelEn ? dtupslots : 0;
	}
	else
	{
		int32_t dnslots = bdd->nodes[nodeIdx].ctrlRegs.dnslots ;
		int32_t upslots = bdd->nodes[nodeIdx].ctrlRegs.upslots ;
		int32_t dnSrcSlots = bdd->nodes[nodeIdx].ctrlRegs.ldnslots & 0x7F;
		int32_t upSrcSlots = bdd->nodes[nodeIdx].ctrlRegs.lupslots;

		int32_t maxDnSlots = getmaxMaskDn(pApp_Info,nodeId);
		int32_t maxUpSlots = getmaxMaskUp(pApp_Info,nodeId);

		slotInfo.a_totalDNSlots = (int16_t)((maxDnSlots < dnslots) ? dnslots : maxDnSlots);
		slotInfo.b_totalDNSlots = (dnslots + dnSrcSlots);
		slotInfo.a_totalUPSlots = (upslots + upSrcSlots);
		slotInfo.b_totalUPSlots = (int16_t)((maxUpSlots < upslots) ? upslots : maxUpSlots);

		if (tunnelEn == A2B_TRUE) {
			if (tunnelPos == 2)
			{
				//last node
				slotInfo.a_totalUPSlots += dtupslots;

				if (maxDnSlots < (dtdnslots + dtdnOffset)) {
					//tunnel falls after downstream slots
					slotInfo.a_totalDNSlots += dtdnslots;
				}
			}
			else if (tunnelPos == 0) {
				//middle node
				slotInfo.a_totalUPSlots += dtupslots;
				slotInfo.b_totalDNSlots += dtdnslots;

				if (maxDnSlots < (dtdnslots + dtdnOffset)) {
					//tunnel falls after downstream slots
					slotInfo.a_totalDNSlots += dtdnslots;
				}
				if (maxUpSlots < (dtupslots + dtupOffset)) {
					//tunnel falls after upstream slots
					slotInfo.b_totalUPSlots += dtupslots;
				}
			}
			else if (tunnelPos == 1) {
				//first node
				slotInfo.b_totalDNSlots += dtdnslots;
				if (maxUpSlots < (dtupslots + dtupOffset)) {
					//tunnel falls after upstream slots
					slotInfo.b_totalUPSlots += dtupslots;
				}
			}
			else {
				;
			}
		}
	}

	return slotInfo;

}

int32_t getmaxMaskDn(a2b_App_t *pApp_Info, a2b_Int32 nodeId)
{
	struct _bdd_Network* bdd = &pApp_Info->bdd;
	a2b_UInt8 nodeIdx = nodeId + 1; /* -1 for the master */
	a2b_UInt8 mask0 = bdd->nodes[nodeIdx].slotEnh.dnmask0;
	a2b_UInt8 mask1 = bdd->nodes[nodeIdx].slotEnh.dnmask1;
	a2b_UInt8 mask2 = bdd->nodes[nodeIdx].slotEnh.dnmask2;
	a2b_UInt8 mask3 = bdd->nodes[nodeIdx].slotEnh.dnmask3;
	a2b_UInt32 mask = mask0 | (mask1 << 8) | (mask2 << 16) | (mask3 << 24);
	return (mask ? (int32_t)(log2(mask) + 1) : 0);
}

int32_t getmaxMaskUp(a2b_App_t *pApp_Info, a2b_Int32 nodeId)
{

	struct _bdd_Network* bdd = &pApp_Info->bdd;
	a2b_UInt8 nodeIdx = nodeId + 1; /* -1 for the master */
	a2b_UInt8 mask0 = bdd->nodes[nodeIdx].slotEnh.upmask0;
	a2b_UInt8 mask1 = bdd->nodes[nodeIdx].slotEnh.upmask1;
	a2b_UInt8 mask2 = bdd->nodes[nodeIdx].slotEnh.upmask2;
	a2b_UInt8 mask3 = bdd->nodes[nodeIdx].slotEnh.upmask3;
	a2b_UInt32 mask = mask0 | (mask1 << 8) | (mask2 << 16) | (mask3 << 24);
	return (mask ? (int32_t)(log2(mask) + 1) : 0);
}

/* Example or Demo API , not be documented */
A2B_PNP_RESULT a2b_pnp_UseDemoAudioRt(A2B_PNP_HANDLE hPnp, a2b_UInt8 bNodeDrop)
{
	a2b_Int8 NodeIdx;
	a2b_Int8 SinkNodeIdx;
	a2b_Int8 NumSubNodesDscvrd;
	a2b_UInt8 StreamCount = 0;
	a2b_UInt8 SourceNodeAssigned=A2B_FALSE;
	a2b_UInt8 SourceNodeUnAssigned=A2B_FALSE;
	static a2b_UInt8 PrevStreamIdx = 0;
	a2b_UInt8 nStreamIdx=0;
	a2b_UInt8 devName[4] = {0};

	a2b_App_t * pApp_Info;
	a2bpnp_NetworkInfo *PnpNWInfo;

	/* Verify PnP handle */
	if(a2b_VerifyPnPHandle(hPnp) == A2B_PNP_RESULT_FAILED)
	{
		return A2B_PNP_RESULT_FAILED;
	}
	pApp_Info = (a2b_App_t *)hPnp;
	PnpNWInfo = &pApp_Info->PnpNWInfo;

	A2B_UNUSED(pApp_Info);

	NumSubNodesDscvrd =  (a2b_Int8)PnpNWInfo->nDeviceCount; /* No of sub nodes discovered */
	for(NodeIdx=0; NodeIdx<NumSubNodesDscvrd; NodeIdx++)
	{
		strncpy((char *)devName, (char *)PnpNWInfo->DevInfo[NodeIdx].DeviceName, sizeof(devName)-1u);
		if((strcmp((char *)devName, "SRC") == 0u) || (strcmp((char *)devName, "src") == 0u) || (strcmp((char *)devName, "Src") == 0u) ||
			(strcmp((char *)devName, "MIC") == 0u) || (strcmp((char *)devName, "mic") == 0u) || (strcmp((char *)devName, "Mic") == 0u))
		{
			pApp_Info->SubNodeTypeInfo[NodeIdx].NodeType = A2B_PNP_SOURCE;
		}
		else if((strcmp((char *)devName, "SNK") == 0u) || (strcmp((char *)devName, "snk") == 0u) || (strcmp((char *)devName, "Snk") == 0u))
		{
			pApp_Info->SubNodeTypeInfo[NodeIdx].NodeType = A2B_PNP_SINK;
		}
		else
		{
			pApp_Info->SubNodeTypeInfo[NodeIdx].NodeType = A2B_PNP_SOURCE_SINK;
		}
		memset(devName, '\0', sizeof(devName));
	}

	if(NumSubNodesDscvrd == 1)
	{
		pApp_Info->SubNodeTypeInfo[0].IsStreamDefined = A2B_FALSE;
	}

	if(bNodeDrop == 0u)
	{
		for(NodeIdx=0; NodeIdx<NumSubNodesDscvrd; NodeIdx++)
		{
			if( (pApp_Info->SubNodeTypeInfo[NodeIdx].NodeType == A2B_PNP_SOURCE) && (pApp_Info->SubNodeTypeInfo[NodeIdx].IsStreamDefined == A2B_FALSE))
			{
				SourceNodeAssigned = A2B_FALSE;
				for(SinkNodeIdx = NodeIdx+1; SinkNodeIdx < NumSubNodesDscvrd; SinkNodeIdx++)
				{
					if((pApp_Info->SubNodeTypeInfo[SinkNodeIdx].NodeType == A2B_PNP_SINK) && (pApp_Info->SubNodeTypeInfo[SinkNodeIdx].IsStreamDefined != A2B_TRUE))
					{
						PnpNWInfo->NWStream[PnpNWInfo->nStreamCount].streamIdx = PnpNWInfo->nStreamCount;
						PnpNWInfo->NWStream[PnpNWInfo->nStreamCount].SourceNodeIdx = NodeIdx;
						PnpNWInfo->NWStream[PnpNWInfo->nStreamCount].DestNodeIdx[PnpNWInfo->NWStream[PnpNWInfo->nStreamCount].nDestNodes] = SinkNodeIdx;
						PnpNWInfo->NWStream[PnpNWInfo->nStreamCount].nDestNodes++;
						PnpNWInfo->nStreamCount++;
						pApp_Info->SubNodeTypeInfo[SinkNodeIdx].IsStreamDefined = A2B_TRUE;
						SourceNodeAssigned = A2B_TRUE;
						break;
					}
				}
				if(SourceNodeAssigned == A2B_FALSE)
				{
					for(SinkNodeIdx = 0; SinkNodeIdx < NodeIdx; SinkNodeIdx++)
					{
						if((pApp_Info->SubNodeTypeInfo[SinkNodeIdx].NodeType == A2B_PNP_SINK) && (pApp_Info->SubNodeTypeInfo[SinkNodeIdx].IsStreamDefined != A2B_TRUE))
						{
							PnpNWInfo->NWStream[PnpNWInfo->nStreamCount].streamIdx = PnpNWInfo->nStreamCount;
							PnpNWInfo->NWStream[PnpNWInfo->nStreamCount].SourceNodeIdx = NodeIdx;
							PnpNWInfo->NWStream[PnpNWInfo->nStreamCount].DestNodeIdx[PnpNWInfo->NWStream[PnpNWInfo->nStreamCount].nDestNodes] = SinkNodeIdx;
							PnpNWInfo->NWStream[PnpNWInfo->nStreamCount].nDestNodes++;
							PnpNWInfo->nStreamCount++;
							pApp_Info->SubNodeTypeInfo[SinkNodeIdx].IsStreamDefined = A2B_TRUE;
							SourceNodeAssigned = A2B_TRUE;
							break;
						}
					}
				}

				if(SourceNodeAssigned == A2B_FALSE)
				{
					/* TODO One Enhancement could be that a source with multiple destination can be found and this source can be assigned to one
					 * of the destination */

	//				A2B_APP_DBG_LOG("Stream not assigned for %d\n\r", NodeIdx);
					pApp_Info->SubNodeTypeInfo[NodeIdx].IsStreamDefined = A2B_FALSE;
					SourceNodeUnAssigned = A2B_TRUE;
				}
				else
				{
					pApp_Info->SubNodeTypeInfo[NodeIdx].IsStreamDefined = A2B_TRUE;
				}
			}
		}

		/* Assign source to all left over sinks */
		if(PnpNWInfo->nStreamCount != 0u)
		{
			for(SinkNodeIdx=0; SinkNodeIdx<NumSubNodesDscvrd; SinkNodeIdx++)
			{
				if((pApp_Info->SubNodeTypeInfo[SinkNodeIdx].NodeType == A2B_PNP_SINK) && (pApp_Info->SubNodeTypeInfo[SinkNodeIdx].IsStreamDefined != A2B_TRUE))
				{

					PnpNWInfo->NWStream[PrevStreamIdx].DestNodeIdx[PnpNWInfo->NWStream[PrevStreamIdx].nDestNodes] = SinkNodeIdx;
					PnpNWInfo->NWStream[PrevStreamIdx].nDestNodes++;

					PrevStreamIdx++;
					if(PrevStreamIdx == PnpNWInfo->nStreamCount)
					{
						PrevStreamIdx = 0;
					}

					pApp_Info->SubNodeTypeInfo[SinkNodeIdx].IsStreamDefined = A2B_TRUE;
				}
			}
		}

		StreamCount = PnpNWInfo->nStreamCount;

		/* Check if there is any source which does not have SINK assigned */
		if(SourceNodeUnAssigned == A2B_TRUE)
		{
			for(nStreamIdx=0; nStreamIdx<StreamCount; nStreamIdx++)
			{
				/* Check if there is any stream which can spare a destination node */
				if(PnpNWInfo->NWStream[nStreamIdx].nDestNodes > 1u)
				{
					/* Found a stream assigned to multiple destination nodes */

					/* loop through the source nodes to find the one which do not have sink assigned */
					for(NodeIdx=0; NodeIdx<NumSubNodesDscvrd; NodeIdx++)
					{
						if( (pApp_Info->SubNodeTypeInfo[NodeIdx].NodeType == A2B_PNP_SOURCE) && (pApp_Info->SubNodeTypeInfo[NodeIdx].IsStreamDefined == A2B_FALSE))
						{
							/* Found it */
							PnpNWInfo->NWStream[PnpNWInfo->nStreamCount].streamIdx = PnpNWInfo->nStreamCount;
							PnpNWInfo->NWStream[PnpNWInfo->nStreamCount].SourceNodeIdx = NodeIdx;

							/* Take away the last assigned SINK node from this stream */
							SinkNodeIdx = PnpNWInfo->NWStream[nStreamIdx].DestNodeIdx[PnpNWInfo->NWStream[nStreamIdx].nDestNodes-1u];
							PnpNWInfo->NWStream[nStreamIdx].nDestNodes--;

							PnpNWInfo->NWStream[PnpNWInfo->nStreamCount].DestNodeIdx[PnpNWInfo->NWStream[PnpNWInfo->nStreamCount].nDestNodes] = SinkNodeIdx;
							PnpNWInfo->NWStream[PnpNWInfo->nStreamCount].nDestNodes++;
							PnpNWInfo->nStreamCount++;

							pApp_Info->SubNodeTypeInfo[SinkNodeIdx].IsStreamDefined = A2B_TRUE;
						}
					}
				}
			}
		}

		StreamCount = PnpNWInfo->nStreamCount;

		/* TODO: Number of channels has to come from Module information */
		for(nStreamIdx=0u; nStreamIdx<StreamCount; nStreamIdx++)
		{
			PnpNWInfo->NWStream[nStreamIdx].nNumChannels = 2u;
		}
	}
	else
	{
		/* Assign source to all left over sinks */
		if(PnpNWInfo->nStreamCount != 0u)
		{
			for(SinkNodeIdx=0; SinkNodeIdx<NumSubNodesDscvrd; SinkNodeIdx++)
			{
				if((pApp_Info->SubNodeTypeInfo[SinkNodeIdx].NodeType == A2B_PNP_SINK) && (pApp_Info->SubNodeTypeInfo[SinkNodeIdx].IsStreamDefined != A2B_TRUE))
				{

					PnpNWInfo->NWStream[PrevStreamIdx].DestNodeIdx[PnpNWInfo->NWStream[PrevStreamIdx].nDestNodes] = SinkNodeIdx;
					PnpNWInfo->NWStream[PrevStreamIdx].nDestNodes++;

					PrevStreamIdx++;
					if(PrevStreamIdx == PnpNWInfo->nStreamCount)
					{
						PrevStreamIdx = 0u;
					}

					pApp_Info->SubNodeTypeInfo[SinkNodeIdx].IsStreamDefined = A2B_TRUE;
				}
			}
		}
	}


	/*Assign Rx and Tx Group 0 by default to all streams*/
	for(nStreamIdx=0u; nStreamIdx<StreamCount; nStreamIdx++)
	{
		PnpNWInfo->NWStream[nStreamIdx].srcRxGroup = 0u;
		for(SinkNodeIdx=0; SinkNodeIdx<PnpNWInfo->NWStream[nStreamIdx].nDestNodes; SinkNodeIdx++)
		{
			PnpNWInfo->NWStream[nStreamIdx].destTxGroup[SinkNodeIdx] = 0u;
		}
		PnpNWInfo->NWStream[nStreamIdx].nNumChannels = PnpNWInfo->DevInfo[PnpNWInfo->NWStream[nStreamIdx].SourceNodeIdx].RxGroups[0].nGroupChannels;
	}

	return A2B_PNP_RESULT_SUCCESS;

}

a2b_UInt32 networkMute(a2b_App_t *pApp_Info, a2b_Bool bMute)
{
	 a2b_UInt8 	val, rBuf;
	 a2b_HResult res;
	 if(bMute == A2B_FALSE)
	 {
		  val = (pApp_Info->bdd.nodes[0].ctrlRegs.datctl | 0x03);
		  res = a2b_AppWriteReg(pApp_Info->ctx, A2B_NODEADDR_MASTER, A2B_REG_DATCTL, val);
	 }
	 else
	 {
		  val = ((pApp_Info->bdd.nodes[0].ctrlRegs.datctl) & (0xFF ^ 0x03));
		  res = a2b_AppWriteReg(pApp_Info->ctx, A2B_NODEADDR_MASTER, A2B_REG_DATCTL, val);
	 }

	/* Write the new struct */
	res |= a2b_AppReadReg(pApp_Info->ctx, A2B_NODEADDR_MASTER, A2B_REG_CONTROL, (a2b_UInt32*)&rBuf);
	rBuf |= A2B_ENUM_CONTROL_START_NS;
	res |= a2b_AppWriteReg(pApp_Info->ctx, A2B_NODEADDR_MASTER, A2B_REG_CONTROL, rBuf);

	return res;
}

a2b_Bool isCrossbarDefault(a2b_UInt32 crossbarReg[])
{
	a2b_UInt32 idx;

	for (idx =0; idx <32; idx++)
	{
		if(crossbarReg[idx] != idx)
		{
			return(A2B_FALSE);
		}
	}

	return(A2B_TRUE); 

}

void CrossbarLocalMute(a2b_App_t *pApp_Info, a2b_Int8 node)
{
	for(a2b_UInt32 i= 0; i<32; i++)
	{
		(void)a2b_AppWriteReg(pApp_Info->ctx, node, A2B_REG_TXXBAR0 + i, 31u);
	}
}
