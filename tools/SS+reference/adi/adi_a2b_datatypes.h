/*******************************************************************************
Copyright (c) 2014 - Analog Devices Inc. All Rights Reserved.
This software is proprietary & confidential to Analog Devices, Inc.
and its licensors.
******************************************************************************
* @file: adi_a2b_datatypes.h
* @brief: This file contains typoedefs for basic data types used in A2B framework
* @date: $Date: 2015-07-21 09:58:13 +0530 (Tue, 21 Jul 2015) $
* Developed by: Automotive Software and Systems team, Bangalore, India
*****************************************************************************/

/*! \addtogroup Basic_Types Basic Types
* @{
*/

#ifndef _ADI_A2B_DATA_TYPES_H_
#define _ADI_A2B_DATA_TYPES_H_

/*============= I N C L U D E S =============*/
#include "a2b/ctypes.h"
/*============= D E F I N E S =============*/

/*
 * ClockWorks Signal Processing LLC: for single-core targets (Windows, Linux, RP2040), as
 * the app ports of this file in app/windows, app/linux_rpi and app/rp2040 are. ADI's file
 * chose between two sets of memory sections with CORE0, both for its multi-core DSPs'
 * linker sections; here the section macros are empty, so CORE0 doesn't matter. The two
 * CONST_DATA macros are const, as ADI's structures point to those tables (codec programs,
 * peripheral devices, streams) through const pointers, so a microcontroller keeps them in
 * flash. An export puts them before "static", and GCC warns "'static' is not at
 * beginning of declaration" for each; that warning is off for the files that include this
 * one, as the export is generated.
 */

/*! Internal memory code sections */
#define A2B_L1_CODE
/*! External memory code sections */
#define A2B_L3_CODE
/*! Internal memory data sections */
#define A2B_L1_DATA
/*! External memory data sections */
#define A2B_L3_DATA
/*! Internal memory code sections */
#define ADI_MEM_A2B_CODE_CRIT
/*! External memory code sections */
#define ADI_MEM_A2B_CODE_NO_CRIT
/*! Internal memory data sections */
#define ADI_MEM_A2B_DATA_CRIT_CAHE
/*! Internal memory data sections */
#define ADI_MEM_A2B_DATA_CRIT_NO_CACHE
/*! External memory data sections */
#define ADI_MEM_A2B_DATA_NO_CRIT
/*! Graph data section  */
#define ADI_A2B_MEM_BCF_DATA
/*! Graph code section  */
#define ADI_A2B_MEM_BCF_CONST_DATA const
/*! Graph data section  */
#define ADI_A2B_MEM_GRAPH_DATA

#define ADI_A2B_DATA_MEM_ALIGN4

/*! Peripheral data  section  */
#define ADI_A2B_MEM_PERI_CONFIG_DATA
/*! Graph code section  */
#define ADI_A2B_MEM_PERI_CONFIG_CONST_DATA const
/*! Peripheral configuration section  */
#define ADI_A2B_MEM_PERI_CONFIG_UNIT

/* SS+ exports write "const static" (see above); a C-only warning. */
#if defined(__GNUC__) && !defined(__clang__) && !defined(__cplusplus)
#pragma GCC diagnostic ignored "-Wold-style-declaration"
#endif

/* Storage classes */

#define STATIC          static

#define EXTERN          extern

#define VOLATILE        volatile

#define REGISTER        register

#define NULL_PTR        ((void *)0)


/*============= D A T A T Y P E S=============*/

typedef unsigned short uint16;

typedef unsigned int uint32;

typedef unsigned long long uint64;

typedef unsigned char uint8;

typedef short int16;

typedef long int32;

typedef long long int64;

typedef signed char int8;

/* Boolean */
#define TRUE  (1)

#define FALSE (0)

/*======= P U B L I C P R O T O T Y P E S ========*/



#endif /* _ADI_A2B_FRAMEWORK_H_ */

/** 
 @}
*/

/*
*
* EOF: $URL$
*
*/

