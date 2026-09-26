/*=============================================================================
 *
 * Project: a2bstack
 *
 * Copyright (c) 2015 - Analog Devices Inc. All Rights Reserved.
 * See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
 *
 *=============================================================================
 *
 * \file:   palecb.h
 * \author: Mentor Graphics, Embedded Software Division
 * \brief:  Platform specific extensions to the environment control block (ECB).
 *
 *=============================================================================
 */

/*============================================================================*/
/** 
 * \defgroup a2bstack_palecb        PAL ECB
 *  
 * Platform specific extensions to the environment control block (ECB).
 *
 * \{ */
/*============================================================================*/

#ifndef A2B_PALECB_H_
#define A2B_PALECB_H_

/*======================= I N C L U D E S =========================*/

#include "a2b/macros.h"
#include "a2b/ctypes.h"
#include "adi_a2b_busconfig.h"
#include "bdd_pb2.pb.h"
#include "a2b/msgtypes.h"


/*======================= D E F I N E S ===========================*/
#define A2B_I2C_ADAPTER_USBI

/*======================= D A T A T Y P E S =======================*/

A2B_BEGIN_DECLS

/*======================= P U B L I C  P R O T O T Y P E S ========*/

/* Forward definitions */
#if A2B_I2C_ADAPTER == usbi
struct libusb_context;
#endif


typedef struct a2b_PalEcb
{
#ifndef A2B_QAC

	/* Empty for the non-platform specific case. The contents of this type
     * definition should be replaced by a target/platform specific
     * implementation.
     */
	a2b_Handle                       i2chnd;
	a2b_Handle                       fp;
	uint64_t                         nCurrTime;
	a2b_Int32						 nChainIndex;
	a2b_UInt8						 cfgHostFromFile;
#if defined(A2B_FEATURE_SEQ_CHART) || defined(A2B_FEATURE_TRACE)
	A2B_LOG_INFO                   oLogConfig[A2B_TOTAL_LOG_CH];
#endif
	/*! Table to get peripheral configuration structure */
	ADI_A2B_NODE_PERICONFIG *pAudioHostDeviceConfig;
	a2b_UInt8*				 pEepromAudioHostConfig;
#else
	a2b_Handle                       i2chnd;
#endif /* A2B_QAC */

	a2b_UInt32 nDeviceInterface;
	
#if defined(A2B_I2C_ADAPTER_USBI)

	/** If true, the first available adapter with a matching vendor/product
	* identifier (VID/PID) is opened by default.
	*/
	a2b_Bool    useFirstAvailable;

	/** If not selecting the first available adapter, the available adapter with
	* a matching VID/PID plus bus number and device address is selected.
	*/
	a2b_UInt8  busNum;
	a2b_UInt8  devAddr;

	/** The libusb library context */
	struct libusb_context* usbCtx;

	/** The timeout (in milliseconds) control transfers should wait before
	* giving up due to no response being received. For an unlimited
	* timeout zero (0) should be used. The default is 200 msec.
	*/
	a2b_UInt32  xferTimeout;
#endif
	
} a2b_PalEcb;

A2B_END_DECLS

/*======================= D A T A =================================*/

/** \} -- a2bstack_palecb */

#endif /* A2B_PALECB_H_ */
