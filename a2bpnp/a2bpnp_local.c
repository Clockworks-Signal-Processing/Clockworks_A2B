/*******************************************************************************
Copyright (c) 2022 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
*******************************************************************************

   Name       : a2bpnp_local.c

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
#include "a2bpnp_conn_wrapper.h"

a2b_App_t* gpApp_Info[A2B_CONF_MAX_NUM_MASTER_NODES];
/* Hold handles for all active Networks */
A2B_PNP_HANDLE hPnPList[A2B_PNP_CFG_MAX_NUM_A2B_CHAIN];

a2b_Byte gE2PromMemBuf[A2BAPP_E2PROM_BLOCK_MEMORY];
ADI_A2B_NETWORK_CONFIG gTgtProperties;

static void a2b_appCtxReset(a2b_App_t *pApp_Info);
static void a2b_app_handle_becovf(struct a2b_Timer* timer, a2b_Handle userData);
static a2b_HResult a2bapp_HandlePwrFaultAnomaly(a2b_App_t *pApp_Info, a2b_Interrupt* interrupt);
static a2b_HResult a2bapp_VMTRMonitor(struct a2b_StackContext *ctx, a2b_Int16 nodeAddr, a2b_UInt8 *minThres, a2b_UInt8 *maxThres);
A2B_PNP_RESULT deployConnections(a2b_App_t *pApp_Info, a2b_Char nw_chipids[A2B_CONF_MAX_NUM_SLAVE_NODES+1][6], a2b_Int8 nodeCnt);

#ifdef ENABLE_INTERRUPT_PROCESS
static void a2b_IntrptCallbk(a2b_App_t *pApp_Info)
{
	pApp_Info->bIntrptLatch = 1u;
}
#endif

/* Verify the handle */
A2B_PNP_RESULT a2b_VerifyPnPHandle(A2B_PNP_HANDLE hPnp)
{
	a2b_UInt8 nChainIndex;

	for(nChainIndex=0u; nChainIndex<A2B_PNP_CFG_MAX_NUM_A2B_CHAIN; nChainIndex++)
	{
		if(hPnp == hPnPList[nChainIndex])
		{
			return A2B_PNP_RESULT_SUCCESS;
		}
	}
	return A2B_PNP_RESULT_FAILED;
}



/*****************************************************************************
 *
 *  \b               a2b_init
 *
 *  This function initializes the elements of the application context structure.
 *
 *  \param           [in]    pApp_Info   Pointer to a2b_App_t instance
 *
 *  \pre             None
 *
 *  \post            None
 *
 *  \return          0 on Success
 *					 1 on Failure
 ******************************************************************************/
a2b_Int32 a2b_init(a2b_App_t *pApp_Info)
{
	a2b_Int32 nResult = 0;

	if (pApp_Info != NULL)
	{
		a2b_appCtxReset(pApp_Info);
		if(pApp_Info->bDebug)
		{
			A2B_APP_DBG_LOG("Reset context done \n\r");
		}

		/* Init stack functions */
		pApp_Info->stkFunc.stkInit = a2bpnp_stkinterface_Init;
		pApp_Info->stkFunc.stkGetBdd = a2bpnp_stkinterface_getBdd;
		pApp_Info->stkFunc.stkAlloc = a2bpnp_stkinterface_stkAlloc;
		pApp_Info->stkFunc.stkSetAcesInterface = a2bpnp_stkinterface_SetAcesInterface;
		pApp_Info->stkFunc.stkStartInitDisc = a2b_sendInitDiscoveryMessage;
		pApp_Info->stkFunc.stkStartPartialDIsc = a2b_sendPartialDiscoveryMessage;
		pApp_Info->stkFunc.stkSetNotification = a2bpnp_stkinterface_SetNotification;
		pApp_Info->stkFunc.stkStartTimerForBD = a2bpnp_stkinterface_StartTimerForBlindDiscovery;
		pApp_Info->stkFunc.stkSetupDiagnostics = a2b_setupPwrDiag;
		pApp_Info->stkFunc.stkStopStack = a2b_stop;

		pApp_Info->stkFunc.stkInit(pApp_Info);

			
		if(pApp_Info->bDebug)
		{
		    A2B_APP_DBG_LOG("Overall required data memory (in bytes):%d \n",(int)sizeof(a2b_App_t));
			A2B_APP_DBG_LOG("Size of BDD data(in bytes) %d \n\r",(int)sizeof(bdd_Network));
			A2B_APP_DBG_LOG("Size Device Info %d \n\r",(int)sizeof(a2bpnp_DeviceInfo));	
			A2B_APP_DBG_LOG("Size of BDD %d \n\r",(int)sizeof(bdd_Node));
			A2B_APP_DBG_LOG("Size of Group info Pernode %d \n\r",(int)sizeof(a2bpnp_GroupInfo) * A2B_PNP_CFG_MAX_NUM_GROUPS *2);	
			A2B_APP_DBG_LOG("Size of Periconfig Pernode %d \n\r",(int)sizeof(ADI_A2B_NODE_PERICONFIG));			
			
			A2B_APP_DBG_LOG("Size of network Info( in bytes) %d \n\r",(int)sizeof(a2bpnp_NetworkInfo));
			A2B_APP_DBG_LOG("Stack Init Done \n\r");
		
		}

#ifdef ENABLE_INTERRUPT_PROCESS
		(void)adi_a2b_EnablePinInterrupt(4, (void*)&a2b_IntrptCallbk, (a2b_UIntPtr)pApp_Info, 0u);
#endif
	}

	return nResult;
}

/*****************************************************************************
 *
 *  \b               a2b_load
 *
 *  This function loads network configuration into the Stack context
 *
 *  \param           [in]    pApp_Info   Pointer to a2b_App_t instance
 *
 *  \pre             None
 *
 *  \post            None
 *
 *  \return          0 on Success
 *					 1 on Failure
 ******************************************************************************/
a2b_Int32 a2b_load(a2b_App_t *pApp_Info)
{
	a2b_Int32 nResult = 0;

	/*
	 * Decode the network configuration and store it into the bdd element of
	 * the Application context.
	 */

	/* TO DO: Right now main node configuration and generic configuration for sub nodes are used from BCF
	 * Provide option to read them from EEPROM also */

	if(pApp_Info->pMainNodeCfg->mainNodeCfg == ADI_A2B_MAINNODE_CONFIG_BCF)
	{
		/* using BCF adi_a2b_busconfig.c */
		pApp_Info->pBusDescription = pApp_Info->pMainNodeCfg->pBcf;
		pApp_Info->pTargetProperties = &(pApp_Info->pMainNodeCfg->pBcf->sTargetProperties);
	}
	nResult = pApp_Info->stkFunc.stkGetBdd(pApp_Info);

	if(nResult == 1)
	{
		if(pApp_Info->bDebug)
		{
			A2B_APP_DBG_LOG("Failed to parse BDD\n\r");
		}
		return nResult;
	} 

	if(pApp_Info->bDebug)
	{
		A2B_APP_DBG_LOG("BCF parse done \n\r");
	}

	/* assign the peripheral configuration table (including audio host) */
	pApp_Info->ecb.palEcb.pAudioHostDeviceConfig = &pApp_Info->aPeriNetworkTable[0u];

	pApp_Info->stkFunc.stkAlloc(pApp_Info);

	/* No context, means failure */
	if (pApp_Info->ctx == A2B_NULL)
	{
		nResult = 1;
		if(pApp_Info->bDebug)
		{
			A2B_APP_DBG_LOG("Failed to allocate Stack. \n\r");
		}
	}
	else
	{
		if(pApp_Info->bDebug)
		{
			A2B_APP_DBG_LOG("Allocate Stack done \n\r");
		}
		pApp_Info->stkFunc.stkSetAcesInterface(pApp_Info);

	}

	return nResult;
}

