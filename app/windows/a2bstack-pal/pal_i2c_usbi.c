/*=============================================================================
 *
 * Project: a2bstack
 *
 * Copyright (c) 2015 -2023 Analog Devices Inc. All Rights Reserved.
 * See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
 *
 *=============================================================================
 *
 * \file:   pal_i2c_usbi.c
 * \author: Mentor Graphics, Embedded Software Division
 * \brief:  The platform I2C implementation using libusb and the ADI USBi
 *          (I2C to USB) adapter.
 *
 *=============================================================================
 */

/*============================================================================*/
/** 
 * \ingroup  a2bstack_pal_i2c
 * \defgroup a2bstack_pal_i2c_usbi          USBi I2C Module 
 *   
 * The platform I2C implementation using libusb and the ADI USBi
 * (I2C to USB) adapter.
 *  
 * \{ */
/*============================================================================*/


/*======================= I N C L U D E S =========================*/

#include "libusb.h"
#include "pal_i2c.h"
#include "a2b/conf.h"
#include "a2b/error.h"
#include "platform.h"
#include "a2b/util.h"
#include "stdio.h"

#if TRUE
/*----------------------------------------------------------------------------*/
/** 
 * \defgroup a2bstack_pal_i2c_usbi_priv             \<Private\> 
 *  
 * This defines the private API's that are private to USBi I2C. 
 *  
 * \{ */
/*----------------------------------------------------------------------------*/
/**
 * \defgroup a2bstack_pal_i2c_usbi_defs          Types/Defs
 *  
 * The various defines and data types used within the USBi I2C modules.
 *
 * \{ */
/*----------------------------------------------------------------------------*/

/*======================= D E F I N E S ===========================*/


#ifndef A2B_USBI_DOT_NET_DRIVER

/* USB product/vendor identifiers for the ADI USBi I2C/SPI adapter */
#define A2B_ADI_USBI_VENDOR_ID      (0x0456)
#define A2B_ADI_USBI_PRODUCT_ID     (0x7031)

#define A2B_ADI_USBI_INTERFACE      (1)


/* USBi command set */

/* Write (write address + data), stop on write */
#define A2B_USBI_CMD_WRITE          (0xB2)

/* Write (write address + data) - follow immediately by read of data */
#define A2B_USBI_CMD_WRITE_READ     (0xB3)

/* Read (data) */
#define A2B_USBI_CMD_READ           (0xB4)

/* Command "Index" data is not used */
#define A2B_USBI_CMD_CHECK_WRITE    (0xB5)

/* Command "Index" data is not used */
#define A2B_USBI_CMD_CHECK_READ     (0xB6)

/* Pulls the BRD_RESET line low for 1 msec and then returns it high */
#define A2B_USBI_CMD_RESET          (0xB7)

/* Controls the power transistor that provides 5V supply to the target board */
#define A2B_USBI_CMD_POWERBOARD     (0xB8)

/* Enable or disable the LED on the USBi */
#define A2B_USBI_CMD_LED            (0xB9)

/* Toggle the reset */
#define A2B_USBI_CMD_RESET_TOGGLE   (0xBA)

/*
 * Transfer status values
 */

/* Nothing valid or invalid to report (idle) */
#define A2B_USBI_XFER_STATUS_NONE       (0)

/* Last transaction completed successfully (I2C/SPI) */
#define A2B_USBI_XFER_STATUS_SUCCESS    (1)

/* Currently writing */
#define A2B_USBI_XFER_STATUS_WRITING    (2)

/* Last transaction was not acknowledged by target device (I2C) or SPI failure */
#define A2B_USBI_XFER_STATUS_FAILED     (3)

/* Bit error during last transaction (I2C) */
#define A2B_USBI_XFER_STATUS_ERROR      (4)

/* Something incredibly wrong happened and the USBi needs to be reset */
#define A2B_USBI_XFER_STATUS_EXCEPTION  (5)


/* The maximum spin count waiting for a read/write to complete */
#define A2B_USBI_MAX_SPIN_COUNT         (30)

/* The maximum spin period (msec) */
#define A2B_USBI_MAX_SPIN_PERIOD        (10)

/* Define the amount of time to delay (usec) before checking the
 * status of an I2C transaction.
 */
#define A2B_USBI_CHK_COMPLETION_DELAY   (2000)

/* Maximum number of bytes that can be read/written in one chunk */
#define A2B_USBI_MAX_RW_CHUNK_SIZE      (6000)

/* in ms */
#define LIBUSB_TRANSFER_TIME_OUT        (500)


#define A2B_USBI_BM_REQUEST_TYPE_XFER_TO_DEVICE  (LIBUSB_ENDPOINT_OUT | \
                                            LIBUSB_REQUEST_TYPE_VENDOR | \
                                            LIBUSB_RECIPIENT_DEVICE)

