/*******************************************************************************
Copyright (c) 2018 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
******************************************************************************
 * @file:    adi_a2b_commch.c
 * @brief:   This is the implementation Communication Channel on the slave side
 * @version: $Revision$
 * @date:    $Date$
 * Developed by: Automotive Software and Systems team, Bangalore, India
*****************************************************************************/
/*! \addtogroup Communication_Channel Communication Channel
 *  @{
 */

/** @defgroup Communication_Channel_Slave Communication Channel Slave
 *
 * This module handles the Communication Channel
 *
 */

/*! \addtogroup Communication_Channel_Slave
 *  @{
 */

/*============= I N C L U D E S =============*/
#include <string.h>
#include "adi_a2b_commch.h"
#include "regdefs.h"
/*============= D E F I N E S =============*/

#define A2B_MAILBOXSTAT_FULL_MSK 		A2B_BITM_MBOX1STAT_MB1FULL	/*!< MBOX status full mask */
#define A2B_MBOXSTAT_EMPTY_MSK			A2B_BITM_MBOX1STAT_MB1EMPTY	/*!< MBOX status empty mask */
#define A2B_MBOXFULL_ACITVE_INTR_MSK	A2B_BITM_MBOX1STAT_MB1FIRQ	/*!< MBOX full interrupt mask */
#define A2B_MBOXEMPTY_ACITVE_INTR_MSK	A2B_BITM_MBOX1STAT_MB1EIRQ	/*!< MBOX empty interrupt mask */

/*============= D A T A =============*/

static a2b_UInt8 aMboxStatRegAddr[2] = { A2B_REG_MBOX0STAT, A2B_REG_MBOX1STAT};

/*============= L O C A L  P R O T O T Y P E S =============*/
static A2B_COMMCH_RET 	a2b_CommChTxmitNxtFrame(a2b_CommChInfo *pCommChInfo, a2b_CommChMsg *pCommChTxMsg);
static void 			a2b_CommChChkTxmitTimeOut(a2b_CommChInfo *pCommChInfo, a2b_CommChMsg *pCommChTxMsg);
static A2B_COMMCH_RET 	a2b_CommChMboxRead(a2b_CommChInfo *pCommChInfo, a2b_UInt8 nMboxNo, a2b_UInt8 nNoOfBytes, a2b_Byte *pReadBuf, a2b_Int8 nNodeAddr);
static a2b_UInt8 		a2b_CommChRxMboxStatusQuery(a2b_CommChInfo *pCommChInfo);
static a2b_Bool 		a2b_CommChTxMboxStatusQuery(a2b_CommChInfo *pCommChInfo);
static void 			a2b_CommChTxStateRst(a2b_CommChInfo *pCommChInfo, a2b_Bool bCurTxSuccess);
static A2B_COMMCH_RET 	a2b_CommChEngMboxWriteCbk(void* hCommCh, a2b_UInt8 nMboxNo, a2b_UInt16 nNoOfBytes, a2b_UInt8 *pWriteBuf, a2b_Int8 nNodeAddr);


/*============= C O D E =============*/

/*****************************************************************************/
/*!
@brief			This function opens the slave communication channel and does some housekeeping work such as initializing the I2C driver

@param [in]     pCommChPal		Pointer to comm ch PAL functions
@param [in]     pMem			Pointer to a block of memory
@param [in]     nSizeInBytes	Size of memory which is passed to this function in bytes

@return			a2b_CommPalCtx type
                - NULL	: If failed
                - Valid	: Upon success
*/
/*****************************************************************************/
a2b_CommPalCtx*	adi_a2b_CommChPalInit(a2b_CommChPal *pCommChPal, a2b_UInt8 *pMem, a2b_UInt8 nSizeInBytes, a2b_Handle pCallBackParam)
{
	a2b_CommPalCtx	*pCommPalCtx = A2B_NULL;
	a2b_UInt32		nRes;

	if((pCommChPal != A2B_NULL) && (pMem != A2B_NULL))
	{
		if(nSizeInBytes >= sizeof(a2b_CommPalCtx))
		{
			pCommPalCtx 				= (a2b_CommPalCtx*)pMem;
			pCommPalCtx->pCommChPal 	= pCommChPal;
			pCommPalCtx->hCallbackParam	= pCallBackParam;

		}
	}

	return (pCommPalCtx);
}