/*****************************************************************************
 *
 *  \b               a2b_start
 *
 *  This function involves instructing the Stack to begin polling for interrupts,
 *  enabling sequence charts and debugging output, and hooking in application
 *  level call-backs.
 *
 *  \param           [in]    pApp_Info   Pointer to a2b_App_t instance
 *
 *  \pre             None
 *
 *  \post            None
 *
 *  \return          0 on Success
 *					 1 on Failure
 ******************************************************************************/
a2b_Int32 a2b_start(a2b_App_t *pApp_Info)
{
	a2b_Int32 nResult = 0;

	/* Enable sequence charts */

	pApp_Info->stkFunc.stkSetNotification(pApp_Info);

	/* Power diagnostic is set by default. Depending on SigmaStudio
	 * settings we should only allow re-discovery or not from a2bapp_onPowerFault() callback
	 */
	(void)pApp_Info->stkFunc.stkSetupDiagnostics(pApp_Info);

	return nResult;
}

/// @brief Preparation for discovery
/// @param pApp_Info 
/// @return 
a2b_Int32 a2b_discPrep(a2b_App_t *pApp_Info)
{
	a2b_Int32 nResult = 0; 
	do
	{

		/* Load BDD */
		nResult = a2b_load(pApp_Info);

		if (nResult != 0)
		{
			A2B_APP_ERR_LOG("ERROR Load \n\r");
			break;
		}

		/* Check parameters mandatory for PnP feature to work */
		nResult = a2b_checkBCFProp(pApp_Info);
		if (nResult != 0)
		{
			A2B_APP_ERR_LOG("Incorrect BCF properties \n\r");
			break;
		}
	}while(0);
	return nResult;
}

/*!****************************************************************************
 *
 *  \b               a2b_appCtxReset
 *
 *  This function initializes the elements of the application context structure.
 *
 *  \param           [in]    pApp_Info   Pointer to a2b_App_t instance
 *
 *  \pre             None
 *
 *  \post            None
 *
 *  \return          0 on Success
 *					 1 on Failure
 ******************************************************************************/
static void a2b_appCtxReset(a2b_App_t *pApp_Info)
{
	a2b_Int32 nIndex;
	/* Input flags */
	a2b_Bool	bDebug;
#ifdef A2B_FEATURE_SEQ_CHART
	a2b_Char* seqFile;
#endif
	a2b_Bool	bFrstTimeDisc;
	PNP_CALLBACK pfCallback;
	INTR_CALLBACK pfIntrCallback;
	ERR_CALLBACK pfErrCallback;
	a2bpnp_MainNodeProp* pMainNodeCfgTemp;
	a2b_UInt8 ignoreE2prom,bStreamByConnection;
	a2b_UInt8 bGlobalMuteDuringCrossbar;
	a2b_UInt8 maxNumSubnodes;
	a2b_UInt32 maxPWconsumption, maxCurrentConsumption;
	a2b_UInt8 i, j, BWErrorLevel, BWWarningLevel;
	a2b_UInt8 mainXcvrBInv, subXcvrBInv;


	/* take back up */
	nIndex = pApp_Info->ecb.palEcb.nChainIndex;
	bDebug = pApp_Info->bDebug;
	ignoreE2prom = pApp_Info->ignoreE2Prom ;
	maxNumSubnodes = pApp_Info->maxNumSubnodes;


#ifdef A2B_FEATURE_SEQ_CHART
	seqFile = pApp_Info->seqFile;
#endif	/* A2B_FEATURE_SEQ_CHART */

	/* Save the value of discovery try count & bFrstTimeDisc */
	bFrstTimeDisc = pApp_Info->bFrstTimeDisc;
	pfCallback = pApp_Info->pfCallback;
	pfIntrCallback = pApp_Info->pfIntrCallback;
	pfErrCallback = pApp_Info->pfErrCallback;
	pMainNodeCfgTemp = pApp_Info->pMainNodeCfg;
	bStreamByConnection =  pApp_Info->bStreamByConnection;
	bGlobalMuteDuringCrossbar = pApp_Info->bGlobalMuteDuringCrossbar;
	maxPWconsumption = pApp_Info->maxPWconsumption; 
	maxCurrentConsumption = pApp_Info->maxCurrentConsumption; 
	BWErrorLevel = pApp_Info->bwErrorLvl;
	BWWarningLevel = pApp_Info->bwWarningLvl;
	mainXcvrBInv = pApp_Info->mainXcvrBInv;
	subXcvrBInv = pApp_Info->subXcvrBInv;

	/* Clear out the Application's context container */
	(void)memset(pApp_Info, 0, sizeof(a2b_App_t));

	/*Restore the value of discovery try count & bFrstTimeDisc */
	pApp_Info->bFrstTimeDisc = bFrstTimeDisc;
	pApp_Info->pMainNodeCfg = pMainNodeCfgTemp;
	pApp_Info->bBlindDiscTimerExp = A2B_TRUE;
	pApp_Info->pfCallback = pfCallback;
	pApp_Info->pfIntrCallback = pfIntrCallback;
	pApp_Info->pfErrCallback = pfErrCallback;
	pApp_Info->ignoreE2Prom = ignoreE2prom;
	pApp_Info->bStreamByConnection =bStreamByConnection;
	/* Restore inputs */
	pApp_Info->ecb.palEcb.nChainIndex = nIndex;
	pApp_Info->bDebug 	=  bDebug;
	pApp_Info->bGlobalMuteDuringCrossbar = bGlobalMuteDuringCrossbar;
	pApp_Info->maxNumSubnodes = maxNumSubnodes;
	pApp_Info->maxPWconsumption = maxPWconsumption; 
	pApp_Info->maxCurrentConsumption = maxCurrentConsumption; 
	pApp_Info->bwErrorLvl = BWErrorLevel;
	pApp_Info->bwWarningLvl = BWWarningLevel;
	pApp_Info->mainXcvrBInv = mainXcvrBInv;
	pApp_Info->subXcvrBInv = subXcvrBInv;
#ifdef A2B_FEATURE_SEQ_CHART
	pApp_Info->seqFile 	=  seqFile;
#endif	/* A2B_FEATURE_SEQ_CHART */

	/*Intialize the Default crossbar register values*/
	for(i= 0; i < 32; i++)

	{
		pApp_Info->txxbarPrevMain[i] = i;
		for(j = 0;j < A2B_CONF_MAX_NUM_SLAVE_NODES; j++ )
		{
			pApp_Info->txxbarPrev[j][i] = i;
		} 
	}

	/* Store the Pointers */
	gpApp_Info[nIndex] = pApp_Info;

}


