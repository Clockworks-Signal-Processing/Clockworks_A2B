/*******************************************************************************
Copyright (c) 2014 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
******************************************************************************
* @file: adi_a2b_driverprototypes.h
* @brief: This file contains all the abstracted driver prototypes 
* @version: $Revision: 3599 $
* @date: $Date: 2015-09-18 18:20:21 +0530 (Fri, 18 Sep 2015) $
* Developed by: Automotive Software and Systems team, Bangalore, India
*****************************************************************************/

/*! \addtogroup PAL_BF PAL_BF
* @{
*/

#ifndef _ADI_A2B_DRIVERPROTOPTYPES_H_
#define _ADI_A2B_DRIVERPROTOPTYPES_H_

/*============= I N C L U D E S =============*/
#include "a2b/pal.h"
/*============= D E F I N E S =============*/

/*============= E X T E R N A L S ============*/

/*============= D A T A T Y P E S=============*/

/** 
 @}
*/

/*======= P U B L I C P R O T O T Y P E S ========*/


/* Timer */
uint32_t adi_a2b_TimerOpen(uint32_t nTimerNo,void* pUserArgument);
uint32_t adi_a2b_TimerStop(uint32_t nTimerNo);
uint32_t adi_a2b_TimerClose(uint32_t nTimerNo);
uint32_t adi_a2b_TimerStart(uint32_t nTimerNo, uint32_t nTime);
void  adi_a2b_Delay(uint32_t nTime);



/*============= D A T A =============*/

#endif /* _ADI_A2B_EXTERNS_H_ */
/*
*
* EOF: $URL$
*
*/


