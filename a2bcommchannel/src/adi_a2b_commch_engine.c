/*******************************************************************************
Copyright (c) 2018 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
******************************************************************************
 * @file:    adi_a2b_commch_engine.c
 * @brief:   This is the implementation of Tx and Rx state machines for Communication Channel
 * @version: $Revision$
 * @date:    $Date$
 * Developed by: Automotive Software and Systems team, Bangalore, India
*****************************************************************************/
/*! \addtogroup Communication_Channel Communication Channel
 *  @{
 */

/** @defgroup Communication_Channel_Engine    Communication Channel Engine
 *
 * This module handles the Tx and Rx state machines for Communication Channel
 *
 */

/*! \addtogroup Communication_Channel_Engine
 *  @{
 */

/*============= I N C L U D E S =============*/
#include "adi_a2b_commch_engine.h"
#include <string.h>

/*============= D E F I N E S =============*/

/*============= D A T A =============*/

/*============= L O C A L  P R O T O T Y P E S =============*/
static A2B_COMMCH_RET adi_a2b_CommChEngRawTxProcess(a2b_CommChEngInfo *pCommChEngInfo);
static A2B_COMMCH_RET adi_a2b_CommChEngTxProcess(a2b_CommChEngInfo *pCommChEngInfo);
static void adi_a2b_CommChEngRxProcess(a2b_CommChEngInfo *pCommChEngInfo, a2b_UInt8 *pMsg);
static void adi_a2b_CommChEngRawRxProcess(a2b_CommChEngInfo *pCommChEngInfo, a2b_UInt8 *pMsg);

/*============= C O D E =============*/

/*****************************************************************************/
/*!
@brief			This function calls the respective with and without framing enabled functions of the Tx state machine

@param [in]     pCommChEngInfo		Comm ch engine instance pointer

@return			A2B_COMMCH_RET type
                - 0: A2B_COMMCH_SUCCESS
                - 1: A2B_COMMCH_FAILED
*/
/*****************************************************************************/
A2B_COMMCH_RET adi_a2b_CommChEngTxAction(a2b_CommChEngInfo *pCommChEngInfo)
{
	A2B_COMMCH_RET		eRet = A2B_COMMCH_SUCCESS;

	if(pCommChEngInfo->eFraming == A2B_COMMCH_FRAMING_ENABLED)
	{
		eRet = adi_a2b_CommChEngTxProcess(pCommChEngInfo);
	}
	else
	{
		eRet = adi_a2b_CommChEngRawTxProcess(pCommChEngInfo);
	}
	return(eRet);
}

/*****************************************************************************/
/*!
@brief			This function calls the respective with and without framing enabled functions of the Rx state machine

@param [in]     pCommChEngInfo		Comm ch engine instance pointer
@param [in]     pMsg				Pointer to Rx msg

@return			None
*/
/*****************************************************************************/
void adi_a2b_CommChEngRxAction(a2b_CommChEngInfo *pCommChEngInfo, a2b_UInt8 *pMsg)
{
	if(pCommChEngInfo->eFraming == A2B_COMMCH_FRAMING_ENABLED)
	{
		adi_a2b_CommChEngRxProcess(pCommChEngInfo, pMsg);
	}
	else
	{
		adi_a2b_CommChEngRawRxProcess(pCommChEngInfo, pMsg);
	}
}

