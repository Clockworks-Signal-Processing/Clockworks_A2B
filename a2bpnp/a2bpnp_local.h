/*******************************************************************************
Copyright (c) 2022 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
******************************************************************************
* @file: a2bpnp_local.h
* @brief: This file contains the declaration of APIs provided by PnP library for the Application.
*
* Developed by: Automotive Software and Systems team, Bangalore, India
*****************************************************************************/

#ifndef A2BPNP_LOCAL_H__
#define A2BPNP_LOCAL_H__

#include <stddef.h>
#include "a2b/pal.h"
#include "a2b/defs.h"
#include "a2b/util.h"
#include "a2b/msg.h"
#include "a2b/msgrtr.h"
#include "a2b/trace.h"
#include "a2b/stack.h"
#include "a2b/seqchart.h"
#include "a2b_bdd_helper.h"
#include "a2b/regdefs.h"
#include "a2b/interrupt.h"
#include "a2b/hwaccess.h"
#include "a2b/system.h"
#include "a2b/diag.h"
#include "a2bplugin-master/plugin.h"
#include "a2bplugin-slave/plugin.h"
#include "timer_priv.h"
#include "stackctx.h"
#include "stdio.h"

#define A2B_PNP_MAX_CONNECTIONS		  100

/* PNP Macros*/

#define A2B_PNP_MODINFO_ID_ADDR          0x00u
#define A2B_PNP_MODINFO_ID_VAL           0xAAu

#define A2B_PNP_MODINFO_VENDORID_ADDR    0x01u
#define A2B_PNP_MODINFO_VENDORID_VAL     0xADu
#define A2B_PNP_MODINFO_PRODUCTID_ADDR   0x03u
#define A2B_PNP_MODINFO_PRODUCTID_VAL    0x37u
#define A2B_PNP_MODINFO_SIVER_ADDR       0x05u
#define A2B_PNP_MODINFO_SIVER_VAL        0x11u

#define A2B_PNP_MODINFO_SWVER_ADDR       0x09u
#define A2B_PNP_MODINFO_PRIM_FUNC_ADDR   0x0Du

#define A2B_PNP_BLOCK_MODINFO_ADRR       0x20u
#define A2B_PNP_BLOCK_MODINFO_TYPE       0x03u

#define A2B_PNP_BLOCK_MODINFO_POWERCONFIG_ADDR   ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x03u)
#define A2B_PNP_BLOCK_MODINFO_PWRCONFIG          0x4u
#define A2B_PNP_BLOCK_MODINFO_MAX_PWR_ADDR	(A2B_PNP_BLOCK_MODINFO_POWERCONFIG_ADDR + 1)
#define A2B_PNP_BLOCK_MODINFO_MIN_PWR_ADDR	(A2B_PNP_BLOCK_MODINFO_POWERCONFIG_ADDR + 3)

#define A2B_PNP_BLOCK_MODINFO_CABLETYPE_ADDR     ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x08u)
#define A2B_PNP_BLOCK_MODINFO_MAX_CURRENT_ADDR (A2B_PNP_BLOCK_MODINFO_CABLETYPE_ADDR + 1)

#define A2B_PNP_SUPPLIER_INFO_ADDR				(0x400)

#define A2B_PNP_BLOCK_MODINFO_DEVICEID_ADDR     ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x47u)
#define A2B_PNP_BLOCK_MODINFO_DEVICENAME_ADDR   ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x0Bu)

#define A2B_PNP_BLOCK_MODINFO_CABLELEN_ADDR      ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x4C)
#define A2B_PNP_BLOCK_MODINFO_PERIPHBLOCK_ADDR   ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x4E)

#define A2B_PNP_BLOCK_MODINFO_DEVINFOBLOCK_ADDR     ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x56)

#define A2B_PNP_BLOCK_MODINFO_NUMPERIHBLOCK_ADDR  ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x5A)

#define A2B_PNP_BLOCK_MODINFO_DNSLOTSIZE_ADDR     ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x5D)
#define A2B_PNP_BLOCK_MODINFO_UPSLOTSIZE_ADDR     ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x5E)


