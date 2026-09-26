/*******************************************************************************
Copyright (c) 2017 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
*******************************************************************************

   Name       : adi_a2b_pal.c
   
   Description: This file is responsible for handling all TWI related functions.        
                 
   Functions  : a2b_I2cInit()
                a2b_I2cOpenFunc()
                a2b_I2cCloseFunc()
                a2b_I2cReadFunc()
                a2b_I2cWriteFunc()
                a2b_I2cWriteReadFunc()
                a2b_I2cShutdownFunc()
               
                 
   Prepared &
   Reviewed by: Automotive Software and Systems team, 
                IPDC, Analog Devices,  Bangalore, India
                
   @version: $Revision: 3626 $
   @date: $Date: 2015-10-05 14:04:13 +0530 (Mon, 05 Oct 2015) $
               
******************************************************************************/
/*! \addtogroup Target_Dependent Target Dependent
 *  @{
 */

/** @defgroup PAL
 *
 * This module handles all the pal CB wrappers for the underlying target
 * platform
 *
 */

/*! \addtogroup PAL
 *  @{
 */


/*============= I N C L U D E S =============*/

#include "a2b/ctypes.h"
#include "a2b/error.h"
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "adi_a2b_datatypes.h"
#include "a2bplugin-slave/adi_a2b_periconfig.h"
#include "a2b/stack.h"
#include "a2b/util.h"
#include "a2b/conf.h"
#include "a2b/i2c.h"
#include "adi_a2b_pal.h"
#include "adi_a2b_externs.h"
#include "pal_i2c.h"
#include "a2bplugin-master/plugin.h"
#include "plugin_priv.h"
#include "pal_timer.h"


/*============= D E F I N E S =============*/


/*============= D A T A =============*/

static a2b_PalEcb* pTimerEcb;


/*=============================================================================*/

/*============= C O D E =============*/
/*
** Function Prototype section
*/

// TODO: Determine what this should actually be. This is based on what is returned
#define ADI_SPORT_RESULT a2b_UInt32

static ADI_SPORT_RESULT adi_a2b_EnableAudioHost(void);
static a2b_UInt32 adi_a2b_AudioHostConfig(a2b_PalEcb*  palEcb, ADI_A2B_PERI_DEVICE_CONFIG* psDeviceConfig);
static a2b_UInt8 adi_a2b_TwiWriteComplete(a2b_UInt8 nTWIDeviceNo);
static a2b_UInt8 adi_a2b_TwiReadComplete(a2b_UInt8 nTWIDeviceNo);
static a2b_UInt32 adi_a2b_HandleError(a2b_UInt8 nTWIDeviceNo ,  a2b_UInt8 bComplete );

// TODO: Temporary fix for code imported
#define A2B_PAL_L3_CODE
#define A2B_PAL_L3_DATA

A2B_PAL_L3_DATA
static a2b_UInt8 aDataBuffer[ADI_A2B_MAX_PERI_CONFIG_UNIT_SIZE];

void adi_a2b_Delay( uint32_t nTime );

/*
** Function Definition section
*/

