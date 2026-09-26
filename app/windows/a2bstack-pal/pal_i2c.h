/*=============================================================================
 *
 * Project: a2bstack
 *
 * Copyright (c) 2015 - Analog Devices Inc. All Rights Reserved.
 * See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
 *
 *=============================================================================
 *
 * \file:   pal_i2c.h
 * \author: Mentor Graphics, Embedded Software Division
 * \brief:  This is the abstract definition of the platform I2C API.
 *
 *=============================================================================
 */

/*============================================================================*/
/** 
 * \defgroup a2bstack_pal_i2c         PAL I2C Module 
 *  
 * This is the abstract definition of the platform I2C API.
 *  
 */
/*============================================================================*/

#ifndef A2B_PAL_I2C_H_
#define A2B_PAL_I2C_H_

/*======================= I N C L U D E S =========================*/

#include "a2b/macros.h"
#include "a2b/ctypes.h"
#include "a2b/ecb.h"
#include "a2b/pal.h"

/*======================= D E F I N E S ===========================*/

/*======================= D A T A T Y P E S =======================*/

A2B_BEGIN_DECLS


/*======================= P U B L I C  P R O T O T Y P E S ========*/


__declspec(dllexport) a2b_HResult pal_i2cInit(A2B_ECB* ecb);

A2B_EXPORT A2B_DSO_LOCAL a2b_Handle pal_i2cOpen(a2b_I2cAddrFmt  fmt,
                                                a2b_I2cBusSpeed speed, 
                                                A2B_ECB*        ecb);

A2B_EXPORT A2B_DSO_LOCAL a2b_HResult pal_i2cClose(a2b_Handle hnd);

A2B_EXPORT A2B_DSO_LOCAL a2b_HResult pal_i2cRead(a2b_Handle hnd, 
                                                 a2b_UInt16 addr,
                                                 a2b_UInt16 nRead, 
                                                 a2b_Byte*  rBuf);

A2B_EXPORT A2B_DSO_LOCAL a2b_HResult pal_i2cWrite(a2b_Handle        hnd, 
                                                  a2b_UInt16        addr,
                                                  a2b_UInt16        nWrite, 
                                                  const a2b_Byte*   wBuf);

A2B_EXPORT A2B_DSO_LOCAL a2b_HResult pal_i2cWriteRead(a2b_Handle        hnd,
                                                     a2b_UInt16         addr,
                                                     a2b_UInt16         nWrite,
                                                     const a2b_Byte*    wBuf,
                                                     a2b_UInt16         nRead,
                                                     a2b_Byte*          rBuf);

A2B_EXPORT A2B_DSO_LOCAL a2b_HResult pal_i2cShutdown(A2B_ECB* ecb);

A2B_END_DECLS

/*======================= D A T A =================================*/


#endif /* A2B_PAL_I2C_H_ */