/*****************************************************************************/
/*!
@brief			This function creates and initializes an instance of slave communication channel

@param [in]     pCommChConfig		Comm ch slave configuration pointer

@return			a2b_Handle type
                - NULL	: If failed
                - Valid	: Upon success
*/
/*****************************************************************************/
a2b_Handle adi_a2b_CommChCreate(a2b_CommChConfig *pCommChConfig)
{
	a2b_CommChInfo	*pCommChInfo = A2B_INVALID_HANDLE;
	a2b_UInt8			nIdx;

	if(pCommChConfig != A2B_NULL)
	{
		pCommChInfo 		 								= (a2b_CommChInfo*)pCommChConfig->pMem;
		pCommChInfo->eCommChMsgTxState						= A2B_COMMCH_TX_IDLE;
		for(nIdx=0; nIdx<ADI_A2B_COMMCH_ENG_MAX_NO_OF_MAILBOX; nIdx++)
		{
			pCommChInfo->abReadComplete[nIdx]				= 0u;
		}
		pCommChInfo->nTxTimeout								= 0u;
		pCommChInfo->nCurrTimeInMsec	 					= 0u;
		pCommChInfo->nFrameStartTime 						= 0u;
		pCommChInfo->nIntrPollStartTime						= 0u;
		pCommChInfo->nIntrPollPeriod						= 0u;
		pCommChInfo->pCommPalCtx 							= pCommChConfig->pCommPalCtx;

		(void)adi_a2b_CommChEngRstRxInfo(&pCommChInfo->oCommChEngInfo);
		(void)adi_a2b_CommChEngRstTxInfo(&pCommChInfo->oCommChEngInfo);


		pCommChInfo->oCommChEngInfo.pfStatCb			  	= pCommChConfig->pfStatCb;
		pCommChInfo->oCommChEngInfo.pCbParam			  	= pCommChConfig->pCbParam;
		pCommChInfo->oCommChEngInfo.pfMboxWriteCb			= &a2b_CommChEngMboxWriteCbk;
		pCommChInfo->oCommChEngInfo.eFraming				= pCommChConfig->eFraming;
		pCommChInfo->oCommChEngInfo.hCommCh				  	= pCommChInfo;
		pCommChInfo->bMBoxPoll 								= pCommChConfig->bMBoxPoll;
		pCommChInfo->eNodeType								= pCommChConfig->eNodeType;
		if(pCommChInfo->eNodeType == A2B_SLAVE_NODE)
		{
			pCommChInfo->eMailboxAccessType					= A2B_LOCAL_ACCESS;
			pCommChInfo->nTargetNodeNum						= -1;	/* No slave to slave communication */
			pCommChInfo->oCommChEngInfo.nRxMbox				= ADI_A2B_COMMCH_RX_MAILBOX_NO;
			pCommChInfo->oCommChEngInfo.nTxMbox				= ADI_A2B_COMMCH_TX_MAILBOX_NO;
		}
		else
		{
			pCommChInfo->eMailboxAccessType					= A2B_BUS_ACCESS;
			pCommChInfo->nTargetNodeNum						= pCommChConfig->nTargetNodeNum;
			pCommChInfo->oCommChEngInfo.nRxMbox				= ADI_A2B_COMMCH_TX_MAILBOX_NO;
			pCommChInfo->oCommChEngInfo.nTxMbox				= ADI_A2B_COMMCH_RX_MAILBOX_NO;
		}
	}

	/* Query to Mailbox status register to pave way transmission */
	(void)a2b_CommChTxMboxStatusQuery(pCommChInfo);

	return ((a2b_Handle)(pCommChInfo));
}