/*!****************************************************************************
*
*  \b              a2b_palInit
*
*  <b> API Details: </b><br>
*  Initializes the Platform Abstraction Layer (PAL) structure [function
*  pointers] with defaults for the given platform. These defaults can be
*  over-ridden as needed if necessary.
*                                                                       <br><br>
*  <b> Linux Implementation Details: </b><br>
*  This sets up all the function pointers for core PAL functionality used
*  by the A2B stack.  If A2B_FEATURE_MEMORY_MANAGER is NOT defined then
*  Linux malloc/free is used instead of the stack memory manager.  This
*  also implements some environment variable support for Linux:             <br>
*  - A2B_CONF_TRACE_CHAN_URL                                                <br>
*  - A2B_CONF_TRACE_LEVEL                                                   <br>
*  - A2B_CONF_I2C_DEVICE_PATH                                               <br>
*  - A2B_CONF_PLUGIN_SEARCH_PATTERN                                         <br>
*                                                                           <br>
*  \param          [in]    pal      Pointer to the Platform Abstraction Layer to
*                                   initialize with defaults.
*
*  \param          [in]    ecb      The environment control block (ecb) for
*                                   this platform.
*
*  \pre            None
*
*  \post           The pal function pointer API will be fully
*                  populated with default values.
*
*  \return         None
*
******************************************************************************/
void
a2b_palInit
    (
    struct a2b_StackPal*    pal,
    A2B_ECB*                ecb
    )
{

    a2b_UInt8 nIndex = 0u;
    if ( A2B_NULL != pal )
    {
        nIndex = ecb->palEcb.nChainIndex;
        a2b_memset(pal, 0, sizeof(*pal));
        a2b_memset(ecb, 0, sizeof(*ecb));
        ecb->palEcb.nChainIndex = nIndex;

#ifndef A2B_FEATURE_MEMORY_MANAGER
        /* Use Linux malloc/free */
        pal->memMgrInit      = a2b_pal_memMgrInit;
        pal->memMgrOpen      = a2b_pal_memMgrOpen;
        pal->memMgrMalloc    = a2b_pal_memMgrMalloc;
        pal->memMgrFree      = a2b_pal_memMgrFree;
        pal->memMgrClose     = a2b_pal_memMgrClose;
        pal->memMgrShutdown  = a2b_pal_memMgrShutdown;
#endif

        pal->timerInit       = pal_timerInit;
        pal->timerGetSysTime = pal_timerGetSysTime;
        pal->timerShutdown   = pal_timerShutdown;

#if defined(A2B_FEATURE_SEQ_CHART) || defined(A2B_FEATURE_TRACE)
        pal->logInit         = a2b_pal_logInit;
        pal->logOpen         = a2b_pal_logOpen;
        pal->logClose        = a2b_pal_logClose;
        pal->logWrite        = a2b_pal_logWrite;
        pal->logShutdown     = a2b_pal_logShutdown;
#endif

        pal->i2cInit         = pal_i2cInit;
        pal->i2cOpen         = pal_i2cOpen;
        pal->i2cClose        = pal_i2cClose;
        pal->i2cRead         = pal_i2cRead;
        pal->i2cWrite        = pal_i2cWrite;
        pal->i2cWriteRead    = pal_i2cWriteRead;
        pal->i2cShutdown     = pal_i2cShutdown;

        pal->spiInit		 = A2B_NULL;
        pal->spiOpen		 = A2B_NULL;
        pal->spiClose		 = A2B_NULL;
        pal->spiRead		 = A2B_NULL;
        pal->spiWrite		 = A2B_NULL;
        pal->spiWriteRead	 = A2B_NULL;
        pal->spiFd			 = A2B_NULL;
		pal->spiShutdown	 = A2B_NULL;
#if 1
        pal->audioInit       = a2b_pal_AudioInitFunc;
        pal->audioOpen       = a2b_pal_AudioOpenFunc;
        pal->audioClose      = a2b_pal_AudioCloseFunc;
        pal->audioConfig     = a2b_pal_AudioConfigFunc;
        pal->audioShutdown   = a2b_pal_AudioShutdownFunc;
#endif

        pal->getVersion      = a2b_pal_infoGetVersion;
        pal->getBuild        = a2b_pal_infoGetBuild;

#if defined A2B_BCF_FROM_FILE_IO
        pal->fileRead		 = a2b_pal_FileRead;
#endif

        if ( A2B_NULL != ecb )
        {
            ecb->baseEcb.i2cAddrFmt    = A2B_I2C_ADDR_FMT_7BIT;
            ecb->baseEcb.i2cBusSpeed   = A2B_I2C_BUS_SPEED_100KHZ;
            ecb->baseEcb.i2cMasterAddr = A2B_CONF_DEFAULT_MASTER_NODE_I2C_ADDR;
        }
    }
} /* a2b_palInit */

#ifdef A2B_BCF_FROM_FILE_IO
/*****************************************************************************/
/*!
@brief  This API reads binary file

@param [in]:hnd  - File pointer.
@param [in]:offset  - Number of bytes ot skip from the start.
@param [in]:nRead  - number of bytes to read.
@param [in]:rBuf  - Buffer to read.

@return Return code
			a2b_HResult : 0 -success, 1 failure

*/
/*****************************************************************************/
a2b_HResult a2b_pal_FileRead(a2b_Handle hnd, a2b_UInt16 offset, a2b_UInt16 nRead,
        a2b_Byte* rBuf)
{
	a2b_HResult nReturnValue = A2B_RESULT_SUCCESS;
    FILE* fp = (FILE*)hnd;
    a2b_UInt16 nActualReadBytes;

    if(fp != NULL)
    {
    	nReturnValue = fseek(fp, offset, 0);

    	nActualReadBytes = fread(rBuf, 1, nRead, fp);
    	if(nRead != nActualReadBytes)
    	{
    		nReturnValue = 1;
    	}
    }
    else
    {
    	nReturnValue = 1u;
    }
	return nReturnValue;

}
/*****************************************************************************/
/*!
@brief  This API opens binary file (read mode)

@param [in]:ecb  - A2B ECB structure pointer.
@param [in]:url  - File Path.

@return Return code
			a2b_HResult : 0 -success, 1 failure

*/
/*****************************************************************************/
a2b_HResult a2b_pal_FileOpen(A2B_ECB* ecb, char* url)
{
	a2b_HResult res = A2B_RESULT_SUCCESS;

	/* File read is a binary file. So "rb" in fopen */
	ecb->palEcb.fp = fopen(url, "rb");
	if(ecb->palEcb.fp != A2B_NULL)
	{
		res = A2B_RESULT_SUCCESS;
	}
	else
	{
		res = 1u;
	}

	return(res);
}
/*****************************************************************************/
/*!
@brief  This API closes binary file (read mode)

@param [in]:ecb  - A2B ECB structure pointer.

@return Return code
			a2b_HResult : 0 -success, 1 failure

*/
/*****************************************************************************/
a2b_HResult a2b_pal_FileClose(A2B_ECB* ecb)
{
	 return(fclose(ecb->palEcb.fp));
}
#endif