/*****************************************************************************/
/*!
@brief			This function handles the Tx state machine with framing enabled

@param [in]     pCommChEngInfo		Comm ch engine instance pointer

@return			A2B_COMMCH_RET type
                - 0: A2B_COMMCH_SUCCESS
                - 1: A2B_COMMCH_FAILED
*/
/*****************************************************************************/
static A2B_COMMCH_RET adi_a2b_CommChEngTxProcess(a2b_CommChEngInfo *pCommChEngInfo)
{
	A2B_COMMCH_RET		eRet = A2B_COMMCH_SUCCESS;
	a2b_UInt8			nNewLenInBytes;

	switch(pCommChEngInfo->eTxState)
	{
		case A2B_COMMCH_MSG_INIT:
			if(pCommChEngInfo->oA2bTxMsg.nMsgLenInBytes <= ADI_A2B_COMMCH_ENG_MAX_TX_MSG_SIZE)
			{
				pCommChEngInfo->nTxSeqCnt = 1u;
				/* Form SF message */
				if(pCommChEngInfo->oA2bTxMsg.nMsgLenInBytes <= 1u )
				{
					pCommChEngInfo->aMboxWbuf[0] =  ((a2b_UInt8)(ADI_A2B_COMMCH_ENG_SF_ID << ADI_A2B_COMMCH_ENG_FRAME_ID_BIT_SHIFT)) | ((a2b_UInt8)(pCommChEngInfo->oA2bTxMsg.nMsgId & ADI_A2B_COMMCH_ENG_MSG_ID_BITMASK));
				}
				/* Form FF message */
				else
				{
					pCommChEngInfo->aMboxWbuf[0] =  ((a2b_UInt8)(ADI_A2B_COMMCH_ENG_FF_ID << ADI_A2B_COMMCH_ENG_FRAME_ID_BIT_SHIFT)) | ((a2b_UInt8)(pCommChEngInfo->oA2bTxMsg.nMsgId & ADI_A2B_COMMCH_ENG_MSG_ID_BITMASK));
				}
				pCommChEngInfo->aMboxWbuf[1] = (a2b_UInt8)((pCommChEngInfo->oA2bTxMsg.nMsgLenInBytes & 0xFF00u) >> 8u);
				pCommChEngInfo->aMboxWbuf[2] = (a2b_UInt8)(pCommChEngInfo->oA2bTxMsg.nMsgLenInBytes & 0x00FFu);

				if(pCommChEngInfo->oA2bTxMsg.nMsgLenInBytes != 0u)
				{
					pCommChEngInfo->aMboxWbuf[3] = pCommChEngInfo->oA2bTxMsg.pMsgPayload[pCommChEngInfo->nWriteIdx];
				}

				pCommChEngInfo->nWriteIdx++;
				eRet = pCommChEngInfo->pfMboxWriteCb(pCommChEngInfo->hCommCh, pCommChEngInfo->nTxMbox, ADI_A2B_COMMCH_ENG_MAX_MBOX_DATA_SIZE, pCommChEngInfo->aMboxWbuf, pCommChEngInfo->nTxNodeAddr);
				if(eRet == A2B_COMMCH_SUCCESS)
				{
					if(pCommChEngInfo->nWriteIdx < pCommChEngInfo->oA2bTxMsg.nMsgLenInBytes)
					{
						pCommChEngInfo->eTxState = A2B_COMMCH_MSG_CTS;
					}
				}
			}
			else
			{
				eRet = A2B_COMMCH_RET_EXCEDDED_TXCAPACITY;
			}
			break;

		case A2B_COMMCH_MSG_CTS:
			/* Form CF message */
			pCommChEngInfo->aMboxWbuf[0] =  ((a2b_UInt8)(ADI_A2B_COMMCH_ENG_CF_ID << ADI_A2B_COMMCH_ENG_FRAME_ID_BIT_SHIFT)) | ((a2b_UInt8)(pCommChEngInfo->nTxSeqCnt & ADI_A2B_COMMCH_ENG_MSG_ID_BITMASK));

			if((pCommChEngInfo->nWriteIdx + (ADI_A2B_COMMCH_ENG_MAX_MBOX_DATA_SIZE - 1u)) <= pCommChEngInfo->oA2bTxMsg.nMsgLenInBytes )
			{
				(void)memcpy(&pCommChEngInfo->aMboxWbuf[1], &pCommChEngInfo->oA2bTxMsg.pMsgPayload[pCommChEngInfo->nWriteIdx], ADI_A2B_COMMCH_ENG_MAX_MBOX_DATA_SIZE - 1u); /* PRQA S 3335 */
				pCommChEngInfo->nWriteIdx += ADI_A2B_COMMCH_ENG_MAX_MBOX_DATA_SIZE - 1u;
			}
			else
			{
				/* Calculate the bytes to be copied and copy the message */
				nNewLenInBytes = (a2b_UInt8)(pCommChEngInfo->oA2bTxMsg.nMsgLenInBytes - pCommChEngInfo->nWriteIdx);
				(void)memcpy(&pCommChEngInfo->aMboxWbuf[1], &pCommChEngInfo->oA2bTxMsg.pMsgPayload[pCommChEngInfo->nWriteIdx], nNewLenInBytes);
				pCommChEngInfo->nWriteIdx += nNewLenInBytes;
			}

			/* Write to mailbox */
			eRet = pCommChEngInfo->pfMboxWriteCb(pCommChEngInfo->hCommCh, pCommChEngInfo->nTxMbox, ADI_A2B_COMMCH_ENG_MAX_MBOX_DATA_SIZE, pCommChEngInfo->aMboxWbuf, pCommChEngInfo->nTxNodeAddr);

			/* Increment the SN */
			pCommChEngInfo->nTxSeqCnt++;

			/* Roll-back the SN if it reaches maximum value. It is 0x3F here since SN is represented by 6-bit value */
			if(pCommChEngInfo->nTxSeqCnt > ADI_A2B_COMMCH_ENG_MAX_SN)
			{
				pCommChEngInfo->nTxSeqCnt = 0u;
			}
			break;

		default:
			eRet = A2B_COMMCH_FAILED;
			break;
	}

	return (eRet);
}

