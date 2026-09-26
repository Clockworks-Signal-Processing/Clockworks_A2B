/*******************************************************************************
Copyright (c) 2022 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
*******************************************************************************

   Name       : a2bpnp.c

   Description: This file is responsible for handling all the Plug and Play level
                functions

   Functions  : a2b_pnp_Init()
                a2b_pnp_StartDiscovery()
                a2b_pnp_Task()

   Developed by: Automotive and Consumer Software and Systems team, Bangalore, India

******************************************************************************/
/*============= I N C L U D E S =============*/
#include <stdlib.h>
#include "a2b/pal.h"
#include "string.h"
#include "assert.h"
#include "a2b/ctypes.h"
#include <a2bpnp.h>
#include <a2bpnp_local.h>
#include <a2bpnp_stack_interface.h>
#include "a2bPnp_conn_wrapper.h"

/* Memory required for PnP Library */
static a2b_App_t gApp_Info[A2B_PNP_CFG_MAX_NUM_A2B_CHAIN];

/***********************************  Defines ***********************************/

#define A2B_PNP_MAJOR_VER (1)
#define A2B_PNP_MINOR_VER (3)
#define A2B_PNP_PATCH_VER (0)


/*! \addtogroup Application_Reference API 
 *  @{
 */


/*********************************** A2B PnP APIs available to the Application ***********************************/


/**
 * @brief      Initialize the Plug and Play (PnP) Feature. Application shall register a callback function which is called by PnP Software to report events (#A2B_PNP_APP_EVENT_TYPE) to application
 *
 * @param [in] nChainIndex :  Chain Index in the Network
 *
 * @param [in] pCfg        :  Main node properties defined by the Application (application needs to preserve the memory).
 *
 * @param [in] pfCallback  : Callback function registered for handling Application Events.
 *
 * @param [in] pfIntrCallback  : Callback function registered for handling Interrupts.
 *
 * @param [in] pfErrCallback  : Callback function registered for handling Errors.
 * 
 * @return Handle for a given Chain Index
 *
 *  - #A2B_PNP_HANDLE              Valid Handle.
 *  - NULL                         PnP feature initialization failed
 *
 * @sa a2b_pnpDeInit()
 *
 */
A2B_PNP_HANDLE a2b_pnp_Init(a2b_UInt8 nChainIndex, a2bpnp_MainNodeProp* pCfg, PNP_CALLBACK pfCallback, INTR_CALLBACK pfIntrCallback, ERR_CALLBACK pfErrCallback)
{
	a2b_App_t *pApp_Info;

	/* Check if this is a valid supported chain index */
	if( (nChainIndex >= A2B_PNP_CFG_MAX_NUM_A2B_CHAIN) || (pfCallback == NULL))
	{
		return NULL;
	}

	/* PnP instance */
	pApp_Info = &gApp_Info[nChainIndex];

	/* Reset PnP Data */
	(void)memset(&pApp_Info->PnpNWInfo, 0, sizeof(a2bpnp_NetworkInfo));
	(void)memset(pApp_Info->NodeStreamInfo, 0, (sizeof(a2bpnp_SubnodeStreamInfo)*A2B_CONF_MAX_NUM_SLAVE_NODES));
	(void)memset(pApp_Info->SubNodeTypeInfo, 0, (sizeof(A2B_PNP_SOUCESINK_INFO)*A2B_CONF_MAX_NUM_SLAVE_NODES));

	/* Store instance handle */
	hPnPList[nChainIndex] = (A2B_PNP_HANDLE)pApp_Info;

	/* Fresh Discovery */
	pApp_Info->bFrstTimeDisc = A2B_TRUE;
	pApp_Info->pfCallback = (PNP_CALLBACK)pfCallback;
	pApp_Info->ePnPModuleState = A2B_PNP_INIT;
	pApp_Info->pfIntrCallback = (INTR_CALLBACK)pfIntrCallback;
	pApp_Info->pfErrCallback = (INTR_CALLBACK)pfErrCallback;
	pApp_Info->pMainNodeCfg = pCfg;
	pApp_Info->ecb.palEcb.nChainIndex = nChainIndex;
     
	//Initializing the connection manager
	connManageInit(nChainIndex);

	return (A2B_PNP_HANDLE)pApp_Info;
}


/**
 * @brief      De-Initialize the PnP Feature.
 *
 * @param [in] hPnp :  PnP Handle
 *
 * @return Handle for a given Chain Index
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa a2b_pnpInit()
 *
 */
A2B_PNP_RESULT a2b_pnp_DeInit(A2B_PNP_HANDLE hPnp)
{
	a2b_UInt8 Index;
	a2b_App_t * pApp_Info;

	/* Verify PnP handle */
	if(a2b_VerifyPnPHandle(hPnp) == A2B_PNP_RESULT_FAILED)
	{
		return A2B_PNP_RESULT_FAILED;
	}

	pApp_Info = (a2b_App_t *)hPnp;

    /* Stop the stack*/
	pApp_Info->stkFunc.stkStopStack(pApp_Info);

	/* Reset the particular chain index */
	for(Index = 0u; Index < A2B_PNP_CFG_MAX_NUM_A2B_CHAIN; Index++)
	{
		if(hPnPList[Index] == hPnp)
		{
			hPnPList[Index] = NULL;
		}
	}

	/* de-allocate the timer for periodic discovery */
	if(pApp_Info->hTmrToHandleBlindDisc != NULL)
	{
		(void)a2b_timerUnref(pApp_Info->hTmrToHandleBlindDisc);
	}

	/* TODO: Reset anything else required here */
	return A2B_PNP_RESULT_SUCCESS;
}

/**
 * @brief      Called to set certain parameters before the stating the plug and play module
 *
 * @param [in] hPnp         :	PnP Handle
 *
 * @param [in]  PnpInitParams    :  Structure passed to set the Parameters
 *
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa
 *
 * @note: This API needs be called before starting the initial discovery
 */
A2B_PNP_RESULT a2b_pnp_SetInitParams(A2B_PNP_HANDLE hPnp, a2bpnp_AppInitParams *PnpInitParams)
{
	a2b_App_t * pApp_Info;
	a2b_Int32 nRes;
	if(a2b_VerifyPnPHandle(hPnp))
	{
		return A2B_PNP_RESULT_FAILED;
	}
	pApp_Info = (a2b_App_t *)hPnp;
	pApp_Info->bDebug = PnpInitParams->bDebug;

	/* Initialize */
	nRes = a2b_init(pApp_Info);
	if (nRes != 0)
	{
		A2B_APP_ERR_LOG("\n\rERROR INIT \n\r");
	}

	pApp_Info->numNodesToDisc = PnpInitParams->numNodesToDisc;
	pApp_Info->bTwoStepDisc = PnpInitParams->bTwoStepDisc;
	pApp_Info->bGlobalMuteDuringCrossbar = PnpInitParams->bGlobalMuteDuringCrossbar;
	pApp_Info->bStreamByConnection = PnpInitParams->bStreamByConnection;
	pApp_Info->ignoreE2Prom = PnpInitParams->ignoreE2Prom;
	pApp_Info->maxNumSubnodes = PnpInitParams->maxNumSubnodes;
	pApp_Info->maxPWconsumption = PnpInitParams->maxPWconsumption;
	pApp_Info->maxCurrentConsumption = PnpInitParams->maxCurrentConsumption;
	pApp_Info->bwErrorLvl = PnpInitParams->BWErrorLevel;
	pApp_Info->bwWarningLvl = PnpInitParams->BWWarningLevel;
	pApp_Info->mainXcvrBInv = PnpInitParams->mainXcvrBInv;
	pApp_Info->subXcvrBInv = PnpInitParams->subXcvrBInv;

	if(nRes == 0)
	{
		a2b_discPrep(pApp_Info);
	}
	
	return (nRes);
}

/**
 * @brief      Called to configure Main node
 *
 * @param [in] hPnp         :	PnP Handle
 *
 * @param [in]  MainNodeAppInfo   :   Structure passed to get the Parameters
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa
 *
 */
A2B_PNP_RESULT a2b_pnp_ConfigureMainNode(A2B_PNP_HANDLE hPnp, a2bpnp_MainNodeAppInfo *MainNodeAppInfo)
{
	a2b_App_t * pApp_Info;
	a2bpnp_NetworkInfo *PnpNWInfo;

	/* Verify PnP handle */
	if(a2b_VerifyPnPHandle(hPnp) == A2B_PNP_RESULT_FAILED)
	{
		return A2B_PNP_RESULT_FAILED;
	}
	pApp_Info = (a2b_App_t *)hPnp;
	PnpNWInfo = &pApp_Info->PnpNWInfo;

	/*DT*/
	if(MainNodeAppInfo->dtEnabled == A2B_TRUE)
	{
		PnpNWInfo->MainNodeInfo.dtEnabled = A2B_TRUE;

		/*Main node needs to tunnel owner for PnP*/
		(pApp_Info->bdd.nodes[0].spiRegs.spicfg) |=  (1<<2);

		PnpNWInfo->MainNodeInfo.dtRole = A2B_TRUE;

		PnpNWInfo->MainNodeInfo.nDtDnSlots = pApp_Info->bdd.nodes[0].dataTunnelRegs.dtslots & 0x0F;//TODO - Decide the source
		PnpNWInfo->MainNodeInfo.nDtUpSlots = pApp_Info->bdd.nodes[0].dataTunnelRegs.dtslots >> 4;
	}
	(void)memcpy(PnpNWInfo->MainNodeInfo.DeviceName, MainNodeAppInfo->DeviceName, sizeof(PnpNWInfo->MainNodeInfo.DeviceName));
	PnpNWInfo->MainNodeInfo.nDeviceType = MainNodeAppInfo->nDeviceType;
	(void)memcpy(PnpNWInfo->MainNodeInfo.DeviceType, MainNodeAppInfo->DeviceType, sizeof(a2b_UInt8)*MainNodeAppInfo->nDeviceType);

	PnpNWInfo->MainNodeInfo.nRxGroups = MainNodeAppInfo->nRxGroups;
	(void)memcpy(PnpNWInfo->MainNodeInfo.RxGroups, MainNodeAppInfo->RxGroups, sizeof(a2bpnp_GroupInfo)*MainNodeAppInfo->nRxGroups);

	PnpNWInfo->MainNodeInfo.nTxGroups = MainNodeAppInfo->nTxGroups;
	(void)memcpy(PnpNWInfo->MainNodeInfo.TxGroups, MainNodeAppInfo->TxGroups, sizeof(a2bpnp_GroupInfo)*MainNodeAppInfo->nTxGroups);

	return A2B_PNP_RESULT_SUCCESS;
}

