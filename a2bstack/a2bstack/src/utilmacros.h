/*=============================================================================
 *
 * Project: a2bstack
 *
 * Copyright (c) 2023 - Analog Devices Inc. All Rights Reserved.
 * See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
 *
 *=============================================================================
 *
 * \file:   utilmacros.h
 * \author: Mentor Graphics, Embedded Software Division
 * \brief:  Private utility macros used internally.
 *
 *=============================================================================
 */

#ifndef A2B_UTILMACROS_H_
#define A2B_UTILMACROS_H_

/*======================= I N C L U D E S =========================*/

/*======================= D E F I N E S ===========================*/

/* Memory allocation/deallocation helpers */
#define A2B_MALLOC(stk, size)     (stk)->pal.memMgrMalloc((stk)->heapHnd, (size))
#define A2B_FREE(stk, p)          (stk)->pal.memMgrFree((stk)->heapHnd, (p))

#define A2B_MIN(a,b)        (((a) < (b)) ? (a) : (b))
#define A2B_MAX(a,b)        (((a) > (b)) ? (a) : (b))

/* Convert a master node I2C address into the associated "bus" address */
#define A2B_MAKE_I2C_BUS_ADDR(a)    ((a2b_UInt16)(a) | (a2b_UInt16)0x01)

/*======================= D A T A T Y P E S =======================*/

A2B_BEGIN_DECLS

/*======================= P U B L I C  P R O T O T Y P E S ========*/


A2B_END_DECLS

/*======================= D A T A =================================*/


#endif /* A2B_UTILMACROS_H_ */