/* Apply an A2B_PNP_XCVRBINV_x setting to a CONTROL register value */
static a2b_UInt32 a2b_applyXcvrBInv(a2b_UInt32 control, a2b_UInt8 setting)
{
	if (setting == A2B_PNP_XCVRBINV_ON)
	{
		control |= A2B_BITM_CONTROL_XCVRBINV;
	}
	else if (setting == A2B_PNP_XCVRBINV_OFF)
	{
		control &= ~A2B_BITM_CONTROL_XCVRBINV;
	}
	return control;
}

a2b_Int32 a2b_checkBCFProp(a2b_App_t * pApp_Info)
{
	a2b_Int32 nResult = 0, idx;
	/* Sub-nodes start from the main node's configured CONTROL, before its own override */
	a2b_UInt32 cfgControl = pApp_Info->bdd.nodes[0].ctrlRegs.control;
	a2b_UInt32 subControl = a2b_applyXcvrBInv(cfgControl, pApp_Info->subXcvrBInv);

	pApp_Info->bdd.nodes[0].ctrlRegs.control = a2b_applyXcvrBInv(cfgControl, pApp_Info->mainXcvrBInv);
	A2B_APP_DBG_LOG("CONTROL: main 0x%02X, sub-nodes 0x%02X \n\r",
		(unsigned int)pApp_Info->bdd.nodes[0].ctrlRegs.control, (unsigned int)subControl);

	if((pApp_Info->pTargetProperties->bAutoDiscCriticalFault == A2B_DISABLED)  ||
				(pApp_Info->pTargetProperties->bAutoRediscOnFault == A2B_DISABLED) ||
				(pApp_Info->pTargetProperties->bLineDiagnostics != 1u)              ||
				(pApp_Info->pTargetProperties->nAttemptsCriticalFault == 0u)        ||
				(pApp_Info->bdd.policy.bEnablePartialDisc != A2B_TRUE))
		{
			pApp_Info->pTargetProperties->bAutoDiscCriticalFault = A2B_ENABLED;
			pApp_Info->pTargetProperties->bAutoRediscOnFault = A2B_ENABLED;
			pApp_Info->pTargetProperties->bLineDiagnostics = 1;
			pApp_Info->pTargetProperties->nAttemptsCriticalFault = 5;
			pApp_Info->bdd.policy.bEnablePartialDisc = A2B_TRUE;
			nResult = 0;
		}

	for(idx = 1; idx < (a2b_Int32)A2B_CONF_MAX_NUM_SLAVE_NODES+1;idx++)
	{
		pApp_Info->bdd.nodes[idx].ctrlRegs.respcycs = pApp_Info->bdd.nodes[0].ctrlRegs.respcycs - (idx-1)*4;   // changed from 5 -> 4 as per ADI suggestion
		pApp_Info->bdd.nodes[idx].nodeDescr.vendor = 0xAD;
		pApp_Info->bdd.nodes[idx].nodeDescr.product = 0x37;
		pApp_Info->bdd.nodes[idx].nodeDescr.version = 0x21;
		pApp_Info->bdd.nodes[idx].nodeSetting.eHighPwrSwitchCfg = 4;
		pApp_Info->bdd.nodes[idx].ctrlRegs.has_control = pApp_Info->bdd.nodes[0].ctrlRegs.has_control;
		pApp_Info->bdd.nodes[idx].ctrlRegs.control = subControl;
		pApp_Info->bdd.nodes[idx].ctrlRegs.swctl2 = 4;
		pApp_Info->bdd.nodes[idx].ctrlRegs.swctl5 = 1;
		pApp_Info->bdd.nodes[idx].nodeDescr.btwoStepDisc = pApp_Info->bTwoStepDisc;
	}

	/* Clear slot here*/
	AutoCalcSlot(pApp_Info);


	memset(&(pApp_Info->aPeriNetworkTable[1]), 0 , sizeof(ADI_A2B_NODE_PERICONFIG)*A2B_CONF_MAX_NUM_SLAVE_NODES); 

	return nResult;
}

/*!****************************************************************************
 *
 *  \b               a2bapp_onInterrupt
 *
 *  The handler for A2B interrupt notifications.
 *
 *  \param           [in]    msg         The A2B interrupt notification message.
 *
 *  \param           [in]    userData    User data associated with the
 *                                       notification registration.
 *
 *  \pre             None
 *
 *  \post            None
 *
 *  \return          None
 *
 ******************************************************************************/
void a2bapp_onInterrupt(struct a2b_Msg* msg, a2b_Handle userData)
{
	a2b_Interrupt* 	interrupt;
	a2b_App_t 		*pApp_Info  = (a2b_App_t *)userData;
	INTR_CALLBACK pfIntrCallback;

	if (msg != NULL)
	{
		interrupt = a2b_msgGetPayload(msg);

		/* Third parameter is the sub node address which is dropped */
		pfIntrCallback = (INTR_CALLBACK)pApp_Info->pfIntrCallback;
		(void)pfIntrCallback(pApp_Info, (a2b_UInt8)interrupt->intrType, (void*)(a2b_UIntPtr)(interrupt->nodeAddr));

		(void)a2bapp_HandlePwrFaultAnomaly(pApp_Info, interrupt);

		if (pApp_Info->bDebug == A2B_TRUE)
		{
			if (interrupt != NULL)
			{

				A2B_APP_DBG_LOG("\n\rINTERRUPT: intrType=%u nodeAddr=%d", interrupt->intrType, interrupt->nodeAddr);
				/* Add your code to handle interrupt */
			}
			else
			{

				A2B_APP_DBG_LOG("\n\rINTERRUPT: failed to retrieve payload");

			}
		}
	}
}

/*!****************************************************************************
 *
 *  \b               a2bapp_HandlePwrFaultAnamoly
 *
 *  A specific handler for handling A2B interrupts.
 *
 *  \param           [in]    pApp_Info		Application Context Info
 *
 *  \param           [in]    a2b_Interrupt	Notification payload for A2B_MSGNOTIFY_INTERRUPT notifications.
 *
 *  \pre             None
 *
 *  \post            None
 *
 *  \return          None
 *
 ******************************************************************************/
static a2b_HResult a2bapp_HandlePwrFaultAnomaly(a2b_App_t *pApp_Info, a2b_Interrupt* interrupt)
{
	a2b_UInt8 	nValVmtrMxstat = 0U, nValVmtrMnstat = 0U;
	a2b_HResult nRet = 0U;

	/* VBUS max error along with VMTR interrupt */
	if (interrupt->intrType == A2B_ENUM_INTTYPE_VMTR)
	{
		nRet |= a2bapp_VMTRMonitor(pApp_Info->ctx, A2B_NODEADDR_MASTER, &nValVmtrMnstat, &nValVmtrMxstat);
		if ((nValVmtrMxstat & 0x02U) == 0x02U)
		{
			/* Rediscover the network */
		}
	}

	return (nRet);
}