/**
 * @brief      Start the Network Discovery. This function will initiate the discovery and configuration of all the nodes connected in the network.
 *
 * @param [in] hPnp :  PnP Handle
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa a2b_pnpPauseNewNodeDisc(), a2b_pnpResumeNewNodeDisc()
 *
 */
A2B_PNP_RESULT a2b_pnp_StartDiscovery(A2B_PNP_HANDLE hPnp)
{
	a2b_Int32 nResult = 0;
	a2b_App_t * pApp_Info;
	/* Verify PnP handle */
	if(a2b_VerifyPnPHandle(hPnp) == A2B_PNP_RESULT_FAILED)
	{
		return A2B_PNP_RESULT_FAILED;
	}

	pApp_Info = (a2b_App_t *)hPnp;

	/* This is first iteration or fault at Main node (re-discovery is initiated) */
	do
	{

		/* Reset A2B protocol engine to avoid the issue during rediscovery */
		if(pApp_Info->ctx != A2B_NULL)
		{
			nResult = a2b_AppWriteReg(pApp_Info->ctx, A2B_NODEADDR_MASTER , A2B_REG_CONTROL, A2B_ENUM_CONTROL_RESET_PE);
		}

		if (nResult != 0)
		{
			if(pApp_Info->bDebug)
			{
				A2B_APP_DBG_LOG("ERROR Resetting the protocol engine \n\r");
			}
			break;
		}

		/* Register the required callback(s) */
		nResult = a2b_start(pApp_Info);

		if (nResult != 0)
		{
			if(pApp_Info->bDebug)
			{
				A2B_APP_DBG_LOG("ERROR Start \n\r");
			}
			break;
		}

		/* Plug and Play feature expect only main node to be connected first and then discover the sub nodes one by one
		   If there are no sub nodes in the system, Audio routing will not be initiated */
		pApp_Info->bdd.nodes_count = 1u + pApp_Info->numNodesToDisc;

		/* First call after re-discovery is blocking
		 * Periodic discovery post then is non-blocking */
		nResult = a2b_discover(pApp_Info);

		if (nResult != 0)
		{
			if(pApp_Info->bDebug)
			{
				A2B_APP_DBG_LOG("ERROR discover \n\r");
			}
			break;
		}

		/* set the flag A2B_TRUE so that discovery of next node can be initiated immediately */
		pApp_Info->bBlindDiscTimerExp = A2B_TRUE;

	}while(0);

	/* Timer allocation for blind discovery */
	/* Initialize timer to trigger periodic discovery */
	pApp_Info->stkFunc.stkStartTimerForBD(pApp_Info);

	if(nResult != 0)
	{
		return A2B_PNP_RESULT_FAILED;
	}
	return A2B_PNP_RESULT_SUCCESS;
}

/**
 * @brief      Pause the periodic discovery attempt for a new node whenever it is required.
 *
 * @param [in] hPnp :  PnP Handle
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa a2b_pnpStartDiscovery(), a2b_pnpResumeNewNodeDisc()
 *
 */
A2B_PNP_RESULT a2b_pnp_PauseNewNodeDisc(A2B_PNP_HANDLE hPnp)
{
	a2b_App_t * pApp_Info;

	/* Verify PnP handle */
	if(a2b_VerifyPnPHandle(hPnp) == A2B_PNP_RESULT_FAILED)
	{
		return A2B_PNP_RESULT_FAILED;
	}

	pApp_Info = (a2b_App_t *)hPnp;

	/* Set the pause discovery flag */
	pApp_Info->bPauseDisc = A2B_TRUE;

	return A2B_PNP_RESULT_SUCCESS;
}


/**
 * @brief      Resume the periodic discovery attempt for a new node.
 *
 * @param [in] hPnp :  PnP Handle
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa a2b_pnpStartDiscovery(), a2b_pnpPauseNewNodeDisc()
 *
 */
A2B_PNP_RESULT a2b_pnp_ResumeNewNodeDisc(A2B_PNP_HANDLE hPnp)
{
	a2b_App_t * pApp_Info;

	/* Verify PnP handle */
	if(a2b_VerifyPnPHandle(hPnp) == A2B_PNP_RESULT_FAILED)
	{
		return A2B_PNP_RESULT_FAILED;
	}

	pApp_Info = (a2b_App_t *)hPnp;

	/* Reset the pause discovery flag */
	pApp_Info->bPauseDisc = A2B_FALSE;

	return A2B_PNP_RESULT_SUCCESS;
}


/**
 * @brief      Application shall call this PnP task periodically to continue the periodic discovery attempt for a node, detection of nodes disconnected and A2B SW Stack Tick.
 *
 * @param [in] hPnp :  PnP Handle
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa
 *
 */
A2B_PNP_RESULT a2b_pnp_Task(A2B_PNP_HANDLE hPnp)
{
	A2B_PNP_RESULT nResult = A2B_PNP_RESULT_SUCCESS;
	a2b_Int32 nA2bResult=0;
	a2b_App_t * pApp_Info;
	PNP_CALLBACK pfCallback;
	a2b_UInt8 bAudioApplied = A2B_FALSE;
	a2b_pnp_faultRes hwFaultRes;

	/* Verify PnP handle */
	if(a2b_VerifyPnPHandle(hPnp) == A2B_PNP_RESULT_FAILED)
	{
		return A2B_PNP_RESULT_FAILED;
	}
	pApp_Info = (a2b_App_t *)hPnp;

	switch(pApp_Info->ePnPModuleState)
	{
		case A2B_PNP_INIT:
			if(pApp_Info->ctx == A2B_NULL)
			{
				(void)a2b_init(pApp_Info);
				(void)a2b_discPrep(pApp_Info);
				(void)a2b_pnp_StartDiscovery(hPnp);
			}
			break;
		case A2B_PNP_NETWORK_HW_FAULT:
			/* Keep reporting fault */
			pfCallback = (PNP_CALLBACK)pApp_Info->pfCallback;
			hwFaultRes.faultCode = pApp_Info->faultCode;
			hwFaultRes.faultNode = pApp_Info->faultNode;
			hwFaultRes.faultStatus = pApp_Info->faultStatus;
			hwFaultRes.stateDuringFault = pApp_Info->moduleStateduringFault;
			(void)pfCallback(pApp_Info, (a2b_UInt32)A2B_PNP_HW_FAULT, (void*)&hwFaultRes);
			break;
		case A2B_PNP_MAIN_PLL_NOT_LOCKED:
			/* Keep reporting fault */
			pfCallback = (PNP_CALLBACK)pApp_Info->pfCallback;
			(void)pfCallback(pApp_Info, (a2b_UInt32)A2B_PNP_MAIN_PLL_LOCK_FAILURE, NULL);
			break;
		case A2B_PNP_BASIC_AUTH_FAILURE:
			/* Report the authentication */
			pfCallback = (PNP_CALLBACK)pApp_Info->pfCallback;
			(void)pfCallback(pApp_Info, (a2b_UInt32)A2B_PNP_BASIC_AUTH_FAIL, NULL);
			/* Continue with Network manage */
			pApp_Info->ePnPModuleState = A2B_PNP_NETWORK_MNG;
			break;
		case A2B_PNP_BLIND_DISC_IN_PROGRESS:
			if((pApp_Info->bBlindDiscTimerExp == A2B_TRUE))
			{
				if((pApp_Info->pal.timerGetSysTime() - pApp_Info->currTime) > PNP_MODULE_BD_TIMEOUT)
				{
					//This should not really happen
					A2B_APP_ERR_LOG("\n\rBlind Discovery is not getting completed");
					pApp_Info->ePnPModuleState = A2B_PNP_INTERNAL_ERR;
				}
			}
			break;
		case A2B_PNP_INTERNAL_ERR:
		//report to application
		pApp_Info->pfErrCallback(pApp_Info, A2B_PNP_MODULE_CATCH_ALL_ERR, A2B_NULL);
		break;
		case A2B_PNP_NETWORK_MNG:

			/* Timer expired : It is time to initiate discovery for new nodes or check for dropped nodes */
			if ( (pApp_Info->bBlindDiscTimerExp == A2B_TRUE) && (pApp_Info->bPauseDisc == A2B_FALSE))
			{
				/* This function will check the node drop in case of node is dropped from system
				 * It will also check if there is new node connected as we always keep nodes_count as 1 more for periodic discovery of possible new nodes  */
				 (void)a2b_AppDetectBusDrop(pApp_Info);

				 if(pApp_Info->faultNode == (pApp_Info->bdd.nodes_count - 1))
				 {
					 /* No bus drop , Ready for blind discovery */
					 /* increase for the blind discovery */
					 pApp_Info->bdd.nodes_count++;
				 }
				 else if(pApp_Info->faultNode == -2)
				 {
					 //Need to restart the discovery
					 pApp_Info->ePnPModuleState = A2B_PNP_INIT;

					 (void)memset((void*)&(pApp_Info->PnpNWInfo), 0, sizeof(a2bpnp_NetworkInfo));
					(void)memset((void*)&pApp_Info->SubNodeTypeInfo, 0, (sizeof(A2B_PNP_SOUCESINK_INFO)*A2B_CONF_MAX_NUM_SLAVE_NODES));
					pApp_Info->bdd.nodes_count = 1;

					/* stop a2b stack */
					nA2bResult = pApp_Info->stkFunc.stkStopStack(pApp_Info);

					//Let us return immediately
					return nA2bResult;

				 }
				 else
				 {

					 //there is bus drop but we can try to regain the system
					 pApp_Info->bdd.nodes_count = pApp_Info->faultNode + 2;
					 //Let us return immediately
					 return A2B_PNP_RESULT_SUCCESS;

				 }

				nResult = a2b_pnp_StartBlindDiscovery(hPnp);
				pApp_Info->currTime = pApp_Info->pal.timerGetSysTime();
				pApp_Info->ePnPModuleState = A2B_PNP_BLIND_DISC_IN_PROGRESS;

				if (nResult != A2B_PNP_RESULT_SUCCESS)
				{
					return A2B_PNP_RESULT_FAILED;
				}

				/* Reset the time and set it again to expire after A2B_APP_TMRTOHANDLE_BLINDDISC_AFTER_INTERVAL ms */
				pApp_Info->bBlindDiscTimerExp = 0;

				pApp_Info->stkFunc.stkStartTimerForBD(pApp_Info);
			}
			else /* Let us do some services */
			{
				/* Keep on sending request to apply the Audio routing, until it is not done */
				pfCallback = (PNP_CALLBACK)pApp_Info->pfCallback;
				/* Application is expected to return A2B_TRUE if it has applied the Audio routing using a2b_pnp_SetAudioRt() API */
				bAudioApplied = pfCallback(pApp_Info, (a2b_UInt32)A2B_PNP_APPLY_AUDIO_ROUTING, NULL);

				if(bAudioApplied == A2B_TRUE)
				{
					/* Information */
				}

			}
			break;

		default:
                        /* Do nothing */
			break;
	}

	a2b_stackTick(pApp_Info->ctx);


	return A2B_PNP_RESULT_SUCCESS;
}