/*****************************************************************************/
/*!
@brief  This API initializes I2C subsystem.

@param [in]:ecb  - PAL ECB structure.
  
    
@return Return code
        -1: Failure
        -0: Success
        
\note I2C and TWI terms are used  interchangeably        
  
*/   
/*****************************************************************************/
ADI_MEM_A2B_CODE_NO_CRIT
a2b_UInt32 a2b_pal_I2cInit(A2B_ECB* ecb)
{
    A2B_UNUSED( ecb );


    return A2B_RESULT_SUCCESS;
}

/*****************************************************************************/
/*!
@brief  This API Post Initialization of I2C subsystem and
        returns the handle

@param [in]:fmt  - 7-bit or 10-bit address.
@param [in]:speed  - I2C Bus Speed.
@param [in]:ecb  - PAL ECB structure.

@return Return code
			Handle to the I2C module

*/
/*****************************************************************************/
ADI_MEM_A2B_CODE_NO_CRIT
a2b_Handle a2b_pal_I2cOpenFunc(a2b_I2cAddrFmt fmt,
        a2b_I2cBusSpeed speed, A2B_ECB* ecb)
{

    return (0);

}


/*****************************************************************************/
/*!
@brief  This API Reads a bytes of data from an I2C device

@param [in]:hnd  - Handle to the I2C Sub-system.
@param [in]:addr  - Device Address to which I2C communication
                    should happen.
@param [in]:nRead  - Number of bytes to be read.
@param [in]:rBuf  - Pointer to the buffer where read bytes are
                    to be stored.
@return Return code
        -1: Failure
        -0: Success

*/
/*****************************************************************************/
ADI_MEM_A2B_CODE_CRIT
a2b_HResult a2b_pal_I2cReadFunc(a2b_Handle hnd,
        a2b_UInt16 addr, a2b_UInt16 nRead, a2b_Byte* rBuf)
{
    a2b_UInt32 					nReturnValue 	= (a2b_UInt32)1;
   
    /* NOT IMPLEMENTED FOR A2B*/
	A2B_UNUSED(hnd);
    return(nReturnValue);
}

/*****************************************************************************/
/*!
@brief  This API writes a bytes of data to an I2C device

@param [in]:hnd  - Handle to the I2C Sub-system.
@param [in]:addr  - Device Address to which I2C communication
                    should happen.
@param [in]:nWrite  - Number of bytes to be written.
@param [in]:wBuf  - Pointer to the buffer from where bytes are
                    to be written.
@param [in]:nRead  - Number of bytes to be read.
@param [in]:rBuf  - Pointer to the buffer where read bytes are
                    to be stored.
@return Return code
        -1: Failure
        -0: Success

*/
/*****************************************************************************/
ADI_MEM_A2B_CODE_CRIT
a2b_HResult a2b_pal_I2cWriteReadFunc(a2b_Handle hnd,
        a2b_UInt16 addr, a2b_UInt16 nWrite,
        const a2b_Byte* wBuf, a2b_UInt16 nRead,
        a2b_Byte* rBuf)
{
    A2B_UNUSED(hnd);

    a2b_UInt32			nReturnValue;
    return 0;
}

