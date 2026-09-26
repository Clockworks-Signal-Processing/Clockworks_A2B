/*******************************************************************************
Copyright (c) 2018 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
******************************************************************************
 * @file:    adi_a2b_commch.h
 * @brief:   This  header file contains structure definitions for Communication channel on the slave side
 * @version: $Revision$
 * @date:    $Date$
 * Developed by: Automotive Software and Systems team, Bangalore, India
*****************************************************************************/
/** \addtogroup Communication_Channel_Slave
 *  @{
 */

#ifndef ADI_A2B_COMMCH_H_
#define	ADI_A2B_COMMCH_H_

#include "adi_a2b_commch_engine.h"


#define ADI_A2B_COMMCH_TXMIT_MBOX_TIMEOUT_IN_TICKS		(1000u)	/*!< Timeout for mailbox read response for a transmission is specified in ticks at which the time base input to comm channel */
#define ADI_A2B_COMMCH_INTR_POLLING_PERIOD				(1u)	/*!< Polling period for interrupt query. Only applicable for slave nodes is specified in ticks at which the time base runs for comm channel */

#define ADI_A2B_COMMCH_RX_MAILBOX_NO			(0u) 	/*!< Default Mailbox No on which Master transmits and slave receives, DO NOT Change */
#define ADI_A2B_COMMCH_TX_MAILBOX_NO			(1u) 	/*!< Default Mailbox No on which Slave transmits and master receives  DO NOT Change */


/*! \enum A2B_COMMCH_RET
Enumeration for A2B Comm Channel API return values
*/
typedef enum A2B_TRANSACTION_TYPE
{
	A2B_LOCAL_ACCESS,      			/*!< API execution success	*/
	A2B_BUS_ACCESS 				/*!< General failure		*/
}A2B_TRANSACTION_TYPE;

/*! \enum A2B_COMMCH_RET
Enumeration for A2B Comm Channel API return values
*/
typedef enum A2B_CURRENT_NODE_TYPE
{
	A2B_SLAVE_NODE, 				/*!< Slave Node		*/
	A2B_MASTER_NODE      			/*!< MASTER NODE	*/

}A2B_CURRENT_NODE_TYPE;


typedef a2b_HResult (* pfA2BRegWriteFunc)(A2B_TRANSACTION_TYPE eAccessType, a2b_Int16 nTargetNodeAdr,a2b_UInt16 nRegAddr, a2b_UInt16 nWrite,  const a2b_UInt8* wBuf, a2b_Handle callbackparam);
typedef a2b_HResult (* pfA2BRegReadFunc)(A2B_TRANSACTION_TYPE eAccessType, a2b_Int16 nTargetNodeAdr, a2b_UInt16 nRegAddr, a2b_UInt16 nRead, a2b_UInt8* rBuf, a2b_Handle callbackParam);

/* Must return time in milliseconds */
typedef a2b_UInt32  (* pfCommChTimerGetSysTimeFunc)(void);

/*! \struct a2b_CommChPal
    Communication Channel Platform Abstraction Layer (PAL) function pointers
*/
typedef struct a2b_CommChPal
{
    /** \name I2C Implementation
     *  I2C prototypes requiring app implementation.
     *
     * \{ */
	pfA2BRegWriteFunc         regWrite ;		/*!< Function pointer for: RegRead */
	pfA2BRegReadFunc          regRead;		/*!< Function pointer for: RegWrite */

    /** \} */

    /** \name Timer Implementation
     *  Timer prototypes requiring app implementation.
     *
     * \{ */
    pfCommChTimerGetSysTimeFunc timerGetSysTime;/*!< Function pointer for: timerGetSysTime */
    /** \} */
}a2b_CommChPal;

/*! \struct a2b_CommPalCtx
    Communication Channel slave PAL context
*/
typedef struct a2b_CommPalCtx
{
    /*!< Pointer to comm channel Pal functions */
	a2b_CommChPal	*pCommChPal;

    /*!< CallbackHandle */
	void*			hCallbackParam;
}a2b_CommPalCtx;