#define A2B_PNP_BLOCK_MODINFO_I2CCFG_ADDR         ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x5F)
#define A2B_PNP_BLOCK_MODINFO_SPICFG_ADDR         ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x60)
#define A2B_PNP_BLOCK_MODINFO_SPICKDIV_ADDR       ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x61)
#define A2B_PNP_BLOCK_MODINFO_SPIPINCFG_ADDR      ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x62)
#define A2B_PNP_BLOCK_MODINFO_I2SCFG_ADDR         ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x63)
#define A2B_PNP_BLOCK_MODINFO_I2SGCFG_ADDR        ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x64)
#define A2B_PNP_BLOCK_MODINFO_I2SRATE_ADDR        ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x65)
#define A2B_PNP_BLOCK_MODINFO_PDMCTL_ADDR         ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x66)
#define A2B_PNP_BLOCK_MODINFO_PDMCTL2_ADDR        ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x67)

#define A2B_PNP_BLOCK_MODINFO_CLKOUT1_ADDR        ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x68)
#define A2B_PNP_BLOCK_MODINFO_CLKOUT2_ADDR        ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x69)

#define A2B_PNP_BLOCK_MODINFO_GPIOOEN_ADDR        ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x6A)
#define A2B_PNP_BLOCK_MODINFO_GPIOIEN_ADDR        ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x6B)

#define A2B_PNP_BLOCK_MODINFO_PINTEN_ADDR        ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x6C)
#define A2B_PNP_BLOCK_MODINFO_PINTINV_ADDR       ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x6D)
#define A2B_PNP_BLOCK_MODINFO_PINCFG_ADDR        ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x6E)

#define A2B_PNP_BLOCK_MODINFO_GPIODAT_ADDR        ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x6F)

#define A2B_PNP_BLOCK_MODINFO_DTREQ_ADDR        ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x70)
#define A2B_PNP_BLOCK_MODINFO_DTDNRNG_ADDR        ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x71)
#define A2B_PNP_BLOCK_MODINFO_DTUPRNG_ADDR        ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x73)

#define A2B_PNP_BLOCK_MODINFO_NBRANCH_ADDR        ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x75u)

#define A2B_PNP_BLOCK_MODINFO_LMICRO_ADDR        ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x76u)
#define A2B_PNP_BLOCK_MODINFO_LMMODE_ADDR        ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x77u)
#define A2B_PNP_BLOCK_MODINFO_LMPCFG_ADDR        ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x78u)

#define A2B_PNP_BLOCK_MODINFO_SUPINFOBLOCK_ADDR        ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0x79u)

#define A2B_PNP_BLOCK_MODINFO_NTXGRP_ADDR        ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0xE0u)
#define A2B_PNP_BLOCK_MODINFO_GRPID_ADDR        ((A2B_PNP_BLOCK_MODINFO_ADRR) + 0xE1u)

#define A2B_PNP_NEXT_GRPID_OFFSET					37u
#define A2B_PNP_NEXT_GRPMAP_OFFSET					33u
/* Rest All TODO*/
/* Device Information block starts at 0x300 */
#define A2B_PNP_BLOCK_DEVINFO_ADDR               (0x0300u)
#define A2B_PNP_BLOCK_DEVINFO_TYPE                0x07u

/* Supplier Information block starts at 0x400 */
#define A2B_PNP_BLOCK_SUPPLIERINFO_ADDR               (0x0400u)
#define A2B_PNP_BLOCK_SUPPLIERINFO_TYPE                0x08u

/* Peripheral Information block starts at 0x500 */
#define A2B_PNP_BLOCK_PERIINFO_ADDR                (0x0800u)//(0x0400u)
#define A2B_PNP_BLOCK_PERIINFO_TYPE                0x04u

/* Peripheral Configuration block */
#define A2B_PNP_BLOCK_PERICONFIG_TYPE              0x05u

#define A2B_PNP_BLOCK_SUPPLIERINFO_TYPE            0x08u

#define A2B_PNP_BLOCK_HEADER_SIZE                  0x03u
#define A2B_PNP_DEVICE_NAME_SIZE                   64u

#ifndef A2BAPP_LINK_STATICALLY
#define A2BAPP_LINK_STATICALLY
#endif

//set as 20s
#define PNP_MODULE_BD_TIMEOUT					20000 

#define A2B_DISABLE				(0)
#define A2B_ENABLE				(1)

