/*******************************************************************************
Copyright (c) 2022 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
*******************************************************************************

   Name       : a2bpnp_stack_interface.c

   Description: This file is responsible for interfacing A2B Stack functions,

   Developed by: Automotive Software and Systems team, Bangalore, India

******************************************************************************/

/*============= I N C L U D E S =============*/

/*============= D E F I N E S =============*/

#include <string.h>
#include "a2b/pal.h"
#include "assert.h"
#include "a2b/msgtypes.h"
#include <a2bpnp.h>
#include <a2bpnp_local.h>
#include <a2bpnp_stack_interface.h>
#include <stdlib.h>

/*============= D A T A =============*/
a2b_HResult a2bapp_pluginsLoad(struct a2b_PluginApi** plugins, a2b_UInt16* numPlugins, A2B_ECB* ecb);
a2b_HResult a2bapp_pluginsUnload(struct a2b_PluginApi* plugins, a2b_UInt16 numPlugins, A2B_ECB* ecb);

#ifdef A2B_APP_STATIC_MEMORY_FOR_STACK
static a2b_Byte gStackMemBuf[A2BAPP_STACK_NW_MEMORY];
static a2b_Byte gPluginMemBuf[A2BAPP_PLUGIN_NW_MEMORY];
#endif
/*============= C O D E =============*/

/* Intializes stack interface functions */
a2b_UInt8 a2bpnp_stkinterface_Init(a2b_App_t * pApp_Info)
{
	/* Initialize the A2B Software Stack and Environment Control Block (ECB) */
	(void)a2b_systemInitialize(A2B_NULL, &pApp_Info->ecb);

	/* Initialize the Platform Abstraction Layer (PAL) */
	a2b_palInit(&pApp_Info->pal, &pApp_Info->ecb);

	/* Do necessary base initialization */
	a2b_stackPalInit(&pApp_Info->pal, &pApp_Info->ecb);

    /*make sure Plugins are loaded */
	pApp_Info->pal.pluginsLoad     = a2bapp_pluginsLoad;
    pApp_Info->pal.pluginsUnload   = a2bapp_pluginsUnload;

	return 0;
}

/* Populates Bdd */
a2b_UInt8 a2bpnp_stkinterface_getBdd(a2b_App_t * pApp_Info)
{
 a2b_UInt32 res = 0;
 a2b_UInt8 nIndex1;
 if(pApp_Info->pMainNodeCfg->mainNodeCfg == ADI_A2B_MAINNODE_CONFIG_FILE)
 {
#if defined(A2B_BCF_FROM_FILE_IO)
	/* BCF file Open */
	res = a2b_pal_FileOpen(&pApp_Info->ecb, (char*)pApp_Info->pMainNodeCfg->pFilePath);

	if(res != 0)
	{
		ERR_CALLBACK pfErrCallback = pApp_Info->pfErrCallback;
		pfErrCallback((A2B_PNP_HANDLE)pApp_Info, A2B_PNP_BCF_FILE_OPEN_ERR, A2B_NULL);
		if(pApp_Info->bDebug)
		{
			A2B_APP_DBG_LOG("\n\rFAILED to Open the file \n\r");
		}
		return res;
	}

#ifdef A2B_APP_STATIC_MEMORY_FOR_STACK
	pApp_Info->panDatFileBuff = gE2PromMemBuf;
	pApp_Info->pTargetProperties = &gTgtProperties;
#else
	/* Assuming the max size of file read */
	pApp_Info->panDatFileBuff = malloc( A2BAPP_E2PROM_BLOCK_MEMORY );
	pApp_Info->pTargetProperties = malloc(sizeof(ADI_A2B_NETWORK_CONFIG));
#endif

	/* Create A2B Target properties */
	memset(pApp_Info->pTargetProperties, 0, sizeof(ADI_A2B_NETWORK_CONFIG));

	/* Populate BDD through binary file read */
	(void)a2b_get_bddFrmE2promOrFileIO(&pApp_Info->ecb, &pApp_Info->bdd, pApp_Info->panDatFileBuff, pApp_Info->anEeepromPeriCfgInfo, pApp_Info->pTargetProperties);
	/* Find the pointer where audio host config info is stored */
	pApp_Info->ecb.palEcb.pEepromAudioHostConfig = &pApp_Info->anEeepromPeriCfgInfo[0];
	pApp_Info->ecb.palEcb.cfgHostFromFile = 1;
	for (nIndex1 = 0u; nIndex1 < (a2b_UInt8)A2B_CONF_MAX_NUM_SLAVE_NODES; nIndex1++)
	{
		pApp_Info->bdd.nodes[1+nIndex1].verifyNodeDescr = A2B_FALSE;
	}

#endif
 }
 else
 {
	/* Parse BCf and store in BDD */
	a2b_bcfParse_bdd(pApp_Info->pBusDescription, &pApp_Info->bdd, pApp_Info->ecb.palEcb.nChainIndex);

	/* Parse BCF to store peripheral info */
	adi_a2b_ParsePeriCfgTable(pApp_Info->pBusDescription, &pApp_Info->aPeriNetworkTable[0], pApp_Info->ecb.palEcb.nChainIndex);

	for (nIndex1 = 0u; nIndex1 < (a2b_UInt8)pApp_Info->pBusDescription->apNetworkconfig[pApp_Info->ecb.palEcb.nChainIndex]->nNumSlaveNode; nIndex1++)
	{
		pApp_Info->bdd.nodes[1+nIndex1].verifyNodeDescr = A2B_FALSE;
	}
 }
	return res;
}

