/*******************************************************************************
Copyright (c) 2021 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
******************************************************************************
* @file: a2bapp_common.h
* @brief: This file contains Typedefs for various configuration parameters of stack
*         specific to this A2B application
* @version: $Revision: 3888 $
* @date: $Date: 2016-01-17 18:46:38 +0530 (Sun, 17 Jan 2016) $
* Developed by: Consumer Systems team, Bangalore, India
*****************************************************************************/

#ifndef __A2BAPP_COMMON_H__
#define __A2BAPP_COMMON_H__

/*============= I N C L U D E S =============*/
/*============= D E F I N E S =============*/
/* Add your custom header content here */

#define A2B_APP_NETWORK_DRIVEN 1
#define A2B_APP_USER_DRIVEN	 0
#define A2B_APP_ROUTING_MODE A2B_APP_USER_DRIVEN

/*!< Default blind discovery interval in ms */
#define A2B_APP_DEFAULT_BLIND_DISC_INTERVAL 500
/*!< Interrupt query interval in ms*/
#define A2B_APP_DEFAULT_INTERRUPT_QUERY_INTERVAL 20

/*!< MAX allowed power consumption */
#define A2B_APP_DEFAULT_ALLOWED_PWR_CONSUMPTION 35000

/*!< MAX allowed current */
#define A2B_APP_DEFAULT_ALLOWED_CURRENT_CONSUMPTION 2000

/*!< Default BW level warning */
#define A2B_APP_DEFAULT_BW_WARNING_LEVEL 85

/*!< Default BW level error */
#define A2B_APP_DEFAULT_BW_ERROR_LEVEL 95

/*!< CONTROL.XCVRBINV (B-side LVDS polarity) for the main node and for all sub-nodes:
     -1 = keep the bus configuration value (sub-nodes take the main node's), 0 = off, 1 = on.
     Override per build with -D, or at run time with the xcvrBInv command before start. */
#ifndef A2B_APP_DEFAULT_MAIN_XCVRBINV
#define A2B_APP_DEFAULT_MAIN_XCVRBINV (-1)
#endif
#ifndef A2B_APP_DEFAULT_SUB_XCVRBINV
#define A2B_APP_DEFAULT_SUB_XCVRBINV (-1)
#endif

/*!< Enable Test Mode */
#define A2B_APP_TEST_MODE 1


/** APIs Return Values */
typedef enum A2B_APP_PNP_STATE
{
	A2B_PNP_APP_WAIT_FOR_START,            	/*!< Init State */
	A2B_PNP_APP_NETWORK_MNG,      			/*!< Network manage state */
	A2B_PNP_APP_SYS_ERR      			    /*!< Set up Error, likely require app restart */

}A2B_APP_PNP_STATE;

typedef struct A2B_APP_NW_PARAM{
	a2bpnp_MainNodeProp g_MainNodeCfg;
	A2B_PNP_HANDLE hPnp;
	a2bpnp_StreamInfo* pNWStream;
	a2bpnp_DTStreamInfo DTStream;
	a2bpnp_NetworkInfo *A2BNetworkInfo;
	uint8_t StreamCount;
	uint8_t eEnableDisc;
	uint8_t dtStreamCount;
	a2bpnp_AppInitParams initParam;
	A2B_APP_PNP_STATE eAppState;
	uint8_t bUserReady;
	uint8_t bMixerDSPCntrl;
	uint8_t bApplyRequired;
	uint8_t bEnableDebug;
	uint8_t ignoreE2Prom;
	uint8_t bStreamBycon;
	uint8_t bGlobalMute;   /*<! Global mute during the crossbar update/ audio rerouting  */
	uint8_t bSystemReTry;
	uint8_t enAutoApply;
	uint8_t bTestRegression;
}A2B_APP_NW_PARAM;


extern A2B_APP_NW_PARAM nw[A2B_CONF_MAX_NUM_MASTER_NODES];
extern uint8_t nwIdx;

void app_state_process(a2b_UInt8 nwIdx);
void a2b_pnp_PrintNWInfo(a2b_UInt8 nwIdx, uint8_t bPrintNWInfo, uint8_t bPrintAudioRt);
void print_audio_routing(a2b_UInt8);
void print_main_node_info(a2b_UInt8);
void print_node_info(a2b_UInt8 nwIdx, a2b_UInt8 NodeIdx);
#if A2B_APP_ROUTING_MODE == A2B_APP_USER_DRIVEN
void adi_a2b_consoleExecution(void);
a2b_UInt8 getNwIdx(A2B_PNP_HANDLE hPnp);
void welcomeScreen();
void app_default_inits();
a2b_UInt8 app_xcvrBInvSetting(int value);
void a2b_printNodeRejection(a2bpnp_NodeRejectionInfo* pRejection);
a2b_UInt8 prepareAudioConJSON(connPairByChipId *NWStream, a2b_UInt8 streamCnt);
#endif

#endif /* __A2BAPP_COMMON_H__ */