/*****************************************************************************/
/*!
@brief			This function destroys an instance of slave communication channel

@param [in]     hCommCh		Comm ch slave instance pointer

@return			void
*/
/*****************************************************************************/
void adi_a2b_CommChDestroy(a2b_Handle hCommCh)
{
	a2b_CommChInfo	*pCommChInfo = (a2b_CommChInfo*)hCommCh;
	if(pCommChInfo != A2B_NULL)
	{
		(void)adi_a2b_CommChEngRstRxInfo(&pCommChInfo->oCommChEngInfo);
		(void)adi_a2b_CommChEngRstTxInfo(&pCommChInfo->oCommChEngInfo);
		(void)memset(pCommChInfo, 0, sizeof(a2b_CommChInfo));
	}

}

/*****************************************************************************/
/*!
@brief			This API is used to transmit a message using communication channel

@param [in]     hCommCh	Comm ch slave instance pointer
@param [in]     pMsg		Pointer to comm ch msg
@param [in]     nNodeAddr	Node address for which the msg is being transmitted

@return			A2B_COMMCH_RET type
                - 0: A2B_COMMCH_SUCCESS
                - 1: A2B_COMMCH_FAILED
*/
/*****************************************************************************/
A2B_COMMCH_RET adi_a2b_CommChTxMsg(a2b_Handle hCommCh, a2b_CommChMsg *pMsg , a2b_Int8 nNodeAddr)
{
	A2B_COMMCH_RET		eRet = A2B_COMMCH_SUCCESS;
	a2b_CommChInfo	*pCommChInfo = (a2b_CommChInfo*)hCommCh;
	a2b_CommChMsg	*pCommChTxMsg = &pCommChInfo->oCommChEngInfo.oA2bTxMsg;
	a2b_UInt8	nIdx;

	if(nNodeAddr != pCommChInfo->nTargetNodeNum)
	{
		eRet = A2B_COMMCH_FAILED;
		return(eRet);
	}
	if (pCommChInfo->eCommChMsgTxState == A2B_COMMCH_TX_IDLE)
	{
		if((pMsg->pMsgPayload != A2B_NULL) && (pMsg->nMsgLenInBytes != 0u))
		{
			(void)memcpy(pCommChTxMsg->pMsgPayload, pMsg->pMsgPayload, pMsg->nMsgLenInBytes);	/* PRQA S 3335 */
		}

		pCommChTxMsg->nMsgLenInBytes				= pMsg->nMsgLenInBytes;
		pCommChTxMsg->nMsgId						= pMsg->nMsgId;
		pCommChInfo->oCommChEngInfo.nTxNodeAddr  	= nNodeAddr;
		pCommChInfo->eCommChMsgTxState 				= A2B_COMMCH_TX_BUSY;

	}
	else
	{
		/* return busy */
		eRet = A2B_COMMCH_RET_TXBUSY;
	}

	return(eRet);
}