/* Allocates stack memeory */
a2b_UInt8 a2bpnp_stkinterface_stkAlloc(a2b_App_t * pApp_Info)
{
	a2b_HResult stat;
	/*
	 * Initialize vendor, product, and version information in the ECB.
	 */
	a2b_bddPalInit(&pApp_Info->ecb, &pApp_Info->bdd);
	if(pApp_Info->bDebug)
	{
		A2B_APP_DBG_LOG("BDD PAL Init done \n\r");
	}

#ifdef A2B_APP_STATIC_MEMORY_FOR_STACK
	if(pApp_Info->ecb.baseEcb.heapSize > A2BAPP_STACK_MEMORY_PER_CHAIN)
	{
		stat = 1;
		A2B_APP_DBG_LOG("Insufficient memory \n\r");
		return stat;
	}
	else
	{
		pApp_Info->ecb.baseEcb.heap = &gStackMemBuf[pApp_Info->ecb.palEcb.nChainIndex * A2BAPP_STACK_MEMORY_PER_CHAIN];
	}
#else

	pApp_Info->ecb.baseEcb.heap = malloc(pApp_Info->ecb.baseEcb.heapSize);
	if(pApp_Info->bDebug)
	{
		A2B_APP_DBG_LOG("Allocate Heap done \n\r");
	}
#endif

	/*
	 * Perform the final allocation of the stack based off of the
	 * specifics of this network configuration.
	 */
	pApp_Info->ctx = a2b_stackAlloc(&pApp_Info->pal, &pApp_Info->ecb, &stat);

	return 0;
}

/* sets the access interface for stack , either I2C or SPI */
a2b_UInt8 a2bpnp_stkinterface_SetAcesInterface(a2b_App_t * pApp_Info)
{
	a2b_HResult nResult;
	/* Set the current interface, SPI or I2C */
	pApp_Info->ecb.palEcb.nDeviceInterface = (a2b_UInt32)pApp_Info->bdd.policy.eA2bDeviceInterface;
	a2b_stackSetAccessInterface(pApp_Info->ctx, (ADI_A2B_ACCESS_INTERFACE)pApp_Info->ecb.palEcb.nDeviceInterface);
	nResult = adi_a2b_spiPeriCreate(pApp_Info->ctx);
	return nResult;
}

/* Starts Blind discovery timer */
a2b_UInt8 a2bpnp_stkinterface_StartTimerForBlindDiscovery(a2b_App_t * pApp_Info)
{
	a2b_UInt8 nResult = 0;

	if( pApp_Info->hTmrToHandleBlindDisc == A2B_NULL)
	{
		pApp_Info->hTmrToHandleBlindDisc = a2b_timerAlloc(pApp_Info->ctx, (a2b_TimerFunc)a2b_app_handle_BlindDiscovery, (a2b_Handle)(pApp_Info));
	}

	if(pApp_Info->hTmrToHandleBlindDisc != A2B_NULL)
	{
		/* Set period for blind discovery attempt */
		a2b_timerSet(pApp_Info->hTmrToHandleBlindDisc, pApp_Info->pMainNodeCfg->periodicDiscInterval, 0);
		/* Callback called on timer expiry */
		a2b_timerSetHandler(pApp_Info->hTmrToHandleBlindDisc, a2b_app_handle_BlindDiscovery);
		a2b_timerSetData(pApp_Info->hTmrToHandleBlindDisc, pApp_Info);
		/* Start Periodic Timer for discovery of new node */
		a2b_timerStart(pApp_Info->hTmrToHandleBlindDisc);
	}
	else
	{
		nResult = 1;
	}

	return(nResult);


}