#define A2B_USBI_BM_REQUEST_TYPE_XFER_TO_HOST (LIBUSB_ENDPOINT_IN | \
                                                LIBUSB_REQUEST_TYPE_VENDOR | \
                                                LIBUSB_RECIPIENT_DEVICE)

/* Define the USBi protocol being used */
#define A2B_USBI_PROTOCOL_I2C           (0)


/*======================= L O C A L  P R O T O T Y P E S  =========*/

typedef struct pal_I2cDcb
{
    a2b_Bool                inUse;
    a2b_I2cAddrFmt          fmt;
    libusb_context*         ctx;
    libusb_device_handle*   hnd;
    a2b_UInt32              xferTimeout;
    a2b_Bool                xferOk;         /* an I2C transfer has succeeded since open */
    a2b_Bool                hintShown;      /* the "is the USBi connected" hint was printed */
} pal_I2cDcb;

/*======================= D A T A  ================================*/

static pal_I2cDcb gsI2cDcbPool[A2B_CONF_MAX_NUM_I2C_DEVICES];

/** \} -- a2bstack_pal_i2c_usbi_defs */

a2b_Bool bi2cLog = 0;
FILE *file =  A2B_NULL;
char file_name[] = "pnp_i2c_log.csv";

/*======================= C O D E =================================*/
 int usleep(unsigned int mseconds);

/*!****************************************************************************
*
*  \b              pal_checkWrite
*
*  This routine attempts to get the USB write transfer status.
*
*  \param          [in]    dcb      USB data control block (dcb)      
*
*  \param          [in]    addr     The I2C address
*
*  \pre            None
*
*  \post           None
*
*  \return         USB write tranfer status
*
******************************************************************************/
static a2b_Byte
pal_checkWrite
    (
    pal_I2cDcb* dcb,
    a2b_UInt16  addr
    )
{

    a2b_Byte xferStatus = A2B_USBI_XFER_STATUS_EXCEPTION;
    int nRead;

    if ( A2B_NULL != dcb )
    {
        nRead = libusb_control_transfer(dcb->hnd,
                        A2B_USBI_BM_REQUEST_TYPE_XFER_TO_HOST,
                        A2B_USBI_CMD_CHECK_WRITE,
                        addr,
                        0,
                        &xferStatus,
                        sizeof(xferStatus),
                        dcb->xferTimeout
                        );

        if (nRead != sizeof(xferStatus) )
        {
            xferStatus = A2B_USBI_XFER_STATUS_EXCEPTION;
        }
    }

    return xferStatus;
} /* pal_checkWrite */


/*!****************************************************************************
*
*  \b              pal_checkRead
*
*  This routine attempts to get the USB read transfer status.
*
*  \param          [in]    dcb      USB data control block (dcb)      
*
*  \param          [in]    addr     The I2C address
*
*  \pre            None
*
*  \post           None
*
*  \return         USB read tranfer status
*
******************************************************************************/
static a2b_Byte
pal_checkRead
    (
    pal_I2cDcb* dcb,
    a2b_UInt16  addr
    )
{

    a2b_Byte xferStatus = A2B_USBI_XFER_STATUS_EXCEPTION;
    int nRead;

    if ( A2B_NULL != dcb )
    {
        nRead = libusb_control_transfer(dcb->hnd,
                        A2B_USBI_BM_REQUEST_TYPE_XFER_TO_HOST,
                        A2B_USBI_CMD_CHECK_READ,
                        addr,
                        0,
                        &xferStatus,
                        sizeof(xferStatus),
                        dcb->xferTimeout
                        );

        if (nRead != sizeof(xferStatus) )
        {
            xferStatus = A2B_USBI_XFER_STATUS_EXCEPTION;
        }
    }

    return xferStatus;
} /* pal_checkRead */