/*****************************************************************************/
/*!
@brief			This function should be called periodically and checks for Tx done or a timeout occurred.

@param [in]     hCommCh	Comm ch slave instance pointer

@return			A2B_COMMCH_RET type
                - 0: A2B_COMMCH_SUCCESS
                - 1: A2B_COMMCH_FAILED
*/
/*****************************************************************************/
A2B_COMMCH_RET adi_a2b_CommChTick(a2b_Handle *hCommCh)
{
	a2b_CommChInfo	*pCommChInfo = (a2b_CommChInfo*)hCommCh;
	a2b_CommChMsg		*pCommChTxMsg = &pCommChInfo->oCommChEngInfo.oA2bTxMsg;
	A2B_COMMCH_RET		eRet = A2B_COMMCH_SUCCESS;
	a2b_UInt8			nIdx;
	a2b_Bool 			bMboxIrptClrd;

	/* Get current time tick */
	pCommChInfo->nCurrTimeInMsec = pCommChInfo->pCommPalCtx->pCommChPal->timerGetSysTime();

	/* Check if polling is enabled */
	if(pCommChInfo->bMBoxPoll == 1u)
	{
		/* If Slave node then query for mailbox interrupts once every polling period */
		pCommChInfo->nIntrPollPeriod =  pCommChInfo->nCurrTimeInMsec -  pCommChInfo->nIntrPollStartTime ;
		if( pCommChInfo->nIntrPollPeriod >= ADI_A2B_COMMCH_INTR_POLLING_PERIOD)
		{
			pCommChInfo->nIntrPollStartTime =  pCommChInfo->nCurrTimeInMsec;
			/* Check interrupt status */
			eRet = a2b_CommChIntrQuery(hCommCh, &bMboxIrptClrd);
		}
	}

	/* If transmission is scheduled */
	if(pCommChInfo->eCommChMsgTxState == A2B_COMMCH_TX_BUSY)
	{
		/* If Tx response received for current frame */
		if (pCommChInfo->abReadComplete[pCommChInfo->oCommChEngInfo.nTxMbox] == A2B_TRUE)
		{
			eRet = a2b_CommChTxmitNxtFrame(pCommChInfo, pCommChTxMsg);
		}
		/* If no response yet for current frame check for timeout */
		else
		{
			a2b_CommChChkTxmitTimeOut(pCommChInfo, pCommChTxMsg);
		}
	}

	return (eRet);
}

/*****************************************************************************/
/*!
@brief			This function checks if any bytes to be transmitted. If all the payload bytes
				are transmitted then it triggers a transmission finished callback.

@param [in]     pCommChInfo	Comm Channel slave instance pointer
@param [in]		pCommChTxMsg	Pointer to Buffer which holds the message being transmitted

@return			A2B_COMMCH_RET type
                - 0: A2B_COMMCH_SUCCESS
                - 1: A2B_COMMCH_FAILED
*/
/*****************************************************************************/
static A2B_COMMCH_RET a2b_CommChTxmitNxtFrame(a2b_CommChInfo *pCommChInfo, a2b_CommChMsg *pCommChTxMsg)
{
	A2B_COMMCH_RET		eRet = A2B_COMMCH_SUCCESS;

	/* Special handling of zero byte message with framing */
	if( pCommChTxMsg->nMsgLenInBytes == 0u)
	{
		pCommChInfo->nTxTimeout		= 0u;
		/* Transmit next frame */
		pCommChInfo->abReadComplete[pCommChInfo->oCommChEngInfo.nTxMbox] = A2B_FALSE;
		eRet = adi_a2b_CommChEngTxAction(&pCommChInfo->oCommChEngInfo);

		if(eRet != A2B_COMMCH_SUCCESS)
		{
			a2b_CommChTxStateRst(pCommChInfo, A2B_FALSE);
		}
		else
		{
			/* Get the start tick for next frame */
			pCommChInfo->nFrameStartTime = pCommChInfo->nCurrTimeInMsec;
		}

		/* Reset CommCh state once message is transmitted */
	}
	/* If all payload bytes transmitted and acknowledged */
	if((pCommChInfo->oCommChEngInfo.nWriteIdx == pCommChTxMsg->nMsgLenInBytes)  || (pCommChTxMsg->nMsgLenInBytes == 0u))
	{
		/* Trigger an transmission finished callback */
		pCommChInfo->oCommChEngInfo.pfStatCb(pCommChInfo->oCommChEngInfo.pCbParam, pCommChTxMsg, A2B_COMMCH_EVENT_TX_DONE, pCommChInfo->oCommChEngInfo.nTxNodeAddr);

		/* Reset Tx in progress flag,state and timeout count */
		a2b_CommChTxStateRst(pCommChInfo, A2B_TRUE);

	}
	else	/* if bytes pending */
	{
		/* reset timeout for next frame */
		pCommChInfo->nTxTimeout		= 0u;
		/* Transmit next frame */
		pCommChInfo->abReadComplete[pCommChInfo->oCommChEngInfo.nTxMbox] = A2B_FALSE;
		eRet = adi_a2b_CommChEngTxAction(&pCommChInfo->oCommChEngInfo);

		if(eRet != A2B_COMMCH_SUCCESS)
		{
			a2b_CommChTxStateRst(pCommChInfo, A2B_FALSE);
		}
		else
		{
			/* Get the start tick for next frame */
			pCommChInfo->nFrameStartTime = pCommChInfo->nCurrTimeInMsec;
		}
	}

	return (eRet);
}