/* Defines notification functions */
a2b_UInt8 a2bpnp_stkinterface_SetNotification(a2b_App_t * pApp_Info)
{
	/* Register for notifications on interrupts */
	pApp_Info->notifyInterrupt = a2b_msgRtrRegisterNotify(pApp_Info->ctx,
			A2B_MSGNOTIFY_INTERRUPT, a2bapp_onInterrupt, pApp_Info, A2B_NULL);

	/* Register for notifications on power faults */
	pApp_Info->notifyPowerFault = a2b_msgRtrRegisterNotify(pApp_Info->ctx,
			A2B_MSGNOTIFY_POWER_FAULT, a2bapp_onPowerFault, pApp_Info, A2B_NULL);

	/* Register for notifications on node discovery */
	pApp_Info->notifyNodeDiscvry = a2b_msgRtrRegisterNotify(pApp_Info->ctx,
	A2B_MSGNOTIFY_NODE_DISCOVERY, a2bapp_onNodeDiscovery, pApp_Info, A2B_NULL);


	pApp_Info->notifyI2CError = a2b_msgRtrRegisterNotify(pApp_Info->ctx,
			A2B_MSGREQ_PERIPH_I2C_ERROR_APP, a2bapp_onI2CFailure, pApp_Info, A2B_NULL);
	return 0;
}

/* Sends partial discovery message */
a2b_UInt8 a2b_sendPartialDiscoveryMessage(a2b_App_t *pApp_Info)
{
	a2b_NetPostDiscovery* discPartialReq;
	struct a2b_Msg *msg;

	msg = a2b_msgAlloc(pApp_Info->ctx, A2B_MSG_REQUEST, A2B_MSGREQ_NET_POST_DISCOVERY);
	if (msg == A2B_NULL) {
		return 1u;
	}
	/* Attach the Boolean flag to the message */
	discPartialReq = (a2b_NetPostDiscovery*)a2b_msgGetPayload(msg);
	discPartialReq->req.bIsPartialDiscEnabled = A2B_TRUE;
	discPartialReq->req.bBusDropDetected = A2B_TRUE;
	discPartialReq->req.nBusDropNodeId = (a2b_Int8)pApp_Info->bdd.nodes_count - 2;

	/* Add this context to the message */
	a2b_msgSetUserData(msg, (a2b_Handle)pApp_Info, A2B_NULL);
	(void)a2b_msgRtrSendRequest(msg, A2B_NODEADDR_MASTER, a2bapp_onBlindDiscoveryComplete);

	/* The message router adds its own reference to the submitted message. */
	(void)a2b_msgUnref(msg);

	return 0;
}

/*****************************************************************************
 *
 *  \b               a2b_stop
 *
 *  This function stops stack, un-registering call-backs, turning off interrupt polling,
 *  disabling sequence charts, and freeing resources associated with the application
 *  context.
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
a2b_UInt8 a2b_stop(a2b_App_t *pApp_Info)
{
	/* Unregister notifications */
	a2b_msgRtrUnregisterNotify(pApp_Info->notifyInterrupt);
	a2b_msgRtrUnregisterNotify(pApp_Info->notifyPowerFault);
	a2b_msgRtrUnregisterNotify(pApp_Info->notifyNodeDiscvry);

	if(pApp_Info->bDebug)
	{
		A2B_APP_DBG_LOG("Unregister notification done \r\n");
	}

	/* Stop interrupt polling */
	(void)a2b_intrStopIrqPoll(pApp_Info->ctx);
	if(pApp_Info->bDebug)
	{
		A2B_APP_DBG_LOG("Stop IRQ done... \r\n");
	}

#ifdef A2B_FEATURE_SEQ_CHART
	if ( A2B_NULL != pApp_Info->seqFile )
	{
		/* Stop the sequence chart */
		a2b_seqChartStop(pApp_Info->ctx);
	}