/*!****************************************************************************
*
*  \b              pal_waitForRead
*
*  This routine queries USB to get the read transfer status and waits for 
*  the bus to be clear of the read operation before returning.  This 
*  added delay is needed to ensure smooth operation with the ADI USBi.
*
*  \param          [in]    dcb      USB data control block (dcb)      
*
*  \param          [in]    addr     The I2C address
*
*  \pre            None
*
*  \post           None
*
*  \return         USB read tranfer status
*
******************************************************************************/
static a2b_Byte
pal_waitForRead
    (
    pal_I2cDcb* dcb,
    a2b_UInt16  addr
    )
{
    a2b_Int32 spinCount = 0;
    a2b_Byte xferStatus = A2B_USBI_XFER_STATUS_EXCEPTION;

    if ( A2B_NULL != dcb )
    {
        /* This delay seems to be a necessary hack for the
         * USBi adapter.
         */
        usleep(A2B_USBI_CHK_COMPLETION_DELAY);

        xferStatus = pal_checkRead(dcb, addr);
        while ( (xferStatus == A2B_USBI_XFER_STATUS_NONE) &&
            (spinCount < A2B_USBI_MAX_SPIN_COUNT) )
        {
            usleep(A2B_USBI_MAX_SPIN_PERIOD * 1000);
            spinCount = spinCount + 1;
            xferStatus = pal_checkRead(dcb, addr);
        }
    }

    return xferStatus;
} /* pal_waitForRead */
 int usleep(unsigned int mseconds)
{
	/*clock_t goal = mseconds + clock();
	while (goal > clock());*/
	unsigned int milliseconds = mseconds / 1000;
	Sleep(milliseconds);
	return 0;
}
/*!****************************************************************************
*
*  \b              pal_waitForWrite
*
*  This routine queries USB to get the write transfer status and waits for 
*  the bus to be clear of the write operation before returning.  This 
*  added delay is needed to ensure smooth operation with the ADI USBi.
*
*  \param          [in]    dcb      USB data control block (dcb)      
*
*  \param          [in]    addr     The I2C address
*
*  \pre            None
*
*  \post           None
*
*  \return         USB read tranfer status
*
******************************************************************************/
static a2b_Byte
pal_waitForWrite
    (
    pal_I2cDcb* dcb,
    a2b_UInt16  addr
    )
{
    a2b_Int32 spinCount = 0;
    a2b_Byte xferStatus = A2B_USBI_XFER_STATUS_EXCEPTION;

    if ( A2B_NULL != dcb )
    {
        /* This delay seems to be a necessary hack for the
         * USBi adapter.
         */
        usleep(A2B_USBI_CHK_COMPLETION_DELAY);

        xferStatus = pal_checkWrite(dcb, addr);
        /* Keep polling while the USBi hasn't started the transfer (NONE) as well as
         * while it is writing. The first check used to come ~15 ms after the write
         * (Windows' default sleep granularity); with 1 ms granularity
         * (timeBeginPeriod(1) in a2bapp_server.c) it can come before the USBi has
         * begun, and NONE was taken as a failure. */
        while ( ((xferStatus == A2B_USBI_XFER_STATUS_WRITING) ||
                 (xferStatus == A2B_USBI_XFER_STATUS_NONE)) &&
            (spinCount < A2B_USBI_MAX_SPIN_COUNT) )
        {
            usleep(A2B_USBI_MAX_SPIN_PERIOD * 1000);
            spinCount = spinCount + 1;
            xferStatus = pal_checkWrite(dcb, addr);
        }
    }

    return xferStatus;
} /* pal_waitForWrite */

/*!****************************************************************************
*
*  \b              pal_convertXferStatusToHResult
*
*  This routine converts an ADI USBi API error to an A2B stack error.
*
*  \param          [in]    status   ADI USBi API error/status code
*
*  \pre            None
*
*  \post           None
*
*  \return         A status code that can be checked with the A2B_SUCCEEDED()
*                  or A2B_FAILED() for success or failure.
*
******************************************************************************/
static a2b_HResult
pal_convertXferStatusToHResult
    (
    a2b_Byte    status
    )
{
    a2b_HResult  rc;

    switch ( status )
    {
        case A2B_USBI_XFER_STATUS_SUCCESS:
            return A2B_RESULT_SUCCESS;

        case A2B_USBI_XFER_STATUS_FAILED:
            rc = A2B_EC_I2C_DATA_NACK;
            break;

        case A2B_USBI_XFER_STATUS_ERROR:
            rc = A2B_EC_IO;
            break;

        case A2B_USBI_XFER_STATUS_EXCEPTION:
        case A2B_USBI_XFER_STATUS_NONE:
        default:
            rc = A2B_EC_INTERNAL;
            break;
    }

    return A2B_MAKE_HRESULT(A2B_SEV_FAILURE, A2B_FAC_PLATFORM, rc);
} /* pal_convertXferStatusToHResult */

/* Until one I2C transfer has succeeded, a failure most likely means the USBi can't
 * reach the A2B board at all, so say that once. Later failures (e.g. a sub-node
 * dropping off the bus) are left to the stack. */