/**
 * @brief      Retrieves the complete A2B NW information
 *
 * @param [in] hPnp         :   PnP Handle
 *
 * @param [out] NetworkInfo :  NetworkInfo
 *
 *		DeviceInfo           Information about the connected device and their peripherals in the Network
 *		nDeviceCount         Number of devices in the Network
 *		NWStream             Stream Information for complete Network
 *		nStreamCount;         Total number of the stream defined
 *		DTStream             DT stream information
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 * @sa
 */
A2B_PNP_RESULT a2b_pnp_GetNWInfo(A2B_PNP_HANDLE hPnp, a2bpnp_NetworkInfo **NetworkInfo)
{
	a2b_App_t * pApp_Info;
	/* Verify PnP handle */
	if(a2b_VerifyPnPHandle(hPnp) == A2B_PNP_RESULT_FAILED)
	{
		return A2B_PNP_RESULT_FAILED;
	}
	pApp_Info = (a2b_App_t *)hPnp;

	*NetworkInfo = &(pApp_Info->PnpNWInfo);
	

	return A2B_PNP_RESULT_SUCCESS;
}

/**
 * @brief      This function adds an audio connection between two nodes by using node ID (position based)
 *
 * @param [in] pRes     :	Overall usage (Pointer)
 *
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa connections will be persistent
 *
 */
A2B_PNP_RESULT a2b_pnp_AddConByNodeId(A2B_PNP_HANDLE hPnp, a2b_Int32 srcNodeId, a2b_Int32 srcNodeGroup, a2b_Int32 dstNodeId, a2b_Int32 dstNodeGroup)
{
	a2b_HResult result = A2B_PNP_RESULT_SUCCESS;
	a2b_App_t * pApp_Info;
	if(a2b_VerifyPnPHandle(hPnp))
	{
		result = A2B_PNP_RESULT_FAILED;
		return result;
	}
	pApp_Info = (a2b_App_t *)hPnp;
	
	if(pApp_Info->bStreamByConnection == A2B_TRUE)
	{
		addConnection(getChainIdx(hPnp), srcNodeId, srcNodeGroup, dstNodeId, dstNodeGroup);
	}
	else
	{
		A2B_APP_ERR_LOG("Creating Stream By Connection Not Enabled\n");
		return A2B_PNP_RESULT_FAILED;
	}
	return result;

}

/**
 * @brief      This function adds an audio connection between two nodes by using chip ID (positionless)
 *
 * @param [in] hPnp         :   PnP Handle
 * @param [in] pair         :   Connection pair
 *
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa connections will be persistent
 *
 */
A2B_PNP_RESULT a2b_pnp_AddConByChipId(A2B_PNP_HANDLE hPnp, connPairByChipId pair)
{
	a2b_HResult result = A2B_PNP_RESULT_SUCCESS;
	a2b_App_t * pApp_Info;
	if(a2b_VerifyPnPHandle(hPnp))
	{
		result = A2B_PNP_RESULT_FAILED;
		return result;
	}
	pApp_Info = (a2b_App_t *)hPnp;
	
	if(pApp_Info->bStreamByConnection == A2B_TRUE)
	{
		addConnectionByChipID(getChainIdx(hPnp), pair);
	}
	else
	{
		A2B_APP_ERR_LOG("Creating Stream By Connection Not Enabled\n");
		return A2B_PNP_RESULT_FAILED;
	}
	return result;

}

/**
 * @brief      This function removes an audio connection between two nodes by using node ID
 *
 * @param [in] hPnp     :	PnP Handle
 *
 * @param [in] srcNodeId   :	Audio source node Id 
 * 
 * @param [in] srcNodeGroup   :	Audio source group
 *
 * @param [in] dstNodeId   :	Audio destination node Id 
 * 
 * @param [in] dstNodeGroup   :	Audio destination group
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa 
 *
 */
A2B_PNP_RESULT a2b_pnp_RemoveConByNodeId(A2B_PNP_HANDLE hPnp, a2b_Int32 srcNodeId, a2b_Int32 srcNodeGroup, a2b_Int32 dstNodeId, a2b_Int32 dstNodeGroup)
{
	a2b_HResult result = A2B_PNP_RESULT_SUCCESS;
	if(a2b_VerifyPnPHandle(hPnp))
	{
		return A2B_PNP_RESULT_FAILED;
	}
	removeConnection(getChainIdx(hPnp), srcNodeId, srcNodeGroup, dstNodeId, dstNodeGroup);	
	return result;
}

/**
 * @brief      This function clears all the connection
 *
 * @param [in] hPnp     :	PnP Handle
 *
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa connections will be persistent
 *
 */
A2B_PNP_RESULT a2b_pnp_ClearAllCon(A2B_PNP_HANDLE hPnp)
{
	a2b_HResult result = A2B_PNP_RESULT_SUCCESS;
	if(a2b_VerifyPnPHandle(hPnp))
	{
		result = A2B_PNP_RESULT_FAILED;
		return result;
	}
	removeAll(getChainIdx(hPnp));
	return result;
}

/**
 * @brief      This function exports all audio connections on the network
 *
 * @param [in] hPnp     :	Overall usage (Pointer)
 *
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa connections will be persistent
 *
 */
A2B_PNP_RESULT a2b_pnp_GetConnectionList(A2B_PNP_HANDLE hPnp, connPairByChipId *NWCon, a2b_UInt8 *connectionCnt)
{
	a2b_HResult result = A2B_PNP_RESULT_SUCCESS;
	a2b_App_t * pApp_Info;
 

	if(a2b_VerifyPnPHandle(hPnp))
	{
		result = A2B_PNP_RESULT_FAILED;
		return result;
	}
	pApp_Info = (a2b_App_t *)hPnp;
	
	if(pApp_Info->bStreamByConnection == A2B_TRUE)
	{
		getListOfConnection(getChainIdx(hPnp), NWCon, connectionCnt);
	}
	else
	{
		A2B_APP_ERR_LOG("Creating Stream By Connection Not Enabled\n");
		return A2B_PNP_RESULT_FAILED;
	}
	return result;
}

/**
 * @brief      Sets the Custom Audio routing for A2B Network
 *
 * @param [in] hPnp         :	PnP Handle
 *
 * @param [in] NWStream     :	Audio Routing details
 *
 * @param [in] nStreamCount :	Number of streams
 *
 * @param [in] bIsAppend    :	A2B_TRUE-if this is an append to the existing audio routing, A2B_FALSE:if this is a new audio routing
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed (Invalid Handle, Stream Validation failed, New stream is not appended to the existing Audio routing)
 *
 * @sa
 */