/*****************************************************************************/
/*!
@brief			This function handles the Tx state machine without framing enabled

@param [in]     pCommChEngInfo		Comm ch engine instance pointer

@return			A2B_COMMCH_RET type
                - 0: A2B_COMMCH_SUCCESS
                - 1: A2B_COMMCH_FAILED
*/
/*****************************************************************************/
static A2B_COMMCH_RET adi_a2b_CommChEngRawTxProcess(a2b_CommChEngInfo *pCommChEngInfo)
{
	A2B_COMMCH_RET		eRet = A2B_COMMCH_SUCCESS;
	a2b_UInt8          i, idx;
	a2b_UInt16 nRemBytes = 0u;

	switch(pCommChEngInfo->eTxState)
	{
		case A2B_COMMCH_MSG_INIT:
			if(pCommChEngInfo->oA2bTxMsg.nMsgLenInBytes <= ADI_A2B_COMMCH_ENG_MAX_TX_MSG_SIZE)
			{
				pCommChEngInfo->nTxSeqCnt = 1u;
				for (i = 0u; i < ADI_A2B_COMMCH_ENG_MAX_MBOX_DATA_SIZE; i++)
				{
					pCommChEngInfo->aMboxWbuf[i] = (a2b_UInt8)(pCommChEngInfo->oA2bTxMsg.pMsgPayload[pCommChEngInfo->nWriteIdx++]);
				}
				eRet = pCommChEngInfo->pfMboxWriteCb(pCommChEngInfo->hCommCh, pCommChEngInfo->nTxMbox, ADI_A2B_COMMCH_ENG_MAX_MBOX_DATA_SIZE, pCommChEngInfo->aMboxWbuf, pCommChEngInfo->nTxNodeAddr);
				if(eRet == A2B_COMMCH_SUCCESS)
				{
					if(pCommChEngInfo->nWriteIdx < pCommChEngInfo->oA2bTxMsg.nMsgLenInBytes)
					{
						pCommChEngInfo->eTxState = A2B_COMMCH_MSG_CTS;
					}
				}
			}
			else
			{
				eRet = A2B_COMMCH_RET_EXCEDDED_TXCAPACITY;
			}
			break;

		case A2B_COMMCH_MSG_CTS:

			nRemBytes = (pCommChEngInfo->oA2bTxMsg.nMsgLenInBytes) % ADI_A2B_COMMCH_ENG_MAX_MBOX_DATA_SIZE;
			if(pCommChEngInfo->nWriteIdx == (pCommChEngInfo->oA2bTxMsg.nMsgLenInBytes - nRemBytes))
			{
				if(nRemBytes != 0U)
				{
					idx = nRemBytes;
				}

				else
				{
					idx = 0U;
				}
			}
			else
			{
				idx = ADI_A2B_COMMCH_ENG_MAX_MBOX_DATA_SIZE;
			}

			for (i = 0U; i < idx; i++)
			{
				pCommChEngInfo->aMboxWbuf[i] = (a2b_UInt8)(pCommChEngInfo->oA2bTxMsg.pMsgPayload[pCommChEngInfo->nWriteIdx++]);
			}

			/* Write to mailbox */
			eRet = pCommChEngInfo->pfMboxWriteCb(pCommChEngInfo->hCommCh, pCommChEngInfo->nTxMbox, ADI_A2B_COMMCH_ENG_MAX_MBOX_DATA_SIZE, pCommChEngInfo->aMboxWbuf, pCommChEngInfo->nTxNodeAddr);

			/* Increment the SN */
			pCommChEngInfo->nTxSeqCnt++;

			/* Roll-back the SN if it reaches maximum value. It is 0x3F here since SN is represented by 6-bit value */
			if(pCommChEngInfo->nTxSeqCnt > ADI_A2B_COMMCH_ENG_MAX_SN)
			{
				pCommChEngInfo->nTxSeqCnt = 0u;
			}
			break;

		default:
			eRet = A2B_COMMCH_FAILED;
			break;
	}

	return (eRet);
}

