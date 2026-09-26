/*=============================================================================
 *
 * Project: a2bstack
 *
 * Copyright (c) 2015 - Analog Devices Inc. All Rights Reserved.
 * See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
 *
 *=============================================================================
 *
 * \file:   platform.h
 * \author: Mentor Graphics, Embedded Software Division
 * \brief:  This file contains platform specific definitions.
 *
 *=============================================================================
 */

/*============================================================================*/
/** 
 * \defgroup a2bstack_linux_platform        Platform Definitions
 *  
 * This file contains platform specific definitions.
 *
 * \{ */
/*============================================================================*/

#ifndef A2B_PLATFORM_H_
#define A2B_PLATFORM_H_

/*======================= I N C L U D E S =========================*/

#include "a2b/macros.h"
#include "a2b/ctypes.h"

/*======================= D E F I N E S ===========================*/
#define A2B_I2C_ADAPTER_USBI

/*===========================================================================
 *
 * Platform specific configuration options
 *
 =============================================================================
 */

/** The length of the buffer to hold the path to the I2C device. This
 * is typically something like /dev/i2c-N where 'N' is the device number.
 */
#define A2B_PAL_I2C_DEVICE_PATH_LEN (32)

/** Define the default (7-bit) A2B master node I2C address */
#define A2B_CONF_DEFAULT_MASTER_NODE_I2C_ADDR   (0x68)

/** Define the name of the TRACE log channel in terms of a URL.
 * The URL is encoded as follows:
 *      \verbatim      <protocol>//:<resource>:<port>      \endverbatim
 *
 * With the following supported protocol options: 
 *      \verbatim
        file://<filename>
        stdio://stderr
        stdio://stdout
        tcp://<host>:<port>
        udp://<host>:<port>
        syslog://<level>
            where level = emerg, alert, crit, err, warning, notice, info, debug
        \endverbatim
 *
 * Example: 
 *      \verbatim       udp://localhost:18001       \endverbatim
 */
#define A2B_CONF_DEFAULT_TRACE_CHAN_URL     "stdio://stderr"

#ifdef A2B_I2C_ADAPTER_IOCTL
/**
 * Define the *default* path to the I2C/I2C device connected to the A2B
 * chip.
 */
#define A2B_CONF_DEFAULT_I2C_DEVICE_PATH    "/dev/i2c-1"

#elif defined(A2B_I2C_ADAPTER_AARDVARK)

/** The default lock timeout (in msec) for the Aardvark I2C adapater */
#define A2B_CONF_DEFAULT_AARDVARK_LOCK_TIMEOUT  (200)

#elif defined(A2B_I2C_ADAPTER_USBI)

/**
 * The default control transfer timeout (in msec) to wait for a response.
 * Zero (0) equates to an unlimited timeout.
 */
#define A2B_CONF_DEFAULT_USBI_XFER_TIMEOUT  (200)

#endif


/**
 * The default path to the master/slave dynamically loadable plugin directory.
 */
#define A2B_CONF_DEFAULT_PLUGIN_SEARCH_PATTERN  "/usr/lib/a2bplugins/*.so"


/*===========================================================================
 *
 * End of platform specific configuration options
 *
 =============================================================================
 */

/** One possible invalid file descriptor value. Generally, any negative
 * value could be an invalid file descriptor since open() only
 * indicates it returns file descriptor is a small, positive number on
 * success.
 */
#define A2B_INVALID_FD          (-1)

/** Macro to determine whether a file descriptor is in the valid range */
#define A2B_IS_VALID_FD(x)      ((x) >= 0)

/*======================= D A T A T Y P E S =======================*/

A2B_BEGIN_DECLS

/**
 * Prototype of the function signature required to be exported
 * by a dynamically loaded A2B plugin. The name of the exported
 * function should be: "a2b_pluginInit". The dlopen()/dlsym()
 * functions are used to load the plugin(s) and then resolve the
 * above function before calling it.
 */

/* Forward declaration */
struct a2b_PluginApi;

typedef a2b_Bool (* a2b_PluginInitFunc)(struct a2b_PluginApi* api);


/*======================= P U B L I C  P R O T O T Y P E S ========*/


A2B_END_DECLS

/*======================= D A T A =================================*/

/** \} -- a2bstack_linux_platform */

#endif /* A2B_PLATFORM_H_ */
