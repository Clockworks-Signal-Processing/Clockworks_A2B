/*******************************************************************************
Copyright (c) 2014 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
******************************************************************************

   Name       : adi_a2b_init.c
   
   Description: This file contains functions which initializes host/target processor  
                       
                 
   Functions  : adi_a2b_SystemInit()
                 

   Prepared &
   Reviewed by: Automotive Software and Systems team, 
                IPDC, Analog Devices,  Bangalore, India
                
   @version: $Revision: 3626 $
   @date: $Date: 2015-10-05 14:04:13 +0530 (Mon, 05 Oct 2015) $
               
******************************************************************************/
/*! \addtogroup Target_Dependent Target Dependent
 *  @{
 */

/*! \addtogroup System_Init System Init
 *  @{
 */

/*============= I N C L U D E S =============*/

#include "adi_a2b_datatypes.h"
#include "a2bapp_defs.h"

#include <stdio.h>

#ifdef A2B_PRINT_CONSOLE
#include <stdio.h>
#endif
#include "pico/stdlib.h"
/*============= D E F I N E S =============*/

#define ALIVE_LED       25  // On board PICO LED
#define PUSH_BUTTON     20

/*============= D A T A =============*/

/*============= C O D E =============*/
/*
** Function Prototype section
*/
/*
** Function Definition section
*/


 
/********************************************************************************/
/*!
@brief This function does the system initialization. Sets Core and system clock

@param [in] none
   
    
@return     ADI_A2B_RESULT
            - ADI_A2B_FAILURE : Failure(If any of the peripheral initialization fails)
            - ADI_A2B_SUCEESS : Success    
    
*/
/***********************************************************************************/
ADI_MEM_A2B_CODE_CRIT
a2b_HResult adi_a2b_SystemInit(void)
{
    printf("Alive 1 \n");

    return 0;        
}



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