/*****************************************************************************/
/*!
@brief			This function handles the Rx state machine with framing enabled

@param [in]     pCommChEngInfo		Comm ch engine instance pointer
@param [in]     pMsg				Pointer to Rx msg

@return			None
*/
/*****************************************************************************/
static void adi_a2b_CommChEngRxProcess(a2b_CommChEngInfo *pCommChEngInfo, a2b_UInt8 *pMsg)
{
	a2b_UInt8		nLenMsb, nLenLsb,nFrameID;
	a2b_UInt8 		nNewLenInBytes;
	a2b_Int8        nRxNodeAddr;

	nFrameID = *(pMsg) >> ADI_A2B_COMMCH_ENG_FRAME_ID_BIT_SHIFT;

	if(nFrameID == ADI_A2B_COMMCH_ENG_FF_ID)
	{
		/* Clear the Rx state m/c  */
		adi_a2b_CommChEngRstRxInfo(pCommChEngInfo);

		pCommChEngInfo->eCommChRxMsgState 		 = A2B_COMMCH_RX_BUSY;
		pCommChEngInfo->oA2bRxMsg.nMsgId 		 = (*pMsg) & ADI_A2B_COMMCH_ENG_MSG_ID_BITMASK;
		nLenMsb 						 		 = *(pMsg+1u);
		nLenLsb 						 		 = *(pMsg+2u);
		pCommChEngInfo->oA2bRxMsg.nMsgLenInBytes = (a2b_UInt16)((a2b_UInt16)(((a2b_UInt16)nLenMsb << (a2b_UInt16)8u)) | ((a2b_UInt16)nLenLsb));

		if(pCommChEngInfo->oA2bRxMsg.nMsgLenInBytes < ADI_A2B_COMMCH_ENG_MAX_RX_MSG_SIZE)
		{
			*pCommChEngInfo->oA2bRxMsg.pMsgPayload = *(pMsg+3u);

			/* Increment the number of payload bytes which is copied into the local buffer */
			pCommChEngInfo->nReadIdx = 1u;

			/* Change the state */
			pCommChEngInfo->eRxState = A2B_COMMCH_MSG_CTS;
		}
		else
		{
			/* Trigger a callback with receive message size error event */
			pCommChEngInfo->pfStatCb(pCommChEngInfo->pCbParam, &pCommChEngInfo->oA2bRxMsg, A2B_COMMCH_EVENT_RX_EXCEDDED_CAPACITY, pCommChEngInfo->nRxNodeAddr);
			/* Clear the Rx state m/c  */
			adi_a2b_CommChEngRstRxInfo(pCommChEngInfo);
		}
	}
	else if ((nFrameID == ADI_A2B_COMMCH_ENG_CF_ID) && (pCommChEngInfo->eCommChRxMsgState == A2B_COMMCH_RX_BUSY))
	{
		if(((*pMsg) & ADI_A2B_COMMCH_ENG_MSG_ID_BITMASK) != pCommChEngInfo->nRxSeqCnt)
		{
			/* If there is a sequence mismatch we clear our receive states and inform the higher layer */
			/* Trigger a callback with sequence count error event */
			pCommChEngInfo->pfStatCb(pCommChEngInfo->pCbParam, &pCommChEngInfo->oA2bRxMsg, A2B_COMMCH_EVENT_RX_SN_ERROR, pCommChEngInfo->nRxNodeAddr);
			/* Clear the Rx state m/c */
			adi_a2b_CommChEngRstRxInfo(pCommChEngInfo);
		}
		else
		{
			pCommChEngInfo->nRxSeqCnt++;

			/* Roll-back the SN if it reaches maximum value. It is 0x3F here since SN is represented by 6-bit value */
			if(pCommChEngInfo->nRxSeqCnt > ADI_A2B_COMMCH_ENG_MAX_SN)
			{
				pCommChEngInfo->nRxSeqCnt = 0u;
			}
		}

		/* Copied payload is less than actual payload length */
		if((pCommChEngInfo->nReadIdx + (ADI_A2B_COMMCH_ENG_MAX_MBOX_DATA_SIZE-1u)) < pCommChEngInfo->oA2bRxMsg.nMsgLenInBytes)
		{
			/* Copy the message from the mailbox to local buffer */
			(void)memcpy(&pCommChEngInfo->oA2bRxMsg.pMsgPayload[pCommChEngInfo->nReadIdx], pMsg+1u, ADI_A2B_COMMCH_ENG_MAX_MBOX_DATA_SIZE-1u);

			/* Increment the number of payload bytes which are copied into the local buffer */
			pCommChEngInfo->nReadIdx += ADI_A2B_COMMCH_ENG_MAX_MBOX_DATA_SIZE-1u;
		}
		else
		{
			/* Calculate the bytes to be copied and copy the message */
			nNewLenInBytes = (a2b_UInt8) (pCommChEngInfo->oA2bRxMsg.nMsgLenInBytes - pCommChEngInfo->nReadIdx);
			(void)memcpy(&pCommChEngInfo->oA2bRxMsg.pMsgPayload[pCommChEngInfo->nReadIdx], pMsg+1u, nNewLenInBytes);

			/* Increment the number of payload bytes which are copied into the local buffer */
			pCommChEngInfo->nReadIdx += nNewLenInBytes;
		}

		/* Copied payload is equal to actual payload length */
		if(pCommChEngInfo->nReadIdx == pCommChEngInfo->oA2bRxMsg.nMsgLenInBytes)
		{
			/* Trigger a receive callback */
			pCommChEngInfo->pfStatCb(pCommChEngInfo->pCbParam, &pCommChEngInfo->oA2bRxMsg, A2B_COMMCH_EVENT_RX_MSG, pCommChEngInfo->nRxNodeAddr);

			/* Clear the Rx state m/c  */
			adi_a2b_CommChEngRstRxInfo(pCommChEngInfo);
		}
	}
	else if(nFrameID == ADI_A2B_COMMCH_ENG_SF_ID)
	{
	    /* Preserve node address */
	    nRxNodeAddr = pCommChEngInfo->nRxNodeAddr;

		/* Clear the Rx state m/c  */
		adi_a2b_CommChEngRstRxInfo(pCommChEngInfo);

		/* Restore node address */
		pCommChEngInfo->nRxNodeAddr = nRxNodeAddr;

		pCommChEngInfo->oA2bRxMsg.nMsgId 		 = (*pMsg) & ADI_A2B_COMMCH_ENG_MSG_ID_BITMASK;
		nLenMsb 								 = *(pMsg+1u);
		nLenLsb 								 = *(pMsg+2u);
		pCommChEngInfo->oA2bRxMsg.nMsgLenInBytes = (a2b_UInt16)(((a2b_UInt16)((a2b_UInt16)nLenMsb << (a2b_UInt16)8u)) | (a2b_UInt16)nLenLsb);

		if(pCommChEngInfo->oA2bRxMsg.nMsgLenInBytes != 0u)
		{
			/* Copy the message from the mailbox to local buffer */
			(void)memcpy(&pCommChEngInfo->oA2bRxMsg.pMsgPayload[pCommChEngInfo->nReadIdx], pMsg+3, pCommChEngInfo->oA2bRxMsg.nMsgLenInBytes);
		}
		/* Trigger a receive callback */
		pCommChEngInfo->pfStatCb(pCommChEngInfo->pCbParam, &pCommChEngInfo->oA2bRxMsg, A2B_COMMCH_EVENT_RX_MSG, pCommChEngInfo->nRxNodeAddr);
	}
	else
	{
		/* Trigger a callback with invalid frame error event */
		pCommChEngInfo->pfStatCb(pCommChEngInfo->pCbParam, &pCommChEngInfo->oA2bRxMsg, A2B_COMMCH_EVENT_RX_INVALID_FRAME, pCommChEngInfo->nRxNodeAddr);

		/* Clear the Rx state m/c  */
		adi_a2b_CommChEngRstRxInfo(pCommChEngInfo);
	}
}

