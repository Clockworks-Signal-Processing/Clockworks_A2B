/*********************************************************************************
Copyright(c) 2022 Analog Devices, Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
 *********************************************************************************/

/*****************************************************************************
 * adi_a2b_uart.c
 *****************************************************************************/


#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include "adi_a2b_datatypes.h"
#include "adi_a2b_externs.h"
#include <stdint.h>
#include "cmd_platform.h"
#include "cmd_queue.h"
#include <a2bpnp.h>
#include "a2bapp_defs.h"



/*============= D A T A =============*/

extern a2bpnp_NetworkInfo *A2BNetworkInfo;
void adi_UartPrintf(uint8 *pBuffer);
/*============= D E F I N E S =============*/


#define MAX_BUFF_LENGTH		(256u)


/*!< Flag to Manage UART RX Status. */
static volatile bool bRxInProgress = FALSE;
/*!< Flag to Manage UART TX Status. */
static volatile bool bTxInProgress = FALSE;

/*!< Queue for UART Receive Data. */
static  ADI_TERMINAL_RX_MSG_FIFO oRxMsgQueue;
/*!< Queue for UART Transmit Data. */
static  ADI_TERMINAL_TX_MSG_FIFO oTxMsgQueue;


/* UART Handle */
extern A2B_PNP_HANDLE hPnp;

/**********************PROTOTYPES FOR static FUNCTIONS*************************/




/*Function to initialize Tx Queue*/
void adi_terminal_InitTxQueue(void)
{
	oTxMsgQueue.Head = 0;
	oTxMsgQueue.Tail = 0;
	oTxMsgQueue.Size = 0;
	oTxMsgQueue.Max = ADI_TERMINAL_TX_MSG_QUEUE_SIZE;
}

/*Function to initialize Rx Queue*/
void adi_terminal_InitRxQueue(void)
{
	oRxMsgQueue.Head = 0;
	oRxMsgQueue.Tail = 0;
	oRxMsgQueue.Size = 0;
	oRxMsgQueue.Max = ADI_TERMINAL_RX_MSG_QUEUE_SIZE;
}


void terminal_printf(const char *pData, ...)
{
	va_list ap;
	uint32_t NumChars;
	static uint8 aPrintString[UART_BUFFER_SIZE];


	va_start(ap, pData);
	NumChars = vsprintf((char*)aPrintString, (char *) pData, ap);
	va_end(ap);
	//char *pointer1 = pData;
	PAL_PRINTF(aPrintString);

}



void dummy_printf(const char *data, ...)
{

}


bool adi_terminal_RxEnqueue(uint8_t nChar)
{
	bool bRet = TRUE;
	if(oRxMsgQueue.Size > oRxMsgQueue.Max)
	{
		bRet = FALSE;
	}

	if(bRet == TRUE)
	{
		oRxMsgQueue.aData[oRxMsgQueue.Tail] = nChar;
		oRxMsgQueue.Tail++;
		oRxMsgQueue.Size++;
		if(oRxMsgQueue.Tail >= oRxMsgQueue.Max)
		{
			oRxMsgQueue.Tail = 0;
		}
	}
	return(bRet);
}

/*Function to add a message to the UART queue*/
bool adi_terminal_TxEnqueue(uint8_t *pData)
{
	uint32_t  nIndex;
	if(oTxMsgQueue.Size > oTxMsgQueue.Max)
	{
		return(FALSE);
	}

	for(nIndex = 0u; pData[nIndex] != '\0'; nIndex++)
	{
		oTxMsgQueue.oMsg[oTxMsgQueue.Tail].aMsg[nIndex] = pData[nIndex];
	}
	oTxMsgQueue.oMsg[oTxMsgQueue.Tail].nLen = nIndex;


	oTxMsgQueue.Tail++;
	oTxMsgQueue.Size++;

	if(oTxMsgQueue.Tail >= oTxMsgQueue.Max)
	{
		oTxMsgQueue.Tail = 0;
	}
	return(TRUE);
}

/*Function to fetch from the UART Tx queue*/
bool adi_terminal_TxDequeue(uint8_t **pData, uint32_t *pLen)
{
	if(oTxMsgQueue.Size == 0)
	{
		*pLen = 0;
		return(FALSE);
	}

	memset(&(oTxMsgQueue.oMsg[oTxMsgQueue.Head - 1u].aMsg), 0, sizeof(oTxMsgQueue.oMsg[oTxMsgQueue.Head - 1u].aMsg));

	*pData = (uint8_t*)(&(oTxMsgQueue.oMsg[oTxMsgQueue.Head].aMsg));
	*pLen = oTxMsgQueue.oMsg[oTxMsgQueue.Head].nLen;
	oTxMsgQueue.Head++;
	oTxMsgQueue.Size--;

	if(oTxMsgQueue.Head >= oTxMsgQueue.Max)
	{
		oTxMsgQueue.Head = 0;
	}

	return(TRUE);
}

bool adi_terminal_RxDequeue(uint8_t *pData)
{
	bool bRet;
	{
		bRet = TRUE;
		if(oRxMsgQueue.Size == 0)
		{
			bRet = FALSE;
		}

		if(bRet == TRUE)
		{
			*pData = oRxMsgQueue.aData[oRxMsgQueue.Head];
			oRxMsgQueue.Head++;
			oRxMsgQueue.Size--;

			if(oRxMsgQueue.Head >= oRxMsgQueue.Max)
			{
				oRxMsgQueue.Head = 0;
			}
		}
	}
	return(bRet);
}