/*! \struct a2b_CommChSlvConfig
    Strcuture holding the various configurable parameters of a Communication channel instance
*/
typedef struct a2b_CommChConfig
{
	/*!< Pointer to status call back function used for indicating events to application*/
	pfCommChStatusCb pfStatCb;

	/*!< Pointer to callback parameter passed during the callback */
	void*			pCbParam;

	/*!< Pointer to  block of memory used for holding channel instance state information */
	void*			pMem;

	/*!< Comm channel Pal context pointer */
	a2b_CommPalCtx	*pCommPalCtx;

	/*!< Mail Box Polling in Slave */
	a2b_UInt8		bMBoxPoll;

	/*! Framing enable or disable */
	A2B_COMMCH_FRAMING		eFraming;

	/*! Current Node type */
	A2B_CURRENT_NODE_TYPE	eNodeType;

	/*! Target Node number */
	a2b_Int8				nTargetNodeNum;


}a2b_CommChConfig;

/*! \struct a2b_CommChSlvInfo
    Structure holding the Communication Channel instance state information
*/
typedef struct a2b_CommChInfo
{
	/*!<  Indicates the current Message transmission state */
	A2B_COMMCH_TX_MSG_STATE	eCommChMsgTxState;

	/*!< Flag to indicate whether data transmitted via mailbox is read by receiver */
	volatile a2b_Bool 		abReadComplete[ADI_A2B_COMMCH_ENG_MAX_NO_OF_MAILBOX];

	/*!< Field to calculate the timeout on mailbox empty interrupt for data transmitted to remote node */
	volatile a2b_UInt32		nTxTimeout;

	/*!< Current time in msec */
	a2b_UInt64 				nCurrTimeInMsec;

	/*!< Start Time when a frame is transmitted over mailbox */
	a2b_UInt64 				nFrameStartTime;

	/*!< Interrupt polling start time */
	a2b_UInt64 				nIntrPollStartTime;

	/*!< Interrupt polling period */
	a2b_UInt64 				nIntrPollPeriod;

	/*!< Communication Channel Pal context pointer */
	a2b_CommPalCtx			*pCommPalCtx;

	/*!< Option to poll mailbox status */
	a2b_UInt8				bMBoxPoll;

	/*! Current Node type */
	A2B_CURRENT_NODE_TYPE	eNodeType;

	/*! Target Node number */
	a2b_Int8				nTargetNodeNum;

	/* Access Type */
	A2B_TRANSACTION_TYPE	eMailboxAccessType;

	/*!< Instance of Communication channel engine */
	a2b_CommChEngInfo		oCommChEngInfo;

}a2b_CommChInfo;

a2b_CommPalCtx*	adi_a2b_CommChPalInit(a2b_CommChPal *pCommChPal, a2b_UInt8 *pMem, a2b_UInt8 nSizeInBytes, a2b_Handle pCallBackParam);
a2b_Handle 		adi_a2b_CommChCreate(a2b_CommChConfig *pCommChConfig);
void 			adi_a2b_CommChDestroy(a2b_Handle hCommCh);
A2B_COMMCH_RET 	adi_a2b_CommChTxMsg(a2b_Handle hCommCh, a2b_CommChMsg *pMsg , a2b_Int8 nNodeAddr);
A2B_COMMCH_RET 	adi_a2b_CommChTick(a2b_Handle *hCommCh);
A2B_COMMCH_RET 	a2b_CommChIntrQuery(a2b_Handle *hCommCh, a2b_Bool *pbMboxIrptClrd);
A2B_COMMCH_RET adi_a2b_CommChSetFraming(a2b_Handle hCommCh, A2B_COMMCH_FRAMING eFraming);
A2B_COMMCH_RET adi_a2b_CommChGetFraming(a2b_Handle hCommCh, A2B_COMMCH_FRAMING *peFraming);
#endif /* ADI_A2B_COMMCH_H_ */

/**@}*/