static void
pal_checkFirstXfer
    (
    pal_I2cDcb*  dcb,
    a2b_UInt16   addr,
    a2b_HResult  status
    )
{
    if ( A2B_SUCCEEDED(status) )
    {
        dcb->xferOk = A2B_TRUE;
    }
    else if ( !dcb->xferOk && !dcb->hintShown )
    {
        const char* how;
        switch ( A2B_ERR_CODE(status) )
        {
            case A2B_EC_I2C_DATA_NACK: how = "no acknowledge from the device"; break;
            case A2B_EC_IO:            how = "I2C error reported by the USBi"; break;
            default:                   how = "no result from the USBi"; break;
        }
        dcb->hintShown = A2B_TRUE;
        printf("\n\rI2C transfer to address 0x%02X failed (%s), and none has worked yet. Is the"
               " USBi connected to the A2B board, is the board powered, and is there a device at"
               " that address?\n\r", (unsigned int)addr, how);
    }
} /* pal_checkFirstXfer */

/** \} -- a2bstack_pal_i2c_usbi_priv */


/*!****************************************************************************
*
*  \b              pal_i2cInit
*
*  <b> API Details: </b><br>
*  This routine is called to do initialization the I2C subsystem
*  during the stack allocation process.
*                                                                       <br><br>
*  <b> Linux Implementation Details: </b><br>
*  This call will attempt to initialize libusb (libusb_init).  If successful
*  it will also set the libusb debug verbosity level.
*                                                                           <br>
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
a2b_HResult
pal_i2cInit
    (
    A2B_ECB*    ecb
    )
{
    a2b_HResult status = A2B_MAKE_HRESULT(A2B_SEV_FAILURE, A2B_FAC_PLATFORM,
                                         A2B_EC_INVALID_PARAMETER);

    if ( (A2B_NULL != ecb))
    {
        if(ecb->palEcb.nChainIndex == 0)
        {
            if ( 0 != libusb_init(&ecb->palEcb.usbCtx) )
            {
                status = A2B_MAKE_HRESULT(A2B_SEV_FAILURE, A2B_FAC_PLATFORM,
                                            A2B_EC_INTERNAL);
                printf("\n\r Error: Failed to use LibUSB \n\r");                           
            }
            else
            {
				/* Set the verbosity level to 3 as suggested in the
				 * libusb documentation.
				 */
				libusb_set_option(ecb->palEcb.usbCtx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_WARNING);
                status = A2B_RESULT_SUCCESS;
                if(bi2cLog)
                {
                    // Open the file in write mode
                    file = fopen(file_name, "w");
                }
            }
        }
        else
        {
            status = A2B_RESULT_SUCCESS;
            return status;
        }
    }
   ecb->palEcb.useFirstAvailable = 1;
   ecb->palEcb.xferTimeout = LIBUSB_TRANSFER_TIME_OUT;
    return status;
} /* pal_i2cInit */