/*****************************************************************************/
/*!
@brief  This API writes a bytes of data to an I2C device

@param [in]:hnd  - Handle to the I2C Sub-system.
@param [in]:addr  - Device Address to which I2C communication
                    should happen.
@param [in]:Write  - Number of bytes to be written.
@param [in]:wBuf  - Pointer to the buffer from where bytes are
                    to be written.
@return Return code
        -1: Failure
        -0: Success


*/
/*****************************************************************************/
ADI_MEM_A2B_CODE_CRIT
a2b_HResult a2b_pal_I2cWriteFunc(a2b_Handle hnd,
        a2b_UInt16 addr, a2b_UInt16 nWrite,
        const a2b_Byte* wBuf)
{
    A2B_UNUSED(hnd);

    return 0;
}
/*****************************************************************************/
/*!
@brief  This API initializes I2C subsystem.

@param [in]:ecb  - PAL ECB structure.


@return Return code
        -1: Failure
        -0: Success

\note I2C and TWI terms are used  interchangeably

*/
/*****************************************************************************/
ADI_MEM_A2B_CODE_NO_CRIT
a2b_UInt32 a2b_pal_I2cShutdownFunc(A2B_ECB* ecb)
{
    A2B_UNUSED( ecb );

    return A2B_RESULT_SUCCESS;
}


/*****************************************************************************/
/*!
@brief  This API Post Initialization of I2C subsystem and
        returns the handle

@param [in]:fmt  - 7-bit or 10-bit address.
@param [in]:speed  - I2C Bus Speed.
@param [in]:ecb  - PAL ECB structure.

@return Return code
			Success or Failure

*/
/*****************************************************************************/
ADI_MEM_A2B_CODE_NO_CRIT
a2b_HResult a2b_pal_I2cCloseFunc(a2b_Handle hnd)
{
    A2B_UNUSED(hnd);

	return (a2b_UInt32)0;
}