/*****************************************************************************/
/*!
@brief			This function handles the Rx state machine without framing enabled

@param [in]     pCommChEngInfo		Comm ch engine instance pointer
@param [in]     pMsg				Pointer to Rx msg

@return			None
*/
/*****************************************************************************/
static void adi_a2b_CommChEngRawRxProcess(a2b_CommChEngInfo *pCommChEngInfo, a2b_UInt8 *pMsg)
{
	pCommChEngInfo->oA2bRxMsg.nMsgLenInBytes = ADI_A2B_COMMCH_ENG_MAX_MBOX_DATA_SIZE;

	/* Copy the message from the mailbox to local buffer */
	(void)memcpy(&pCommChEngInfo->oA2bRxMsg.pMsgPayload[pCommChEngInfo->nReadIdx], pMsg, pCommChEngInfo->oA2bRxMsg.nMsgLenInBytes);

	/* Trigger a receive callback */
	pCommChEngInfo->pfStatCb(pCommChEngInfo->pCbParam, &pCommChEngInfo->oA2bRxMsg, A2B_COMMCH_EVENT_RX_MSG, pCommChEngInfo->nRxNodeAddr);
}

/*****************************************************************************/
/*!
@brief			This function resets the Rx state machine by clearing the
				state variables

@param [in]     pCommChEngInfo		Comm ch engine instance pointer

@return			none
*/
/*****************************************************************************/
void adi_a2b_CommChEngRstRxInfo(a2b_CommChEngInfo *pCommChEngInfo)
{
	pCommChEngInfo->eRxState				= A2B_COMMCH_MSG_INIT;
	pCommChEngInfo->nReadIdx				= 0u;
	pCommChEngInfo->oA2bRxMsg.nMsgId		= 0u;
	pCommChEngInfo->oA2bRxMsg.nMsgLenInBytes= 0u;
	pCommChEngInfo->oA2bRxMsg.pMsgPayload	= &pCommChEngInfo->oA2bRxBuf[0];
	pCommChEngInfo->nRxNodeAddr				= 0;
	pCommChEngInfo->eCommChRxMsgState		= A2B_COMMCH_RX_IDLE;
	pCommChEngInfo->nRxSeqCnt				= 1u;

	(void)memset((void*)&pCommChEngInfo->oA2bRxBuf, 0, ADI_A2B_COMMCH_ENG_MAX_RX_MSG_SIZE);
}

