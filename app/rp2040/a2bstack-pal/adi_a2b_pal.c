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
#include "pico/stdlib.h"
#include "hardware/spi.h"
#include "hardware/i2c.h"
#include  "adi_a2b_externs.h"
#include "a2b/error.h"
#include "pio_spi.h"
#include "hardware/dma.h"


/*============= D E F I N E S =============*/

#define PROTOZOA 1 // TODO: need to actually get this defined in compile
#define SPI_USE_HW 0
#define SPI_USE_DMA 0
#define USBI_SENSE          19  // USBi sense on GPIO19 on main RP2040

#if (defined(PROTOZOA) && PROTOZOA)
// IIC For use on AmeNote ProtoZOA Board with A2B Bridge Board
#define PICO_A2B_I2C        i2c0
#define PICO_SDA_PIN        16 // GP2 - U9.13   //GP26 J13.15 SIGMASTUDIO.3
#define PICO_SCL_PIN        17 // GP3 - U9.12 with Jumper on J5.1-2 //GP27 J13.13 SIGMASTUDIO.1

// SPI for use on AmeNote ProtoZOA Board with A2B Bridge Board
#define PICO_SPI            spi1
//#define PICO_SPI_DATARATE   2000 * 1000 // 2MHz with fly wires
//#define PICO_SPI_RX_PIN     13 //8 //GP8 U9.11      //GP19 J13.27 SIGMASTUDIO.5 MISO
//#define PICO_SPI_SCK_PIN    15 //10 //GP10 U9.6     //GP20 J13.25 SIGMASTUDIO.7 SCK
//#define PICO_SPI_TX_PIN     12 //11 //GP11 U9.14    //GP21 J13.23 SIGMASTUDIO.8 MOSI
#define PICO_SPI_CSN_PIN    14 //GP9 U9.1           //GP22 J13.21 SIGMASTUDIO.9 SS

#else // PROTOZOA
// IIC Hardware Defines
#define PICO_A2B_I2C       i2c_default
#define PICO_SDA_PIN       PICO_DEFAULT_I2C_SDA_PIN
#define PICO_SCL_PIN       PICO_DEFAULT_I2C_SCL_PIN

// SPI Hardware Defines
#define PICO_SPI_DATARATE  2000 * 1000 // 2MHz with fly wires
#define PICO_SPI_RX_PIN   4 //GP4
#define PICO_SPI_SCK_PIN  2 //GP2
#define PICO_SPI_TX_PIN   3 //GP3
#define PICO_SPI_CSN_PIN  5 //GP5
#endif

/*============= D A T A =============*/

static a2b_PalEcb* pTimerEcb;
pio_spi_inst_t pio_spi;
#if SPI_USE_DMA
    int spiTxDMA;
    dma_channel_config spiTxDMAConfig;
    int spiRxDMA;
    dma_channel_config spiRxDMAConfig;
#endif // SPI_USE_DMA

/*=============================================================================

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
*  \b              a2b_dump_IDs
* Debug function added for ADI. Dumps VENDOR_ID field for all nodes (where 'all' is hardcoded)
* definitely some very hacked in code...
******************************************************************************/

a2b_UInt32 a2b_dump_IDs(void)
{
    int i, nReturnValue;
    uint32 addr;
    a2b_UInt8 aDataBuf[8u];
    a2b_UInt8 aReadBuf[8u];

    for (i=0; i< 5; i++)  // 5 subnodes, index starts at 0
    {
        // write NODE_ADDR
        aDataBuf[0] = 0x1; //reg on main
        aDataBuf[1] = i;
        addr = 0x6A; // should be A2B_CONF_DEFAULT_MASTER_NODE_I2C_ADDR  but .h files are scattered so just hard code this too

        nReturnValue = i2c_write_timeout_us (PICO_A2B_I2C , addr, aDataBuf, 2, false, 2000); 
        if( (nReturnValue == PICO_ERROR_GENERIC) || (nReturnValue == PICO_ERROR_TIMEOUT) || (nReturnValue < 0))
        {
            printf(" I2C transaction Write Fail: i2c addr 0x%x, Reg add 0x%x, Reg val 0x%x, code : %d \n", addr, aDataBuf[0], aDataBuf[1], nReturnValue);
        }
        // read sub node ID reg
        aDataBuf[0] = 2;
        aReadBuf[0] = 0xFF; // not a valid return value, if we get an error we don't want to see leftover data
        nReturnValue = a2b_pal_I2cWriteReadFunc(PICO_A2B_I2C, addr+1, 1, aDataBuf, 1, aReadBuf);
        if( (nReturnValue == PICO_ERROR_GENERIC) || (nReturnValue == PICO_ERROR_TIMEOUT) || (nReturnValue < 0))
        {
            printf(" I2C transaction read  Fail: i2c addr 0x%x, Reg add 0x%x, code : %d \n", addr, aDataBuf[0], nReturnValue);
        }
        printf("Node %i ID is 0x%X\n", i, aReadBuf[0] );
    }
}