/*****************************************************************************/
/*!
@brief  This API Initializes the audio sub-system.

@param [in]:ecb  - PAL ECB structure.


@return Return code
        -1: Failure
        -0: Success

\note I2C and TWI terms are used  interchangeably

*/
/*****************************************************************************/
ADI_MEM_A2B_CODE_NO_CRIT
a2b_HResult a2b_pal_AudioInitFunc(A2B_ECB* ecb)
{

    a2b_UInt8 nReturn = 0xFFu;
    a2b_UInt8 nIndex;
    ADI_A2B_PERI_DEVICE_CONFIG* psDeviceConfig;
    ADI_A2B_NODE_PERICONFIG *psPeriConfig;
    a2b_UInt8 wBuf[2];
    a2b_UInt8 rBuf[8];
    a2b_UInt8 aDataWriteReadBuf[4u];

    if(ecb->palEcb.nChainIndex)
    {
        return (a2b_UInt32)A2B_RESULT_SUCCESS;
    }
    /* A2B feature EEPROM processing */
if(ecb->palEcb.cfgHostFromFile == 1)
{
    a2b_PeripheralNode oPeriphNode, *pPeriphNode;
    a2b_HResult status = A2B_RESULT_SUCCESS;
    a2b_UInt16 nOffset = 0u, payloadLen = 0u, payloadDataLen = 0u;
    a2b_UInt8 cfgType = 0u, cfgCrc = 0u,  nCfgBlocks = 0u, regAddr = 0u, crc8 = 0u;

    nReturn 	= 0u;
    pPeriphNode = &oPeriphNode;

    /* To read the number of EEPROM */
    A2B_GET_UINT16_BE(nOffset, ecb->palEcb.pEepromAudioHostConfig , 0);
    nOffset += 3u;
#ifdef A2B_BCF_FROM_SOC_EEPROM
    A2B_PUT_UINT16_BE(nOffset, wBuf,0);
    status  = a2b_pal_I2cWriteReadFunc( ecb->palEcb.i2chnd,A2B_I2C_EEPROM_ADDR,
                                      2u,  wBuf,
                                      1u,  &nCfgBlocks );
#else
    status  = a2b_pal_FileRead( ecb->palEcb.fp,  nOffset,
                                      1u,  &nCfgBlocks );
#endif


    A2B_GET_UINT16_BE(pPeriphNode->addr, ecb->palEcb.pEepromAudioHostConfig, 0);

    /* 4 bytes from marker */
    pPeriphNode->addr 	  += 4u;
    pPeriphNode->cfgIdx     = 0u;
    pPeriphNode->nodeAddr   = -1;
    pPeriphNode->nCfgBlocks = nCfgBlocks;

    for (pPeriphNode->cfgIdx = 0u;
         pPeriphNode->cfgIdx < pPeriphNode->nCfgBlocks;
         pPeriphNode->cfgIdx++)
    {

#ifdef A2B_BCF_FROM_SOC_EEPROM
        /* Read the config block header bytes */
        /* [Two byte internal EEPROM address] */
        wBuf[0] = (a2b_UInt8)(pPeriphNode->addr >> 8u);
        wBuf[1] = (a2b_UInt8)(pPeriphNode->addr & 0xFFu);
        status  = a2b_pal_I2cWriteReadFunc( ecb->palEcb.i2chnd, A2B_I2C_EEPROM_ADDR,
                                          2u,  wBuf,
                                          3u,  &aDataBuffer[0u] );
#else
        status  = a2b_pal_FileRead( ecb->palEcb.fp, pPeriphNode->addr,
                                          3u,  &aDataBuffer[0u] );
#endif
        if(status != 0u)
        {
        	nReturn = 0xFFu;
			return (nReturn);
        }

        pPeriphNode->addr += 3u;
        cfgType = (aDataBuffer[0u] >> 4u);
        cfgCrc  = aDataBuffer[2];

        payloadLen = (a2b_UInt16)((a2b_UInt16)(((a2b_UInt16)aDataBuffer[0u]) << (a2b_UInt16)8u) |
                       ((a2b_UInt16)aDataBuffer[1])) & (a2b_UInt16)0xFFFu;
        payloadDataLen = payloadLen;

        /* Read the payload if needed */
        if ( (a2b_UInt8)A2B_DEALY_OP == cfgType )
        {
        	(void)adi_a2b_Delay(payloadLen);
        }
        else if( (a2b_UInt8)A2B_WRITE_OP == cfgType )
        {
        	/* The cfgCrc is byte[2] which for this message
			 * it equates to the addr/reg
			 */
        	regAddr = cfgCrc;
#ifdef A2B_BCF_FROM_SOC_EEPROM
            /* Read the payload */
            wBuf[0] = (a2b_UInt8)(pPeriphNode->addr >> 8u);
            wBuf[1] = (a2b_UInt8)(pPeriphNode->addr & 0xFFu);
            status  = pal_i2cWriteRead( ecb->palEcb.i2chnd,
            								  	  A2B_I2C_EEPROM_ADDR,
												  2u, wBuf,
												  payloadLen,
												  &aDataBuffer[0u] );
#else

            status  = a2b_pal_FileRead( ecb->palEcb.fp,
            									  pPeriphNode->addr,
												  payloadLen,
												  &aDataBuffer[0u] );
#endif

            if(status != 0u)
			{
				nReturn = 0xFFu;
				return (nReturn);
			}

	        /* Write to the peripheral */
	        status = pal_i2cWrite( ecb->palEcb.i2chnd,
	                                     (a2b_UInt16)regAddr,
										 payloadDataLen,
										 &aDataBuffer[0u] );
	        if(status != 0u)
			{
				nReturn = 0xFFu;
				return (nReturn);
			}

	        pPeriphNode->addr += payloadLen;
        }
        else{
            regAddr = cfgCrc;

            status  = a2b_pal_FileRead( ecb->palEcb.fp,
                                        pPeriphNode->addr,
                                        payloadLen,
                                        &aDataBuffer[0u] );


            nReturn= pal_i2cWriteRead(ecb->palEcb.i2chnd, (a2b_UInt16)regAddr,
                                        /*Address width:: TBD. ecb->palEcb.pAudioHostDeviceConfig->aDeviceConfig->paPeriConfigUnit[0].nAddrWidth*/   2u, &aDataBuffer[0u],
                                        payloadLen - 2u/*Address width*/, &aDataWriteReadBuf[0u]);
            pPeriphNode->addr += payloadLen;
        }
    }

}
else
{
    psPeriConfig = ecb->palEcb.pAudioHostDeviceConfig;
    nReturn = 0u;
    /* Check with exported peripheral configuration */
    for(nIndex  = 0u; nIndex < (a2b_UInt8)psPeriConfig->nNumConfig; nIndex++)
    {
        psDeviceConfig = &psPeriConfig->aDeviceConfig[nIndex];
        if(psDeviceConfig->bPostDiscCfg == 0)
        {
        	nReturn |= (a2b_UInt8)adi_a2b_AudioHostConfig(&(ecb->palEcb), psDeviceConfig);
        }
    }
}
	return (a2b_UInt32)A2B_RESULT_SUCCESS;
}

/*****************************************************************************/
/*!
@brief  This API Initializes the audio sub-system.

@param [in]:ecb  - PAL ECB structure.


@return Return code
        -1: Failure
        -0: Success

\note I2C and TWI terms are used  interchangeably

*/
/*****************************************************************************/
ADI_MEM_A2B_CODE_NO_CRIT
a2b_Handle a2b_pal_AudioOpenFunc(void)
{
    //dummy return to avoid the null check
	return ((a2b_Handle)1);
}