/*****************************************************************************/
/*!
@brief			This function resets TX state machine after current message transmission(successful or failed )

@param [in]     pCommChInfo		Comm Channel slave instance pointer
@param [in]		bCurTxSuccess	Flag to indicate whether the current transmission is successful

@return			void
*/
/*****************************************************************************/
static void a2b_CommChTxStateRst(a2b_CommChInfo *pCommChInfo, a2b_Bool bCurTxSuccess)
{
	/* Reset the Tx in progress flag, timeout and state */
	pCommChInfo->eCommChMsgTxState 										= A2B_COMMCH_TX_IDLE;
	pCommChInfo->nTxTimeout												= 0u;
	pCommChInfo->nFrameStartTime 										= 0u;
	if(bCurTxSuccess == A2B_FALSE)
	{
		(void)a2b_CommChTxMboxStatusQuery(pCommChInfo);
	}
	(void)adi_a2b_CommChEngRstTxInfo(&pCommChInfo->oCommChEngInfo);
}

/*****************************************************************************/
/*!
@brief			This function checks for transmission timeout id there is no response

@param [in]     pCommChInfo	Comm Channel slave instance pointer
@param [in]		pCommChTxMsg	Pointer to Buffer which holds the message being transmitted

@return			A2B_COMMCH_RET type
                - 0: A2B_COMMCH_SUCCESS
                - 1: A2B_COMMCH_FAILED
*/
/*****************************************************************************/
static void a2b_CommChChkTxmitTimeOut(a2b_CommChInfo *pCommChInfo, a2b_CommChMsg *pCommChTxMsg)
{
	pCommChInfo->nTxTimeout = (a2b_UInt32)(pCommChInfo->nCurrTimeInMsec - pCommChInfo->nFrameStartTime);

	/* If timeout reached */
	if(pCommChInfo->nTxTimeout == ADI_A2B_COMMCH_TXMIT_MBOX_TIMEOUT_IN_TICKS )
	{
		/* Trigger a timeout callback */
		pCommChInfo->oCommChEngInfo.pfStatCb(pCommChInfo->oCommChEngInfo.pCbParam, pCommChTxMsg, A2B_COMMCH_EVENT_TX_TIMEOUT, pCommChInfo->oCommChEngInfo.nTxNodeAddr);
		/* Best effort to clear the states */
		a2b_CommChTxStateRst(pCommChInfo, A2B_FALSE);
	}
}