/* Bus Drop Check period in seconds */
#define A2B_BUS_DROP_CHK_PERIOD      	(5u)

#define A2B_APP_TMRTOHANDLE_BECOVF_AFTER_INTERVAL	(1000u)	/* In milliseconds */
#define A2B_APP_TMRTOHANDLE_BECOVF_REPEAT_INTERVAL	(1000u)	/* In milliseconds */

/* Base memory for stack depends upon the macros in Conf.h & feaure.h. If the macros are changed, profile the stack to update the memory.
 * The base memory can vary across the stack versions.  */
#define A2BAPP_STACK_MAX_BASE_MEMEORY  		(2304u) /* In Bytes, includes Trace and Commchan */
#define A2BAPP_STACK_MEM_PER_SLAVE	   		(256u)  /* In Bytes, aligning to 256 byte boundary */
#define A2BAPP_STACK_MEMORY_PER_CHAIN  		(A2BAPP_STACK_MAX_BASE_MEMEORY  + A2BAPP_STACK_MEM_PER_SLAVE* A2B_CONF_MAX_NUM_SLAVE_NODES)
#define A2BAPP_STACK_NW_MEMORY         		(A2BAPP_STACK_MEMORY_PER_CHAIN * A2B_CONF_MAX_NUM_MASTER_NODES)

/* Plugin memory requirment */
#define A2BAPP_MAX_MEM_PER_PLUGIN			(64u)	/* Size of a2b_PluginApi is 48 bytes , aligning to 64 byte boundary */
#define A2BAPP_PLUGIN_MEMORY_PER_CHAIN		(A2BAPP_MAX_MEM_PER_PLUGIN * (A2B_CONF_MAX_NUM_SLAVE_NODES + 1U))
#define A2BAPP_PLUGIN_NW_MEMORY				(A2BAPP_PLUGIN_MEMORY_PER_CHAIN * A2B_CONF_MAX_NUM_MASTER_NODES)

/* Memory requirement to read single E2PROM block, usually determined by I2C burst size for remote peripheral */
#define A2BAPP_E2PROM_BLOCK_MEMORY			(4095u)

/* Avergage cable length to be used for BW calc */
#define A2B_AVG_CABLE_LENGTH_FOR_BW_CALC		(10)


/* Debug log */
#define A2B_APP_DBG_LOG(...) do{\
						if(pApp_Info->bDebug)\
						(void)printf(__VA_ARGS__);\
                        }while(0)

#define A2B_APP_ERR_LOG(...) do{\
						(void)printf(__VA_ARGS__);\
                        }while(0)

#define A2B_APP_LOG(...) do{\
						if(gpApp_Info[0]->bDebug)\
						(void)printf(__VA_ARGS__);\
                        }while(0)


typedef enum A2B_PNP_DEVICETYPE
{
	A2B_PNP_DEVICETYPE_PDMMIC = 0u,
	A2B_PNP_DEVICETYPE_MONOMIC = 1u,
	A2B_PNP_DEVICETYPE_AUDIOOUT = 2u,
	A2B_PNP_DEVICETYPE_AUDIOIN = 3u,
	A2B_PNP_DEVICETYPE_MIDI = 4u,
	A2B_PNP_DEVICETYPE_SENSOR = 5u
	// more to ADD
}A2B_PNP_DEVICETYPE;

typedef enum
{
	A2B_PNP_SOURCE,
	A2B_PNP_SINK,
	A2B_PNP_SOURCE_SINK
}A2B_PNP_NODETYPE;

typedef struct A2B_PNP_SOUCESINK_INFO
{
	a2b_Int8 NodeAddr;
	A2B_PNP_NODETYPE NodeType;
	a2b_UInt8 IsStreamDefined;
}A2B_PNP_SOUCESINK_INFO;


typedef struct _A2B_PNP_BW_CALC_PARM
{
	double downStreamPropDelay;
	double upStreamPropDelay;
	double cableDelay;
	double bitsPerNs;
	a2b_UInt8 sltfmt;
	double mainTosubNode0Cablelen;

}A2B_PNP_BW_CALC_PARM;