/****************************************************************************/
/*!
    @brief          This function configures devices connected to slave node
                    through remote I2C

    @param [in]     pNode                   Pointer to A2B node
    @param [in]     psDeviceConfig          Pointer to peripheral device configuration structure

    @return          Return code
                    - 0: Success
                    - 1: Failure
*/
/********************************************************************************/
A2B_PAL_L3_CODE
static a2b_UInt32 adi_a2b_AudioHostConfig(a2b_PalEcb*  palEcb, ADI_A2B_PERI_DEVICE_CONFIG* psDeviceConfig)
{
	A2B_PAL_L3_DATA
    static a2b_UInt8 aDataWriteReadBuf[4u];

    a2b_UInt32 nReturn = 0u;
    ADI_A2B_PERI_CONFIG_UNIT* pOPUnit;
    a2b_UInt8 nIndex, nIndex1;
    a2b_UInt32 nNumOpUnits;
    a2b_UInt32 nDelayVal;
    a2b_Int16 nodeAddr;

    nNumOpUnits = psDeviceConfig->nNumPeriConfigUnit;

    for(nIndex= 0u ; nIndex < nNumOpUnits ; nIndex++ )
    {
        pOPUnit = &psDeviceConfig->paPeriConfigUnit[nIndex];
        /* Operation code*/
        switch(pOPUnit->eOpCode)
        {
           /* write */
            case 0u:
                    adi_a2b_Concat_Addr_Data(&aDataBuffer[0u], pOPUnit->nAddrWidth, pOPUnit->nAddr, 0);
            	    memcpy(&aDataBuffer[pOPUnit->nAddrWidth], pOPUnit->paConfigData, pOPUnit->nDataCount);

            	    nReturn = pal_i2cWrite(palEcb->i2chnd, (a2b_UInt16)psDeviceConfig->nDeviceAddress,
            	    		(pOPUnit->nAddrWidth + pOPUnit->nDataCount), &aDataBuffer[0u]);
                    break;
            /* read */
            case 1u:
            	    adi_a2b_Concat_Addr_Data(&aDataWriteReadBuf[0u], pOPUnit->nAddrWidth, pOPUnit->nAddr, 0);
            	    nReturn= pal_i2cWriteRead(palEcb->i2chnd, (a2b_UInt16)psDeviceConfig->nDeviceAddress,
            	    		pOPUnit->nAddrWidth, &aDataWriteReadBuf[0u],
							pOPUnit->nDataCount, &aDataBuffer[0u]);

                    break;
            /* delay */
            case 2u:nDelayVal = 0u;
			for(nIndex1 = 0u; nIndex1 < pOPUnit->nDataCount; nIndex1++)
			{
				nDelayVal = (pOPUnit->paConfigData[nIndex1] << (8u * nIndex1)) | nDelayVal;
			}
            (void)adi_a2b_Delay(nDelayVal);
            break;


            default: break;

        }

        if(nReturn !=0u)
        {
            break;
        }
    }

    return(nReturn);
}

/*****************************************************************************/
/*!
@brief  This API configures the audio sub-system based on the Master
        TDM settigns

@param [in]:ecb  - PAL ECB structure.


@return Return code
        -1: Failure
        -0: Success

\note I2C and TWI terms are used  interchangeably

*/
/*****************************************************************************/
ADI_MEM_A2B_CODE_NO_CRIT
a2b_HResult a2b_pal_AudioConfigFunc(a2b_Handle hnd,
                                a2b_TdmSettings* tdmSettings)
{
/* Function not needed - special case where one wants update ADAU1452 configuration after discovery. */
	return (a2b_UInt32)A2B_RESULT_SUCCESS;
}

/****************************************************************************/
/*!
    @brief			This function triggers audio data routing

    @return         void

*/
/********************************************************************************/
static ADI_SPORT_RESULT adi_a2b_EnableAudioHost(void)
{

	return((a2b_UInt32)A2B_RESULT_SUCCESS);

}

/*****************************************************************************/
/*!
@brief  This API Initializes the audio sub-system.

@param [in]:ecb  - PAL ECB structure.


@return Return code
        -1: Failure
        -0: Success

\note I2C and TWI terms are used  interchangeably

*/
/*****************************************************************************/
ADI_MEM_A2B_CODE_NO_CRIT
a2b_HResult a2b_pal_AudioCloseFunc(a2b_Handle hnd)
{
	return ((a2b_UInt32)A2B_RESULT_SUCCESS);
}

/*****************************************************************************/
/*!
@brief  This API Initializes the audio sub-system.

@param [in]:ecb  - PAL ECB structure.


@return Return code
        -1: Failure
        -0: Success

\note I2C and TWI terms are used  interchangeably

*/
/*****************************************************************************/
ADI_MEM_A2B_CODE_NO_CRIT
a2b_HResult a2b_pal_AudioShutdownFunc(A2B_ECB* ecb)
{
	A2B_UNUSED(ecb);
	return A2B_RESULT_SUCCESS;
}