/*****************************************************************************/
/*!
@brief			This function writes the message to a specific mailbox

@param [in]     hCommCh		Comm ch slave instance pointer
@param [in]     nMboxNo		Mailbox number
@param [in]     nNoOfBytes	Number of bytes to be transmitted
@param [in]     pWriteBuf	Write buffer pointer
@param [in]     nNodeAddr	Node address for which the msg is being transmitted

@return			A2B_COMMCH_RET type
                - 0: A2B_COMMCH_SUCCESS
                - 1: A2B_COMMCH_FAILED
*/
/*****************************************************************************/
static A2B_COMMCH_RET a2b_CommChEngMboxWriteCbk(void* hCommCh, a2b_UInt8 nMboxNo, a2b_UInt16 nNoOfBytes, a2b_UInt8 *pWriteBuf, a2b_Int8 nNodeAddr)
{
	A2B_COMMCH_RET		eRet = A2B_COMMCH_SUCCESS;
	a2b_HResult 		eI2CRet;
	a2b_UInt8       	regOffset;
	a2b_UInt8       	awTmpBuf[2];
	a2b_CommChInfo		*pCommChInfo = (a2b_CommChInfo*)hCommCh;

	switch(nMboxNo)
	{
		case 0u:
				/* Mailbox 0 */
				regOffset = A2B_REG_MBOX0B0;
				break;

		case 1u:
				/* Mailbox 1 */
				regOffset = A2B_REG_MBOX1B0;
				break;

		default:
				/* Do Nothing */
				break;
	}


	/* Write data to mailbox register */
	eI2CRet = pCommChInfo->pCommPalCtx->pCommChPal->regWrite(pCommChInfo->eMailboxAccessType, pCommChInfo->nTargetNodeNum, regOffset, nNoOfBytes, pWriteBuf, pCommChInfo->pCommPalCtx->hCallbackParam);
	if(eI2CRet != 0u)
	{
		eRet = A2B_COMMCH_RET_MBOXWRITE_FAILED;
	}
	return (eRet);
}

/*****************************************************************************/
/*!
@brief			This function process the mailbox interrupts

@param [in]     pCommChInfo	Comm ch slave instance pointer

@return			A2B_COMMCH_RET type
                - 0: A2B_COMMCH_SUCCESS
                - 1: A2B_COMMCH_FAILED
*/
/*****************************************************************************/
static a2b_UInt8 a2b_CommChRxMboxStatusQuery(a2b_CommChInfo *pCommChInfo)
{
	A2B_COMMCH_RET		eRet = A2B_COMMCH_SUCCESS;
	a2b_Byte			anMboxData[ADI_A2B_COMMCH_ENG_MAX_MBOX_DATA_SIZE];
	a2b_UInt8           regOffset, nMailboxStat = 0;
	a2b_HResult 		retTemp;
	a2b_UInt8			nRxMBoxNo = pCommChInfo->oCommChEngInfo.nRxMbox;
	a2b_Bool			bActiveIntrFound = A2B_FALSE;

	/* Read Mailbox Status */
	regOffset = aMboxStatRegAddr[nRxMBoxNo];
	retTemp = pCommChInfo->pCommPalCtx->pCommChPal->regRead(pCommChInfo->eMailboxAccessType, pCommChInfo->nTargetNodeNum, regOffset, 1u, &nMailboxStat, pCommChInfo->pCommPalCtx->hCallbackParam);
	if ( retTemp != 0u )
	{
		eRet = A2B_COMMCH_RET_MBOXREAD_FAILED;
	}

	if(eRet == A2B_COMMCH_SUCCESS)
	{
		if(nMailboxStat & A2B_MAILBOXSTAT_FULL_MSK)
		{
			eRet = a2b_CommChMboxRead(pCommChInfo, pCommChInfo->oCommChEngInfo.nRxMbox, ADI_A2B_COMMCH_ENG_MAX_MBOX_DATA_SIZE, &anMboxData[0], -1);
			if(eRet == A2B_COMMCH_SUCCESS)
			{
				/* Call process function */
				adi_a2b_CommChEngRxAction(&pCommChInfo->oCommChEngInfo, &anMboxData[0]);
				bActiveIntrFound = (nMailboxStat & A2B_MBOXFULL_ACITVE_INTR_MSK) > 0u;
			}

		}

	}

	return bActiveIntrFound;
}