A2B_PNP_RESULT a2b_pnp_SetAudioRt(A2B_PNP_HANDLE hPnp, a2bpnp_StreamInfo *NWStream, a2b_UInt8 nStreamCount, a2b_UInt8 bIsAppend)
{
	a2b_UInt8 StreamIdx;
	a2b_UInt8 Idx;
	a2b_UInt8 Idx1;
	a2b_UInt8 Idx2;
	a2b_Bool bFound = A2B_FALSE;
	a2b_App_t * pApp_Info;
	A2B_UNUSED(bFound);
	a2bpnp_NetworkInfo *PnpNWInfo;

	/* Verify PnP handle */
	if(a2b_VerifyPnPHandle(hPnp) == A2B_PNP_RESULT_FAILED)
	{
		return A2B_PNP_RESULT_FAILED;
	}
	pApp_Info = (a2b_App_t *)hPnp;
	PnpNWInfo = &pApp_Info->PnpNWInfo;
    //removing the connection 
	if(pApp_Info->bStreamByConnection == A2B_TRUE)
	{
		return A2B_PNP_RESULT_FAILED;
	}
	
	/* TODO: Return error if same sink is assigned to multiple source */

	/* Since there are only two channels per source, it is assumed that if there is a new destination with the existing source, it will be appended to the destination node list of that stream */
	if(bIsAppend == A2B_TRUE)
	{
		for(StreamIdx=0; StreamIdx<nStreamCount; StreamIdx++)
		{
			bFound = A2B_FALSE;
			/* Find if there is any stream with the same source */
			for(Idx=0; Idx<PnpNWInfo->nStreamCount; Idx++)
			{
				if(PnpNWInfo->NWStream[Idx].SourceNodeIdx == NWStream[StreamIdx].SourceNodeIdx)
				{
					if(PnpNWInfo->NWStream[Idx].srcRxGroup == NWStream[StreamIdx].srcRxGroup)
					{
						return A2B_PNP_RESULT_FAILED;
					}
					/* loop through all the new nodes and assign streams */
					for(Idx1=0; Idx1<NWStream[StreamIdx].nDestNodes; Idx1++)
					{
						for(Idx2=0; Idx2<PnpNWInfo->NWStream[Idx].nDestNodes; Idx2++)
						{
							if(PnpNWInfo->NWStream[Idx].DestNodeIdx[Idx2] == NWStream[StreamIdx].DestNodeIdx[Idx1])
							{
								//check if same group IDs are already configured
								if((PnpNWInfo->NWStream[Idx].destTxGroup[Idx2] == NWStream[StreamIdx].destTxGroup[Idx1]))
								{
									return A2B_PNP_RESULT_FAILED;
								}
							}
						}

					}
				}
			}

			/* Append the new stream */
			(void)memcpy(&PnpNWInfo->NWStream[PnpNWInfo->nStreamCount], &NWStream[StreamIdx], sizeof(a2bpnp_StreamInfo));

			/* update the flags */
			pApp_Info->SubNodeTypeInfo[NWStream[StreamIdx].SourceNodeIdx].IsStreamDefined = A2B_TRUE;
			for(Idx1=0; Idx1<NWStream[StreamIdx].nDestNodes; Idx1++)
			{
				pApp_Info->SubNodeTypeInfo[NWStream[StreamIdx].DestNodeIdx[Idx1]].IsStreamDefined = A2B_TRUE;
			}

			PnpNWInfo->nStreamCount++;

		}
	}
	else
	{
		/* Reconfigure the Audio streams */
		PnpNWInfo->nStreamCount = 0;
		(void)memset((void*)&pApp_Info->NodeStreamInfo, 0, (sizeof(a2bpnp_SubnodeStreamInfo)*A2B_CONF_MAX_NUM_SLAVE_NODES));
		for(Idx1=0; Idx1<PnpNWInfo->nDeviceCount; Idx1++)
		{
			pApp_Info->SubNodeTypeInfo[Idx1].IsStreamDefined = A2B_FALSE;
		}

		for(StreamIdx=0; StreamIdx<nStreamCount; StreamIdx++)
		{
			/* Validate the stream */
			(void)memcpy(&PnpNWInfo->NWStream[StreamIdx], &NWStream[StreamIdx], sizeof(a2bpnp_StreamInfo));
			PnpNWInfo->nStreamCount++;

			/* update the flags */
			pApp_Info->SubNodeTypeInfo[NWStream[StreamIdx].SourceNodeIdx].IsStreamDefined = A2B_TRUE;
			for(Idx1=0; Idx1<NWStream[StreamIdx].nDestNodes; Idx1++)
			{
				pApp_Info->SubNodeTypeInfo[NWStream[StreamIdx].DestNodeIdx[Idx1]].IsStreamDefined = A2B_TRUE;
			}
		}
	}

	return A2B_PNP_RESULT_SUCCESS;
}


/**
 * @brief      Sets the SPI DT routing for A2B Network
 *
 * @param [in] hPnp         :	PnP Handle
 *
 * @param [in] DTStream     :	SPI DT Routing details
 *
 * @param [in] nDTCount     :	Number of streams
 *
 * @param [in] bIsAppend    :	A2B_TRUE-if this is an append to the existing SPI DT routing, A2B_FALSE:if this is a new SPI DT routing
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed (Invalid Handle, Stream Validation failed, New stream is not appended to the existing SPI DT routing)
 *
 * @sa
 */
A2B_PNP_RESULT a2b_pnp_SetSPIDTRt(A2B_PNP_HANDLE hPnp, a2bpnp_DTStreamInfo *DTStream, a2b_UInt8 nDTCount, a2b_UInt8 bIsAppend)
{
	a2b_App_t * pApp_Info;
	a2bpnp_NetworkInfo *PnpNWInfo;

	A2B_UNUSED(bIsAppend);
	/* Verify PnP handle */
	if(a2b_VerifyPnPHandle(hPnp) == A2B_PNP_RESULT_FAILED)
	{
		return A2B_PNP_RESULT_FAILED;
	}
	pApp_Info = (a2b_App_t *)hPnp;
	PnpNWInfo = &pApp_Info->PnpNWInfo;

	if(nDTCount > 1)
	{
		return A2B_PNP_RESULT_FAILED;
	}
	if(nDTCount == 1)
	{
		/* Append the new stream */
		(void)memcpy(&PnpNWInfo->DTStream[PnpNWInfo->nDTStreamCount], DTStream, sizeof(a2bpnp_DTStreamInfo));
		PnpNWInfo->nDTStreamCount++;
	}
	else
	{
		PnpNWInfo->nDTStreamCount = 0;
	}

	return A2B_PNP_RESULT_SUCCESS;
}

/**
 * @brief      Clears any tunnel in the network
 *
 * @param [in] hPnp     :	PnP Handle
 *
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa
 *
 */
A2B_PNP_RESULT a2b_pnp_ClearTunnel(A2B_PNP_HANDLE hPnp, a2bpnp_DTStreamInfo *DTStream)
{
	a2b_App_t * pApp_Info;
	a2b_UInt8 nNode;
	a2bpnp_NetworkInfo *PnpNWInfo;

	/* Verify PnP handle */
	if(a2b_VerifyPnPHandle(hPnp) == A2B_PNP_RESULT_FAILED)
	{
		return A2B_PNP_RESULT_FAILED;
	}
	pApp_Info = (a2b_App_t *)hPnp;
	PnpNWInfo = &pApp_Info->PnpNWInfo;

	if(PnpNWInfo->nDTStreamCount)
	{
		for(nNode = 0; nNode<pApp_Info->bdd.nodes_count - 1u; nNode++)
		{
			pApp_Info->bdd.nodes[nNode].dataTunnelRegs.dtcfg = 0u;
			pApp_Info->bdd.nodes[nNode].dataTunnelRegs.dtslots = 0u;
			pApp_Info->bdd.nodes[nNode].dataTunnelRegs.dtndnoffs = 0u;
			pApp_Info->bdd.nodes[nNode].dataTunnelRegs.dtnupoffs = 0u;
		}
		PnpNWInfo->nDTStreamCount = 0;
		memset(PnpNWInfo->DTStream, '\0', sizeof(PnpNWInfo->DTStream[0]));
		memset(DTStream, '\0', sizeof(a2bpnp_DTStreamInfo));
	}

	return A2B_PNP_RESULT_SUCCESS;
}


/**
 * @brief      Get Default Data Tunnel Stream for A2B Network
 *
 * @param [in] DTStream     :	SPI DT Routing details
 *
 * @param [in] dtStreamCount     :	Number of DT streams
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed (Invalid Handle, Stream Validation failed, New stream is not appended to the existing SPI DT routing)
 *
 * @note
 *	Single Data tunnel stream
 *	All the in between nodes needs to be participating
 * @sa
 */
A2B_PNP_RESULT a2b_pnp_GetDefaultDTStream(A2B_PNP_HANDLE hPnp, a2bpnp_DTStreamInfo *DTStream, a2b_UInt8* dtStreamCount)
{
	a2b_UInt8 dtNode = 0;
	a2b_Int8 nNode, lastTunnel = -1;
	a2b_App_t * pApp_Info;
	a2bpnp_NetworkInfo *PnpNWInfo;

	/* Verify PnP handle */
	if(a2b_VerifyPnPHandle(hPnp) == A2B_PNP_RESULT_FAILED)
	{
		return A2B_PNP_RESULT_FAILED;
	}
	pApp_Info = (a2b_App_t *)hPnp;
	PnpNWInfo = &pApp_Info->PnpNWInfo;
	/*Initialize with default slots*/
	DTStream->nDtDnSlots = 2u;
	DTStream->nDtUpSlots = 2u;

	if(PnpNWInfo->MainNodeInfo.dtEnabled)
	{
		DTStream->dtNodes[dtNode++] = A2B_NODEADDR_MASTER;
		DTStream->nNodes++;
		DTStream->dtOwner = A2B_NODEADDR_MASTER;

		if(PnpNWInfo->MainNodeInfo.nDtDnSlots > DTStream->nDtDnSlots)
		{
			DTStream->nDtDnSlots = PnpNWInfo->MainNodeInfo.nDtDnSlots;
		}
		if(PnpNWInfo->MainNodeInfo.nDtUpSlots > DTStream->nDtUpSlots)
		{
			DTStream->nDtUpSlots = PnpNWInfo->MainNodeInfo.nDtUpSlots;
		}

		/*Look for last tunnel node*/
		for(nNode= (PnpNWInfo->nDeviceCount - 1); nNode >= 0; nNode--)
		{
			if(PnpNWInfo->DevInfo[nNode].dtEnabled)
			{
				lastTunnel = nNode;
				break;
			}
		}
	}

	if(lastTunnel == -1)
	{
		*dtStreamCount = 0u;
		return A2B_PNP_RESULT_SUCCESS;
	}
	else
	{
		*dtStreamCount = 1u;
	}
	for( nNode= 0; nNode <= lastTunnel; nNode++)
	{
		PnpNWInfo->DevInfo[nNode].dtEnabled = A2B_TRUE;
		DTStream->dtNodes[dtNode++] = nNode;
		DTStream->nNodes++;
		if(PnpNWInfo->DevInfo[nNode].nDtDnSlots > DTStream->nDtDnSlots)
		{
			DTStream->nDtDnSlots = PnpNWInfo->DevInfo[nNode].nDtDnSlots;
		}
		if(PnpNWInfo->DevInfo[nNode].nDtUpSlots > DTStream->nDtUpSlots)
		{
			DTStream->nDtUpSlots = PnpNWInfo->DevInfo[nNode].nDtUpSlots;
		}
	}

	return A2B_PNP_RESULT_SUCCESS;
}

/**
 * @brief      Apply both Audio and SPI DT routing for A2B Network
 *
 * @param [in] hPnp         :	PnP Handle
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 * @sa
 */
