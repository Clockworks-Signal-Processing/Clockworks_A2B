
/*=============================================================================
 *
 * Project: a2bstack
 *
 * Copyright (c) 2015 - Analog Devices Inc. All Rights Reserved.
 * See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
 *
 *=============================================================================
 *
 * \file:   pal_timer.c
 * \author: Mentor Graphics, Embedded Software Division
 * \brief:  Implementation of the platform Timer services.
 *
 *=============================================================================
 */

/*======================= I N C L U D E S =========================*/

#include <time.h>
#include <windows.h>
#include "pal_timer.h"
#include "a2b/conf.h"
#include "a2b/error.h"

/*======================= D E F I N E S ===========================*/

/*======================= D A T A T Y P E S =======================*/

/*======================= L O C A L  P R O T O T Y P E S  =========*/

/*======================= D A T A  ================================*/

/*======================= C O D E =================================*/

/*!****************************************************************************
*
*  \b              pal_timerInit
* 
*  <b> API Details: </b><br>
*  This routine is called to do initialization the timer subsystem
*  during the stack allocation process.
*                                                                       <br><br>
*  <b> QNX Implementation Details: </b><br>
*  Nothing to do, always A2B_RESULT_SUCCESS
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
pal_timerInit
    (
    A2B_ECB*    ecb
    )
{
    A2B_UNUSED(ecb);
    return A2B_RESULT_SUCCESS;

} /* pal_timerInit */


/*!****************************************************************************
*
*  \b              pal_timerShutdown
* 
*  <b> API Details: </b><br>
*  This routine is called to shutdown the timer subsystem
*  during the stack destroy process.
*                                                                       <br><br>
*  <b> QNX Implementation Details: </b><br>
*  Always returns A2B_RESULT_SUCCESS.
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
pal_timerShutdown
    (
    A2B_ECB*    ecb
    )
{
    A2B_UNUSED(ecb);
    return A2B_RESULT_SUCCESS;

} /* pal_timerShutdown */


/*!****************************************************************************
*
*  \b              pal_timerGetSysTime
* 
*  <b> API Details: </b><br>
*  This routine returns the current "system" time in milliseconds. The
*  underlying system time is platform specific.
*                                                                       <br><br>
*  <b> QNX Implementation Details: </b><br>
*  The time is returned using clock_gettime().
*                                                                           <br>
*  \pre            None
*
*  \post           None
*
*  \return         The current system time referenced from some epoch.  For
*                  QNX, time is referenced from the first time this function
*                  is called.  The units are milliseconds.
*
******************************************************************************/
a2b_UInt32
pal_timerGetSysTime(void)
{

    a2b_UInt msec= GetTickCount();
	return msec;

} /* pal_timerGetSysTime */