/*****************************************************************************/
/*!
@brief  This function implements software delay.

@param [in] nTime Delay in milliseconds (ms)


@return void
*/
/*****************************************************************************/
void adi_a2b_Delay( uint32_t nTime )
{

}

/*****************************************************************************/
/*!
@brief  This API initializes Timer subsystem.

@param [in]:ecb  - PAL ECB structure.


@return Return code
        -1: Failure
        -0: Success

\note I2C and TWI terms are used  interchangeably

*/
/*****************************************************************************/
ADI_MEM_A2B_CODE_NO_CRIT
a2b_HResult a2b_pal_TimerInitFunc(A2B_ECB* ecb)
{
	a2b_HResult 	nReturnValue = (a2b_UInt32)0;
    a2b_UInt32  	nDummy;

    return nReturnValue;
}

/*****************************************************************************/
/*!
@brief  This API gives the current system time

@return Return : Current time in millisec

\note I2C and TWI terms are used  interchangeably

*/
/*****************************************************************************/
ADI_MEM_A2B_CODE_CRIT
a2b_UInt32 a2b_pal_TimerGetSysTimeFunc()
{
	/* The stack uses pal_timerGetSysTime (pal_timer.c) directly; same clock */
	return pal_timerGetSysTime();
}

/*****************************************************************************/
/*!
@brief  This API shuts the Timer subsystem.

@param [in]:ecb  - PAL ECB structure.


@return Return code
        -1: Failure
        -0: Success

\note I2C and TWI terms are used  interchangeably

*/
/*****************************************************************************/
ADI_MEM_A2B_CODE_NO_CRIT
a2b_HResult a2b_pal_TimerShutdownFunc(A2B_ECB* ecb)
{
	a2b_HResult nReturnValue = (a2b_UInt32)0;
    return nReturnValue;
}

/****************************************************************************/
/*!
    @brief          This function returns the Version of the A2B Stack Software

    @param [in]     major                   Major Version
    @param [in]     minor                   Minor Version
    @param [in]     release           Release number of Software

    @return          Return code
                    - 0: Success
                    - 1: Failure
*/
/********************************************************************************/

void a2b_pal_infoGetVersion(a2b_UInt32* major,
        a2b_UInt32* minor,
        a2b_UInt32* release)
{
    if ( A2B_NULL != major ) {
        *major = ADI_A2B_PAL_VER_MAJOR;
    }

    if ( A2B_NULL != minor ) {
        *minor = ADI_A2B_PAL_VER_MINOR;
    }

    if ( A2B_NULL != release ) {
        *release = ADI_A2B_PAL_VER_RELEASE;
    }
}

/****************************************************************************/
/*!
    @brief          This function returns the Version of the A2B Stack Software

    @param [in]     major                   Major Version
    @param [in]     minor                   Minor Version
    @param [in]     release           Release number of Software

    @return          Return code
                    - 0: Success
                    - 1: Failure
*/
/********************************************************************************/

void a2b_pal_infoGetBuild(a2b_UInt32* buildNum,
        const a2b_Char** const buildDate,
        const a2b_Char** const buildOwner,
        const a2b_Char** const buildSrcRev,
        const a2b_Char** const buildHost)
{
    if ( A2B_NULL != buildNum ) {
        *buildNum = ADI_A2B_PAL_BUILD_NUM;
    }

    if ( A2B_NULL != buildDate ) {
        *buildDate = __DATE__;
    }

    if ( A2B_NULL != buildOwner ) {
        *buildOwner = A2B_NULL;
    }

    if ( A2B_NULL != buildSrcRev ) {
        *buildSrcRev = A2B_NULL;
    }

    if ( A2B_NULL != buildHost ) {
        *buildHost = A2B_NULL;
    }
}
#if defined(A2B_FEATURE_SEQ_CHART) || defined(A2B_FEATURE_TRACE)
/*!****************************************************************************
*
*  \b              a2b_pal_logInit
*
*  <b> API Details: </b><br>
*  This routine is called to do initialization the log subsystem
*  during the stack allocation process.
*
*
*
*  \param          [in]    ecb      The environment control block (ecb) for
*                                   this platform.
*
*  \pre            None
*
*  \post           None
*
*  \return         A status code that can be checked with the A2B_SUCCEEDED()
*                  or A2B_FAILED() for success or failure.
*
******************************************************************************/
a2b_HResult a2b_pal_logInit(A2B_ECB*   ecb)
{
	uint8_t i = 0u;
	for(i=0; i<(A2B_TOTAL_LOG_CH); i++)
	{
		ecb->palEcb.oLogConfig[i].fd = A2B_INVALID_FD;
		ecb->palEcb.oLogConfig[i].inUse = false;
	}
	return A2B_RESULT_SUCCESS;
}