A2B_PNP_RESULT a2b_pnp_ApplyRouting(A2B_PNP_HANDLE hPnp)
{
	a2b_App_t * pApp_Info;
	a2b_Int32 streamCnt; 
	a2b_Int32 node;
	a2b_UInt32 usage;
	A2B_PNP_RESULT nResult = A2B_PNP_RESULT_SUCCESS;

	if(a2b_VerifyPnPHandle(hPnp) == A2B_PNP_RESULT_FAILED)
	{
		return A2B_PNP_RESULT_FAILED;
	}
	pApp_Info = (a2b_App_t *)hPnp;

	if(pApp_Info->ePnPModuleState != A2B_PNP_NETWORK_MNG)
	{
		//report to application
		pApp_Info->pfErrCallback(pApp_Info, A2B_PNP_ROUTING_APPLY_DURING_BD, A2B_NULL);
		return A2B_PNP_RESULT_FAILED;
	}
	if(pApp_Info->bStreamByConnection == A2B_TRUE)
	{	
		pApp_Info->PnpNWInfo.nStreamCount = 0;
		getStreamFromConnection(getChainIdx(hPnp), pApp_Info->PnpNWInfo.NWStream, (int*)&streamCnt);
		pApp_Info->PnpNWInfo.nStreamCount = streamCnt;
	}

	AutoCalcSlot(pApp_Info);
    AutoCalcSlotDT(pApp_Info);/* Update AutoCalcSlot to add the slot calculation for SPI Data tunnel also */

	/* let us get the bandwidth usage*/
	getNwBw(pApp_Info, &node, &usage);

	if(usage > pApp_Info->bwErrorLvl)
	{
		(void)pApp_Info->pfCallback(pApp_Info, (a2b_UInt32)A2B_PNP_BW_ERROR_LEVEL, &node);
		nResult = A2B_PNP_RESULT_FAILED;
	}
	else if(usage > pApp_Info->bwWarningLvl)
	{
		(void)pApp_Info->pfCallback(pApp_Info, (a2b_UInt32)A2B_PNP_BW_WARNING_LEVEL, &node);
	}

	//Let us apply only the valid ones.
	if(nResult == A2B_PNP_RESULT_SUCCESS)
	{
		/* Update the response cycles based on the new bandwidth utilization */
		applyRespCys(calcMstrRespCycs(pApp_Info), pApp_Info);
		UpdateSlots(pApp_Info);
	}

	return nResult;
}


/**
 * @brief       Write A2B register on main/sub nodes
 *
 * @param [in] hPnp         :	PnP Handle
 *
 * @param [in] nodeAddr     :	Node Address
 *
 * @param [in] RegAddr      :	Register Address
 *
 * @param [in] Value        :	Register Value
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 * @sa
 */
A2B_PNP_RESULT a2b_pnp_WriteReg(A2B_PNP_HANDLE hPnp, a2b_Int16 nodeAddr, a2b_UInt8 RegAddr, a2b_UInt8 Value)
{
	a2b_App_t * pApp_Info;
	A2B_PNP_RESULT nResult = A2B_PNP_RESULT_SUCCESS;
	a2b_UInt32 nA2bResult = 0;

	if(a2b_VerifyPnPHandle(hPnp) == A2B_PNP_RESULT_FAILED)
	{
		return A2B_PNP_RESULT_FAILED;
	}

	pApp_Info = (a2b_App_t *)hPnp;

	/* TODO: Error check for correct node */

	nA2bResult = a2b_AppWriteReg(pApp_Info->ctx, nodeAddr, RegAddr, Value);
	if(nA2bResult != 0u)
	{
		nResult = A2B_PNP_RESULT_FAILED;
	}

	return nResult;
}


/**
 * @brief       Read A2B register on main/sub nodes
 *
 * @param [in] hPnp         :	PnP Handle
 *
 * @param [in] nodeAddr     :	Node Address
 *
 * @param [in] RegAddr      :	Register Address
 *
 * @param [out] Value       :	Register Value
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 * @sa
 */
A2B_PNP_RESULT a2b_pnp_ReadReg(A2B_PNP_HANDLE hPnp, a2b_Int16 nodeAddr, a2b_UInt8 RegAddr, a2b_UInt8* Value)
{
	a2b_App_t * pApp_Info;
	A2B_PNP_RESULT nResult = A2B_PNP_RESULT_SUCCESS;
	a2b_UInt32 nA2bResult=0;

	if(a2b_VerifyPnPHandle(hPnp) == A2B_PNP_RESULT_FAILED)
	{
		return A2B_PNP_RESULT_FAILED;
	}

	pApp_Info = (a2b_App_t *)hPnp;

	nA2bResult =  a2b_AppReadReg(pApp_Info->ctx, nodeAddr, (a2b_UInt32)RegAddr, (a2b_UInt32*)Value);
	if(nA2bResult != 0u)
	{
		nResult = A2B_PNP_RESULT_FAILED;
	}

	return nResult;
}


/**
 * @brief       Read memory/register of the I2C Peripheral when access interface is I2C
 *
 * @param [in] hPnp         :	PnP Handle
 *
 * @param [in] nodeAddr     :	Node Address
 *
 * @param [in] I2CAddr      :	I2C Address of peripheral
 *
 * @param [in] nWrite       :	Number of bytes to be written
 *
 * @param [in] wBuf         :	write buffer
 *
 * @param [in] nRead        :	Number of bytes to be read
 *
 * @param [out] rBuf        :	pointer to read buffer
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 * @sa
 *
 * @note: This API can only be used when access interface is I2C
 */
A2B_PNP_RESULT a2b_pnp_I2CReadPeri(A2B_PNP_HANDLE hPnp, a2b_Int16 nodeAddr, a2b_UInt16 I2CAddr, a2b_UInt16 nWrite,
		                  void* wBuf, a2b_UInt16 nRead, void* rBuf)
{
	a2b_App_t * pApp_Info;
	A2B_PNP_RESULT nResult = A2B_PNP_RESULT_SUCCESS;
	a2b_UInt32 nA2bResult=0;


	if(a2b_VerifyPnPHandle(hPnp) == A2B_PNP_RESULT_FAILED)
	{
		return A2B_PNP_RESULT_FAILED;
	}

	pApp_Info = (a2b_App_t *)hPnp;

	/* TO DO:
	 * Verify the access interface */

	nA2bResult =   a2b_i2cPeriphWriteRead(pApp_Info->ctx, nodeAddr, I2CAddr, nWrite, wBuf, nRead, rBuf);
	if(nA2bResult != 0u)
	{
		nResult = A2B_PNP_RESULT_FAILED;
	}

	return nResult;
}


/**
 * @brief       Write memory/register of the I2C Peripheral when access interface is I2C
 *
 * @param [in] hPnp         :	PnP Handle
 *
 * @param [in] nodeAddr     :	Node Address
 *
 * @param [in] I2CAddr      :	I2C Address of peripheral
 *
 * @param [in] nWrite       :	Number of bytes to be written
 *
 * @param [in] wBuf         :	write buffer
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 * @sa
 *
 * @note: This API can only be used when access interface is I2C
 */
A2B_PNP_RESULT a2b_pnp_I2CWritePeri(A2B_PNP_HANDLE hPnp, a2b_Int16 nodeAddr, a2b_UInt16 I2CAddr, a2b_UInt16 nWrite,
                           void* wBuf)
{
	a2b_App_t * pApp_Info;
	A2B_PNP_RESULT nResult = A2B_PNP_RESULT_SUCCESS;
	a2b_UInt32 nA2bResult=0;

	if(a2b_VerifyPnPHandle(hPnp) == A2B_PNP_RESULT_FAILED)
	{
		return A2B_PNP_RESULT_FAILED;
	}

	pApp_Info = (a2b_App_t *)hPnp;

	/* TO DO:
	 * Verify the access interface */

	nA2bResult =  a2b_i2cPeriphWrite(pApp_Info->ctx, nodeAddr, I2CAddr, nWrite, wBuf);
	if(nA2bResult != 0u)
	{
		nResult = A2B_PNP_RESULT_FAILED;
	}

	return nResult;
}


/**
 * @brief       Read memory/register of the I2C Peripheral when access interface is SPI
 *
 * @param [in] hPnp         :	PnP Handle
 *
 * @param [in] nodeAddr     :	Node Address
 *
 * @param [in] I2CAddr      :	I2C Address of peripheral
 *
 * @param [in] nWrite       :	Number of bytes to be written
 *
 * @param [in] wBuf         :	write buffer
 *
 * @param [in] nRead        :	Number of bytes to be read
 *
 * @param [out] rBuf        :	pointer to read buffer
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 * @sa
 *
 * @note: This API can only be used when access interface is SPI
 */
A2B_PNP_RESULT a2b_pnp_SPIReadPeri(A2B_PNP_HANDLE hPnp, a2b_Int16 nodeAddr, a2b_UInt16 I2CAddr, a2b_UInt16 nWrite,
		                  void* wBuf, a2b_UInt16 nRead, void* rBuf)
{
	/* SPI to I2C read */
	a2b_App_t * pApp_Info;
	a2b_HResult status;
	a2b_UInt16 spiCmd = A2B_CMD_SPI_REMOTE_I2C_READ_REQUEST;

	if(a2b_VerifyPnPHandle(hPnp))
	{
		return A2B_PNP_RESULT_FAILED;
	}
	pApp_Info = (a2b_App_t *)hPnp;

	status = a2b_spiPeriphWriteRead(pApp_Info->ctx, nodeAddr, spiCmd, I2CAddr, 0/*No Slave select*/, nWrite, wBuf, nRead, rBuf);

	return (A2B_PNP_RESULT)status;

}


/**
 * @brief       Write memory/register of the I2C Peripheral when access interface is SPI
 *
 * @param [in] hPnp         :	PnP Handle
 *
 * @param [in] nodeAddr     :	Node Address
 *
 * @param [in] I2CAddr      :	I2C Address of peripheral
 *
 * @param [in] nWrite       :	Number of bytes to be written
 *
 * @param [in] wBuf         :	write buffer
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 * @sa
 *
 * @note: This API can only be used when access interface is SPI
 */