/*!****************************************************************************
 *
 *  \b               a2bapp_VMTRMonitor
 *
 *  API for VMTR monitor to check if the monitored voltages are beyond the min and max thresholds.
 *  This API can be periodically called to determine voltage related errors.
 *  This API assumes that all the VMTR threshold registers are already statically configured.
 *
 *  \param           [in]    pApp_Info		Application Context Info
 *
 *  \param           [in]    nodeAddr       Node address of the node
 *
 *  \param           [out]   minThres		Bit encoded minimum voltage errors
 *  										bit 0: VIN; bit 1: VBUS; bit 2: VIOVDD; bit 3: VTRXVDD;
 *  										bit 4: VDVDD; bit 5: VVBUS - VISENSEP; bit 6: VISENSEN - VVSENSEN;
 *
 *  \param           [out]   maxThres		Bit encoded maximum voltage errors
 *  										bit 0: VIN; bit 1: VBUS; bit 2: VIOVDD; bit 3: VTRXVDD;
 *  										bit 4: VDVDD; bit 5: VVBUS - VISENSEP; bit 6: VISENSEN - VVSENSEN;
 *
 *  \pre             None
 *
 *  \post            None
 *
 *  \return          0 on Success
 *					 1 on Failure
 *
 ******************************************************************************/
static a2b_HResult a2bapp_VMTRMonitor(struct a2b_StackContext *ctx, a2b_Int16 nodeAddr, a2b_UInt8 *minThres, a2b_UInt8 *maxThres)
{
	a2b_HResult nRet = 0U;
	a2b_UInt8   nReg;

	nRet |= a2b_AppWriteReg(ctx, nodeAddr, A2B_REG_MMRPAGE, 0x01U);
	nReg = (A2B_REG_VMTR_MXSTAT & 0xFFu);
	nRet |= a2b_AppReadReg (ctx, nodeAddr, nReg, (a2b_UInt32*)maxThres);
	nReg = (A2B_REG_VMTR_MNSTAT & 0xFFu);
	nRet |= a2b_AppReadReg (ctx, nodeAddr, nReg, (a2b_UInt32*)minThres);
	nRet |= a2b_AppWriteReg(ctx, nodeAddr, A2B_REG_MMRPAGE, 0x00U);

	return (nRet);
}

/*!****************************************************************************
 *
 *  \b               a2bapp_onNodeDiscovery
 *
 *  The handler for A2B node level discovery notifications.
 *
 *  Unlike a2bapp_onDiscoveryComplete, this is an OPTIONAL callback which user may register to get notification upon each node discovery or node authentication failed.
 *
 *  \param           [in]    msg         The A2B node level discovery notification message.
 *
 *  \param           [in]    userData    User data associated with the
 *                                       notification registration.
 *
 *  \pre             None
 *
 *  \post            None
 *
 *  \return          None
 *
 ******************************************************************************/
void a2bapp_onNodeDiscovery(struct a2b_Msg* msg, a2b_Handle userData)
{

	a2b_Int8 				nRes = 0 ;
	a2b_App_t *pApp_Info        =   userData;
	a2b_Nodedscvry* dscvrdNodeMsg;
	PNP_CALLBACK pfCallback;
	a2b_Int16 nodeAddr;
	a2b_Char chipid[6];
	a2bpnp_NetworkInfo *PnpNWInfo;
	PnpNWInfo = &pApp_Info->PnpNWInfo;
	a2bpnp_NodeRejectionInfo rejectInfo;
	a2b_Int32 node;
	a2b_UInt32 usage;

	if (msg != NULL)
	{
		/* details of the currently discovered node */
		dscvrdNodeMsg = a2b_msgGetPayload(msg);

		nodeAddr = dscvrdNodeMsg->nodeAddr; /* this will number of slave node discovered */

		a2b_pnp_RestoreSwctlAftrBD(pApp_Info, nodeAddr );

		if(pApp_Info->maxNumSubnodes < nodeAddr)
		{
			PnpNWInfo->nDeviceCount = (uint8_t)nodeAddr-1u;
			pfCallback = (PNP_CALLBACK)pApp_Info->pfCallback;
			rejectInfo.NodeId = nodeAddr-1;
			rejectInfo.rejection = A2B_PNP_EXCEED_MAX_NODE_COUNT;
			(void)pfCallback(pApp_Info, (a2b_UInt32)A2B_PNP_NODE_REJECTED, (void*)(&rejectInfo));
			nRes = 1;
		}
		else
		{
			/* let us get the bandwidth usage*/
			getNwBw(pApp_Info, &node, &usage);

			if(usage > pApp_Info->bwErrorLvl)
			{
				PnpNWInfo->nDeviceCount = (uint8_t)nodeAddr-1u;
				pfCallback = (PNP_CALLBACK)pApp_Info->pfCallback;
				rejectInfo.NodeId = nodeAddr-1;
				rejectInfo.rejection = A2B_PNP_EXCEED_BW_BUDGET;
				(void)pfCallback(pApp_Info, (a2b_UInt32)A2B_PNP_NODE_REJECTED, (void*)(&rejectInfo));
				nRes = 1;
			}
			else if(usage > pApp_Info->bwWarningLvl)
			{
				pfCallback = (PNP_CALLBACK)pApp_Info->pfCallback;
				(void)pfCallback(pApp_Info, (a2b_UInt32)A2B_PNP_BW_WARNING_LEVEL, &node);
			}
		}
		
		if( (pApp_Info->ignoreE2Prom == A2B_FALSE) && (nRes == 0))
		{
			/* Node Discovered event - PnP Module reads Module Info in this event */
			nRes = a2b_ReadVerify_ModuleInfo(pApp_Info, nodeAddr);

			if(nRes != 0)
			{
				PnpNWInfo->nDeviceCount = (uint8_t)nodeAddr-1u;
				if(pApp_Info->bDebug)
				{
					A2B_APP_DBG_LOG("Node Disconnected/Rejected: %d",nodeAddr-1);
				}

				pfCallback = (PNP_CALLBACK)pApp_Info->pfCallback;
				rejectInfo.NodeId = nodeAddr-1;
				rejectInfo.rejection = A2B_PNP_INVALID_MODULE_HEADER;
				(void)pfCallback(pApp_Info, (a2b_UInt32)A2B_PNP_NODE_REJECTED, (void*)(&rejectInfo));
			}
			else
			{
				pfCallback = (PNP_CALLBACK)pApp_Info->pfCallback;				
				(void)pfCallback(pApp_Info, (a2b_UInt32)A2B_PNP_NEW_NODE_CONNECTED, (void*)(a2b_UIntPtr)(nodeAddr-1));
			}
		}

		if(pApp_Info->ignoreE2Prom == A2B_TRUE)
		{
			PnpNWInfo->nDeviceCount = (uint8_t)nodeAddr;
			pfCallback = (PNP_CALLBACK)pApp_Info->pfCallback;				
			(void)pfCallback(pApp_Info, (a2b_UInt32)A2B_PNP_NEW_NODE_CONNECTED, (void*)(a2b_UIntPtr)(nodeAddr-1));
		}

		if ((dscvrdNodeMsg!= NULL) && (nRes == 0))
		{

		    /* CRITICAL: Populate the further action which is required to be taken by stack
		     * Set,
		     * 		bContinueDisc to A2B_TRUE if required to proceed with discovery process
		     * 		bContinueDisc to A2B_FALSE if required to end the discovery process
		     */
		    dscvrdNodeMsg->bContinueDisc = A2B_TRUE;
			readChipId(pApp_Info, (nodeAddr-1), chipid);
			addNewNodeChipId(getChainIdx(pApp_Info) ,(nodeAddr-1), chipid, pApp_Info->bdd.nodes_count);
			if(IsNodePartofAnyConnection(getChainIdx(pApp_Info), (nodeAddr-1)))
			{
				pfCallback = (PNP_CALLBACK)pApp_Info->pfCallback;
				(void)pfCallback(pApp_Info, (a2b_UInt32)A2B_PNP_CONN_IMPORT, NULL);
			}
		}
		else
		{
			if(pApp_Info->bDebug)
			{
				A2B_APP_DBG_LOG("NODE DISCOVERY: failed to retrieve payload\n\r");
			}
			dscvrdNodeMsg->bContinueDisc = A2B_FALSE;
		}

	}
}