typedef enum A2B_PNP_DISCSTATE
{
	A2B_PNP_DISCSTATE_NA,
	A2B_PNP_DISCSTATE_BD,
	A2B_PNP_DISCSTATE_OPT,
	A2B_PNP_DISCSTATE_DONE
}A2B_PNP_DISCSTATE;

typedef struct SlotInfo_ {
    int32_t b_totalDNSlots;
    int32_t a_totalDNSlots;
    int32_t b_totalUPSlots;
    int32_t a_totalUPSlots;
}SlotInfo;

typedef struct a2bpnp_SubnodeSlotInfo {
	a2b_UInt8 nContributeDNSlots;
	a2b_UInt8 nContributeUPSlots;

	a2b_UInt8 nConsumeDNSlots;
	a2b_UInt8 nConsumeUPSlots;

	a2b_UInt8 nPassDNSlots;
	a2b_UInt8 nPassUPSlots;


	a2b_UInt32 UPMask;
	a2b_UInt32 DNMask;

	a2b_UInt8 UPOffset;
	a2b_UInt8 DNOffset;
}a2bpnp_SubnodeSlotInfo;

typedef struct a2bpnp_SubnodeStreamInfo {

	a2bpnp_StreamInfo *SourceStreamList[8];
	a2b_UInt8 SourceStreamListCnt;

	a2bpnp_StreamInfo *SinkStreamList[8];
	a2b_UInt8 SinkStreamListCnt;

	a2bpnp_StreamInfo *DNSinkStreamList[8];
	a2b_UInt8 DNSinkStreamListCnt;

	a2bpnp_StreamInfo *UPSinkStreamList[8];
	a2b_UInt8 UPSinkStreamListCnt;

	a2bpnp_StreamInfo *DNSourceStreamList[8];
	a2b_UInt8 DNSourceStreamListCnt;

	a2bpnp_StreamInfo *UPSourceStreamList[8];
	a2b_UInt8 UPSourceStreamListCnt;

	a2bpnp_StreamInfo *PassDNStreamList[8];
	a2b_UInt8 PassDNStreamListCnt;

	a2bpnp_StreamInfo *PassUPStreamList[8];
	a2b_UInt8 PassUPStreamListCnt;

	a2bpnp_SubnodeSlotInfo SubNodeSlotInfo;

}a2bpnp_SubnodeStreamInfo;

typedef struct a2bpnp_MainnodeStreamInfo {

	a2bpnp_StreamInfo *DNSourceStreamList[8];
	a2b_UInt8 DNSourceStreamListCnt;

	a2bpnp_StreamInfo *UPSinkStreamList[8];
	a2b_UInt8 UPSinkStreamListCnt;

	a2b_UInt8 nContributeDNSlots;
	a2b_UInt8 nConsumeUPSlots;

}a2bpnp_MainnodeStreamInfo;

typedef struct a2bpnp_SubnodeDTStreamInfo {

	a2bpnp_DTStreamInfo *OwnerStream[4];
	a2b_UInt8 OwnerStreamCnt;

	a2bpnp_DTStreamInfo *ResponderStream[4];
	a2b_UInt8 ResponderStreamCnt;

	a2bpnp_DTStreamInfo *UPDTOwnerStream[4];
	a2b_UInt8 UPDTOwnerStreamCnt;

	a2bpnp_DTStreamInfo *DNDTOwnerStream[4];
	a2b_UInt8 DNDTOwnerStreamCnt;

	a2bpnp_DTStreamInfo *UPDTRespStream[4];
	a2b_UInt8 UPDTRespStreamCnt;

	a2bpnp_DTStreamInfo *DNDTRespStream[4];
	a2b_UInt8 DNDTRespStreamCnt;
}a2bpnp_SubnodeDTStreamInfo;

struct a2b_App_t;

