/*******************************************************************************
Copyright (c) 2014 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
******************************************************************************
* @file: adi_a2b_externs.h
* @brief: This file contains all extern declarations required by A2B Framework.
* @version: $Revision: 3378 $
* @date: $Date: 2015-08-04 18:44:46 +0530 (Tue, 04 Aug 2015) $
* Developed by: Automotive Software and Systems team, Bangalore, India
*****************************************************************************/

/*! \addtogroup Global_Variables Global Variables
* @{
*/

#ifndef _ADI_A2B_EXTERNS_H_
#define _ADI_A2B_EXTERNS_H_

/*============= I N C L U D E S =============*/
#include "a2b/pal.h"
/*============= D E F I N E S =============*/

/*============= E X T E R N A L S ============*/

/** 
 @}
*/

/*============= D A T A T Y P E S=============*/

// had to find a widleyt shared .h file
// stuff to help debug code, probably conflicts with whatever ADI might have planned
#define DEBUG_IO_1 21 // GPIO 21 is on pin 27
#define AD2437_RESET_n 2 // low keeps AD2437 in reset, had external pullup so OK to let float


/*======= P U B L I C P R O T O T Y P E S ========*/
a2b_HResult adi_a2b_SystemInit(void);
void adi_a2b_palInit(struct a2b_StackPal*  pal,
                     A2B_ECB*         ecb);

// IIC Interfaces
a2b_UInt32 a2b_pal_I2cInit(A2B_ECB* ecb);
a2b_Handle a2b_pal_I2cOpenFunc(a2b_I2cAddrFmt fmt,
        a2b_I2cBusSpeed speed, A2B_ECB* ecb);
a2b_HResult a2b_pal_I2cReadFunc(a2b_Handle hnd,
        a2b_UInt16 addr, a2b_UInt16 nRead, a2b_Byte* rBuf);
a2b_HResult a2b_pal_I2cWriteFunc(a2b_Handle hnd,
        a2b_UInt16 addr, a2b_UInt16 nWrite,
        const a2b_Byte* wBuf);
a2b_HResult a2b_pal_I2cWriteReadFunc(a2b_Handle hnd,
        a2b_UInt16 addr, a2b_UInt16 nWrite,
        const a2b_Byte* wBuf, a2b_UInt16 nRead,
        a2b_Byte* rBuf);
a2b_UInt32 a2b_pal_I2cShutdownFunc(A2B_ECB* ecb);
a2b_HResult a2b_pal_I2cCloseFunc(a2b_Handle hnd);

// SPI Interfaces
a2b_UInt32 a2b_pal_spiInit(A2B_ECB* ecb);
a2b_Handle a2b_pal_spiOpen(A2B_ECB* ecb);
a2b_HResult a2b_pal_spiRead(a2b_Handle hnd, a2b_UInt16 addr,
        a2b_UInt16 nRead, a2b_Byte* rBuf);
a2b_HResult a2b_pal_spiWrite(a2b_Handle hnd, a2b_UInt16 addr,
        a2b_UInt16 nWrite, const a2b_Byte* wBuf);
a2b_HResult a2b_pal_spiWriteRead(a2b_Handle hnd, a2b_UInt16 addr,
        a2b_UInt16 nWrite, const a2b_Byte* wBuf, a2b_UInt16 nRead, a2b_Byte* rBuf);
a2b_HResult a2b_pal_spiFd(a2b_Handle hnd, a2b_UInt16 addr, a2b_UInt16 nWrite,
        const a2b_Byte* wBuf, a2b_UInt16 nRead, a2b_Byte* rBuf);
a2b_HResult a2b_pal_spiClose(a2b_Handle hnd);

// Processor General Interfaces
a2b_HResult a2b_pal_TimerInitFunc(A2B_ECB* ecb);
a2b_UInt32 a2b_pal_TimerGetSysTimeFunc();
a2b_HResult a2b_pal_TimerShutdownFunc(A2B_ECB* ecb);
a2b_HResult a2b_pal_AudioInitFunc(A2B_ECB* ecb);
a2b_Handle a2b_pal_AudioOpenFunc(void);
a2b_HResult a2b_pal_AudioConfigFunc(a2b_Handle hnd,
                                a2b_TdmSettings* tdmSettings);
a2b_HResult a2b_pal_AudioCloseFunc(a2b_Handle hnd);
a2b_HResult a2b_pal_AudioShutdownFunc(A2B_ECB* ecb);
a2b_HResult a2bapp_pluginsLoad(struct a2b_PluginApi**  plugins,
    a2b_UInt16*  numPlugins, A2B_ECB*     ecb);
a2b_HResult a2bapp_pluginsUnload(struct a2b_PluginApi*   plugins,
    a2b_UInt16   numPlugins,  A2B_ECB*  ecb);
void a2b_pal_infoGetVersion(a2b_UInt32* major,
        a2b_UInt32* minor, a2b_UInt32* release);
void a2b_pal_infoGetBuild(a2b_UInt32* buildNum,
        const a2b_Char** const buildDate,
        const a2b_Char** const buildOwner,
        const a2b_Char** const buildSrcRev,
        const a2b_Char** const buildHost);
#if defined(A2B_BCF_FROM_FILE_IO)
a2b_HResult a2b_pal_FileOpen(A2B_ECB* ecb, char* url);
a2b_HResult a2b_pal_FileRead(a2b_Handle hnd, a2b_UInt16 offset, a2b_UInt16 nRead,
        a2b_Byte* rBuf);
a2b_HResult a2b_pal_FileClose(A2B_ECB* ecb);
#endif
#if defined(A2B_FEATURE_SEQ_CHART) || defined(A2B_FEATURE_TRACE)
a2b_HResult a2b_pal_logInit(A2B_ECB*   ecb);
a2b_Handle a2b_pal_logOpen(const a2b_Char* url);
a2b_HResult a2b_pal_logClose(a2b_Handle  hnd);
a2b_HResult a2b_pal_logShutdown(A2B_ECB* ecb);
a2b_HResult a2b_pal_logWrite(a2b_Handle  hnd, const a2b_Char* msg);
#endif
a2b_HResult adi_a2b_SystemInit(void);
a2b_UInt32 adi_a2b_EnablePinInterrupt(a2b_UInt8 nGPIONum, void* pUserCallBack, a2b_UIntPtr CallBackParam, a2b_UInt8 bFallingEdgeTrig);
/*============= D A T A =============*/


#endif /* _ADI_A2B_EXTERNS_H_ */
/*
*
* EOF: $URL$
*
*/