/*!****************************************************************************
*
*  \b              pal_i2cOpen
*
*  <b> API Details: </b><br>
*  This routine is called to do post-initialization the I2C subsystem
*  during the stack allocation process.  This routine is called immediately
*  after the pal_i2cInit (assuming the init was successful).
*                                                                       <br><br>
*  <b> Linux Implementation Details: </b><br>
*  Attempts to find and open an available ADI USBi device
*  with the user defined settings for the address.
* 
*  \note    The speed setting is not supported for the ADI USBi device.
*                                                                           <br>
*  \param          [in]    fmt      The I2C address format (7-bit or 10-bit)
* 
*  \param          [in]    speed    The I2C bus speed (100KHz, 400KHz)
*                                   (Speed setting is not supported for the
*                                   ADI USBi device)
* 
*  \param          [in]    ecb      The environment control block (ecb) for
*                                   this platform.
*
*  \pre            None
*
*  \post           None
*
*  \return         If non-zero the open was considered sucessfully.  If A2B_NULL
*                  I2C initialization must have failed and the stack
*                  allocation will fail.  This returned handle will be passed
*                  back into the other PAL I2C (pal_i2cXXX) API calls.
*
******************************************************************************/
a2b_Handle
pal_i2cOpen
    (
    a2b_I2cAddrFmt  fmt,
    a2b_I2cBusSpeed speed,
    A2B_ECB*        ecb
    )
{
    static pal_I2cDcb* dcb = A2B_NULL;
    a2b_UInt16 idx;
    libusb_device** devices;
    libusb_device* dev;
    struct libusb_device_descriptor desc;
    int nDevs;
    int devIdx;
    int rc;
    int libusbret = 0;
    a2b_Bool usbiFound = A2B_FALSE;


    /* The ADI USBi doesn't provide a mechanism to select the I2C speed */
    A2B_UNUSED(speed);


    /* Ten-bit I2C addresses are not supported by USBi */
    if ( (A2B_NULL != ecb) && (A2B_I2C_ADDR_FMT_7BIT == fmt))
    {
        if(ecb->palEcb.nChainIndex == 0)
        {
            /* Look for an available device control block */
            for ( idx = 0; idx < A2B_ARRAY_SIZE(gsI2cDcbPool); ++idx )
            {
                /* If this device control block is available then ... */
                if ( !gsI2cDcbPool[idx].inUse )
                {
                    dcb = &gsI2cDcbPool[idx];
                    a2b_memset(dcb, 0, sizeof(*dcb));

                    nDevs = libusb_get_device_list(ecb->palEcb.usbCtx, &devices);

                    for ( devIdx = 0; devIdx < nDevs; ++devIdx )
                    {
                        dev = devices[devIdx];

                        libusb_get_device_descriptor(dev, &desc);

                        if ( (0 == libusb_get_device_descriptor(dev, &desc)) &&
                            (desc.idVendor == A2B_ADI_USBI_VENDOR_ID) &&
                            (desc.idProduct == A2B_ADI_USBI_PRODUCT_ID) )
                        {
                            if ( ecb->palEcb.useFirstAvailable ||
                                ((ecb->palEcb.busNum ==
                                libusb_get_bus_number(dev)) &&
                                (ecb->palEcb.devAddr ==
                                libusb_get_device_address(dev))) )
                            {
                                usbiFound = A2B_TRUE;
                                libusbret =libusb_open(dev, &dcb->hnd);
                                if ( 0 ==  libusbret)
                                {
                                    break;
                                }
                                else
                                {

                                }
                            }
                        }
                    }

                    /* Free the device list */
                    libusb_free_device_list(devices, A2B_TRUE);
                    break;
                }
            }

            /* If a free device control block was allocated then ...
            */
            if ( A2B_NULL != dcb )
            {
                /* If the USB adapter couldn't be opened */
                if ( A2B_NULL == dcb->hnd )
                {
                    if ( !usbiFound )
                    {
                        printf("\n\rNo ADI USBi found (USB ID %04x:%04x). Is the USBi connected"
                               " to this PC, and does it use the WinUSB driver? (app/windows/README.md)\n\r",
                               A2B_ADI_USBI_VENDOR_ID, A2B_ADI_USBI_PRODUCT_ID);
                    }
                    else
                    {
                        printf("\n\rThe ADI USBi was found but can't be opened (%s). Is it using the"
                               " WinUSB driver, and is no other program (e.g. SigmaStudio) using it?\n\r",
                               libusb_error_name(libusbret));
                    }
                    /* Reset to indicate allocation failed */
                    dcb = A2B_NULL;
                }
                else
                {
                    
                    /* See to see if the kernel has already claimed the device */
                    //rc = libusb_kernel_driver_active(dcb->hnd, A2B_ADI_USBI_INTERFACE);
        //            if ( 1 == rc)
        //            {
        //                /* Detach the kernel before we try to claim it */
        //                libusb_detach_kernel_driver(dcb->hnd,
        //                                                A2B_ADI_USBI_INTERFACE);
        //            }

                    /* Claim the interface for ourselves */
                    rc = 0;
                    //rc = libusb_claim_interface(dcb->hnd, A2B_ADI_USBI_INTERFACE);
                    if ( rc != 0 )
                    {
                        libusb_close(dcb->hnd);
                        dcb = A2B_NULL;
                    }
                    else
                    {
                        dcb->inUse = A2B_TRUE;
                        dcb->ctx = ecb->palEcb.usbCtx;
                        dcb->fmt = fmt;
                        dcb->xferTimeout = ecb->palEcb.xferTimeout;
                    }
                }
            }
        }
    }
    ecb->palEcb.i2chnd = dcb;
    return dcb;
} /* pal_i2cOpen */


/*!****************************************************************************
*
*  \b              pal_i2cClose
*
*  <b> API Details: </b><br>
*  This routine is called to de-initialization the i2c subsystem
*  during the stack destroy process.
*                                                                       <br><br>
*  <b> Linux Implementation Details: </b><br>
*  If an ADI USBi device was opened it will be called by
*  calling its libusb release/close API.
*                                                                           <br>
*  \param          [in]    hnd          The handle returned from pal_i2cOpen
*
*  \pre            None
*
*  \post           None
*
*  \return         A status code that can be checked with the A2B_SUCCEEDED()
*                  or A2B_FAILED() for success or failure.
*
******************************************************************************/
a2b_HResult
pal_i2cClose
    (
    a2b_Handle  hnd
    )
{
    a2b_HResult status = A2B_MAKE_HRESULT(A2B_SEV_FAILURE, A2B_FAC_I2C,
                                        A2B_EC_INVALID_PARAMETER);
    pal_I2cDcb* dcb;

    if ( A2B_NULL != hnd )
    {
        dcb = (pal_I2cDcb*)hnd;

        if ( A2B_NULL != dcb->hnd )
        {
            libusb_release_interface(dcb->hnd, A2B_ADI_USBI_INTERFACE);
            libusb_close(dcb->hnd);
            status = A2B_RESULT_SUCCESS;
            if(bi2cLog)
            {
                if(file != A2B_NULL)
                {
                 fclose(file);
                }
            }
        }
        else
        {
            status = A2B_MAKE_HRESULT(A2B_SEV_FAILURE, A2B_FAC_I2C,
                                        A2B_EC_INTERNAL);
        }
        dcb->hnd = A2B_NULL;
        dcb->inUse = A2B_FALSE;
    }
    return status;
} /* pal_i2cClose */