typedef a2b_UInt8 (* a2bpnp_StackInitFunc)(struct a2b_App_t* pAppInfo);
typedef a2b_UInt8 (* a2bpnp_StackGetbddFunc)(struct a2b_App_t* pAppInfo);
typedef a2b_UInt8 (* a2bpnp_StacKAlloc)(struct a2b_App_t* pAppInfo);
typedef a2b_UInt8 (* a2bpnp_StacKSetAccessInterface)(struct a2b_App_t* pAppInfo);
typedef a2b_UInt8 (* a2bpnp_StartIntialDisc)(struct a2b_App_t* pAppInfo);
typedef a2b_UInt8 (* a2bpnp_StartPartialDisc)(struct a2b_App_t* pAppInfo);
typedef a2b_UInt8 (* a2bpnp_SetNotification)(struct a2b_App_t* pAppInfo);
typedef a2b_UInt8 (* a2bpnp_StartTimerForBLindDisc)(struct a2b_App_t* pAppInfo);
typedef a2b_UInt8 (* a2bpnp_StackStop)(struct a2b_App_t* pAppInfo);
typedef a2b_UInt8 (* a2bpnp_SetupDiagnostics)(struct a2b_App_t* pAppInfo);

typedef struct a2bpnp_StackFunc
{
    /** \name Memory Manager Implementation
     *  Memory manager prototypes requiring platform implementation.
     *
     * \{ */
	a2bpnp_StackInitFunc      stkInit;
	a2bpnp_StackGetbddFunc    stkGetBdd;
	a2bpnp_StacKAlloc    	  stkAlloc;
	a2bpnp_StacKSetAccessInterface stkSetAcesInterface;
	a2bpnp_StartIntialDisc    stkStartInitDisc;
	a2bpnp_StartPartialDisc   stkStartPartialDIsc;
	a2bpnp_SetNotification    stkSetNotification;
	a2bpnp_StartTimerForBLindDisc stkStartTimerForBD;
	a2bpnp_SetupDiagnostics	  stkSetupDiagnostics;
	a2bpnp_StackStop	  	  stkStopStack;

    /** \} */
} a2bpnp_StackFunc;

/** Network performance information */
typedef struct performanceInfo
{
	a2b_UInt32 timeToDiscMs[A2B_CONF_MAX_NUM_SLAVE_NODES];
	a2b_UInt32 MainNodeLockTimeMs;

	/* TODO: Add more fields */
}a2bpnp_performanceInfo;