A2B_PNP_RESULT a2b_pnp_SPIWritePeri(A2B_PNP_HANDLE hPnp, a2b_Int16 nodeAddr, a2b_UInt16 I2CAddr, a2b_UInt16 nWrite,
                           void* wBuf)
{
	/* SPI to I2C write */
	a2b_App_t * pApp_Info;
	a2b_HResult status;
	a2b_UInt16 spiCmd = A2B_CMD_SPI_REMOTE_I2C_WRITE;

	if(a2b_VerifyPnPHandle(hPnp))
	{
		return A2B_PNP_RESULT_FAILED;
	}
	pApp_Info = (a2b_App_t *)hPnp;

	status = a2b_spiPeriphWrite(pApp_Info->ctx, nodeAddr, spiCmd, I2CAddr, 0/*No Slave select*/, nWrite, wBuf);

	return (A2B_PNP_RESULT)status;
}


/**
 * @brief       Write memory/register of the SPi Peripheral when access interface is SPI
 *
 * @param [in] hPnp         :	PnP Handle
 *
 * @param [in] pSpiWrRdParams     :	SPI write/read parameters
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 * @sa
 *
 *
 */
A2B_PNP_RESULT a2b_pnp_SPIDTWriteReadPeri(A2B_PNP_HANDLE hPnp, a2b_SpiWrRdParams * pSpiWrRdParams)
{
	/* SPI to SPI write */
	a2b_App_t * pApp_Info;
	a2b_HResult status;

	if(a2b_VerifyPnPHandle(hPnp))
	{
		return A2B_PNP_RESULT_FAILED;
	}
	pApp_Info = (a2b_App_t *)hPnp;

	status = adi_a2b_spiPeriWrRd(pApp_Info->ctx, pSpiWrRdParams);

	return (A2B_PNP_RESULT)status;
}


/**
 * @brief       Set SPI Mode
 *
 * @param [in] hPnp         :	PnP Handle
 *
 * @param [in] config         :	SPI configuration
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 * @sa
 *
 * @note: This API can be used when access interface is SPI
 */
A2B_PNP_RESULT a2b_pnp_SPIPeriSetMode(A2B_PNP_HANDLE hPnp, a2b_SpiConfig* config)
{
	a2b_App_t * pApp_Info;
	a2b_HResult status;
	if(a2b_VerifyPnPHandle(hPnp))
	{
		return A2B_PNP_RESULT_FAILED;
	}
	pApp_Info = (a2b_App_t *)hPnp;

	status = adi_a2b_spiPeriSetMode(pApp_Info->ctx, config);

	return (A2B_PNP_RESULT)status;
}



/**
 * @brief      This function allows the application to clear the faults & continue with blind discovery
 *
 * @param [in] hPnp         :	PnP Handle
 *
 * @param [in]  bReStartFromMain    :   Flag to restart the blind discovery from Main
 *
 * @return Status
 *
 *
 * @sa
 *
 */
A2B_PNP_RESULT a2b_pnp_ClearHwFaults(A2B_PNP_HANDLE hPnp, a2b_UInt8 bReStartFromMain)
{

 	a2b_App_t * pApp_Info;
	a2b_HResult result = A2B_PNP_RESULT_SUCCESS;
	if(a2b_VerifyPnPHandle(hPnp))
	{
		return A2B_PNP_RESULT_FAILED;
	}
	pApp_Info = (a2b_App_t *)hPnp;

	if( bReStartFromMain == 1)
	{
		pApp_Info->ePnPModuleState = A2B_PNP_INIT;

		(void)memset((void*)&(pApp_Info->PnpNWInfo), 0, sizeof(a2bpnp_NetworkInfo));
		(void)memset((void*)&pApp_Info->SubNodeTypeInfo, 0, (sizeof(A2B_PNP_SOUCESINK_INFO)*A2B_CONF_MAX_NUM_SLAVE_NODES));
		pApp_Info->bdd.nodes_count = 1;

		/* stop a2b stack */
		(void)pApp_Info->stkFunc.stkStopStack(pApp_Info);
		removeNodeId(getChainIdx(pApp_Info), -1, 0);
	}
	else
	{
		pApp_Info->faultStatus = "";
		pApp_Info->faultNode = pApp_Info->bdd.nodes_count - 1;	
		pApp_Info->ePnPModuleState = A2B_PNP_NETWORK_MNG;
	}

	return result;
}

/**
 * @brief      Called to set up a GPIO over distance signaling
 *
 * @param [in] hPnp            :	PnP Handle
 *
 * @param [in]  virtualportNum            :	Port Number
 *
 * @param [in]  participatorList    :   GPIOD participator list
 *
 * @param [in]  count   :   number of items in the list
 *
 *
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa
 *
 */
A2B_PNP_RESULT a2b_pnp_SetupGPIOOverDistance(A2B_PNP_HANDLE hPnp, a2b_UInt8 virtualportNum, a2bpnp_GpiodParam participatorList[], a2b_UInt8 count )
{
	a2b_App_t * pApp_Info;
	a2b_UInt8 idx, bddIdx;
	a2b_UInt32* maskPtr;
	a2b_HResult result = A2B_PNP_RESULT_FAILED;
	if(a2b_VerifyPnPHandle(hPnp))
	{
		return A2B_PNP_RESULT_FAILED;
	}
	pApp_Info = (a2b_App_t *)hPnp;

	for(idx = 0u;idx < count;idx++)
	{
		bddIdx = participatorList[idx].nodeId + 1;

		if(bddIdx >= pApp_Info->bdd.nodes_count)
		{
			return A2B_PNP_RESULT_FAILED;
		}
		maskPtr = &pApp_Info->bdd.nodes[bddIdx].gpioDist.gpiod0msk;
		*(maskPtr + participatorList[idx].gpioNum) |= (1<< virtualportNum);
		pApp_Info->bdd.nodes[bddIdx].gpioDist.gpioden |= (1<< participatorList[idx].gpioNum);
		pApp_Info->bdd.nodes[bddIdx].gpioDist.gpiodinv &= (0xFF ^ (1u << participatorList[idx].gpioNum));
		pApp_Info->bdd.nodes[bddIdx].gpioDist.gpiodinv |= ((uint32_t)participatorList[idx].invertEnable << participatorList[idx].gpioNum);

		result = a2b_AppWriteReg(pApp_Info->ctx, participatorList[idx].nodeId , A2B_REG_GPIODEN, pApp_Info->bdd.nodes[bddIdx].gpioDist.gpioden);
		result |= a2b_AppWriteReg(pApp_Info->ctx, participatorList[idx].nodeId , (A2B_REG_GPIOD0MSK + participatorList[idx].gpioNum), *(maskPtr + participatorList[idx].gpioNum));
		result |= a2b_AppWriteReg(pApp_Info->ctx, participatorList[idx].nodeId , A2B_REG_GPIODINV, pApp_Info->bdd.nodes[bddIdx].gpioDist.gpiodinv);

	}

	return (A2B_PNP_RESULT)result;
}

/**
 * @brief      Called to clear a GPIO over distance signaling
 *
 * @param [in] hPnp            :	PnP Handle
 *
 * @param [in]  virtualportNum            :	Port Number
 *
 * @param [in]  participatorList    :   GPIOD participator list
 *
 * @param [in]  count   :   number of items in the list
 *
 *
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa
 *
 */
A2B_PNP_RESULT a2b_pnp_ClearGPIOOverDistance(A2B_PNP_HANDLE hPnp, a2b_UInt8 virtualportNum, a2bpnp_GpiodParam participatorList[], a2b_UInt8 count )
{
	a2b_App_t * pApp_Info;
	a2b_UInt8 idx, bddIdx;
	a2b_UInt32* maskPtr;
	a2b_HResult result =  A2B_PNP_RESULT_FAILED;
	if(a2b_VerifyPnPHandle(hPnp))
	{
		return A2B_PNP_RESULT_FAILED;
	}
	pApp_Info = (a2b_App_t *)hPnp;

	for(idx = 0u;idx < count;idx++)
	{
		bddIdx = participatorList[idx].nodeId + 1;

		if(bddIdx >= pApp_Info->bdd.nodes_count)
		{
			return A2B_PNP_RESULT_FAILED;
		}
		maskPtr = &pApp_Info->bdd.nodes[bddIdx].gpioDist.gpiod0msk;
		*(maskPtr + participatorList[idx].gpioNum) &= (0xFF^ (1u << virtualportNum));
		pApp_Info->bdd.nodes[bddIdx].gpioDist.gpioden &= (0xFF ^ (1u << participatorList[idx].gpioNum));
		pApp_Info->bdd.nodes[bddIdx].gpioDist.gpiodinv &=(0xFF ^ (participatorList[idx].invertEnable << participatorList[idx].gpioNum));

		result = a2b_AppWriteReg(pApp_Info->ctx, participatorList[idx].nodeId , A2B_REG_GPIODEN, pApp_Info->bdd.nodes[bddIdx].gpioDist.gpioden);
		result |= a2b_AppWriteReg(pApp_Info->ctx, participatorList[idx].nodeId , (A2B_REG_GPIOD0MSK + participatorList[idx].gpioNum), pApp_Info->bdd.nodes[bddIdx].gpioDist.gpioden);
		result |= a2b_AppWriteReg(pApp_Info->ctx, participatorList[idx].nodeId , A2B_REG_GPIODINV, pApp_Info->bdd.nodes[bddIdx].gpioDist.gpioden);

	}

	return (A2B_PNP_RESULT)result;
}

/**
 * @brief      Called to read GPIO over divirtual port
 *
 * @param [in] hPnp     :	PnP Handle
 *
 * @param [in] virtualportNum  :	PnP Handle
 *
 * @param [in] pVal    :   Pointer to virtual port read

 *
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa
 *
 */