/*!****************************************************************************
 *
 *  \b               a2bapp_onPowerFault
 *
 *  The handler which receives power fault diagnostic notifications.
 *
 *  \param           [in]    msg         The diagnostic notification.
 *
 *  \param           [in]    userData    Not used
 *
 *
 *
 *  \pre             None
 *
 *  \post            None
 *
 *  \return          None
 *
 ******************************************************************************/
void a2bapp_onPowerFault(struct a2b_Msg *msg, a2b_Handle userData)
{
	a2b_PowerFault 	*fault;
	a2b_App_t 		*pApp_Info = (a2b_App_t*)userData;

	A2B_UNUSED(userData);

	if ( (pApp_Info->ePnPModuleState == A2B_PNP_BLIND_DISC_IN_PROGRESS) || (pApp_Info->ePnPModuleState == A2B_PNP_INIT))
	{

		A2B_APP_DBG_LOG("\n\r Line fault During Discovery: ");
	}
	else
	{

		A2B_APP_DBG_LOG("\n\r Post Discovery Line fault: ");
	}
	
	pApp_Info->moduleStateduringFault = pApp_Info->ePnPModuleState;

	if (msg != NULL)
	{
		fault = (a2b_PowerFault *)a2b_msgGetPayload(msg);
		if (fault != NULL)
		{
			if (A2B_SUCCEEDED(fault->status))
			{
				switch (fault->intrType)
				{
				case A2B_ENUM_INTTYPE_PWRERR_CS_GND:
					pApp_Info->faultStatus = "Cable Shorted to GND";
					pApp_Info->faultCode = A2B_ENUM_INTTYPE_PWRERR_CS_GND;
					pApp_Info->ePnPModuleState = A2B_PNP_NETWORK_HW_FAULT;
					/* Should be Reported to Application */

					break;
				case A2B_ENUM_INTTYPE_PWRERR_CS_VBAT:
					pApp_Info->faultStatus = "Cable Shorted to VBat";
					pApp_Info->faultCode = A2B_ENUM_INTTYPE_PWRERR_CS_VBAT;
					pApp_Info->ePnPModuleState = A2B_PNP_NETWORK_HW_FAULT;
					/* Should be Reported to Application */

					break;
				case A2B_ENUM_INTTYPE_PWRERR_CS:
					pApp_Info->faultStatus = "Cable Shorted Together";
					pApp_Info->faultCode = A2B_ENUM_INTTYPE_PWRERR_CS;
					pApp_Info->ePnPModuleState = A2B_PNP_NETWORK_HW_FAULT;
					/* Should be Reported to Application */

					break;
				case A2B_ENUM_INTTYPE_PWRERR_CDISC:
					pApp_Info->faultStatus = "Cable Disconnected or Open Circuit";
					pApp_Info->faultCode = A2B_ENUM_INTTYPE_PWRERR_CDISC;
					/* Expected line fault */

					break;
				case A2B_ENUM_INTTYPE_PWRERR_CREV:
					pApp_Info->faultStatus = "Cable Reverse Connected or Wrong Port";
					pApp_Info->faultCode = A2B_ENUM_INTTYPE_PWRERR_CREV;
					/* Should we treat as hw fault ? */

					break;
				case A2B_ENUM_INTTYPE_PWRERR_CDISC_REV:
					pApp_Info->faultStatus = "Cable Open, Reverse Connected or Wrong Port";
					pApp_Info->faultCode = A2B_ENUM_INTTYPE_PWRERR_CREV;
					/* Expected line fault */

					break;
				case A2B_ENUM_INTTYPE_PWRERR_FAULT:
					pApp_Info->faultStatus = "Indeterminate Fault";
					pApp_Info->faultCode = A2B_ENUM_INTTYPE_PWRERR_CREV;
					pApp_Info->ePnPModuleState = A2B_PNP_NETWORK_HW_FAULT;
					/* Should be Reported to Application */

					break;
				case A2B_ENUM_INTTYPE_PWRERR_NLS_GND:
					pApp_Info->faultStatus = "Non-Localized Short to GND";
					pApp_Info->faultCode = A2B_ENUM_INTTYPE_PWRERR_NLS_GND;
					pApp_Info->ePnPModuleState = A2B_PNP_NETWORK_HW_FAULT;
					/* Should be Reported to Application */

					break;
				case A2B_ENUM_INTTYPE_PWRERR_NLS_VBAT:
					pApp_Info->faultStatus = "Non-Localized Short to VBat";
					pApp_Info->faultCode = A2B_ENUM_INTTYPE_PWRERR_NLS_VBAT;
					pApp_Info->ePnPModuleState = A2B_PNP_NETWORK_HW_FAULT;
					/* Add your code to handle fault */

					break;
				case A2B_ENUM_INTTYPE_STRTUP_ERR_RTF:
					pApp_Info->faultStatus = "Startup Error - Return to Factory";
					pApp_Info->faultCode = A2B_ENUM_INTTYPE_STRTUP_ERR_RTF;
					pApp_Info->ePnPModuleState = A2B_PNP_NETWORK_HW_FAULT;
					/* Add your code to handle fault */

					break;
				default:
					pApp_Info->faultStatus = "Unknown";
					pApp_Info->faultCode = 0xFF;
					pApp_Info->ePnPModuleState = A2B_PNP_NETWORK_HW_FAULT;
					/* Add your code to handle fault */

					break;
				}

				if(pApp_Info->bDebug)
				{
					A2B_APP_DBG_LOG("\nfault->intrType : %d\n", fault->intrType);
				}

				pApp_Info->faultNode = fault->faultNode;

				if (fault->faultNode < 0)
				{
					if(fault->faultNode == A2B_NODEADDR_NOTUSED)
					{
						if(pApp_Info->bDebug)
						{
							A2B_APP_DBG_LOG("Fault will not be localized because of AD243x High / Medium power bus powered nodes or AD2430 / AD2438 Main in the network: ");
						}
					}
					else
					{
						pApp_Info->bMasterPowerFault = A2B_TRUE;
						if(pApp_Info->bDebug)
						{
							A2B_APP_DBG_LOG("Fault detected on Master node: ");
						}
					}
				}
				else
				{
					if(pApp_Info->bDebug)
					{
						A2B_APP_DBG_LOG("Fault detected on Slave node %d: ", fault->faultNode);
					}
				}

				if(pApp_Info->bDebug)
				{
					A2B_APP_DBG_LOG("%s\n\r", pApp_Info->faultStatus);
				}

			}
			else
			{
				if(pApp_Info->bDebug)
				{
					A2B_APP_DBG_LOG("\n\r Power diagnostic failure ");
				}
			}
		}
	}

}