/*!****************************************************************************
*  \b              a2b_I2C_test_0921
* Debug function to see if a write of 0x21 to reg 9 in slave worked.
* sub node must be previously selected
******************************************************************************/

a2b_UInt32 a2b_I2C_test_0921(void)
{
    int i, nReturnValue;
    uint32 addr;
    a2b_UInt8 aDataBuf[8u];
    a2b_UInt8 aReadBuf[8u];

    addr = 0x6A; // should be A2B_CONF_DEFAULT_MASTER_NODE_I2C_ADDR

    // read sub node  reg
    aDataBuf[0] = 9;
    // wrte sub node addr
    nReturnValue = i2c_write_timeout_us(PICO_A2B_I2C , addr+1, aDataBuf, 1, true, 1000);
    if((nReturnValue == PICO_ERROR_GENERIC) || (nReturnValue == PICO_ERROR_TIMEOUT) || (nReturnValue < 0))
    {
        printf("\na2b_I2C_test_0921() write phase fail code %i\n",nReturnValue);
        return -1;
    }
    aReadBuf[0] = 0xFF; // not a valid return value, if we get an error we don't want to see leftover data
    nReturnValue = i2c_read_timeout_us(PICO_A2B_I2C , addr+1, aReadBuf, 1, false, 1000);

    if( (nReturnValue == PICO_ERROR_GENERIC) || (nReturnValue == PICO_ERROR_TIMEOUT) || (nReturnValue < 0))
    {
        printf("a2b_I2C_test_0921() read phase fail code %i\n", nReturnValue);
    }
    if (aReadBuf[0] != 0x21 )
    {
        printf("expected 0x21, got 0x%X\n", i, aReadBuf[0] );
    }
}

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

    if ( A2B_NULL != pal )
    {
        a2b_memset(pal, 0, sizeof(*pal));
        a2b_memset(ecb, 0, sizeof(*ecb));

        /* Do necessary base initialization */
        a2b_stackPalInit(pal, ecb);

#ifndef A2B_FEATURE_MEMORY_MANAGER
        /* Use Linux malloc/free */
        pal->memMgrInit      = a2b_pal_memMgrInit;
        pal->memMgrOpen      = a2b_pal_memMgrOpen;
        pal->memMgrMalloc    = a2b_pal_memMgrMalloc;
        pal->memMgrFree      = a2b_pal_memMgrFree;
        pal->memMgrClose     = a2b_pal_memMgrClose;
        pal->memMgrShutdown  = a2b_pal_memMgrShutdown;
#endif

        pal->timerInit       = a2b_pal_TimerInitFunc;
        pal->timerGetSysTime = a2b_pal_TimerGetSysTimeFunc;
        pal->timerShutdown   = a2b_pal_TimerShutdownFunc;

#if defined(A2B_FEATURE_SEQ_CHART) || defined(A2B_FEATURE_TRACE)
        pal->logInit         = a2b_pal_logInit;
        pal->logOpen         = a2b_pal_logOpen;
        pal->logClose        = a2b_pal_logClose;
        pal->logWrite        = a2b_pal_logWrite;
        pal->logShutdown     = a2b_pal_logShutdown;
#endif

        pal->i2cInit         = a2b_pal_I2cInit;
        pal->i2cOpen         = a2b_pal_I2cOpenFunc;
        pal->i2cClose        = a2b_pal_I2cCloseFunc;
        pal->i2cRead         = a2b_pal_I2cReadFunc;
        pal->i2cWrite        = a2b_pal_I2cWriteFunc;
        pal->i2cWriteRead    = a2b_pal_I2cWriteReadFunc;
        pal->i2cShutdown     = a2b_pal_I2cShutdownFunc;

        pal->spiInit		 = a2b_pal_spiInit;
        pal->spiOpen		 = a2b_pal_spiOpen;
        pal->spiClose		 = a2b_pal_spiClose;
        pal->spiRead		 = a2b_pal_spiRead;
        pal->spiWrite		 = a2b_pal_spiWrite;
        pal->spiWriteRead	 = a2b_pal_spiWriteRead;
        pal->spiFd			 = a2b_pal_spiFd;
		pal->spiShutdown	 = A2B_NULL;
#if 1
        pal->audioInit       = a2b_pal_AudioInitFunc;
        pal->audioOpen       = NULL; /* a2b_pal_AudioOpenFunc;  not sure what this one is about*/  
        pal->audioClose      = a2b_pal_AudioCloseFunc;
        pal->audioConfig     = a2b_pal_AudioConfigFunc;
        pal->audioShutdown   = a2b_pal_AudioShutdownFunc;
#endif
        pal->pluginsLoad     = a2bapp_pluginsLoad;
        pal->pluginsUnload   = a2bapp_pluginsUnload;

        pal->getVersion      = a2b_pal_infoGetVersion;
        pal->getBuild        = a2b_pal_infoGetBuild;

        if ( A2B_NULL != ecb )
        {
            ecb->baseEcb.i2cAddrFmt    = A2B_I2C_ADDR_FMT_7BIT;
            ecb->baseEcb.i2cBusSpeed   = A2B_I2C_BUS_SPEED_400KHZ; // A2B_I2C_BUS_SPEED_100KHZ;
            ecb->baseEcb.i2cMasterAddr = A2B_CONF_DEFAULT_MASTER_NODE_I2C_ADDR;
        }
    }
} /* a2b_palInit */