/*****************************************************************************/
/*!
@brief			This function process the mailbox interrupts

@param [in]     pCommChInfo	Comm ch slave instance pointer

@return			A2B_COMMCH_RET type
                - 0: A2B_COMMCH_SUCCESS
                - 1: A2B_COMMCH_FAILED
*/
/*****************************************************************************/
static a2b_Bool a2b_CommChTxMboxStatusQuery(a2b_CommChInfo *pCommChInfo)
{
	A2B_COMMCH_RET		eRet = A2B_COMMCH_SUCCESS;
	a2b_Byte			anMboxData[ADI_A2B_COMMCH_ENG_MAX_MBOX_DATA_SIZE];
	a2b_UInt8           regOffset,nMailboxStat;
	a2b_HResult 		retTemp;
	a2b_UInt8			nTxMBoxNo = pCommChInfo->oCommChEngInfo.nTxMbox;
	a2b_Bool			bActiveIntrFound = A2B_FALSE;

	/* Read interrupt type */
	regOffset = aMboxStatRegAddr[nTxMBoxNo];
	retTemp = pCommChInfo->pCommPalCtx->pCommChPal->regRead(pCommChInfo->eMailboxAccessType, pCommChInfo->nTargetNodeNum, regOffset, 1u, &nMailboxStat, pCommChInfo->pCommPalCtx->hCallbackParam);
	if (retTemp != 0u)
	{
		eRet = A2B_COMMCH_RET_MBOXREAD_FAILED;
	}

	if(eRet == A2B_COMMCH_SUCCESS)
	{
		if(nMailboxStat & A2B_MBOXSTAT_EMPTY_MSK)
		{
			pCommChInfo->abReadComplete[nTxMBoxNo] = A2B_TRUE;
			bActiveIntrFound = (nMailboxStat & A2B_MBOXEMPTY_ACITVE_INTR_MSK) > 0u;
		}
		else
		{
			pCommChInfo->abReadComplete[nTxMBoxNo] = A2B_FALSE;
		}

	}

	return bActiveIntrFound;
}

/*****************************************************************************/
/*!
@brief			This function queries for the mailbox interrupts

@param [in]     hCommCh				Comm ch slave instance pointer
@param [in]     pbMboxIrptClrd		MBOX active interrupt found, read & cleared.
									True: Application need not read the MBOX interrupt
									False: Application needs to read the MBOX interrupt

@return			A2B_COMMCH_RET type
                - 0: A2B_COMMCH_SUCCESS
                - 1: A2B_COMMCH_FAILED
*/
/*****************************************************************************/
A2B_COMMCH_RET a2b_CommChIntrQuery(a2b_Handle *hCommCh, a2b_Bool *pbMboxIrptClrd)
{
	a2b_UInt8       nType;
	a2b_Bool 		bClrIntrpt;
	a2b_CommChInfo	*pCommChInfo = (a2b_CommChInfo*)hCommCh;
	A2B_COMMCH_RET	eRet = A2B_COMMCH_SUCCESS;

	/* Check for status of each mailbox */
	bClrIntrpt =  a2b_CommChTxMboxStatusQuery(pCommChInfo);

	bClrIntrpt |=  a2b_CommChRxMboxStatusQuery(pCommChInfo);

	if( (pCommChInfo->eNodeType == A2B_SLAVE_NODE) && bClrIntrpt)
	{
		/* Read Local Interrupt type - To clear the interrupt */
		eRet = (A2B_COMMCH_RET)pCommChInfo->pCommPalCtx->pCommChPal->regRead(pCommChInfo->eMailboxAccessType, pCommChInfo->nTargetNodeNum, A2B_REG_LINTTYPE, 1u, &nType, pCommChInfo->pCommPalCtx->hCallbackParam);
	}

	*pbMboxIrptClrd = bClrIntrpt;

	return (eRet);
}

