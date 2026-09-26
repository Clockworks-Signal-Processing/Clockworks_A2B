/*******************************************************************************
Copyright (c) 2021 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
*******************************************************************************

   Name       : adi_a2b_irq.c
   
   Description: This file is responsible for handling A2B Pin/GPIO interrupt related functions.        
                 
   Functions  :  adi_a2b_EnableInterrupt()
                 

   Prepared &
   Reviewed by: Automotive Software and Systems team, 
                IPDC, Analog Devices,  Bangalore, India
                
******************************************************************************/
/*! \addtogroup Target_Dependent Target Dependent
 *  @{
 */

/** @defgroup GPIO
 *
 * This driver handles GPIO interrupts from AD24XX. All the interface functions needs to be re-implemented for different processor.
 *
 */


/*! \addtogroup GPIO
 *  @{
 */

/*============= I N C L U D E S =============*/

#include "adi_a2b_datatypes.h"


/*============= D E F I N E S =============*/

/*============= D A T A =============*/

/*============= C O D E =============*/
/*
** Function Prototype section
*/


/*
** Function Definition section
*/
/*****************************************************************************/

/*!
@brief              This function enables GPIO pin as input pin and configures for  
                    edge triggered interrupt

@param [in]         nGPIONum        Abstracted GPIO number  
@param [in]         pUserCallBack   User parameter
@param [in]         CallBackParam   a2b_App_t instance called on interrupt
@param [in]         bFallingEdgeTrig Indicates the either Falling edge or Rising Edge trigger
    
@return             Return code
                    - 0: Success
                    - 1: Failure  
*/    
/*****************************************************************************/
ADI_MEM_A2B_CODE_CRIT
a2b_UInt32 adi_a2b_EnablePinInterrupt(a2b_UInt8 nGPIONum , void* pUserCallBack, a2b_UIntPtr CallBackParam, a2b_UInt8 bFallingEdgeTrig)
{
	a2b_UInt32 gpioMaxCallbacks;
	A2B_UNUSED(nGPIONum);

    return((a2b_UInt32)0);
}


/*! \addtogroup GPIO_Internal_Functions GPIO Internal Functions
 *  @{
 */
/******************************************************************************/
/*!
@brief      Interrupt service routine(ISR), calls user call back function if enabled
            This function shall be invoked upon GPIO event 

@param [in] ePinInt        GPIO port enum
@param [in] nPins          Pin Number within the port
@param [in] pCBParam       Call back parameter

@return     none
    
*/
/*****************************************************************************/
ADI_MEM_A2B_CODE_CRIT
static void adi_a2b_PinInterruptHandler(a2b_UInt32 ePinInt, a2b_UInt32 nPins,  void *pCBParam)
{
	A2B_UNUSED(ePinInt);
	A2B_UNUSED(nPins);

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