/*!****************************************************************************
 *
 *  \b               a2b_app_handle_becovf
 *
 *  The routine to periodically reset BECNT and BECOVF registers
 *
 *  \param           [in]        timer		Timer pointer
 *
 *  \param           [in]        userData	User data populated in timer callback function.
 *
 *  \post            None
 *
 *  \return          None
 *
 ******************************************************************************/
static void a2b_app_handle_becovf(struct a2b_Timer* timer, a2b_Handle userData)
{
	a2b_App_t* pApp_Info;
	pApp_Info = (a2b_App_t*)(timer->userData);
	A2B_UNUSED(userData);

	/* Reset the BECNT register for every call back of the timer */
	(void)a2b_diagWriteReg(pApp_Info->ctx, A2B_NODEADDR_NOTUSED, A2B_REG_BECNT, A2B_REG_BECNT_RESET);


}

/*****************************************************************************
 *
 *  \b               a2b_app_handle_BlindDiscovery
 *
 *  The routine to periodically initiate the blind discovery for PnP feature
 *
 *  \param           [in]        timer		Timer pointer
 *
 *  \param           [in]        userData	User data populated in timer callback function.
 *
 *  \post            None
 *
 *  \return          None
 *
 ******************************************************************************/
void a2b_app_handle_BlindDiscovery(struct a2b_Timer* timer, a2b_Handle userData)
{

	a2b_App_t* pApp_Info;
	pApp_Info = (a2b_App_t*)(timer->userData);
	A2B_UNUSED(userData);

	pApp_Info->bBlindDiscTimerExp = A2B_TRUE;
}




/*****************************************************************************
 *
 *  \b               a2b_discover
 *
 *  This function sends a discovery initating message and ticks till completion.
 *  When using Super BCF this will iterate through the BCFs to complete discovery.
 *
 *  \param           [in]    pApp_Info   Pointer to a2b_App_t instance
 *
 *  \pre             None
 *
 *  \post            None
 *
 *  \return          0 on Success
 *					 1 on Failure
 ******************************************************************************/
a2b_Int32 a2b_discover(a2b_App_t *pApp_Info)
{
	a2b_HResult result = 0;

	result = pApp_Info->stkFunc.stkStartInitDisc(pApp_Info);

	if (result != 0u)
	{

		if(pApp_Info->bDebug)
		{
			A2B_APP_DBG_LOG("Error while sending discovery message \n\r");
		}
		return 1;
	}
	if(pApp_Info->bDebug)
	{
		A2B_APP_DBG_LOG("Triggering discovery... \r\n");
	}

	/* Clear any outstanding interrupts */
	pApp_Info->bIntrptLatch = 1u;

#ifdef ENABLE_INTERRUPT_PROCESS
		(void)a2b_processIntrpt(pApp_Info);
#endif

	return (a2b_Int32)result;
}


/*****************************************************************************
 *
 *  \b               a2b_reset
 *
 *  This function does A2B network soft reset
 *
 *  \param           [in]    pApp_Info   Pointer to a2b_App_t instance
 *
 *  \pre             None
 *
 *  \post            None
 *
 *  \return          0 on Success
 *					 1 on Failure
 ******************************************************************************/
a2b_UInt32 a2b_reset(a2b_App_t *pApp_Info)
{
	a2b_HResult nRet = 0U;

    nRet = a2b_AppWriteReg(pApp_Info->ctx, A2B_NODEADDR_MASTER , A2B_REG_CONTROL, A2B_ENUM_CONTROL_RESET_PE);
    if ( A2B_FAILED(nRet) )
    {
    	nRet = 1U;
    }

    return(nRet);
}


/*This function reads the Vendor Id register of all A2B nodes discovered
 *and declares a bus drop at a particular node where the read value is not the expected */
a2b_HResult a2b_AppDetectBusDrop(a2b_App_t *pApp_Info)
{
	a2b_Int16 i, temp;
	a2b_UInt8 nVal;
	a2b_HResult nRet = 0;
	PNP_CALLBACK pfCallback;
	a2bpnp_NetworkInfo *PnpNWInfo = &pApp_Info->PnpNWInfo;

	//By default last node is always fault node
	pApp_Info->faultNode = pApp_Info->bdd.nodes_count - 1;
	pApp_Info->bBusDropDetected = A2B_FALSE;
	/* Let us detect bus drop fault */
	for (i = 0; i < pApp_Info->bdd.nodes_count; i++)
	{
		nVal = 0u;
		nRet = a2b_AppReadReg(pApp_Info->ctx, (i - 1), A2B_REG_VENDOR, (a2b_UInt32*)&nVal);
		if (nVal != 0xADu)
		{
			pApp_Info->faultNode = (i - 2); /* TODO: For master fault, this will capture the wrong value */
			pApp_Info->bBusDropDetected = A2B_TRUE;

			/* Do this only when there is at last one sub node in the NW */
			if(pApp_Info->bdd.nodes_count >= 1u)
			{
				/* Actual Node drop has occurred and streams shall be calculated again
				 * Notify application about the node drop */

				/* Node Disconnect event - PnP Module updates the device count and the Audio Routing in this call */
				PnpNWInfo->nDeviceCount = (a2b_UInt8)(i-1);
				UpdateAudioRt(pApp_Info, i-1);
				temp = pApp_Info->bdd.nodes_count;
				pApp_Info->bdd.nodes_count = 1 + PnpNWInfo->nDeviceCount;
				
				removeNodeId(getChainIdx(pApp_Info), pApp_Info->faultNode + 1, pApp_Info->faultNode + 2);

				(void)a2b_pnp_ApplyRouting((A2B_PNP_HANDLE)pApp_Info);
				pApp_Info->bdd.nodes_count = temp;

				/* TODO: Update PnpNWInfo Device Info */

				/* Third parameter is the sub node address which is dropped */
				pfCallback = (PNP_CALLBACK)pApp_Info->pfCallback;
				(void)pfCallback(pApp_Info, (a2b_UInt32)A2B_PNP_NODE_DISCONNECTED, (void*)(a2b_UIntPtr)(i-1));
			}

			/* A2B_APP_DBG_LOG("\n\rBus Drop Detected @ Node: %d", i-2); */
			break;
		}
	}
	return (nRet);
}

/*!****************************************************************************
 *
 *  \b               a2bapp_onDiscoveryComplete
 *
 *  The handler which receives the response to the request to discover the
 *  A2B network. This notification is received once complete discovery of the A2B network is complete or upon a fault condition
 *
 *  \param           [in]    msg         The response message to the network
 *                                       discovery request.
 *
 *  \param           [in]    isCancelled An indication of whether the original
 *                                       request was cancelled before it was
 *                                       completed.
 *
 *  \pre             None
 *
 *  \post            None
 *
 *  \return          None
 *
 ******************************************************************************/