/*****************************************************************************/
/*!
@brief			This function reads the message from the specified mailbox

@param [in]     pCommChInfo	Comm ch slave instance pointer
@param [in]     nMboxNo			Mailbox number
@param [in]     nNoOfBytes		Number of bytes to be read
@param [in]     pReadBuf		Read buffer pointer
@param [in]     nNodeAddr		Node address

@return			A2B_COMMCH_RET type
                - 0: A2B_COMMCH_SUCCESS
                - 1: A2B_COMMCH_FAILED
*/
/*****************************************************************************/
static A2B_COMMCH_RET a2b_CommChMboxRead(a2b_CommChInfo *pCommChInfo, a2b_UInt8 nMboxNo, a2b_UInt8 nNoOfBytes, a2b_Byte *pReadBuf, a2b_Int8 nNodeAddr)
{
	A2B_COMMCH_RET		eRet = A2B_COMMCH_SUCCESS;
	a2b_HResult 		eI2CRet;
	a2b_UInt8       	regOffset;
	a2b_UInt8       	awTmpBuf[2];

	switch(nMboxNo)
	{
		case 0u:
				/* Mailbox 0 */
				regOffset = A2B_REG_MBOX0B0;
				break;

		case 1u:
				/* Mailbox 1 */
				regOffset = A2B_REG_MBOX1B0;
				break;

		default:
				/* Do Nothing */
				break;
	}

	/* Read data from mailbox register */
	eI2CRet= pCommChInfo->pCommPalCtx->pCommChPal->regRead(pCommChInfo->eMailboxAccessType, pCommChInfo->nTargetNodeNum, regOffset, 4u, pReadBuf, pCommChInfo->pCommPalCtx->hCallbackParam);
	if(eI2CRet != 0u)
	{
		eRet = A2B_COMMCH_RET_MBOXREAD_FAILED;
	}

	return (eRet);
}

/*****************************************************************************/
/*!
@brief			This function sets the framing for a commch instance

@param [in]     hCommCh     Comm ch instance pointer
@param [in]     eFraming	Enumeration for A2B Comm Channel enable/disable framing

@return			A2B_COMMCH_RET type
                - 0: A2B_COMMCH_SUCCESS
                - 1: A2B_COMMCH_FAILED
*/
/*****************************************************************************/
A2B_COMMCH_RET adi_a2b_CommChSetFraming(a2b_Handle hCommCh, A2B_COMMCH_FRAMING eFraming)
{
	A2B_COMMCH_RET	eRet = A2B_COMMCH_SUCCESS;
	a2b_CommChInfo	*pCommChInfo;

	if(hCommCh != A2B_NULL)
	{
		pCommChInfo = (a2b_CommChInfo*)hCommCh;
		pCommChInfo->oCommChEngInfo.eFraming = eFraming;
	}
	else
	{
		eRet = A2B_COMMCH_FAILED;
	}

	return(eRet);
}

/*****************************************************************************/
/*!
@brief			This function gest the framing en/dis for a commch instance

@param [in]     hCommCh		Comm ch instance pointer
@param [in]     peFraming	Pointer to A2B Comm Channel enable/disable framing

@return			A2B_COMMCH_RET type
                - 0: A2B_COMMCH_SUCCESS
                - 1: A2B_COMMCH_FAILED
*/
/*****************************************************************************/
A2B_COMMCH_RET adi_a2b_CommChGetFraming(a2b_Handle hCommCh, A2B_COMMCH_FRAMING *peFraming)
{
	A2B_COMMCH_RET	eRet = A2B_COMMCH_SUCCESS;
	a2b_CommChInfo	*pCommChInfo;

	if((hCommCh != A2B_NULL) && (peFraming!= A2B_NULL))
	{
		pCommChInfo = (a2b_CommChInfo*)hCommCh;

		*peFraming = pCommChInfo->oCommChEngInfo.eFraming;
	}
	else
	{
		eRet = A2B_COMMCH_FAILED;
	}

	return(eRet);
}


/**
 @}
*/

/**
 @}
*/


/*
**
** EOF: $URL$
**
*/