A2B_PNP_RESULT a2b_pnp_ReadVirtualIOPort(A2B_PNP_HANDLE hPnp, a2b_UInt8 virtualportNum, a2b_UInt8* pVal)
{
	a2b_App_t * pApp_Info;
	a2b_UInt32 value;
	a2b_HResult result = A2B_PNP_RESULT_SUCCESS;
	if(a2b_VerifyPnPHandle(hPnp))
	{
		return A2B_PNP_RESULT_FAILED;
	}
	pApp_Info = (a2b_App_t *)hPnp;

	(void)a2b_AppReadReg(pApp_Info->ctx, A2B_NODEADDR_MASTER, A2B_REG_GPIODDAT, &value);
	*pVal = (a2b_UInt8)((value & (1<<virtualportNum)>>virtualportNum));

	return (A2B_PNP_RESULT)result;
}

/**
 * @brief      Reads vendor/OEM specific supplier info from the PnP EEPROM
 *
 * @param [in] hPnp     :	PnP Handle
 *
 * @param [in] location  :	Address field
 *
 * @param [in] buf       :   Buffer pointer
 *
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa
 *
 */
A2B_PNP_RESULT a2b_pnp_ReadOEMInfo(A2B_PNP_HANDLE hPnp, a2b_Int16 nodeId, a2b_UInt8* buf)
{
    a2b_HResult result = A2B_PNP_RESULT_SUCCESS;
	a2b_App_t * pApp_Info;
	a2bpnp_NetworkInfo *PnpNWInfo;

	/* Verify PnP handle */
	if(a2b_VerifyPnPHandle(hPnp) == A2B_PNP_RESULT_FAILED)
	{
		return A2B_PNP_RESULT_FAILED;
	}
	pApp_Info = (a2b_App_t *)hPnp;
	PnpNWInfo = &pApp_Info->PnpNWInfo;

	if(PnpNWInfo->DevInfo[nodeId].vendorDataLen > 0)
	{
		(void*)memcpy(buf, PnpNWInfo->DevInfo[nodeId].vendorData, PnpNWInfo->DevInfo[nodeId].vendorDataLen);
	}
	return (A2B_PNP_RESULT)result;
}

/**
 * @brief      This function is called to mute or unmute the whole A2B network
 *
 * @param [in] hPnp     :	PnP Handle
 *
 * @param [in] bMute   :	A2B_TRUE : mute or A2B_FALSE :unmute
 *
 *
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa
 *
 */
A2B_PNP_RESULT a2b_pnp_MuteNetwork(A2B_PNP_HANDLE hPnp, a2b_Bool bMute)
{
	a2b_App_t * pApp_Info;
	a2b_HResult result = A2B_PNP_RESULT_SUCCESS;
	if(a2b_VerifyPnPHandle(hPnp))
	{
		return A2B_PNP_RESULT_FAILED;
	}
	pApp_Info = (a2b_App_t *)hPnp;

	/* Add code here */
	result = networkMute(pApp_Info,bMute);

	return (A2B_PNP_RESULT)result;
}

/**
 * @brief      This function is called to get network Bandwidth
 *
 * @param [in] pNode     :	Node which adds the maximum bandwidth (Pointer)
 *
 * @param [in] pUsage     :	Overall usage (Pointer)
 *
 *
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa
 *
 */
A2B_PNP_RESULT a2b_pnp_GetNwBW(A2B_PNP_HANDLE hPnp, a2b_Int32* pNode, a2b_UInt32* pUsage)
{
	a2b_App_t * pApp_Info;
	a2b_HResult result = A2B_PNP_RESULT_SUCCESS;
	if(a2b_VerifyPnPHandle(hPnp))
	{
		return A2B_PNP_RESULT_FAILED;
	}
	pApp_Info = (a2b_App_t *)hPnp;

	/* Get the network bandwidth*/
	getNwBw(pApp_Info, pNode, pUsage);

	return (A2B_PNP_RESULT)result;
}

/**
 * @brief      This function gets the state of the PnP Module
 * *
 * @param [in] peState     :	Pointer to the PNP module state
 *
 *
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa
 *
 */
A2B_PNP_RESULT a2b_pnp_GetModuleState(A2B_PNP_HANDLE hPnp, A2B_PNP_STATE* peState )
{
	a2b_App_t * pApp_Info;
	a2b_HResult result = A2B_PNP_RESULT_SUCCESS;
	if(a2b_VerifyPnPHandle(hPnp))
	{
		return A2B_PNP_RESULT_FAILED;
	}
	pApp_Info = (a2b_App_t *)hPnp; 
   *peState  = pApp_Info->ePnPModuleState;

	return (A2B_PNP_RESULT)result;
}

/**
 * @brief      This function gets the nodeID for the given node name
 *
 * @param [in] hPnp     :	PnP Handle
 *
 * @param [in] tgtNodeId   : Pointer node ID (ouput)
 * 
 * @param [in] nodeName   :	 Pointer Node name 
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa 
 *
 */
A2B_PNP_RESULT a2b_pnp_GetNodeIDByName(A2B_PNP_HANDLE hPnp, a2b_Int32 *tgtNodeId, a2b_Char* nodeName, a2b_Int32* found)
{
	a2b_HResult result = A2B_PNP_RESULT_SUCCESS;
	a2b_App_t *pApp_Info;
	a2b_UInt32 i;
	*found = 0;

	if(a2b_VerifyPnPHandle(hPnp))
	{
		return A2B_PNP_RESULT_FAILED;
	}
    pApp_Info = (a2b_App_t *)hPnp;

	for(i = 0;i < pApp_Info->PnpNWInfo.nDeviceCount; i++)
	{
		if(compareNodeName(nodeName, (char*)pApp_Info->PnpNWInfo.DevInfo[i].DeviceName))
		{
			*tgtNodeId = i;
			*found = 1;
			break;
		}
	}
	return result;
}
/**
 * @brief      This function gets the time taken by the PnP module to discover & configure a node
 *
 * @param [in] hPnp     :	PnP Handle
 *
 * @param [in] nodId   :	 node Id 
 * 
 * @param [in] discTimeMs   :	Pointer to time (ms)
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa 
 *
 */
A2B_PNP_RESULT a2b_pnp_GetDiscTimeInfo(A2B_PNP_HANDLE hPnp, a2b_Int32 nodId, a2b_UInt32 *discTimeMs)
{
	a2b_HResult result = A2B_PNP_RESULT_SUCCESS;
	a2b_App_t * pApp_Info;
	if(a2b_VerifyPnPHandle(hPnp))
	{
		return A2B_PNP_RESULT_FAILED;
	}
    pApp_Info = (a2b_App_t *)hPnp;

	*discTimeMs = (pApp_Info->nwPerformance.timeToDiscMs[nodId]);

	return result;

}

/**
 * @brief      This function gets power configuration info of the node
 *
 * @param [in] hPnp     :	PnP Handle
 *
 * @param [in] nodeId   :	 node Id 
 * 
 * @param [in] pPwrInfo   :	Pointer to power information structure
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa 
 *
 */
A2B_PNP_RESULT a2b_pnp_GetDevicePwrInfo(A2B_PNP_HANDLE hPnp, a2b_Int32 nodeId, a2bpnp_DevicePwrInfo *pPwrInfo)
{
	a2b_HResult result = A2B_PNP_RESULT_SUCCESS;
	a2b_App_t * pApp_Info;
	if(a2b_VerifyPnPHandle(hPnp))
	{
		return A2B_PNP_RESULT_FAILED;
	}
	pApp_Info = (a2b_App_t *)hPnp;
	if(nodeId > (pApp_Info->PnpNWInfo.nDeviceCount - 1))
	{
		A2B_APP_ERR_LOG("Node is not discovered");
		return A2B_PNP_RESULT_FAILED;
	}
	if(nodeId == A2B_NODEADDR_MASTER)
	{
		A2B_APP_ERR_LOG("Invalid Node ID");
		return A2B_PNP_RESULT_FAILED;
	}


	*pPwrInfo = (pApp_Info->PnpNWInfo.DevInfo[nodeId].pwrInfo);

	return result;

}

/**
 * @brief      This function gets the version number for PnP module
 *
 * @param [in] hPnp     :	PnP Handle
 *
 * @param [in] nodId   :	 node Id 
 * 
 * @param [in] discTimeMs   :	Pointer to time (ms)
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa 
 *
 */
A2B_PNP_RESULT a2b_pnp_GetVersion( a2b_UInt32* major,
        a2b_UInt32* minor,
        a2b_UInt32* patch)
{

	*major = A2B_PNP_MAJOR_VER;
	*minor = A2B_PNP_MINOR_VER;
	*patch = A2B_PNP_PATCH_VER;

	return A2B_PNP_RESULT_SUCCESS;

}

/**
 * @brief      This function updates all the EEPROM with supplied module info
 *
 * @param [in] hPnp     :	PnP Handle
 *
 * @param [in] fileName   :	 Pointer to binary file
 * 
 * @param [in] eePromAddr   :	EEPROM I2C Address
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa 
 *
 */
A2B_PNP_RESULT a2b_pnp_UpdatePnPModuleInfoAll(A2B_PNP_HANDLE hPnp, a2b_Char* fileName, a2b_UInt16 eePromAddr)
{
	FILE *hexFile;
    a2b_UChar buffer[A2B_PNP_EEPROM_PAGE_SIZE +  2];
    a2b_UInt32 bytesRead = 0, e2promRegAddr = 0;
	a2b_App_t * pApp_Info;
	a2b_UInt16 eePromAddrToWrite, nodeId;
	a2b_UInt32 currTime;

	if(a2b_VerifyPnPHandle(hPnp))
	{
		return A2B_PNP_RESULT_FAILED;
	}
    pApp_Info = (a2b_App_t *)hPnp;

	hexFile = fopen(fileName, "rb");
    if (hexFile == NULL) {
		A2B_APP_ERR_LOG("Failed to open the file");
        return A2B_PNP_RESULT_FAILED;
    }

	for(nodeId = 0; nodeId < pApp_Info->PnpNWInfo.nDeviceCount; nodeId++)
	{
		//check whether is there any need to override the 
		eePromAddrToWrite = (eePromAddr == 0) ? pApp_Info->PnpNWInfo.DevInfo[nodeId].eepromAddr: eePromAddr;

		//init register to zero
		buffer[0] = 0;
		buffer[1] = 0;
		//setting the registers to 0
        e2promRegAddr = 0;

		//reading the file
		while ((bytesRead = fread(&buffer[2], 1, A2B_PNP_EEPROM_PAGE_SIZE, hexFile)) > 0) {
			//write to EEPROM page	
			if(a2b_pnp_I2CWritePeri(pApp_Info, nodeId, eePromAddrToWrite, (A2B_PNP_EEPROM_PAGE_SIZE +  2), buffer))
			{
				A2B_APP_ERR_LOG("Failed to write to EEPROM:0x%x of Node:%d",eePromAddrToWrite, nodeId);
				return A2B_PNP_RESULT_FAILED;
			}

			currTime = pApp_Info->pal.timerGetSysTime();
			while((pApp_Info->pal.timerGetSysTime() - currTime) < 50);

			//increment the address
			e2promRegAddr+= A2B_PNP_EEPROM_PAGE_SIZE;
			buffer[1] = e2promRegAddr & 0xFF;
			buffer[0] = (e2promRegAddr & 0xFF00)>>8;

		}
		fseek(hexFile, 0, SEEK_SET);
	}
	return A2B_PNP_RESULT_SUCCESS;

}