/*!****************************************************************************
*
*  \b              a2b_pal_logShutdown
*
*  <b> API Details: </b><br>
*  This routine is called to shutdown the log subsystem
*  during the stack destroy process.  This routine is called immediately
*  after the a2b_pal_logClose (assuming the close was successful).
*
*
*  \param          [in]    ecb      The environment control block (ecb) for
*                                   this platform.
*
*  \pre            None
*
*  \post           None
*
*  \return         A status code that can be checked with the A2B_SUCCEEDED()
*                  or A2B_FAILED() for success or failure.
*
******************************************************************************/
a2b_HResult a2b_pal_logShutdown(A2B_ECB* ecb)
{
    A2B_UNUSED(ecb);
    return A2B_RESULT_SUCCESS;
} /* pal_logShutdown */


/*!****************************************************************************
*
*  \b              a2b_pal_logOpen
*
*  This routine is called to do post-initialization the log subsystem
*  during the stack allocation process.  This routine is called immediately
*  after the pal_logInit (assuming the init was successful).
*
*
*
*  \param          [in]    ecb      The environment control block (ecb) for
*                                   this platform.
*
*  \pre            None
*
*  \post           None
*
*  \return         A status code that can be checked with the A2B_SUCCEEDED()
*                  or A2B_FAILED() for success or failure.
*
******************************************************************************/
a2b_Handle a2b_pal_logOpen(const a2b_Char* url)
{
	uint8_t i = 0u;
	a2b_HResult nHandle = 0U;

	if ( A2B_NULL != url )
	{
		for(i=0; i<(A2B_TOTAL_LOG_CH); i++)
		{
			if(!pTimerEcb->oLogConfig[i].inUse)
			{
				pTimerEcb->oLogConfig[i].fd = (a2b_UInt32)fopen(url, "w");
				nHandle = (a2b_HResult)&pTimerEcb->oLogConfig[i];
				pTimerEcb->oLogConfig[i].inUse = true;
				break;
			}
		}
	}

	return (a2b_Handle)nHandle;
}

/*!****************************************************************************
*
*  \b              a2b_pal_logClose
*
*  This routine is called to de-initialization the log subsystem
*  during the stack destroy process.
*
*
*
*  \param          [in]    ecb      The environment control block (ecb) for
*                                   this platform.
*
*  \pre            None
*
*  \post           None
*
*  \return         A status code that can be checked with the A2B_SUCCEEDED()
*                  or A2B_FAILED() for success or failure.
*
******************************************************************************/
a2b_HResult a2b_pal_logClose(a2b_Handle  hnd)
{
	a2b_HResult nResult = 0xFFFFFFFFU;
	A2B_LOG_INFO *pLogInfo;

	if ( A2B_NULL != hnd )
	{
		pLogInfo = (A2B_LOG_INFO *)hnd;

		fclose((FILE*)pLogInfo->fd);
		pLogInfo->fd = A2B_INVALID_FD;
		pLogInfo->inUse = false;
		nResult = A2B_RESULT_SUCCESS;
	}

	return nResult;
}

/*!****************************************************************************
*
*  \b              a2b_pal_logWrite
*
*  <b> API Details: </b><br>
*  This routine writes to a log channel.
*
*
*  \param          [in]    hnd      The handle returned from pal_logOpen
*
*  \param          [in]    msg      NULL terminated string to log
*
*  \pre            None
*
*  \post           None
*
*  \return         A status code that can be checked with the A2B_SUCCEEDED()
*                  or A2B_FAILED() for success or failure.
*
******************************************************************************/
a2b_HResult a2b_pal_logWrite(
    a2b_Handle      hnd,
    const a2b_Char* msg)
{
	a2b_HResult nResult = 0xFFFFFFFFu;
	a2b_UInt32 msg_len = 0u;
	A2B_LOG_INFO *pLogInfo;
	a2b_Char* newline = "\n";

	if ( (A2B_NULL != hnd) && (A2B_NULL != msg) )
	{
		strcat((char *__restrict__)msg, (char *__restrict__)newline);
		pLogInfo = (A2B_LOG_INFO *)hnd;
		msg_len = a2b_strlen(msg) * sizeof(a2b_Char);
		fwrite(msg, 1, msg_len, (FILE *)pLogInfo->fd);
		nResult = A2B_RESULT_SUCCESS;
	}
	return nResult;
}
#endif

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