/*****************************************************************************/
/*!
@brief			This function resets the Tx state machine by clearing the
				state variables

@param [in]     pCommChEngInfo		Comm ch engine instance pointer

@return			none
*/
/*****************************************************************************/
void adi_a2b_CommChEngRstTxInfo(a2b_CommChEngInfo *pCommChEngInfo)
{
	pCommChEngInfo->eTxState				= A2B_COMMCH_MSG_INIT;
	pCommChEngInfo->nWriteIdx				= 0u;
	pCommChEngInfo->oA2bTxMsg.nMsgId		= 0u;
	pCommChEngInfo->oA2bTxMsg.nMsgLenInBytes= 0u;
	pCommChEngInfo->oA2bTxMsg.pMsgPayload	= &pCommChEngInfo->oA2bTxBuf[0];
	pCommChEngInfo->nTxNodeAddr				= 0;
	pCommChEngInfo->nTxSeqCnt				= 1u;

	(void)memset((void*)&pCommChEngInfo->oA2bTxBuf, 0, ADI_A2B_COMMCH_ENG_MAX_TX_MSG_SIZE);
	(void)memset((void*)&pCommChEngInfo->aMboxWbuf, 0, ADI_A2B_COMMCH_ENG_MAX_MBOX_DATA_SIZE);
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