/* No file system on the RP2040: a bus configuration from a file (configType 2)
 * fails to open instead of being read. */
a2b_HResult a2b_pal_FileRead(a2b_Handle hnd, a2b_UInt16 offset, a2b_UInt16 nRead,
        a2b_Byte* rBuf)
        {
            return 1;
        }
a2b_HResult a2b_pal_FileOpen(A2B_ECB* ecb, char* url)
{
return 1;
}
a2b_HResult a2b_pal_FileClose(A2B_ECB* ecb)
{
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

    if(speed == A2B_I2C_BUS_SPEED_100KHZ)
    {
        i2c_init(PICO_A2B_I2C , 100*1000);
    }
    else
    {
        i2c_init(PICO_A2B_I2C , 400*1000);
    }
	gpio_set_function(PICO_SDA_PIN, GPIO_FUNC_I2C);
	gpio_set_function(PICO_SCL_PIN, GPIO_FUNC_I2C);
    A2B_UNUSED( fmt );
    return (PICO_A2B_I2C );

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
	nReturnValue = i2c_write_timeout_us(PICO_A2B_I2C , addr, wBuf, nWrite, true, 1000*nWrite);
    if((nReturnValue == PICO_ERROR_GENERIC) || (nReturnValue == PICO_ERROR_TIMEOUT) || (nReturnValue < 0))
    {
        printf(" I2C transaction write phase for Read Fail : Reg add 0x%x, Reg val 0x%x, status : %d \n", wBuf[0], rBuf[0], nReturnValue);
        // try again
        nReturnValue = i2c_write_timeout_us(PICO_A2B_I2C , addr, wBuf, nWrite, true, 1000*nWrite);
        if((nReturnValue == PICO_ERROR_GENERIC) || (nReturnValue == PICO_ERROR_TIMEOUT) || (nReturnValue < 0))
        {
            printf(" Retry I2C writephase for Read Fail : Reg add 0x%x, Reg val 0x%x, status : %d \n", wBuf[0], rBuf[0], nReturnValue);
            return(1); // give up
        }
    }
	nReturnValue = i2c_read_timeout_us(PICO_A2B_I2C , addr, rBuf, nRead, false,1000*nRead);
    if((nReturnValue == PICO_ERROR_GENERIC) || (nReturnValue == PICO_ERROR_TIMEOUT) || (nReturnValue < 0))
    {
        printf(" I2C transaction Read Fail : Reg add 0x%x, Reg val 0x%x, status : %d \n", wBuf[0], rBuf[0], nReturnValue);
        //try again
	    nReturnValue = i2c_read_timeout_us(PICO_A2B_I2C , addr, rBuf, nRead, false,1000*nRead);
        if((nReturnValue == PICO_ERROR_GENERIC) || (nReturnValue == PICO_ERROR_TIMEOUT) || (nReturnValue < 0))
        {
            printf(" Retry I2C Read Fail : Reg add 0x%x, Reg val 0x%x, status : %d \n", wBuf[0], rBuf[0], nReturnValue);
            return(1);
        }
        else printf(" Retry worked!\n");
    }
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
    static a2b_UInt16 lastAddr = 0;

    a2b_Int32			nReturnValue;
#if 0
    if ((addr == 0x68 ) && ( lastAddr == 0x69))
        sleep_ms(1000);   // AD2437 seems to NACK a write and then things go south.
    
    if ((wBuf[0] == 1) && ( wBuf[1]==2))
        printf("\ntime for the old 1-2\n");
    if ((wBuf[0] == 1) && ( wBuf[1]==3))
        printf("\ntime for the old 1-3\n");
#endif
    if ( (wBuf[0] == 0x01) && (wBuf[1] == 0x04) )
        //sleep_ms(10);    // give scope enough time to arm again
        gpio_put(DEBUG_IO_1, true);  // trigger scope, etc
	nReturnValue = i2c_write_timeout_us (PICO_A2B_I2C , addr, wBuf, nWrite, false, 1000*nWrite); 
    gpio_put(DEBUG_IO_1, false);     

    if( (nReturnValue == PICO_ERROR_GENERIC) || (nReturnValue == PICO_ERROR_TIMEOUT) || (nReturnValue < 0))
    {
        // gpio_put(DEBUG_IO_1, true);  // trigger scope, etc
        printf(" I2C transaction Write Fail: i2c addr 0x%x, Reg add 0x%x, Reg val 0x%x, code : %d \n", addr, wBuf[0], wBuf[1], nReturnValue);
        //a2b_dump_IDs();
        return 1;
        #if 0
        // IRL I2C is probably now in a broken state, probably want to somehow reset things before a redo
        //otoh a redo means something is really broken.
        // try again
        nReturnValue = i2c_write_timeout_us (PICO_A2B_I2C , addr, wBuf, nWrite, false, 1000*nWrite); 
        if( (nReturnValue == PICO_ERROR_GENERIC) || (nReturnValue == PICO_ERROR_TIMEOUT) || (nReturnValue < 0))
        {
            printf(" I2C retry failed: i2c addr 0x%x, Reg add 0x%x, Reg val 0x%x, code : %d \n", addr, wBuf[0], wBuf[1], nReturnValue);
            return 1;
        }
        else   printf("Retry worked!\n");
        #endif
    }
    else
    {
        if (nReturnValue != nWrite)
        {
            printf(" I2C transaction Write Fail: i2c addr 0x%x, Reg add 0x%x, Reg val 0x%x, expected count: %d actual byte count : %d \n", addr, wBuf[0], wBuf[1], nWrite, nReturnValue);
            return 1;
        }
    }
    lastAddr = addr;
    // test sub node success
    if ( (addr == 0x6B) && (wBuf[0] == 0x9) && (wBuf[1] == 0x21))
    {
        //sleep_ms(10);    // this is a hack to see if changing timing helps
        a2b_I2C_test_0921();
    }
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

	a2b_UInt32		nResult = 0u;
    i2c_deinit(PICO_A2B_I2C );
	return (a2b_UInt32)0;
}