/**
 * @brief      This function updates the EEPROM with supplied module info
 *
 * @param [in] hPnp     :	PnP Handle
 *
 * @param [in] fileName   :	 Pointer to binary file
 * 
 * @param [in] eePromAddr   :	EEPROM I2C Address
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa 
 *
 */
A2B_PNP_RESULT a2b_pnp_UpdatePnPModuleInfo(A2B_PNP_HANDLE hPnp, a2b_Char* fileName, a2b_Int32 nodeId, a2b_UInt16 eePromAddr)
{
	FILE *hexFile;
    a2b_UChar buffer[A2B_PNP_EEPROM_PAGE_SIZE +  2];
    a2b_UInt32 bytesRead = 0, e2promRegAddr = 0;
	a2b_App_t * pApp_Info;
	a2b_UInt16 eePromAddrToWrite;
	a2b_UInt32 currTime;

	if(a2b_VerifyPnPHandle(hPnp))
	{
		return A2B_PNP_RESULT_FAILED;
	}
    pApp_Info = (a2b_App_t *)hPnp;

	if(nodeId > (pApp_Info->PnpNWInfo.nDeviceCount - 1))
	{
		A2B_APP_ERR_LOG("Node is not discovered");
		return A2B_PNP_RESULT_FAILED;
	}
	if(nodeId == A2B_NODEADDR_MASTER)
	{
		A2B_APP_ERR_LOG("Invalid Node ID");
		return A2B_PNP_RESULT_FAILED;
	}

	hexFile = fopen(fileName, "rb");
    if (hexFile == NULL) {
		A2B_APP_ERR_LOG("Failed to open the file");
        return A2B_PNP_RESULT_FAILED;
    }

    //check whether is there any need to override the 
	eePromAddrToWrite = (eePromAddr == 0) ? pApp_Info->PnpNWInfo.DevInfo[nodeId].eepromAddr: eePromAddr;

    //init register to zero
	buffer[0] = 0;
	buffer[1] = 0;
	//reading the file
	while ((bytesRead = fread(&buffer[2], 1, A2B_PNP_EEPROM_PAGE_SIZE, hexFile)) > 0) {
		//write to EEPROM page	
		if(a2b_pnp_I2CWritePeri(pApp_Info, nodeId, eePromAddrToWrite, (A2B_PNP_EEPROM_PAGE_SIZE +  2), buffer))
		{
			A2B_APP_ERR_LOG("Failed to write to EEPROM:0x%x",eePromAddrToWrite);
			return A2B_PNP_RESULT_FAILED;
		}
		//Add page delay
		currTime = pApp_Info->pal.timerGetSysTime();
		while((pApp_Info->pal.timerGetSysTime() - currTime) < 50);
		//increment the address
		e2promRegAddr+= A2B_PNP_EEPROM_PAGE_SIZE;
		buffer[1] = e2promRegAddr & 0xFF;
		buffer[0] = (e2promRegAddr & 0xFF00)>>8;

    }

	return A2B_PNP_RESULT_SUCCESS;

}
/**
 * @brief      This function ugets the vendor ID (8 bytes) of the module/device
 *
 * @param [in] hPnp     :	PnP Handle
 *
 * @param [in] nodeId   :	 Targeted node ID
 * 
 * @param [in] vendorID   :	pointer to vendor ID
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa 
 *
 */
A2B_PNP_RESULT a2b_pnp_GetVendorID(A2B_PNP_HANDLE hPnp, a2b_Int32 nodeId, a2b_UInt8 *vendorID)
{
	a2b_HResult result = A2B_PNP_RESULT_SUCCESS;
	a2b_App_t * pApp_Info;
	if(a2b_VerifyPnPHandle(hPnp))
	{
		return A2B_PNP_RESULT_FAILED;
	}
    pApp_Info = (a2b_App_t *)hPnp;

	//Not applicable for main node
	if(nodeId == A2B_NODEADDR_MASTER)
	{
 		return A2B_PNP_RESULT_FAILED;
	}
	
	if(nodeId > (pApp_Info->PnpNWInfo.nDeviceCount - 1))
	{
		return A2B_PNP_RESULT_FAILED;
	}


	(void)memcpy(vendorID, pApp_Info->PnpNWInfo.DevInfo[nodeId].vendorID, A2B_PNP_VENDOR_ID_LEN);

	return result;

}
/**
 * @brief      This function allows application to reject a particular node
 *
 * @param [in] hPnp     :	PnP Handle
 *
 * @param [in] nodeId   :	 Node to be removed
 * 
 *
 * @return Status
 *
 *  - #A2B_PNP_RESULT_SUCCESS              Operation Passed.
 *  - #A2B_PNP_RESULT_FAILED               Operation Failed
 *
 * @sa 
 *
 */
A2B_PNP_RESULT a2b_pnp_RejectNode(A2B_PNP_HANDLE hPnp, a2b_Int32 nodeId)
{
	a2b_HResult result = A2B_PNP_RESULT_SUCCESS;
	a2b_App_t * pApp_Info;
	if(a2b_VerifyPnPHandle(hPnp))
	{
		return A2B_PNP_RESULT_FAILED;
	}
    pApp_Info = (a2b_App_t *)hPnp;

	if(nodeId > (pApp_Info->PnpNWInfo.nDeviceCount - 1))
	{
		return A2B_PNP_RESULT_FAILED;
	}

	pApp_Info->PnpNWInfo.nDeviceCount = (uint8_t)nodeId-1u;

    //kill the node
	a2b_pnp_WriteReg(hPnp, (nodeId -1), A2B_REG_SWCTL, 0x00);
	a2b_pnp_WriteReg(hPnp, (nodeId -1), A2B_REG_SWCTL2, 0x00);

	if(pApp_Info->bDebug)
	{
		A2B_APP_DBG_LOG("Node Rejected: %d\n",nodeId);
	}
	return result;
}

/*! \addtogroup Not_Support Unsupported API
	These APIs are not currently supported. Planned for future use
 *  @{
 */

/**
 * @brief      Called to set certain parameters after new device discovery.
 *
 * @param [in] hPnp         :	PnP Handle
 *
 * @param [in]  nodeAddr    :   Node Address
 *
 * @param [in]  PnpParams   :   Structure passed to set the Parameters
 *
 * @return Status
 *
 *  - #A2B_PNP_API_NOT_SUPPORTED      API is not supported.
 *
 * @sa
 *
 * @note: This API can be called only in #A2B_PNP_NEW_NODE_CONNECTED Notification callback function
 */
A2B_PNP_RESULT a2b_pnp_SetNodeParams(A2B_PNP_HANDLE hPnp, a2b_UInt8 nodeAddr, a2bpnp_AppParams *PnpParams)
{
	A2B_UNUSED(hPnp);
	A2B_UNUSED(nodeAddr);
	A2B_UNUSED(PnpParams);
	return A2B_PNP_API_NOT_SUPPORTED;
}

/**
 * @brief      Called to get certain parameters for a give device
 *
 * @param [in] hPnp         :	PnP Handle
 *
 * @param [in]  nodeAddr    :   Node Address
 *
 * @param [in]  PnpParams   :   Structure passed to get the Parameters
 *
 * @return Status
 *
 *  - #A2B_PNP_API_NOT_SUPPORTED      API is not supported.
 *
 * @sa
 *
 */
A2B_PNP_RESULT a2b_pnp_GetNodeParams(A2B_PNP_HANDLE hPnp, a2b_UInt8 nodeAddr, a2bpnp_AppParams **PnpParams)
{
	A2B_UNUSED(hPnp);
	A2B_UNUSED(nodeAddr);
	A2B_UNUSED(PnpParams);
	return A2B_PNP_API_NOT_SUPPORTED;
}

A2B_PNP_RESULT a2b_pnp_AddConByNodeName()
{
    a2b_HResult result = A2B_PNP_API_NOT_SUPPORTED;
	return result;
}

A2B_PNP_RESULT a2b_pnp_RemoveConByNodeName()
{ 
	a2b_HResult result = A2B_PNP_API_NOT_SUPPORTED;
	return result;
}

/**
 * @brief      This function is runs the network diagnostics and shares the result 
 *
 * @param [in] pRes     :	Overall usage (Pointer)
 *
 *
 * @return Status
 *
 *  - #A2B_PNP_API_NOT_SUPPORTED              API not Supported.
 *
 * @sa
 *
 */
A2B_PNP_RESULT a2b_pnp_RunNwDiagnostics(A2B_PNP_HANDLE hPnp, a2b_pnp_diagRes* pRes)
{
	A2B_UNUSED(pRes);
	if(a2b_VerifyPnPHandle(hPnp))
	{
		return A2B_PNP_RESULT_FAILED;
	}

	/* Add code here */

	return (A2B_PNP_RESULT)A2B_PNP_API_NOT_SUPPORTED;
}

/**
 @}
*/

/**
 @}
*/