/*!****************************************************************************
*
*  \b              pal_i2cRead
*
*  <b> API Details: </b><br>
*  This routine reads bytes from an I2C device.
*                                                                       <br><br>
*  <b> Linux Implementation Details: </b><br>
*  The ADI USBi device libusb API is called to read the I2C device.
*                                                                           <br>
*  \param          [in]      hnd        The handle returned from pal_i2cOpen.
*                            
*  \param          [in]      addr       The I2C address.
*                            
*  \param          [in]      nRead      The number of bytes to read from
*                                       the device.
* 
*  \param          [in,out]  rBuf       A buffer in which to write the results
*                                       of the read.
*
*  \pre            None
*
*  \post           The read buffer holds the contents of the read on success.
*
*  \return         A status code that can be checked with the A2B_SUCCEEDED()
*                  or A2B_FAILED() for success or failure.
*
******************************************************************************/
a2b_HResult
pal_i2cRead
    (
    a2b_Handle  hnd,
    a2b_UInt16  addr,
    a2b_UInt16  nRead,
    a2b_Byte*   rBuf
    )
{
    a2b_HResult status = A2B_MAKE_HRESULT(A2B_SEV_FAILURE, A2B_FAC_I2C,
                                        A2B_EC_INVALID_PARAMETER);
    pal_I2cDcb* dcb;
    a2b_Byte xferStatus;
    int bytesRead;
    int timeOutFactor = 1;

    if(nRead > 16)
    {
        timeOutFactor = 4;
    }

    if ( A2B_NULL != hnd )
    {
        dcb = (pal_I2cDcb*)hnd;

        if ( (nRead == 0) || (rBuf != A2B_NULL) )
        {
            bytesRead = libusb_control_transfer(dcb->hnd,
                            A2B_USBI_BM_REQUEST_TYPE_XFER_TO_HOST,
                            A2B_USBI_CMD_READ,
                            addr,
                            A2B_USBI_PROTOCOL_I2C,
                            rBuf,
                            nRead,
                            dcb->xferTimeout * timeOutFactor
                            );

            /* If the expected number of bytes was not transfered over USB */
            if ( bytesRead != nRead )
            {
                status = A2B_MAKE_HRESULT(A2B_SEV_FAILURE, A2B_FAC_I2C,
                                            A2B_EC_INTERNAL);
            }
            else
            {
                /* Wait for the transfer to be complete */
                xferStatus = pal_waitForRead(dcb, addr);
                status = pal_convertXferStatusToHResult(xferStatus);
            }
            pal_checkFirstXfer(dcb, addr, status);
        }
    }

    return status;
} /* pal_i2cRead */