/*****************************************************************************/
/*!
@brief  This API initializes SPI subsystem.

@param [in]:ecb  - PAL ECB structure.
  
    
@return Return code
        -1: Failure
        -0: Success
        
\note Assumes that RP2040 already initialized, only deals with SPI
  
*/   
/*****************************************************************************/
ADI_MEM_A2B_CODE_NO_CRIT
a2b_UInt32 a2b_pal_spiInit(A2B_ECB* ecb)
{
    A2B_UNUSED( ecb );

    // For proper interaction with A2B AD24XX for SPI over Distance - we need to control CSn
    // Set CS pin as GPIO Output
    gpio_init(PICO_SPI_CSN_PIN);
    gpio_set_dir(PICO_SPI_CSN_PIN, GPIO_OUT);
    gpio_put(PICO_SPI_CSN_PIN, 1);
    return A2B_RESULT_SUCCESS;

#if SPI_USE_HW
    spi_init( PICO_SPI, PICO_SPI_DATARATE );

    // Set SPO = 9 and SPI = 1 for continuous data flow
    spi_set_format(PICO_SPI, 8 /*num data bits*/, 1 /*CPOL*/, 1 /*CPHA*/,
            SPI_MSB_FIRST);

    gpio_set_function(PICO_SPI_RX_PIN, GPIO_FUNC_SPI);
    gpio_set_function(PICO_SPI_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(PICO_SPI_TX_PIN, GPIO_FUNC_SPI);

#if SPI_USE_DMA
    // Setup DMA for SPI PIO
    // Setup DMA for Tx and Rx
    // Claim DMA channels - true indicates required which will panic if not available

    // Configure the Tx DMA from buffer to PIO register
    spiTxDMA = dma_claim_unused_channel(true);
    // Set initially to default config
    spiTxDMAConfig = dma_channel_get_default_config(spiTxDMA);
    // Set data transfer size
    channel_config_set_transfer_data_size(&spiTxDMAConfig, DMA_SIZE_8);
    // Configure Tx to increment buffer and write to register
    channel_config_set_read_increment(&spiTxDMAConfig, true);
    channel_config_set_write_increment(&spiTxDMAConfig, false);
    // Set the data request register for SM Tx Register
    channel_config_set_dreq(&spiTxDMAConfig, spi_get_dreq(PICO_SPI, true));

    // Configure the Rx DMA from PIO register to the buffer
    spiRxDMA = dma_claim_unused_channel(true);
    // Set initially to default config
    spiRxDMAConfig = dma_channel_get_default_config(spiRxDMA);
    // Set data transfer size
    channel_config_set_transfer_data_size(&spiRxDMAConfig, DMA_SIZE_8);
    // Configure Rx to read register and write to increment buffer
    channel_config_set_read_increment(&spiRxDMAConfig, false);
    channel_config_set_write_increment(&spiRxDMAConfig, true);
    // Set the data request register for SM Rx Register
    channel_config_set_dreq(&spiRxDMAConfig, spi_get_dreq(PICO_SPI, false));
#endif // SPI_USE_DMA

#else

    pio_spi.pio = pio0;
    pio_spi.sm = pio_claim_unused_sm( pio_spi.pio, true );
    if ( pio_spi_init_hw(&pio_spi) == false )
    {
        printf("a2b_pal_spiInit: Init error in PIO SPI\n");
        return 1;
    }
#endif
    return A2B_RESULT_SUCCESS;
}

/*****************************************************************************/
/*!
@brief  This API Post Initialization of SPI subsystem and
        returns the handle

@param [in]:ecb  - PAL ECB structure.

@return Return code
			Handle to the SPI object

*/
/*****************************************************************************/
ADI_MEM_A2B_CODE_NO_CRIT
a2b_Handle a2b_pal_spiOpen(A2B_ECB* ecb)
{
    A2B_UNUSED( ecb );

    return (PICO_SPI);
}

/*****************************************************************************/
/*!
@brief  This API Reads data from an SPI device

@param [in]:hnd  - Handle to the SPI Sub-system.
@param [in]:addr  - Device Address to which SPI communication
                    should happen (Not used).
@param [in]:nRead  - Number of bytes to be read.
@param [in]:rBuf  - Pointer to the buffer where read bytes are
                    to be stored.
@return Return code
        -1: Failure
        -0: Success

*/
/*****************************************************************************/
ADI_MEM_A2B_CODE_CRIT
a2b_HResult a2b_pal_spiRead(a2b_Handle hnd, a2b_UInt16 addr,
        a2b_UInt16 nRead, a2b_Byte* rBuf)
{
	A2B_UNUSED( addr );
    A2B_UNUSED( hnd );
    return A2B_RESULT_SUCCESS;

#if SPI_USE_HW
    if ( !spi_is_readable(PICO_SPI) )
        return (a2b_HResult)-1;
    
    gpio_put(PICO_SPI_CSN_PIN, 0);
#if SPI_USE_DMA
    uint8_t tempWrite = 0;
    // Config Tx DMA
    dma_channel_configure(
        spiTxDMA,               // the DMA Channel
        &spiTxDMAConfig,        // the configuration structure
        &spi_get_hw(PICO_SPI)->dr,  // address to write
        &tempWrite,             // the read from address
        nRead,               // number of bytes to transfer
        false                   // do not start yet
    );

    // Config Rx DMA
    dma_channel_configure(
        spiRxDMA,               // the DMA Channel
        &spiRxDMAConfig,        // the configuration structure
        rBuf,              // where to write data to
        &spi_get_hw(PICO_SPI)->dr,  // address to read
        nRead,               // number of bytes to transfer
        false                   // do not start yet
    );

    // Start DMA Transfers
    dma_start_channel_mask( ((uint32_t)1 << spiTxDMA) | ((uint32_t)1 << spiRxDMA) );

    while ( dma_channel_is_busy(spiRxDMA) )
    {
        vTaskDelay(0);
    }
    a2b_UInt16 numberRead = nRead;
#else // SPI_USE_DMA
    a2b_UInt16 numberRead = spi_read_blocking( PICO_SPI, /* repeating data */ 0x00,
                                rBuf, nRead );
#endif // SPI_USE_DMA
    gpio_put(PICO_SPI_CSN_PIN, 1);
    
    if ( numberRead != nRead )
    return (a2b_HResult)-1;
#else
    gpio_put(PICO_SPI_CSN_PIN, 0);  // Assert chip select
    pio_spi_read8_blocking(&pio_spi, rBuf, nRead);
    gpio_put(PICO_SPI_CSN_PIN, 1);  // Assert chip select
#endif

    return A2B_RESULT_SUCCESS;
}

/*****************************************************************************/
/*!
@brief  This API writes data to an SPI device

@param [in]:hnd  - Handle to the SPI Sub-system.
@param [in]:addr  - Device Address to which SPI communication
                    should happen (Not Used).
@param [in]:Write  - Number of bytes to be written.
@param [in]:wBuf  - Pointer to the buffer from where bytes are
                    to be written.
@return Return code
        -1: Failure
        -0: Success


*/
/*****************************************************************************/
ADI_MEM_A2B_CODE_CRIT
a2b_HResult a2b_pal_spiWrite(a2b_Handle hnd, a2b_UInt16 addr,
        a2b_UInt16 nWrite, const a2b_Byte* wBuf)
{
	A2B_UNUSED( addr );
    A2B_UNUSED( hnd );
    
    return A2B_RESULT_SUCCESS;
#if SPI_USE_HW
    if ( !spi_is_writable(PICO_SPI) )
        return (a2b_HResult)-1;

    gpio_put(PICO_SPI_CSN_PIN, 0);
#if SPI_USE_DMA
    uint8_t tempRead;
    // Config Tx DMA
    dma_channel_configure(
        spiTxDMA,               // the DMA Channel
        &spiTxDMAConfig,        // the configuration structure
        &spi_get_hw(PICO_SPI)->dr,  // address to write
        wBuf,             // the read from address
        nWrite,               // number of bytes to transfer
        false                   // do not start yet
    );

    // Config Rx DMA
    dma_channel_configure(
        spiRxDMA,               // the DMA Channel
        &spiRxDMAConfig,        // the configuration structure
        &tempRead,              // where to write data to
        &spi_get_hw(PICO_SPI)->dr,  // address to read
        nWrite,               // number of bytes to transfer
        false                   // do not start yet
    );

    // Start DMA Transfers
    dma_start_channel_mask( ((uint32_t)1 << spiTxDMA) | ((uint32_t)1 << spiRxDMA) );

    while ( dma_channel_is_busy(spiRxDMA) )
    {
        vTaskDelay(0);
    }
    a2b_UInt16 numberWrote = nWrite;
#else // SPI_USE_DMA 
    a2b_UInt16 numberWrote = spi_write_blocking( PICO_SPI, wBuf, nWrite );
#endif // SPI_USE_DMA
    gpio_put(PICO_SPI_CSN_PIN, 1);

    if ( numberWrote != nWrite )
        return (a2b_HResult)-1;
#else
    gpio_put(PICO_SPI_CSN_PIN, 0);  // Assert chip select
    pio_spi_write8_blocking(&pio_spi, wBuf, nWrite);
    gpio_put(PICO_SPI_CSN_PIN, 1);  // Assert chip select    
#endif

    return A2B_RESULT_SUCCESS;
}

/*****************************************************************************/
/*!
@brief  This API writes then reads data to an SPI device in
        half duplex mode. Write than read.

@param [in]:hnd  - Handle to the SPI Sub-system.
@param [in]:addr  - Device Address to which SPI communication
                    should happen (Not Used).
@param [in]:nWrite  - Number of bytes to be written.
@param [in]:wBuf  - Pointer to the buffer from where bytes are
                    to be written.
@param [in]:nRead  - Number of bytes to be read.
@param [in]:rBuf  - Pointer to the buffer where read bytes are
                    to be stored.
@return Return code
        -1: Failure of any type.
        -0: Success

*/
/*****************************************************************************/
ADI_MEM_A2B_CODE_CRIT
a2b_HResult a2b_pal_spiWriteRead(a2b_Handle hnd, a2b_UInt16 addr,
        a2b_UInt16 nWrite, const a2b_Byte* wBuf, a2b_UInt16 nRead, a2b_Byte* rBuf)
{
	a2b_Byte readBuf[nRead + nWrite];
    a2b_Byte writeBuf[nRead + nWrite];
    A2B_UNUSED( addr );
    A2B_UNUSED( hnd );

    // TODO: Better detail in return error code
    a2b_HResult returnResult = A2B_RESULT_SUCCESS;
    return returnResult;

    //ignore whatever else this was going to do

#if SPI_USE_HW
    // Try to write
    gpio_put(PICO_SPI_CSN_PIN, 0);
    if ( !spi_is_writable(PICO_SPI) )
    {
        a2b_UInt16 numberWrote = spi_write_blocking( PICO_SPI, wBuf, nWrite );

        if ( numberWrote != nWrite )
            returnResult = -1;
    }
    else
        returnResult = -1;

    // Try to read
    if ( spi_is_readable(PICO_SPI) )
    {
        a2b_UInt16 numberRead = spi_read_blocking( PICO_SPI, /* repeating data */ 0x00,
                            rBuf, nRead );

        if ( numberRead != nRead )
            returnResult = -1;
    }
    else
        returnResult = -1;
    gpio_put(PICO_SPI_CSN_PIN, 1);

#else
#if 1

    memcpy(writeBuf , wBuf ,nWrite);
    memset(&writeBuf[nWrite], 0, nRead);
    gpio_put(PICO_SPI_CSN_PIN, 0);  // Assert chip select
    pio_spi_write8_read8_blocking(&pio_spi, writeBuf, readBuf,  nWrite + nRead);
    gpio_put(PICO_SPI_CSN_PIN, 1);  // Assert chip select   
    memcpy(rBuf, &readBuf[nWrite],nRead);

#else
    gpio_put(PICO_SPI_CSN_PIN, 0);  // Assert chip select
    pio_spi_write8_blocking(&pio_spi, wBuf, nWrite);  
    pio_spi_read8_blocking(&pio_spi, rBuf, nRead);
    gpio_put(PICO_SPI_CSN_PIN, 1);  // Assert chip select
#endif
#endif



    return returnResult;
}

/*****************************************************************************/
/*!
@brief  This API writes and reads data to an SPI device in
        full duplex mode. Read while write. Note that the nRead
        must match nWrite or else system will fail.

@param [in]:hnd  - Handle to the SPI Sub-system.
@param [in]:addr  - Device Address to which SPI communication
                    should happen (Not Used).
@param [in]:nWrite  - Number of bytes to be written.
@param [in]:wBuf  - Pointer to the buffer from where bytes are
                    to be written.
@param [in]:nRead  - Number of bytes to be read.
@param [in]:rBuf  - Pointer to the buffer where read bytes are
                    to be stored.
@return Return code
        -1: Failure of any type.
        -0: Success

*/
/*****************************************************************************/
a2b_HResult a2b_pal_spiFd(a2b_Handle hnd, a2b_UInt16 addr, a2b_UInt16 nWrite,
        const a2b_Byte* wBuf, a2b_UInt16 nRead, a2b_Byte* rBuf)
{
    A2B_UNUSED( addr );
    A2B_UNUSED( hnd );

    // Confirm number of bytes match
    if ( nRead != nWrite )
    {
        return (a2b_HResult)-1;
    }

#if SPI_USE_HW
    // Is SPI writeable
    if ( !spi_is_writable(PICO_SPI) )
        return (a2b_HResult)-1;

    // Is SPI readable
//    if ( !spi_is_readable(PICO_SPI) )
//        return (a2b_HResult)-1;
    gpio_put(PICO_SPI_CSN_PIN, 0);
#if SPI_USE_DMA
    // Config Tx DMA
    dma_channel_configure(
        spiTxDMA,               // the DMA Channel
        &spiTxDMAConfig,        // the configuration structure
        &spi_get_hw(PICO_SPI)->dr,  // address to write
        wBuf,             // the read from address
        nWrite,               // number of bytes to transfer
        false                   // do not start yet
    );

    // Config Rx DMA
    dma_channel_configure(
        spiRxDMA,               // the DMA Channel
        &spiRxDMAConfig,        // the configuration structure
        rBuf,              // where to write data to
        &spi_get_hw(PICO_SPI)->dr,  // address to read
        nWrite,               // number of bytes to transfer
        false                   // do not start yet
    );

    // Start DMA Transfers
    dma_start_channel_mask( ((uint32_t)1 << spiTxDMA) | ((uint32_t)1 << spiRxDMA) );

    while ( dma_channel_is_busy(spiRxDMA) )
    {
        vTaskDelay(0);
    }
    a2b_UInt16 numberReadWrite = nWrite;
#else // SPI_USE_DMA
    sleep_us(100);
    a2b_UInt16 numberReadWrite = spi_write_read_blocking( PICO_SPI, wBuf, rBuf, nRead );
    while ( spi_is_busy(PICO_SPI) );
    sleep_us(100);
#endif // SPI_USE_DMA
    gpio_put(PICO_SPI_CSN_PIN, 1);

    if ( numberReadWrite != nRead )
    {
        return (a2b_HResult)-1;
    }
#else
#if 0
    gpio_put(PICO_SPI_CSN_PIN, 0);  // Assert chip select
    pio_spi_write8_read8_blocking(&pio_spi, wBuf, rBuf, nRead);
    gpio_put(PICO_SPI_CSN_PIN, 1);  // Assert chip select
#endif
#endif
    return A2B_RESULT_SUCCESS;
}

/*****************************************************************************/
/*!
@brief  This API Closes the SPI port from further use

@param [in]:hnd  - Handle to the SPI Sub-system.

@return Return code
			Success or Failure

*/
/*****************************************************************************/
ADI_MEM_A2B_CODE_NO_CRIT
a2b_HResult a2b_pal_spiClose(a2b_Handle hnd)
{
    spi_deinit(hnd);

    return A2B_RESULT_SUCCESS;
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
    nReturn = 0u;
    psPeriConfig = ecb->palEcb.pAudioHostDeviceConfig;
    /* Check with exported peripheral configuration */
    for(nIndex  = 0u; nIndex < (a2b_UInt8)psPeriConfig->nNumConfig; nIndex++)
    {
        psDeviceConfig = &psPeriConfig->aDeviceConfig[nIndex];
        if(psDeviceConfig->bPostDiscCfg == 0)
        {
        	nReturn |= (a2b_UInt8)adi_a2b_AudioHostConfig(&(ecb->palEcb), psDeviceConfig);
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
    // pTimerEcb is never initialized so not sure why/what this code was intended for. I've asked Prasanna at ADI.
	// return (pTimerEcb->pAudioHostDeviceConfig);
    printf("WTF is execution doing in a2b_pal_AudioOpenFunc()?\n");
    return((a2b_Handle) 0); // I wanna see what else blows up.
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

            	    a2b_pal_I2cWriteFunc(palEcb->i2chnd, (a2b_UInt16)psDeviceConfig->nDeviceAddress,
            	    		(pOPUnit->nAddrWidth + pOPUnit->nDataCount), &aDataBuffer[0u]);
                    break;
            /* read */
            case 1u:
            	    adi_a2b_Concat_Addr_Data(&aDataWriteReadBuf[0u], pOPUnit->nAddrWidth, pOPUnit->nAddr, 0);
            	    a2b_pal_I2cWriteReadFunc(palEcb->i2chnd, (a2b_UInt16)psDeviceConfig->nDeviceAddress,
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
#if 0
    a2b_UInt8 nReturn;
    a2b_UInt8 nIndex;
    ADI_A2B_PERI_DEVICE_CONFIG* psDeviceConfig;
    ADI_A2B_NODE_PERICONFIG *psPeriConfig;
	A2B_UNUSED(tdmSettings);

    psPeriConfig = (ADI_A2B_NODE_PERICONFIG*)hnd;
    nReturn = 0u;
    /* Check with exported peripheral configuration */
    for(nIndex  = 0u; nIndex < (a2b_UInt8)psPeriConfig->nNumConfig; nIndex++)
    {
        psDeviceConfig = &psPeriConfig->aDeviceConfig[nIndex];
        if(psDeviceConfig->bPostDiscCfg == 1u)
        {
        	nReturn = (a2b_UInt8)adi_a2b_AudioHostConfig(pTimerEcb, psDeviceConfig);
        }
    }
#endif
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
    absolute_time_t currentTime = get_absolute_time();
    absolute_time_t futureTime = delayed_by_ms( currentTime, nTime );

    // Check for wrap of timer
    while ( to_us_since_boot(currentTime) > to_us_since_boot(futureTime) )
    {
        currentTime = get_absolute_time();
    }

    while ( to_us_since_boot(currentTime) < to_us_since_boot(futureTime) )
    {
        currentTime = get_absolute_time();
    }
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
    return (time_us_32()/1000);
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


/*****************************************************************************/
/*!

@brief  Callback function for TWI events. It is invoked upon TWI events

@param [in]     pCBParam       TWI device handle
@param [in]     Event               TWI event
@param [in]     pArg                pointer to user argument


@return:
    void
*/
/*****************************************************************************/
static void adi_a2b_TWICallbackFunction(void* pCBParam, a2b_UInt32 Event, void* pArg)
{
	a2b_UInt32 event = Event;

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