#endif

	/* Free the stack and heap */
	a2b_stackFree(pApp_Info->ctx);
	pApp_Info->ctx = A2B_NULL;
	if(pApp_Info->bDebug)
	{
		A2B_APP_DBG_LOG("Free Stack context done \r\n");
	}

#ifdef A2B_APP_STATIC_MEMORY_FOR_STACK
	(void)memset(gStackMemBuf, 0, A2BAPP_STACK_NW_MEMORY);
#else
	if (pApp_Info->ecb.baseEcb.heap != NULL)
	{
		(void)free(pApp_Info->ecb.baseEcb.heap);
	}
#endif
	if(pApp_Info->bDebug)
	{
		A2B_APP_DBG_LOG("Free heap done \r\n");
	}

#if  defined (A2B_BCF_FROM_FILE_IO)
	/* BCF file Close */
	(void)a2b_pal_FileClose(&pApp_Info->ecb);
#endif

	/* Shut down the Stack */
	(void)a2b_systemShutdown(A2B_NULL, &pApp_Info->ecb);

	return (0);
}

/*!****************************************************************************
 *
 *  \b               a2b_setupPwrDiag
 *
 *  This function enables/disables line fault monitoring of the stack based on
 *  settings in the bcf.
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
a2b_UInt8 a2b_setupPwrDiag(a2b_App_t *pApp_Info)
{
	a2b_UInt32 nResult = 0;
	struct a2b_Msg *msg;
	a2b_Bool *pIsLineDiagDisabled;

	/* Create a Disable Line Diagnostics request message*/
	msg = a2b_msgAlloc(pApp_Info->ctx, A2B_MSG_REQUEST, A2B_MSGREQ_NET_DISBALE_LINEDIAG);
	if (msg == A2B_NULL) {
		return 1u;
	}

	/* Attach the Boolean flag to the message , By default line diagnostics enabled */
	pIsLineDiagDisabled = (a2b_Bool*)a2b_msgGetPayload(msg);
	*pIsLineDiagDisabled = A2B_FALSE; /* Set the flag to True, in case app wants to disable */

	(void)a2b_msgRtrSendRequest(msg, A2B_NODEADDR_MASTER, A2B_NULL);
	(void)a2b_msgUnref(msg);
	a2b_ActiveDelay(pApp_Info->ctx, 5u);

	return nResult;
}

/*!****************************************************************************
 *
 *  \b               a2b_sendDiscoveryMessage
 *
 *  This function sends a message to master plugin instructing it to initiate
 *  A2B network discovery.
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
a2b_UInt8 a2b_sendInitDiscoveryMessage(a2b_App_t *pApp_Info)
{
	a2b_NetDiscovery *discReq;
	struct a2b_Msg *msg;
	a2b_HResult result = 0;
#ifndef ENABLE_INTERRUPT_PROCESS
	a2b_UInt32 pollTime;
#endif

	/* Create a network discovery request message */
	msg = a2b_msgAlloc(pApp_Info->ctx, A2B_MSG_REQUEST, A2B_MSGREQ_NET_DISCOVERY);
	if (msg == A2B_NULL) {
		return 1u;
	}

	/* Attach the BDD information to the message */
	discReq = (a2b_NetDiscovery*)a2b_msgGetPayload(msg);
	discReq->req.bdd = &pApp_Info->bdd;


#ifdef ADI_SIGMASTUDIO_BCF

		/* Attach additional peripheral initialization data as generated.*/
		discReq->req.periphPkg = (const a2b_Byte *)&pApp_Info->aPeriNetworkTable[0u];
		discReq->req.pkgLen = sizeof(ADI_A2B_NETWORK_PERICONFIG);

		/** Flag which indicates the current is first time discovery or re-discovering the network
		 *   bFrstTimeDisc value
		 *				true: Current execution is first time discovery
		 *				false: Current execution is re-discovery
		 */
		discReq->req.bFrstTimeDisc = pApp_Info->bFrstTimeDisc;

#elif defined(A2B_BCF_FROM_SOC_EEPROM) || defined(A2B_BCF_FROM_FILE_IO)

		/* Attach additional peripheral initialization data as generated.*/
		discReq->req.periphPkg = (const a2b_Byte *)&pApp_Info->anEeepromPeriCfgInfo[2];
		discReq->req.pkgLen = 2 * A2B_CONF_MAX_NUM_SLAVE_NODES;
		discReq->req.bFrstTimeDisc = pApp_Info->bFrstTimeDisc;