typedef struct a2b_App_t {

	/* Input flags */
	a2b_Bool	bDebug;
	a2b_UInt8 numNodesToDisc; /* Number of nodes to be discovered statically, by default zero */
	a2b_UInt8 bTwoStepDisc;
	a2b_UInt8 bGlobalMuteDuringCrossbar;
	a2b_UInt8 bStreamByConnection;
	a2b_UInt8 ignoreE2Prom;
	a2b_UInt8 maxNumSubnodes;
	a2b_UInt16 maxPWconsumption;
	a2b_UInt16 maxCurrentConsumption;
	a2b_UInt8 bwErrorLvl;
	a2b_UInt8 bwWarningLvl;
	a2b_UInt8 mainXcvrBInv;	/* A2B_PNP_XCVRBINV_x */
	a2b_UInt8 subXcvrBInv;	/* A2B_PNP_XCVRBINV_x */
#ifdef A2B_FEATURE_SEQ_CHART
	a2b_Char* seqFile;
#endif

	/* Objects for storing info populated inside a2bapp.c */
	A2B_ECB ecb;											/* App envirnment control block  */
	a2b_StackPal pal;										/* PAL layer  */
	struct a2b_StackContext *ctx;							/* Stack context  */
	bdd_Network bdd;										/* BDD Info  */
	ADI_A2B_BCD* pBusDescription;							/* Pointer to Bus Description File */
	ADI_A2B_NETWORK_CONFIG* pTargetProperties;				 /* Pointer to Bus Description File */
	ADI_A2B_NODE_PERICONFIG  aPeriNetworkTable[A2B_CONF_MAX_NUM_SLAVE_NODES + 1u]; 	/* Table to get peripheral configuration structure */
	a2b_UInt8 anEeepromPeriCfgInfo[2u * (A2B_CONF_MAX_NUM_SLAVE_NODES + 1u)];
	struct a2b_MsgNotifier *notifyInterrupt;				/* Interrupt Notifier  */
	struct a2b_MsgNotifier *notifyPowerFault;				/* Power Fault message notifier */
	struct a2b_MsgNotifier *notifyNodeDiscvry;				/* Node Discovery notifier */
	struct a2b_MsgNotifier *notifyI2CError; 				/* I2C Error notifier */
	struct a2b_Timer* hTmrToHandleBecovf;					/* Timer Handler for Bit-error   */

	struct a2b_Timer* hTmrToHandleBlindDisc;    			/* Timer Handler Blind discovery for Pnp feature   */
	a2b_UInt8 bBlindDiscTimerExp;                           /* Boolean to keep track of Timer expiry   */
	a2b_UInt8 bPnPNodeDiscInProg;                           /* PnP Node discovery initiated   */
	a2b_UInt8 bOptRespCycleInProg;                          /* Optimal response cycle calculation initiated   */
	a2b_UInt8 bMasterPowerFault;                            /* Fault on master and NW has to be rediscovered  */
	a2b_UInt8 bPauseDisc;                                   /* Used to pause the peridic discovery */
	PNP_CALLBACK pfCallback;                                /* PnP Application callback */
	INTR_CALLBACK pfIntrCallback;
	ERR_CALLBACK pfErrCallback;

	/* Processing flags local to pnp module */
	A2B_PNP_STATE ePnPModuleState;									/* PnP Module state */
	a2b_Bool bCustomAuthFailed;								/* Custom node authentication failure indication */
	a2b_Bool bBusDropDetected;								/* Flag to detect the Bus drop */
	a2b_Bool bBecovfTimerEnable;							/* Enable flag for starting timer for resetting bit error count */
	a2b_UInt8 bIntrptLatch;
	a2b_UInt8 bSpiIntrptLatch;								/* Flag to check if there is an SPI interrupt present */
	a2b_UInt32 currTime;
	a2b_UInt32 txxbarPrev[A2B_CONF_MAX_NUM_SLAVE_NODES][32];
	a2b_UInt32 txxbarPrevMain[32];

	/* Output flags */
	a2b_UInt8 nodesDiscovered;								/* Number of slave nodes discovered  */
	const a2b_Char *faultStatus;							/* String indicating line fault */
	a2b_Int16 faultNode;		    						/* Node number at which fault occured */
	A2B_PNP_STATE moduleStateduringFault;					/* Module State during the fault */
	a2b_UInt8 faultCode;									/* Fault code */
    a2b_UInt8* panDatFileBuff; 								/* Buffer for Static or Dynamically memory allocation */
	/* Flag which indicates the current is first time discovery or re-discovering the network
	*   bFrstTimeDisc value
	*				A2B_TRUE: Current execution is first time discovery
	*				A2B_FALSE: Current execution is re-discovery
	*/
	a2b_Bool bFrstTimeDisc;
	a2bpnp_StackFunc stkFunc;
	
	a2bpnp_NetworkInfo PnpNWInfo;
	a2bpnp_SubnodeStreamInfo NodeStreamInfo[A2B_CONF_MAX_NUM_SLAVE_NODES];
	a2bpnp_MainnodeStreamInfo MainStreamInfo;
	a2bpnp_SubnodeDTStreamInfo NodeDTStreamInfo[A2B_CONF_MAX_NUM_SLAVE_NODES];
	A2B_PNP_SOUCESINK_INFO SubNodeTypeInfo[A2B_CONF_MAX_NUM_SLAVE_NODES];
	a2bpnp_MainNodeProp* pMainNodeCfg;

	a2bpnp_performanceInfo nwPerformance;

#ifdef A2B_RUN_BIT_ERROR_TEST
	ADI_A2B_BERT_HANDLER oBertHandler;
	a2b_UInt8 *pBertConfigBuff;
	a2b_Bool bIsBertStart;
#endif

#ifdef A2B_FEATURE_COMM_CH
	a2b_Bool  bTxDoneSuccess;
	a2b_Bool  bTxComplete;

	a2b_UInt32 nTxSeqCnt;
	a2b_UInt32 nRxSeqCnt;
	a2b_Bool   bCommChErrSet;
#endif


} a2b_App_t;

extern a2bpnp_MainNodeProp* pMainNodeCfg;
/* Keep track of the node address from where partial discovery has to be initiated */
extern a2b_Int32 nNodeStartPartialDisc;
/* Hold handles for all active Networks */
extern A2B_PNP_HANDLE hPnPList[A2B_PNP_CFG_MAX_NUM_A2B_CHAIN];
extern a2b_App_t* gpApp_Info[A2B_CONF_MAX_NUM_MASTER_NODES];