void a2bapp_onDiscoveryComplete(struct a2b_Msg* msg, a2b_Bool isCancelled)
{
	a2b_NetDiscovery* results;
	a2b_Char chipid[6];

	if ( A2B_NULL == msg)
	{
		/* This should *never* happen */
		A2B_APP_ERR_LOG("Error: no response message for network discovery\n\r");
	}
	else
	{	
		a2b_App_t *pApp_Info = a2b_msgGetUserData(msg);	
		if (isCancelled == A2B_TRUE)
		{
			if(pApp_Info->bDebug)
			{
				A2B_APP_DBG_LOG("Discovery request was cancelled.\n\r");
			}
		}
		else
		{
			results = (a2b_NetDiscovery*)a2b_msgGetPayload(msg);
			if (A2B_SUCCEEDED(results->resp.status))
			{
				pApp_Info->nodesDiscovered = (a2b_UInt8)results->resp.numNodes;

				readChipId(pApp_Info, A2B_NODEADDR_MASTER, chipid);
				addNewNodeChipId(getChainIdx(pApp_Info) , A2B_NODEADDR_MASTER, chipid, 1);

				/* When line fault monitoring is enabled, Allocate a timer to periodically clear BECNT register to reset the error counter */
				if ((pApp_Info->bBecovfTimerEnable == A2B_FALSE))
				{

					pApp_Info->hTmrToHandleBecovf = a2b_timerAlloc(pApp_Info->ctx, (a2b_TimerFunc)a2b_app_handle_becovf, (a2b_Handle)(pApp_Info));
					pApp_Info->bBecovfTimerEnable = A2B_TRUE;

					a2b_timerSet(pApp_Info->hTmrToHandleBecovf, A2B_APP_TMRTOHANDLE_BECOVF_AFTER_INTERVAL, A2B_APP_TMRTOHANDLE_BECOVF_REPEAT_INTERVAL);
					a2b_timerStart(pApp_Info->hTmrToHandleBecovf);

				}
				pApp_Info->ePnPModuleState = A2B_PNP_NETWORK_MNG;

			}
			else if ((results->resp.status & 0xFFFFu) == A2B_EC_PERMISSION)
			{
				/* Basic authentication failure */
				pApp_Info->faultNode = (a2b_Int16)(results->resp.numNodes + 1u);
				pApp_Info->ePnPModuleState = A2B_PNP_BASIC_AUTH_FAILURE;
				if(pApp_Info->bDebug)
				{
					A2B_APP_DBG_LOG("Node Authentication failed\n\r");
				}

			}
			else if((results->resp.status & 0xFFFFu) == A2B_EC_MSTR_NOT_RUNNING)
			{
				/* No master running interrupt */
				pApp_Info->ePnPModuleState = A2B_PNP_MAIN_PLL_NOT_LOCKED;
				if(pApp_Info->bDebug)
				{
					A2B_APP_DBG_LOG("Master running interrupt not detected. Possible SYNC issues. \n\r");
				}
			}
			else
			{
					/* avoid warning */
			}


		}
	}
}

/*!****************************************************************************
 *
 *  \b               a2bapp_onBlindDiscoveryComplete
 *
 *  The handler which receives the response to the request to discover the
 *  A2B network. This notification is received once complete discovery of the A2B network is complete or upon a fault condition
 *
 *  \param           [in]    msg         The response message to the network
 *                                       discovery request.
 *
 *  \param           [in]    isCancelled An indication of whether the original
 *                                       request was cancelled before it was
 *                                       completed.
 *
 *  \pre             None
 *
 *  \post            None
 *
 *  \return          None
 *
 ******************************************************************************/
void a2bapp_onBlindDiscoveryComplete(struct a2b_Msg* msg, a2b_Bool isCancelled)
{
	a2b_NetDiscovery* results;

	PNP_CALLBACK pfCallback;


	if ( A2B_NULL == msg)
	{

		/* This should *never* happen */
		A2B_APP_ERR_LOG("Error: no response message for network discovery\n\r");
	}
	else
	{
		a2b_App_t *pApp_Info = a2b_msgGetUserData(msg);

		if (isCancelled == A2B_TRUE)
		{
			if(pApp_Info->bDebug)
			{
				A2B_APP_DBG_LOG("Discovery request was cancelled.\n\r");
			}
		}
		else
		{
			results = (a2b_NetDiscovery*)a2b_msgGetPayload(msg);
			if (A2B_SUCCEEDED(results->resp.status))
			{
				/* Discovered a new node */
				pApp_Info->nodesDiscovered = (a2b_UInt8)results->resp.numNodes;
				pApp_Info->nwPerformance.timeToDiscMs[pApp_Info->bdd.nodes_count - 2] = pApp_Info->pal.timerGetSysTime() - pApp_Info->currTime;

				if(results->resp.numNodes != 0)
				{
					/* Callback to application to provide the Audio stream */
					pfCallback = (PNP_CALLBACK)pApp_Info->pfCallback;
					(void)pfCallback(pApp_Info, (a2b_UInt32)A2B_PNP_BD_COMPLETE_WITH_NEW_NODE, NULL);
				}


			}
			else if(  ((results->resp.status & 0xFFu) == A2B_EC_BUSY) || ((results->resp.status & 0xFFu) == A2B_EC_CANCELLED) ||
					((results->resp.status & 0xFFu) == A2B_EC_DISCOVERY_PWR_FAULT) || ((results->resp.status & 0xFFu) == A2B_EC_DISCOVERY_FAILURE) )
			{
				/* This is okay, no new node is discovered */
				/* Lets reset the node count */
				pApp_Info->nodesDiscovered = (a2b_UInt8)results->resp.numNodes;
				pApp_Info->bdd.nodes_count--;
				
			}
			else if (((results->resp.status & 0xFFFFu) == A2B_EC_PERMISSION))
			{
				/* Basic authentication failure */
				pApp_Info->faultNode = (a2b_Int16)(results->resp.numNodes + 1u);
				pApp_Info->bdd.nodes_count--;
				pApp_Info->ePnPModuleState = A2B_PNP_BASIC_AUTH_FAILURE;
				if(pApp_Info->bDebug)
				{
					A2B_APP_DBG_LOG("Node Authentication failed\n\r");
				}

			}
			else
			{
				/* General failure TBD */
				pApp_Info->bdd.nodes_count--;
			}

		}
		if(pApp_Info->ePnPModuleState == A2B_PNP_BLIND_DISC_IN_PROGRESS)
		{
			a2b_pnp_RestoreSwctlAftrBD(pApp_Info, pApp_Info->nodesDiscovered);
			pApp_Info->ePnPModuleState = A2B_PNP_NETWORK_MNG;
		}
	}
}