#else
		/*
		 * Attach additional peripheral initialization data as generated.
		 * by the Network Configuration Tool. In this case no peripheral
		 * initialization is added.
		 */
		discReq->req.periphPkg = A2B_NULL;
		discReq->req.pkgLen = 0;
		discReq->req.bFrstTimeDisc = pApp_Info->bFrstTimeDisc;
#endif

		/* Add this context to the message */
		a2b_msgSetUserData(msg, (a2b_Handle)pApp_Info, A2B_NULL);
		result = a2b_msgRtrSendRequest(msg, A2B_NODEADDR_MASTER, a2bapp_onDiscoveryComplete);

#ifndef ENABLE_INTERRUPT_PROCESS
	/* Interrupt polling interval in milliseconds */
	pollTime = pApp_Info->pMainNodeCfg->pollTime;

	/* Instruct the Stack to begin interrupt polling every A2BAPP_POLL_PERIOD */
	(void)a2b_intrStartIrqPoll(pApp_Info->ctx, pollTime);
#endif
	/* The message router adds its own reference to the submitted message. */
	(void)a2b_msgUnref(msg);

	return result;
}


#ifdef A2BAPP_LINK_STATICALLY
/*!****************************************************************************
 *
 *  \b               a2bapp_pluginsLoad
 *
 *  If the application is linked statically to its plugins then this
 *  function overrides the default PAL version of this function which loads
 *  plugins dynamically. Instead this function initializes the plugins that
 *  are statically linked to the application.
 *
 *  \param           [in,out]    plugins     A pointer to an array plugins that
 *                                           this function will allocate and
 *                                           return.
 *
 *  \param           [in,out]    numPlugins  This will be assigned the number
 *                                           of plugins that have been
 *                                           loaded and initialized.
 *
 *  \param           [in]        ecb         The environment control block
 *                                           for the A2B stack.
 *
 *  \pre             None
 *
 *  \post            None
 *
 *  \return          A status code that can be checked with the A2B_SUCCEEDED()
 *                   or A2B_FAILED() macro for success or failure of the
 *                   request.
 *
 ******************************************************************************/
a2b_HResult a2bapp_pluginsLoad(struct a2b_PluginApi** plugins, a2b_UInt16* numPlugins, A2B_ECB* ecb)
{
	struct a2b_PluginApi *appPlugins;
	a2b_UInt16 i = 0;
	a2b_App_t 		*pApp_Info = gpApp_Info[ecb->palEcb.nChainIndex];

	A2B_UNUSED(ecb);
#ifdef A2B_APP_STATIC_MEMORY_FOR_STACK
	if(gpApp_Info[ecb->palEcb.nChainIndex]->bdd.nodes_count * sizeof(**plugins) > A2BAPP_PLUGIN_MEMORY_PER_CHAIN)
	{
		A2B_APP_LOG("Not sufficient memory : failed to load plugins \r\n");
		return 1;
	}
	appPlugins = (a2b_PluginApi *)&gPluginMemBuf[ecb->palEcb.nChainIndex * A2BAPP_PLUGIN_MEMORY_PER_CHAIN];
#else
	appPlugins = calloc(gpApp_Info[ecb->palEcb.nChainIndex]->bdd.nodes_count, sizeof(**plugins));
#endif

	(void)A2B_MASTER_PLUGIN_INIT(&appPlugins[0]);
	if(gpApp_Info[ecb->palEcb.nChainIndex]->bDebug)
	{
		A2B_APP_DBG_LOG("Master plugin load done \r\n");
	}

	for (i = 1; i < (gpApp_Info[ecb->palEcb.nChainIndex]->bdd.nodes_count); i++)
	{
		(void)A2B_SLAVE_PLUGIN_INIT(&appPlugins[i]);
	}

	if(gpApp_Info[ecb->palEcb.nChainIndex]->bDebug)
	{
		A2B_APP_DBG_LOG("Slave plugins load done \r\n");
	}

	*plugins = appPlugins;
	*numPlugins = gpApp_Info[ecb->palEcb.nChainIndex]->bdd.nodes_count;

	return 0u;
}