/*!****************************************************************************
*
*  \b              pal_i2cWrite
*
*  <b> API Details: </b><br>
*  This routine writes bytes to an I2C device.
*                                                                       <br><br>
*  <b> Linux Implementation Details: </b><br>
*  The ADI USBi device libusb API is called to write to the I2C device.
*                                                                           <br>
*  \param          [in]    hnd          The handle returned from pal_i2cOpen
* 
*  \param          [in]    addr         The I2C address.
* 
*  \param          [in]    nWrite       The number of bytes to write to
*                                       the device.
* 
*  \param          [in]    wBuf         A buffer containing the data to write.
*                                       The buffer is of size 'nWrite' bytes.
*
*  \pre            None
*
*  \post           None
*
*  \return         A status code that can be checked with the A2B_SUCCEEDED()
*                  or A2B_FAILED() for success or failure.
*
******************************************************************************/
a2b_HResult
pal_i2cWrite
    (
    a2b_Handle      hnd,
    a2b_UInt16      addr,
    a2b_UInt16      nWrite,
    const a2b_Byte* wBuf
    )
{
    a2b_HResult status = A2B_MAKE_HRESULT(A2B_SEV_FAILURE, A2B_FAC_I2C,
                                        A2B_EC_INVALID_PARAMETER);
    pal_I2cDcb* dcb;
    int bytesWritten;
    a2b_Byte xferStatus;
    int timeOutFactor = 1;

    if(nWrite > 16)
    {
        timeOutFactor =  4;
    }
    

    if ( A2B_NULL != hnd )
    {
        dcb = (pal_I2cDcb*)hnd;

        if ( (nWrite == 0) || (wBuf != A2B_NULL) )
        {
            bytesWritten = libusb_control_transfer(dcb->hnd,
                                A2B_USBI_BM_REQUEST_TYPE_XFER_TO_DEVICE,
                                A2B_USBI_CMD_WRITE,
                                addr,
                                A2B_USBI_PROTOCOL_I2C,
                                (a2b_Byte*)wBuf,
                                nWrite,
                                dcb->xferTimeout * timeOutFactor
                                );

            /* If the USB layer didn't transfer the number of bytes that
             * was expected then ...
             */
            if ( bytesWritten != nWrite )
            {
                status = A2B_MAKE_HRESULT(A2B_SEV_FAILURE, A2B_FAC_I2C,
                                                           A2B_EC_INTERNAL);
            }
            else
            {
                /* Wait for the transfer to complete */
                xferStatus = pal_waitForWrite(dcb, addr);
                status = pal_convertXferStatusToHResult(xferStatus);
            }
            if ( (bi2cLog) && (status == 0))
            {
                if(nWrite == 1)
                {
                    fprintf(file, "\n\r Op: Write-Success, Addr:0x%02x, Val:0x%02x", addr, wBuf[0]);
                }
                else if(nWrite == 2)
                {
                    fprintf(file, "\n\r Op: Write-Success, Addr:0x%02x, Val:0x%02x,0x%02x", addr, wBuf[0], wBuf[1]);
                }
                else if(nWrite == 3)
                {
                     fprintf(file, "\n\r Op: Write-Success, Addr:0x%02x, Val:0x%02x,0x%02x,0x%02x", addr, wBuf[0], wBuf[1], wBuf[2]);
                }
                else if(nWrite > 3)
                {
                    fprintf(file, "\n\r Op: Write-Success, Addr:0x%02x, Val:0x%02x,0x%02x,0x%02x,0x%02x ..", addr, wBuf[0], wBuf[1], wBuf[2], wBuf[3]);
                }
            }
            else if((bi2cLog) && (status != 0))
            {
                fprintf(file, "\n\r Op: Write-Fail, Addr:0x%02x, Val:0x%02x ..", addr, wBuf[0]);
            }
            pal_checkFirstXfer(dcb, addr, status);
        }
    }

    return status;
} /* pal_i2cWrite */


