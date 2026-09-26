/*******************************************************************************
Copyright (c) 2023 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
******************************************************************************
* @file: a2bpnp_conf.h
* @brief: This file contains the declaration of memory management parameters for PnP Software.
*
* Developed by: Automotive Software and Systems team, Bangalore, India
*****************************************************************************/

#ifndef A2BPNP_CONF_H__
#define A2BPNP_CONF_H__

/* Maximum peripherals on the a sub node */
#define A2B_PNP_CFG_MAX_NUM_PERIPH_ON_NODE 4u
/* Maximum number of Audio stream for a given network */
#define A2B_PNP_CFG_MAX_NUM_NW_STREAMS     16u
/* Maximum number of Audio connections for a given network */
#define A2B_PNP_CFG_MAX_NUM_NW_CONNECTION   32u
/* Maximum number of SPI DT for a given network */
#define A2B_PNP_CFG_MAX_NUM_NW_DT_STREAMS  1u
/* Maximum number of A2B NW chains */
#define A2B_PNP_CFG_MAX_NUM_A2B_CHAIN      1u
/* Maximum number of Groups for a node */
#define A2B_PNP_CFG_MAX_NUM_GROUPS      	16u
/* Maximum number of Device Types for a node */
#define A2B_PNP_CFG_MAX_NUM_DTYPES      	4u
/* Maximum number of Device Functions on a node */
#define A2B_PNP_CFG_MAX_NUM_DEVICE_FUNC		8u
/* Maximum size of Group Name */
#define A2B_PNP_CFG_GROUP_NAME_SIZE		    32u
/* Maximum size of Device Name */
#define A2B_PNP_CFG_DEVICE_NAME_SIZE		64u
/* Maximum size of Peripheral Name */
#define A2B_PNP_CFG_PERIPHERAL_NAME_SIZE	64u
/* Maximum size of Stream Name */
#define A2B_PNP_CFG_STREAM_NAME_SIZE	    64u
/* Maximum number of Group Channels */
#define A2B_PNP_CFG_MAX_NUM_GROUP_CHANNELS  32u
/* Maximum size of Function Information */
#define A2B_PNP_CFG_FUNCTION_INFO_SIZE      64u

#endif /* A2BPNP_CONF_H__ */