/*!****************************************************************************
 *
 *  \b               a2bapp_pluginsUnload
 *
 *  If the application is linked statically to its plugins then this
 *  function overrides the default PAL version of this function which unloads
 *  plugins dynamically. Instead this function deallocates the plugins that
 *  are statically linked to the application.
 *
 *  \param           [in]        plugins     A pointer to an array plugins that
 *                                           this function will deallocate.
 *
 *  \param           [in]        numPlugins  The number of plugins in the array.
 *
 *  \param           [in]        ecb         The environment control block
 *                                           for the A2B stack.
 *
 *  \pre             None
 *
 *  \post            The plugins are unloaded and destroyed after this call.
 *
 *  \return          A status code that can be checked with the A2B_SUCCEEDED()
 *                   or A2B_FAILED() macro for success or failure of the
 *                   request.
 *
 ******************************************************************************/
a2b_HResult a2bapp_pluginsUnload(struct a2b_PluginApi* plugins, a2b_UInt16 numPlugins, A2B_ECB* ecb)
{
	A2B_UNUSED(numPlugins);
	A2B_UNUSED(ecb);
#ifdef A2B_APP_STATIC_MEMORY_FOR_STACK
	(void)memset(gPluginMemBuf, 0, A2BAPP_PLUGIN_NW_MEMORY);
#else
	(void)free(plugins);
#endif
	return 0u;
}
#endif

/* This function  reads a register value of a particular A2B node */
a2b_HResult a2b_AppReadReg(struct a2b_StackContext* ctx, a2b_Int16 nodeAddr, a2b_UInt32 reg, a2b_UInt32* value)
{
	a2b_HResult result = A2B_RESULT_SUCCESS;
	a2b_Byte wBuf[1];
	struct a2b_StackContext* mCtx;

	/* Need a master plugin context to do I2C calls */
	mCtx = (a2b_StackContext*)a2b_stackContextFind(ctx, A2B_NODEADDR_MASTER);
	if ( A2B_NULL == mCtx)
	{
		/* This should *never* happen */
		result = A2B_MAKE_HRESULT(A2B_SEV_FAILURE, A2B_FAC_DIAG, A2B_EC_INTERNAL);
	}
	else
	{
		wBuf[0] = reg;

		result = a2b_regWriteRead(mCtx, nodeAddr, 1u, &wBuf[0u], 1u, value);
	}

	return (result);
}

/* This function  reads a register value of a particular A2B node */
a2b_HResult a2b_AppBulkReadReg(struct a2b_StackContext* ctx, a2b_Int16 nodeAddr, a2b_UInt32 reg, a2b_UInt8 value[], a2b_UInt8 count)
{
	a2b_HResult result = A2B_RESULT_SUCCESS;
	a2b_Byte wBuf[1];
	struct a2b_StackContext* mCtx;

	/* Need a master plugin context to do I2C calls */
	mCtx = (a2b_StackContext*)a2b_stackContextFind(ctx, A2B_NODEADDR_MASTER);
	if ( A2B_NULL == mCtx)
	{
		/* This should *never* happen */
		result = A2B_MAKE_HRESULT(A2B_SEV_FAILURE, A2B_FAC_DIAG, A2B_EC_INTERNAL);
	}
	else
	{
		wBuf[0] = reg;

		result = a2b_regWriteRead(mCtx, nodeAddr, 1u, &wBuf[0u], count, value);
	}

	return (result);
}

/* This function writes a register value to a particular A2B node */
a2b_HResult a2b_AppWriteReg(struct a2b_StackContext* ctx, a2b_Int16 nodeAddr, a2b_UInt32 reg, a2b_UInt32 value)
{
	a2b_HResult result = A2B_RESULT_SUCCESS;
	a2b_Byte wBuf[2];
	struct a2b_StackContext* mCtx;

	/* Need a master plugin context to do I2C calls */
	mCtx = (a2b_StackContext*)a2b_stackContextFind(ctx, A2B_NODEADDR_MASTER);
	if ( A2B_NULL == mCtx)
	{
		/* This should *never* happen */
		result = A2B_MAKE_HRESULT(A2B_SEV_FAILURE, A2B_FAC_DIAG, A2B_EC_INTERNAL);
	}
	else
	{
		wBuf[0] = reg;
		wBuf[1] = value;

		result = a2b_regWrite(mCtx, nodeAddr, 2u, wBuf);
	}

	return (result);
}