/*!****************************************************************************
*
*  \b              pal_i2cWriteRead
*
*  <b> API Details: </b><br>
*  This routine writes and then reads bytes from the I2C device *without* an
*  I2C stop sequence separating the two operations. Instead a repeated I2C start
*  sequence is used as the operation separator.
*                                                                       <br><br>
*  <b> Linux Implementation Details: </b><br>
*  The ADI USBi device libusb API is called to write/read the I2C device.
*                                                                           <br>
*  \param          [in]    hnd          The handle returned from pal_i2cOpen
* 
*  \param          [in]    addr         The I2C address.
*                                                                                          
*  \param          [in]    nWrite       The number of bytes to write.
*                                                                                          
*  \param          [in]    wBuf         A buffer containing the data to write.
*                                       The buffer is of size 'nWrite' bytes.
*                                                                                          
*  \param          [in]    nRead        The number of bytes to read from
*                                       the device.
*                                                                                          
*  \param          [in]    rBuf         A buffer in which to write the results
*                                       of the read.
*
*  \pre            None
*
*  \post           The read buffer holds the contents of the read on success.
*
*  \return         A status code that can be checked with the A2B_SUCCEEDED()
*                  or A2B_FAILED() for success or failure.
*
******************************************************************************/
a2b_HResult
pal_i2cWriteRead
    (
    a2b_Handle      hnd,
    a2b_UInt16      addr,
    a2b_UInt16      nWrite,
    const a2b_Byte* wBuf,
    a2b_UInt16      nRead,
    a2b_Byte*       rBuf
    )
{
    a2b_HResult status = A2B_MAKE_HRESULT(A2B_SEV_FAILURE, A2B_FAC_I2C,
                                        A2B_EC_INVALID_PARAMETER);
    pal_I2cDcb* dcb;
    int bytesWritten = 0;
    a2b_Byte xferStatus;

    if ( A2B_NULL != hnd )
    {
        dcb = (pal_I2cDcb*)hnd;

        if ( (nWrite == 0) || (wBuf != A2B_NULL) )
        {
            bytesWritten = libusb_control_transfer(dcb->hnd,
                    A2B_USBI_BM_REQUEST_TYPE_XFER_TO_DEVICE,
                    A2B_USBI_CMD_WRITE_READ,
                    addr,
                    A2B_USBI_PROTOCOL_I2C,
                    (a2b_Byte*)wBuf,
                    nWrite,
                    dcb->xferTimeout
                    );

            /* If the USB layer didn't transfer the number of bytes that
             * was expected then ...
             */
            if ( bytesWritten != nWrite )
            {
                status = A2B_MAKE_HRESULT(A2B_SEV_FAILURE, A2B_FAC_I2C,
                                          A2B_EC_INTERNAL);
            }
            else
            {
                /* Wait for the transfer to complete */
                xferStatus = pal_waitForWrite(dcb, addr);
                status = pal_convertXferStatusToHResult(xferStatus);
            }
            if ( (bi2cLog) && (status == 0))
            {
                if(nWrite == 1)
                {
                    fprintf(file, "\n\r Op: WRforRd-Success, Addr:0x%02x, Val:0x%02x", addr, wBuf[0]);
                }
                if(nWrite == 2)
                {
                    fprintf(file, "\n\r Op: WRforRd-Success, Addr:0x%02x, Val:0x%02x,0x%02x", addr, wBuf[0], wBuf[1]);
                }
                else if(nWrite == 3)
                {
                     fprintf(file, "\n\r Op: WRforRd-Success, Addr:0x%02x, Val:0x%02x,0x%02x,0x%02x", addr, wBuf[0], wBuf[1], wBuf[2]);
                }
                else if(nWrite > 3)
                {
                    fprintf(file, "\n\r Op: WRforRd-Success, Addr:0x%02x, Val:0x%02x,0x%02x,0x%02x,0x%02x ..", addr, wBuf[0], wBuf[1], wBuf[2], wBuf[3]);
                }
            }
            else if((bi2cLog) && (status != 0))
            {
                fprintf(file, "\n\r Op: WRforRd-Fail, Addr:0x%02x, Val:0x%02x..", addr, wBuf[0]);
            }
        
        }

        if ( A2B_SUCCEEDED(status) )
        {
            usleep(A2B_USBI_CHK_COMPLETION_DELAY/2);
            status = pal_i2cRead(hnd, addr, nRead, rBuf);
            
            if ( (bi2cLog) && (status == 0))
            {
                 if(nRead == 1)
                {
                    fprintf(file, "\n\r Op: Rd-Success, Addr:0x%02x, Val:0x%02x", addr, rBuf[0]);
                }
                else if(nRead == 2)
                {
                    fprintf(file, "\n\r Op: Rd-Success, Addr:0x%02x, Val:0x%02x,0x%02x", addr, rBuf[0], rBuf[1]);
                }
                else if(nRead == 3)
                {
                     fprintf(file, "\n\r Op: Rd-Success, Addr:0x%02x, Val:0x%02x,0x%02x,0x%02x", addr, rBuf[0], rBuf[1], rBuf[2]);
                }
                else if(nRead > 3)
                {
                    fprintf(file, "\n\r Op: Rd-Success, Addr:0x%02x, Val:0x%02x,0x%02x,0x%02x,0x%02x ..", addr, rBuf[0], rBuf[1], rBuf[2], rBuf[3]);
                }
            }
            else if((bi2cLog) && (status != 0))
            {
                fprintf(file, "\n\r Op: Rd-Fail, Addr:0x%02x, Val:0x%02x ..", addr, rBuf[0]);
            }
            pal_checkFirstXfer(dcb, addr, status);
        }
    }

    if(status !=0 )
    {
        status = A2B_MAKE_HRESULT(A2B_SEV_FAILURE, A2B_FAC_I2C,
                                          A2B_EC_INTERNAL);
    }

    return status;
} /* pal_i2cWriteRead */


/*!****************************************************************************
*
*  \b              pal_i2cShutdown
*
*  <b> API Details: </b><br>
*  This routine is called to shutdown the I2C subsystem
*  during the stack destroy process.  This routine is called immediately
*  after the pal_i2cClose (assuming the close was successful).
*                                                                       <br><br>
*  <b> Linux Implementation Details: </b><br>
*  This call will attempt to initialize libusb (libusb_exit).
*                                                                           <br>
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
a2b_HResult
pal_i2cShutdown
    (
    A2B_ECB*    ecb
    )
{
    a2b_HResult status = A2B_MAKE_HRESULT(A2B_SEV_FAILURE, A2B_FAC_I2C,
                                            A2B_EC_INVALID_PARAMETER);

    /* Close down libusb library. The default context is reference counted
     * and only when the last "exit" has been called for that context will
     * the library truly close down.
     */
    if ( A2B_NULL != ecb )
    {
        libusb_exit(ecb->palEcb.usbCtx);
        status = A2B_RESULT_SUCCESS;
    }

    return status;
} /* pal_i2cShutdown */

/** \} -- a2bstack_pal_i2c_usbi */
#endif
#endif