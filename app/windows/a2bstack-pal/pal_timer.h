/*=============================================================================
 *
 * Project: a2bstack
 *
 * Copyright (c) 2015 - Analog Devices Inc. All Rights Reserved.
 * See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
 *
 *=============================================================================
 *
 * \file:   pal_timer.h
 * \author: Mentor Graphics, Embedded Software Division
 * \brief:  This is the abstract definition of the platform Timer API.
 *
 *=============================================================================
 */

/*============================================================================*/
/** 
 * \defgroup a2bstack_pal_timer         PAL Timer Module 
 *  
 * This is the abstract definition of the platform Timer API.
 *  
 * \{ */
/*============================================================================*/

#ifndef A2B_PAL_TIMER_H_
#define A2B_PAL_TIMER_H_

/*======================= I N C L U D E S =========================*/

#include "a2b/macros.h"
#include "a2b/ctypes.h"
#include "a2b/ecb.h"
#include "a2b/pal.h"

/*======================= D E F I N E S ===========================*/

/*======================= D A T A T Y P E S =======================*/

A2B_BEGIN_DECLS

/*======================= P U B L I C  P R O T O T Y P E S ========*/


A2B_EXPORT A2B_DSO_LOCAL a2b_HResult pal_timerInit(A2B_ECB* ecb);

A2B_EXPORT A2B_DSO_LOCAL a2b_HResult pal_timerShutdown(A2B_ECB* ecb);

A2B_EXPORT A2B_DSO_LOCAL a2b_UInt32 pal_timerGetSysTime(void);


A2B_END_DECLS

/*======================= D A T A =================================*/

/** \} -- a2bstack_pal_timer */

#endif /* A2B_PAL_TIMER_H_ */