void a2b_PrintNWInfo(a2bpnp_NetworkInfo *A2BNetworkInfo, a2b_UInt8 bPrintNWInfo, a2b_UInt8 bPrintAudioRt);
a2b_Int32 a2b_discover(a2b_App_t *pApp_Info);
void a2b_app_handle_BlindDiscovery(struct a2b_Timer* timer, a2b_Handle userData);
a2b_Int32 a2b_checkBCFProp(a2b_App_t * pApp_Info);
a2b_Int32 a2b_start(a2b_App_t *pApp_Info);
A2B_PNP_RESULT a2b_VerifyPnPHandle(A2B_PNP_HANDLE hPnp);
void AutoCalcSlot(a2b_App_t *pApp_Info);
void AutoCalcSlotDT(a2b_App_t *pApp_Info);
void UpdateSlots(a2b_App_t *pApp_Info);
a2b_HResult a2b_AppReadReg(struct a2b_StackContext* ctx, a2b_Int16 nodeAddr, a2b_UInt32 reg, a2b_UInt32* value);
a2b_HResult a2b_AppWriteReg(struct a2b_StackContext* ctx, a2b_Int16 nodeAddr, a2b_UInt32 reg, a2b_UInt32 value);
a2b_Int32 a2b_init(a2b_App_t *pApp_Info);
a2b_Int32 a2b_load(a2b_App_t *pApp_Info);
a2b_HResult a2b_AppDetectBusDrop(a2b_App_t *pApp_Info);
a2b_UInt32 a2b_reset(a2b_App_t *pApp_Info);
a2b_UInt32 calcMstrRespCycs(a2b_App_t *pApp_Info);
void applyRespCys(uint32_t respCycleNew, a2b_App_t *pApp_Info);
a2b_Int8 a2b_ReadVerify_ModuleInfo(a2b_App_t *pApp_Info, a2b_Int16 nodeAddr);
void UpdateAudioRt(a2b_App_t *pApp_Info, a2b_Int16 nodeAddr);
void a2bapp_onInterrupt(struct a2b_Msg* msg, a2b_Handle userData);
void a2bapp_onDiscoveryComplete(struct a2b_Msg* msg, a2b_Bool isCancelled);
void a2bapp_onBlindDiscoveryComplete(struct a2b_Msg* msg, a2b_Bool isCancelled);
void a2bapp_onPowerFault(struct a2b_Msg *msg, a2b_Handle userData);
void a2bapp_onNodeDiscovery(struct a2b_Msg* msg, a2b_Handle userData);
void a2bapp_onI2CFailure(struct a2b_Msg* msg, a2b_Handle userData);
void readChipId(a2b_App_t *pApp_Info, a2b_Int8 nodeId, a2b_Char chipId[]);
a2b_UInt8 getChainIdx(void* handle);
 void getNodeBwInfo(a2b_App_t *pApp_Info, a2b_Int32 idx, a2b_UInt32* dwnBits,  a2b_UInt32* upBits, A2B_PNP_BW_CALC_PARM bwParam);
 void getNwBw(a2b_App_t *pApp_Info, a2b_Int32* pNode, a2b_UInt32* pUsage);
 a2b_UInt32 networkMute(a2b_App_t *pApp_Info, a2b_Bool bMute);
 a2b_Int32 getnumChannelsFromDevInfo(a2b_Int32 inst, a2b_Int32 nodeNum, a2b_Int32 grpNo );
 a2b_Int32 a2b_discPrep(a2b_App_t *pApp_Info);
 A2B_PNP_RESULT a2b_pnp_StartBlindDiscovery(a2b_App_t *pApp_Info);
 A2B_PNP_RESULT a2b_pnp_PrepareSwctlforBD(a2b_App_t *pApp_Info);
A2B_PNP_RESULT a2b_pnp_RestoreSwctlAftrBD(a2b_App_t *pApp_Info, a2b_UInt8 nodesDiscovered);
#endif /* A2BPNP_LOCAL_H__ */