void a2bapp_onI2CFailure(struct a2b_Msg* msg, a2b_Handle userData)
{
	a2b_I2CError* I2CFailMsg;
	a2b_App_t *pApp_Info = userData;
	PNP_CALLBACK pfCallback;
	a2bpnp_NetworkInfo *PnpNWInfo = &pApp_Info->PnpNWInfo;

	if (msg != NULL)
	{
		/* details of the currently discovered node */
		I2CFailMsg = a2b_msgGetPayload(msg);
		PnpNWInfo->nDeviceCount = (uint8_t)I2CFailMsg->nodeAddr;
		pfCallback = (PNP_CALLBACK)pApp_Info->pfCallback;
		(void)pfCallback(pApp_Info, (a2b_UInt32)A2B_PNP_I2C_FAILURE, (void*)(a2b_UIntPtr)PnpNWInfo->nDeviceCount);

	}
}

/*!****************************************************************************
 *
 *  \b               a2b_pnp_StreamVerification
 *
 *  Verifies the audio stream being configured by the user
 *
 *  \param           [in]    NWStream     The stream configured by user
 *
 *  \pre             None
 *
 *  \post            None
 *
 *  \return          0 on Success
 *					 1 on Failure
 *
 ******************************************************************************/
a2b_HResult a2b_pnp_StreamVerification(A2B_PNP_HANDLE hPnp, a2bpnp_StreamInfo NWStream)
{
	uint8_t bFlag = false;
	uint8_t i, nDest;
	a2b_App_t * pApp_Info;
	a2bpnp_NetworkInfo *PnpNWInfo;

	/* Verify PnP handle */
	if(a2b_VerifyPnPHandle(hPnp) == A2B_PNP_RESULT_FAILED)
	{
		return A2B_PNP_RESULT_FAILED;
	}
	pApp_Info = (a2b_App_t *)hPnp;
	PnpNWInfo = &pApp_Info->PnpNWInfo;

	if(NWStream.SourceNodeIdx == -1)
	{
		for(i=0; i<PnpNWInfo->MainNodeInfo.nRxGroups;i++)
		{
			if(NWStream.srcRxGroup == PnpNWInfo->MainNodeInfo.RxGroups[i].GroupID)
			{
				bFlag = true;
				break;
			}
		}
	}
	else if(NWStream.SourceNodeIdx < PnpNWInfo->nDeviceCount)
	{
		for(i=0; i<PnpNWInfo->DevInfo[NWStream.SourceNodeIdx].nRxGroups;i++)
		{
			if(NWStream.srcRxGroup == PnpNWInfo->DevInfo[NWStream.SourceNodeIdx].RxGroups[i].GroupID)
			{
				bFlag = true;
				break;
			}
		}
	}
        else
        {
            /* avoid warning */
        }
	if(bFlag == 0)
	{
		return 1;
	}
	bFlag = false;

	for(nDest = 0;nDest<NWStream.nDestNodes; nDest++)
	{
		if(NWStream.DestNodeIdx[nDest] == -1)
		{
			for(i=0; i<PnpNWInfo->MainNodeInfo.nTxGroups;i++)
			{
				if(NWStream.destTxGroup[nDest] == PnpNWInfo->MainNodeInfo.TxGroups[i].GroupID)
				{
					bFlag = true;
					break;
				}
			}
		}
		else if(NWStream.DestNodeIdx[nDest]<PnpNWInfo->nDeviceCount)
		{
			for(i=0; i<PnpNWInfo->DevInfo[NWStream.DestNodeIdx[nDest]].nTxGroups;i++)
			{
				if(NWStream.destTxGroup[nDest] == PnpNWInfo->DevInfo[NWStream.DestNodeIdx[nDest]].TxGroups[i].GroupID)
				{
					bFlag = true;
					break;
				}
			}
		}
                else
                {
                        /* avoid warning */
                }
		if(bFlag == 0)
		{
			return 1;
		}
		bFlag = false;
	}

	return 0;
}


void readChipId(a2b_App_t *pApp_Info, a2b_Int8 nodeId, a2b_Char chipId[])
{
	a2b_AppBulkReadReg(pApp_Info->ctx, nodeId, A2B_REG_CHIPID0, (a2b_UInt8*) chipId, 6);
	return;
}

a2b_UInt8 getChainIdx(void* handle)
{
 	a2b_UInt8 i;
	for(i = 0; i < A2B_CONF_MAX_NUM_MASTER_NODES; i++)
	{
		if(hPnPList[i] == handle)
		{
			return(i);
		}	 
	}
	return 0xFF;
}

a2b_Int32 getnumChannelsFromDevInfo(a2b_Int32 inst, a2b_Int32 nodeNum, a2b_Int32 grpNo )
{
   a2b_App_t *pApp_Info = (a2b_App_t *)hPnPList[inst];
   a2b_Int32 numCh;
   if(nodeNum != -1)
   { 
    	numCh = pApp_Info->PnpNWInfo.DevInfo[nodeNum].RxGroups[grpNo].nGroupChannels;
   }
   else
   {
	  	numCh =  pApp_Info->PnpNWInfo.MainNodeInfo.RxGroups[grpNo].nGroupChannels;
   }
   return numCh;
}

A2B_PNP_RESULT a2b_pnp_StartBlindDiscovery(a2b_App_t *pApp_Info)
{
	a2b_Int32 nResult = 0;

	a2b_pnp_PrepareSwctlforBD(pApp_Info);
	/* Non-blocking call */
	nResult = pApp_Info->stkFunc.stkStartPartialDIsc(pApp_Info);

	if (nResult != 0)
	{
		if(pApp_Info->bDebug)
		{
			A2B_APP_DBG_LOG("ERROR discover \n\r");
		}
		return A2B_PNP_RESULT_FAILED;
	}

	return nResult;

}

A2B_PNP_RESULT a2b_pnp_PrepareSwctlforBD(a2b_App_t *pApp_Info)
{
	a2b_Int32 nResult = 0;
	if(pApp_Info->nodesDiscovered > 0)
	{
    	nResult = a2b_AppWriteReg(pApp_Info->ctx, A2B_NODEADDR_MASTER, A2B_REG_SWCTL, 0x21U);

		for(int i= 0;i < ((pApp_Info->nodesDiscovered -1));i++)
		{
			if(nResult == 0)
			{
				nResult = a2b_AppWriteReg(pApp_Info->ctx, i, A2B_REG_SWCTL, 0x21U);
			}
		}
	}	
	return nResult;

}

A2B_PNP_RESULT a2b_pnp_RestoreSwctlAftrBD(a2b_App_t *pApp_Info, a2b_UInt8 nodesDiscovered)
{
	a2b_Int32 nResult = 0;
	if(nodesDiscovered > 0)
	{
    	nResult = a2b_AppWriteReg(pApp_Info->ctx, A2B_NODEADDR_MASTER, A2B_REG_SWCTL, 0x11U);

		for(int i= 0;i < (nodesDiscovered-1) ;i++)
		{
			if(nResult == 0)
			{
				nResult = a2b_AppWriteReg(pApp_Info->ctx, i, A2B_REG_SWCTL, 0x11U);
			}
		}
	}
	else
	{
		nResult = a2b_AppWriteReg(pApp_Info->ctx, A2B_NODEADDR_MASTER, A2B_REG_SWCTL, 0x0U);
	}
	return nResult;

}
