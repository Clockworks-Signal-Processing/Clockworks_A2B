/*******************************************************************************
Copyright (c) 2022 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
*******************************************************************************

   Name       : a2bapp_common.c

   Description: This file is responsible for handling all the application level
                 functions

   Functions  : main()
                PnPAppCallback()

   Developed by: Automotive Software and Systems team, Bangalore, India

******************************************************************************/
/*============= I N C L U D E S =============*/
#include <stdlib.h>
#include "adi_a2b_externs.h"
#include "assert.h"
#include <cmd_queue.h>
#include <cmd_platform.h>
#include <a2bpnp.h>
#include "a2bapp_defs.h"
#include "a2bapp_common.h"
#include "../test/cJSON-master/cJSON.h"

/*============= D E F I N E S =============*/

/*============= D A T A =============*/

/*Sigma DSP params for Mute/unmute */
a2b_UInt8 Mute[6] = {0x00, 0x29, 0x00, 0x00, 0x00, 0x00};
a2b_UInt8 Unmute[6] = {0x00, 0x29, 0x01, 0x00, 0x00, 0x00};

/* structure which holds applic*/
A2B_APP_NW_PARAM nw[A2B_CONF_MAX_NUM_MASTER_NODES];

/*Main Node Configuration details , reference example */
a2bpnp_MainNodeAppInfo  mainN =
{
		.DeviceName = "MAIN",	/*Device Name*/
		.nDeviceType = 2,		/*No. of device capabilities*/
		.DeviceType = {AUDIO_OUT, AUDIO_IN},  /*Supported device capabilities*/
		.nTxGroups = 3,			/*No. of Tx groups*/
		.nRxGroups = 3,			/*No. of Rx groups*/
		.TxGroups = {{			/*Details of Tx groups*/
						.GroupID = 0,
						.GroupName = "G0",
						.nGroupChannels = 12,
						.GroupChannels = {0,1,2,3,4,5,6,7,8,9,10,11}
					},
					{			/*Details of Tx groups*/
						.GroupID = 1,
						.GroupName = "G1",
						.nGroupChannels = 2,
						.GroupChannels = {12,13}
					},
					{	/*Details of Tx groups*/
						.GroupID = 2,
						.GroupName = "G2",
						.nGroupChannels = 2,
						.GroupChannels = {14,15}
					},
#if 0

					{	/*Details of Tx groups*/
						.GroupID = 3,
						.GroupName = "G3",
						.nGroupChannels = 2,
						.GroupChannels = {6,7}
					},
					{	/*Details of Tx groups*/
						.GroupID = 4,
						.GroupName = "G4",
						.nGroupChannels = 2,
						.GroupChannels = {8,9}
					},
					{	/*Details of Tx groups*/
						.GroupID = 5,
						.GroupName = "G5",
						.nGroupChannels = 2,
						.GroupChannels = {10,11}
					},
					{	/*Details of Tx groups*/
						.GroupID = 6,
						.GroupName = "G6",
						.nGroupChannels = 2,
						.GroupChannels = {12,13}
					},
					{	/*Details of Tx groups*/
						.GroupID = 7,
						.GroupName = "G7",
						.nGroupChannels = 2,
						.GroupChannels = {14,15}
					},
#endif
					},
		.RxGroups = {{			/*Details of Rx groups*/
						.GroupID = 0,
						.GroupName = "G0",
						.nGroupChannels = 12,
						.GroupChannels = {0,1,2,3,4,5,6,7,8,9,10,11}
					},
					{			/*Details of Rx groups*/
						.GroupID = 1,
						.GroupName = "G1",
						.nGroupChannels = 2,
						.GroupChannels = {12,13}
					},
					{			/*Details of Rx groups*/
						.GroupID = 2,
						.GroupName = "G2",
						.nGroupChannels = 2,
						.GroupChannels = {14,15}
					},
#if 0
					{			/*Details of Rx groups*/
						.GroupID = 3,
						.GroupName = "G3",
						.nGroupChannels = 2,
						.GroupChannels = {6,7}
					},
					{			/*Details of Rx groups*/
						.GroupID = 4,
						.GroupName = "G4",
						.nGroupChannels = 2,
						.GroupChannels = {8,9}
					},
					{			/*Details of Rx groups*/
						.GroupID = 5,
						.GroupName = "G5",
						.nGroupChannels = 2,
						.GroupChannels = {10,11}
					},
					{			/*Details of Rx groups*/
						.GroupID = 6,
						.GroupName = "G6",
						.nGroupChannels = 2,
						.GroupChannels = {12,13}
					},
					{			/*Details of Rx groups*/
						.GroupID = 7,
						.GroupName = "G7",
						.nGroupChannels = 2,
						.GroupChannels = {14,15}
					},
#endif
					},
		.dtEnabled = false,		/*Data Tunnel ON/OFF*/
};

/*============= C O D E =============*/
a2b_UInt8 InterruptCallback(A2B_PNP_HANDLE hPnp, uint32_t Event, void *pArg);
a2b_UInt8 PnPAppCallback (A2B_PNP_HANDLE hPnp, uint32_t Event, void *pArg);
a2b_UInt8 ErrorCallback(A2B_PNP_HANDLE hPnp, uint32_t Event, void *pArg);
a2b_UInt8 sigmaDSP_MuteAudio(A2B_PNP_HANDLE hPnp, a2b_Bool muteStatus);
void a2b_pnp_PrintNWInfo(a2b_UInt8 nwIdx, uint8_t bPrintNWInfo, uint8_t bPrintAudioRt);
void welcomeScreen();

void addConJSON(cJSON *network_connections, int8_t srcGroup, int8_t snkGroup, const char *srcChipID, const char *dstChipID) {
    cJSON *connection = cJSON_CreateObject();
	cJSON_AddNumberToObject(connection, "source_group", (double)srcGroup);
	cJSON_AddNumberToObject(connection, "sink_group", (double)snkGroup);
    cJSON_AddStringToObject(connection, "source_chip_ID", srcChipID);
    cJSON_AddStringToObject(connection, "sink_chip_ID", dstChipID);
    cJSON_AddItemToArray(network_connections, connection);
}

a2b_UInt8 prepareAudioConJSON(connPairByChipId *NWStream, a2b_UInt8 streamCnt)
{
	cJSON *root = NULL;
    cJSON *network_connections = NULL;
    FILE *file = fopen("../../../cfg/connection.json", "r");
	char srcChipID[13] = {0};
	char dstChipID[13] = {0};
	a2b_UInt8 bAppend = 1;
    
    if (file) {	//if file is already present
        fseek(file, 0, SEEK_END);
        long fileSize = ftell(file);
        if (fileSize == 0) {
            // If the file is empty, create a new JSON structure
            root = cJSON_CreateObject();
            network_connections = cJSON_CreateArray();
            cJSON_AddItemToObject(root, "connections", network_connections);
        } else {
            // Read the existing JSON content from the file
            fseek(file, 0, SEEK_SET);
            char *fileContent = (char *)malloc(fileSize + 1);
            fread(fileContent, 1, fileSize, file);
            fileContent[fileSize] = '\0';

            root = cJSON_Parse(fileContent);
            free(fileContent);

            network_connections = cJSON_GetObjectItem(root, "connections");
        }
        fclose(file);
    } else {
        // If the file doesn't exist, create a new JSON structure
        root = cJSON_CreateObject();
        network_connections = cJSON_CreateArray();
        cJSON_AddItemToObject(root, "connections", network_connections);
    }

    // Add new connections
	for(a2b_UInt8 sCnt = 0; sCnt<streamCnt; sCnt++)
	{
		sprintf(srcChipID,"%02X%02X%02X%02X%02X%02X", (unsigned char)NWStream[sCnt].src.chipid[0], (unsigned char)NWStream[sCnt].src.chipid[1], (unsigned char)NWStream[sCnt].src.chipid[2], (unsigned char)NWStream[sCnt].src.chipid[3], (unsigned char)NWStream[sCnt].src.chipid[4], (unsigned char)NWStream[sCnt].src.chipid[5]);
		sprintf(dstChipID,"%02X%02X%02X%02X%02X%02X", (unsigned char)NWStream[sCnt].dst.chipid[0], (unsigned char)NWStream[sCnt].dst.chipid[1], (unsigned char)NWStream[sCnt].dst.chipid[2], (unsigned char)NWStream[sCnt].dst.chipid[3], (unsigned char)NWStream[sCnt].dst.chipid[4], (unsigned char)NWStream[sCnt].dst.chipid[5]);

		for(a2b_UInt8 prevCnt = 0; prevCnt < cJSON_GetArraySize(network_connections); prevCnt++)
		{
			cJSON *connection = cJSON_GetArrayItem(network_connections, prevCnt);
			cJSON *source_chip_ID = cJSON_GetObjectItem(connection, "source_chip_ID");
        	cJSON *sink_chip_ID = cJSON_GetObjectItem(connection, "sink_chip_ID");

			if(strcmp(srcChipID, source_chip_ID->valuestring) && strcmp(dstChipID, sink_chip_ID->valuestring))
			{
				bAppend = 1;
			}
			else
			{
				bAppend = 0;
				break;
			}
		}
		if(bAppend)
		{
			addConJSON(network_connections, NWStream[sCnt].src.grpNo, NWStream[sCnt].dst.grpNo, srcChipID, dstChipID);
		}
	}
    char *json_data = cJSON_Print(root);

    file = fopen("../../../cfg/connection.json", "w");
    if (file) {
        fputs(json_data, file);
        fclose(file);
    }

    cJSON_Delete(root);
    free(json_data);
}

void stringToBytes(char *input, char *output, int inSize, int outSize)
{
	int numInts = inSize / 2; // Number of integers

	if (numInts > outSize) {
		numInts = outSize;
	}

	for (int i = 0; i < numInts; i++) {
		char temp[3]; // Buffer to hold the hex characters
		unsigned int value = 0;
		temp[0] = input[i * 2];
		temp[1] = input[i * 2 + 1];
		temp[2] = '\0';

		// %X stores a full unsigned int; scanning straight into output+i
		// overwrote the 3 bytes after each byte (and past the end of output).
		sscanf(temp, "%X", &value);
		output[i] = (char)value;
	}
}
a2b_UInt8 parseAudioConJSON(connPairByChipId *NWCon, int *numCon)
{
	a2b_UInt8 nConnections = 0;
	cJSON *connection;
	cJSON *source_group;
	cJSON *sink_group;
	cJSON *source_chip_ID;
	cJSON *sink_chip_ID;
    FILE *file = fopen("../../../cfg/connection.json", "r");
    if (!file) {
        return -1;
    }

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fseek(file, 0, SEEK_SET);

    char *fileContent = (char *)malloc(fileSize + 1);
    fread(fileContent, 1, fileSize, file);
    fileContent[fileSize] = '\0';

    cJSON *root = cJSON_Parse(fileContent);
    free(fileContent);
    fclose(file);

    if (!root) {
        return -1;
    }

    cJSON *network_connections = cJSON_GetObjectItem(root, "connections");
    if (!network_connections || !cJSON_IsArray(network_connections)) {
        cJSON_Delete(root);
        return -1;
    }

    *numCon = cJSON_GetArraySize(network_connections);
	nConnections = *numCon;

    for (int i = 0; i < nConnections && i < A2B_PNP_CFG_MAX_NUM_NW_STREAMS; i++) {
        connection = cJSON_GetArrayItem(network_connections, i);
		source_group = cJSON_GetObjectItem(connection, "source_group");
        sink_group = cJSON_GetObjectItem(connection, "sink_group");
        source_chip_ID = cJSON_GetObjectItem(connection, "source_chip_ID");
        sink_chip_ID = cJSON_GetObjectItem(connection, "sink_chip_ID");

        if (source_chip_ID && sink_chip_ID &&
            cJSON_IsString(source_chip_ID) && cJSON_IsString(sink_chip_ID)) {
            
			NWCon[i].src.grpNo = source_group->valueint;
			NWCon[i].dst.grpNo = sink_group->valueint;
			stringToBytes(sink_chip_ID->valuestring, NWCon[i].dst.chipid, strlen(sink_chip_ID->valuestring), sizeof(NWCon[i].dst.chipid));
			stringToBytes(source_chip_ID->valuestring, NWCon[i].src.chipid, strlen(source_chip_ID->valuestring), sizeof(NWCon[i].src.chipid));
        }
    }

    cJSON_Delete(root);

	// Report only the entries actually stored in NWCon, so callers can't read past it.
	if (*numCon > (int)A2B_PNP_CFG_MAX_NUM_NW_STREAMS) {
		*numCon = (int)A2B_PNP_CFG_MAX_NUM_NW_STREAMS;
	}
	return 0;
}

/// @brief Mutes/Unmutes SigmaDSP signal chain (Mixer). This is only reference & applicable for Sigma
/// @param hPnp 
/// @param muteStatus 
/// @return 0 - Pass, -1:  Fail
a2b_UInt8 sigmaDSP_MuteAudio(A2B_PNP_HANDLE hPnp, a2b_Bool muteStatus)
{
	a2b_UInt8 nwIdx = getNwIdx(hPnp);
	a2b_UInt32 nResult = 0;

	if(nw[nwIdx].bMixerDSPCntrl == 1)
	{
		if(muteStatus)
		{
			nResult = a2b_pnp_I2CWritePeri(nw[nwIdx].hPnp, -1, 0x38, sizeof(Mute), Mute);
		}
		else
		{
			nResult = a2b_pnp_I2CWritePeri(nw[nwIdx].hPnp, -1, 0x38, sizeof(Unmute), Unmute);
		}
	}
	return nResult;
}

/// @brief Get network instance
/// @param hPnp 
/// @return instance for the given handle
a2b_UInt8 getNwIdx(A2B_PNP_HANDLE hPnp)
{
	for(a2b_UInt8 i = 0; i< A2B_CONF_MAX_NUM_MASTER_NODES; i++)
	{
		if(hPnp == nw[i].hPnp)
		{
			return i;
		}
	}
	return -1;
}

/// @brief  Main call back function, includes system level errors as well
/// @param hPnp 
/// @param Event 
/// @param pArg 
/// @return 
a2b_UInt8 PnPAppCallback (A2B_PNP_HANDLE hPnp, uint32_t Event, void *pArg)
{

	a2b_Int32 nodeAddr;
	a2b_UInt8 nwIdx = getNwIdx(hPnp);
	a2b_UInt8 vendorData[256] = {0};
	a2bpnp_NodeRejectionInfo rejectInfo;
	a2b_pnp_faultRes* pfaultRes;
	a2b_pnp_muteStatus  muteStatus;

#ifndef ADI_CONFIG_ENABLE_PNP_DEMO_ROUTING
#if A2B_APP_ROUTING_MODE == A2B_APP_NETWORK_DRIVEN
	STREAM_INPUT_STATE StreamInputState;
	a2b_UInt8 Result;
#endif
#endif
	//typical cast
	nodeAddr = (a2b_Int32)(a2b_UIntPtr)pArg;

	switch(Event)
	{
		/* New node Connected - This event will be raised multiple times if chain of A2B nodes is connected */
		case A2B_PNP_NEW_NODE_CONNECTED:
			/*Check Vendor specific data*/
			a2b_pnp_ReadOEMInfo(hPnp, nodeAddr, vendorData);
			/*Decide about the rejection*/
			if(0)
			{
				/*Reject Node*/
				SetConsoleFont(MAGENTA_TEXT);
				a2b_pnp_RejectNode(hPnp, nodeAddr);
				SetConsoleFont(NORMAL_TEXT);
			}
			else /*Approve the connection*/
			{
				SetConsoleFont(GREEN_TEXT);
				ADI_UART_PRINT("\n\rNode Connected: %d\n\r", nodeAddr);
				SetConsoleFont(NORMAL_TEXT);
				if(nw[nwIdx].enAutoApply)
				{
					nw[nwIdx].bApplyRequired = 1;
				}
			}
			break;

		/* Node Disconnected - called once with last node as nodeAddr */
		case A2B_PNP_NODE_DISCONNECTED:

			SetConsoleFont(RED_TEXT);
			ADI_UART_PRINT("\n\rBus Drop Detected @ Node: %d\n\r", nodeAddr-1);
			SetConsoleFont(NORMAL_TEXT);
			break;

		/* Node Rejected - This can be due to multiple reason: Signature Authentication failure, Inadequate Power/BW requirements, longer cable lengths */
		case A2B_PNP_NODE_REJECTED:
			rejectInfo = *((a2bpnp_NodeRejectionInfo*)pArg);
			SetConsoleFont(RED_TEXT);
			a2b_printNodeRejection(&rejectInfo);
			SetConsoleFont(NORMAL_TEXT);
			break;

			/* */
		case A2B_PNP_I2C_FAILURE:
			SetConsoleFont(RED_TEXT);
			ADI_UART_PRINT("\n\rNode I2C Failures: %d\n\r", nodeAddr);
			SetConsoleFont(NORMAL_TEXT);
			break;

		/* Any line faults - More information to be added */
		case A2B_PNP_NW_REDISCOVERY_INITIATED:
			SetConsoleFont(MAGENTA_TEXT);
			ADI_UART_PRINT("\n\rNetwork Re-discovery initiated\n\r");
			SetConsoleFont(NORMAL_TEXT);
			break;

		case A2B_PNP_APPLY_AUDIO_ROUTING:
			if(nw[nwIdx].bApplyRequired)
			{

				a2b_pnp_ApplyRouting(hPnp);
				nw[nwIdx].bApplyRequired = 0;
				/* Return 1 to inform PnP Library that Audio Routing has been applied and there is no need to further request for it */
				return 1;
			}
			break;
		case A2B_PNP_MAIN_PLL_LOCK_FAILURE:
			SetConsoleFont(RED_TEXT);
			ADI_UART_PRINT("\n\rMain Node PLL not locked\n\r", nodeAddr);
			SetConsoleFont(NORMAL_TEXT);
			nw[nwIdx].eAppState = A2B_PNP_APP_SYS_ERR;
			break;
		case A2B_PNP_NO_VALID_RESP_CYCS:
			SetConsoleFont(RED_TEXT);
			ADI_UART_PRINT("\n\rNo Valid Response Cycle for the network\n\r");
			SetConsoleFont(NORMAL_TEXT);
			break;
		case A2B_PNP_AUDIO_MUTE:
		    /* Point where audio is going to be muted , notify the DSP */
			muteStatus = *(a2b_pnp_muteStatus*)pArg;
			if(nw[nwIdx].bEnableDebug)
			{
				if(muteStatus.globalNetworkMute)
				{
					ADI_UART_PRINT("\n\rImpending global audio mute\n\r");
				}
				else 
				{
					if(muteStatus.mainNodeMute)
					{
						ADI_UART_PRINT("\n\rImpending main node audio mute\n\r");
					}
				}

			}
			sigmaDSP_MuteAudio(hPnp, A2B_TRUE);
			break;
		case A2B_PNP_AUDIO_UNMUTE_ROUTING_DONE:
		    /* All the muted nodes are unmuted */
			if(nw[nwIdx].bEnableDebug)
			{
				ADI_UART_PRINT("\n\r audio unmute callback \n\r");
			}
			sigmaDSP_MuteAudio(hPnp, A2B_FALSE);
			break;
		case A2B_PNP_HW_FAULT:
			pfaultRes =  (a2b_pnp_faultRes*)pArg;
			SetConsoleFont(RED_TEXT);
			ADI_UART_PRINT("\n\r Hardware fault reported\n\r");
			if(nw[nwIdx].bEnableDebug)
			{
				if(pfaultRes != A2B_NULL)
				{
					ADI_UART_PRINT("\n\r Fault code: %d\n\r", pfaultRes->faultCode);
					ADI_UART_PRINT("\n\r Fault Node: %d\n\r", pfaultRes->faultNode);
				}
			}
			SetConsoleFont(NORMAL_TEXT);
			a2b_pnp_ClearHwFaults(hPnp,0);
			break;
		case A2B_PNP_BW_ERROR_LEVEL:
			ADI_UART_PRINT("\n\r BW overshoot (error) reported \n\r");
			break;
		case A2B_PNP_BW_WARNING_LEVEL:
			ADI_UART_PRINT("\n\r BW overshoot (warning) reported \n\r");
			break;	
		case A2B_PNP_CONN_IMPORT:
			//Some previous connection/s are restored, apply these audio on NW?
			nw[nwIdx].bApplyRequired = 1;
			break;
		default:
			break;
	}

	/* This return value is not used by Plug and Play library */
	return 0;
}

/// @brief  Application level A2B interrupt callback function 
/// @param hPnp 
/// @param Event 
/// @param pArg 
/// @return 
a2b_UInt8 InterruptCallback (A2B_PNP_HANDLE hPnp, uint32_t Event, void *pArg)
{
	a2b_Int32 nodeAddr = (a2b_Int32)(a2b_UIntPtr)pArg;
    A2B_UNUSED(nodeAddr);

	switch(Event)
	{
	case A2B_PNP_INTTYPE_IO0PND:
		break;
	case A2B_PNP_INTTYPE_IO1PND:
		break;
	case A2B_PNP_INTTYPE_IO2PND:
		break;
	case A2B_PNP_INTTYPE_IO3PND:
		break;
	case A2B_PNP_INTTYPE_IO4PND:
		break;
	case A2B_PNP_INTTYPE_IO5PND:
		break;
	case A2B_PNP_INTTYPE_IO6PND:
		break;
	case A2B_PNP_INTTYPE_IO7PND:
		break;
	default:
		break;
	}
	return 0;
}

/// @brief Integration /devleopment error  function 
/// @param hPnp 
/// @param Event 
/// @param pArg 
/// @return 
a2b_UInt8 ErrorCallback (A2B_PNP_HANDLE hPnp, uint32_t Event, void *pArg)
{
	a2b_Int32 nodeAddr = (a2b_Int32)(a2b_UIntPtr)pArg;
	A2B_UNUSED(nodeAddr);
	a2b_UInt8 nwIdx = getNwIdx(hPnp);

	switch(Event)
	{
	/*TODO - specify error codes*/
	case A2B_PNP_NULL_HANDLE_ERR:
	SetConsoleFont(RED_TEXT);
	ADI_UART_PRINT("\n\rNULL Handle Reported\n\r", nodeAddr);
	SetConsoleFont(NORMAL_TEXT);
	break;
	case A2B_PNP_BCF_FILE_OPEN_ERR:
	SetConsoleFont(RED_TEXT);
	ADI_UART_PRINT("\n\rFailed to open BCF file\n\r", nodeAddr);
	SetConsoleFont(NORMAL_TEXT);
	nw[nwIdx].eAppState = A2B_PNP_APP_SYS_ERR;
	break;
	case A2B_PNP_MODULE_CATCH_ALL_ERR:
	ADI_UART_PRINT("\n\rPnP Module internal Error\n\r", nodeAddr);
	nw[nwIdx].eAppState = A2B_PNP_APP_SYS_ERR;  /* putting application into error */
	break;
	case A2B_PNP_ROUTING_APPLY_DURING_BD:
	ADI_UART_PRINT("\n\rApply Routing API called during Blind discovery\n\r", nodeAddr);
		break;
	case A2B_PNP_ERROR_05:
		break;
	case A2B_PNP_ERROR_06:
		break;
	case A2B_PNP_ERROR_07:
		break;
	case A2B_PNP_ERROR_08:
		break;
	default:
		break;
	}
	return 0;
}



/// @brief Application state machine 
/// @param  
void app_state_process(a2b_UInt8 nwIdx)
{
	/* State handled by the previous call, so one-time messages print once per entry */
	static A2B_APP_PNP_STATE eLastHandled[A2B_CONF_MAX_NUM_MASTER_NODES];
	A2B_APP_PNP_STATE eState = nw[nwIdx].eAppState;
	a2b_UInt32 nResult = 0;
	a2b_UInt8 nChainIndex=nwIdx;
	connPairByChipId NWConnections[A2B_PNP_CFG_MAX_NUM_NW_STREAMS] = {0};
	int numCon = 0;	/* parseAudioConJSON() writes it through an int* */
	switch(eState)
	{
		case A2B_PNP_APP_WAIT_FOR_START:
		if(nw[nwIdx].bUserReady)
		{

			nw[nwIdx].g_MainNodeCfg.pBcf = &sBusDescription;
			if(nw[nwIdx].g_MainNodeCfg.pFilePath == A2B_NULL)
			{
				nw[nwIdx].g_MainNodeCfg.pFilePath = (a2b_UInt8 *)"..\\..\\..\\cfg\\adi_a2b_system_autoconfig_RJ45.dat";
			}
			
			/* Initialize PnP Module */
			nw[nwIdx].hPnp =  a2b_pnp_Init(nChainIndex, &nw[nwIdx].g_MainNodeCfg, PnPAppCallback, InterruptCallback, ErrorCallback);

			if(nw[nwIdx].hPnp == NULL)
			{
		    	/* failed to initialize PnP Module */
		    	assert(nw[nwIdx].hPnp == NULL);
			}
			/* internal creation of streams by connection*/
            nw[nwIdx].initParam.bStreamByConnection = nw[nwIdx].bStreamBycon;
            nw[nwIdx].initParam.ignoreE2Prom = nw[nwIdx].ignoreE2Prom;
			nw[nwIdx].initParam.bDebug = nw[nwIdx].bEnableDebug;
			nw[nwIdx].initParam.bGlobalMuteDuringCrossbar = nw[nwIdx].bGlobalMute;
			
			nResult = a2b_pnp_SetInitParams(nw[nwIdx].hPnp, &nw[nwIdx].initParam);
			/*Configure Main Node Audio capability*/
			a2b_pnp_ConfigureMainNode(nw[nwIdx].hPnp, &mainN);
			if(nResult == 0)
			{
				nResult = a2b_pnp_StartDiscovery(nw[nwIdx].hPnp);
			}
			if (nResult)
			{
				nw[nwIdx].eAppState = A2B_PNP_APP_SYS_ERR;
				SetConsoleFont(RED_TEXT);
				ADI_UART_PRINT("\n\rFailed to start, System Error\n\r");
				SetConsoleFont(NORMAL_TEXT);
				a2b_pnp_DeInit(nw[nwIdx].hPnp);
				
			}
			else
			{
				nw[nwIdx].eAppState = A2B_PNP_APP_NETWORK_MNG;

			}

			//Import preset connection info
			if(!parseAudioConJSON(NWConnections, &numCon))
			{
				for(a2b_UInt8 i = 0; i<numCon; i++)
				{		
					a2b_pnp_AddConByChipId(nw[nwIdx].hPnp, NWConnections[i]);
				}
			}

		}
		break;
		case A2B_PNP_APP_NETWORK_MNG:
		/* Monitor a2b network for faults and initiate re-discovery if enabled */
		a2b_pnp_Task(nw[nwIdx].hPnp);
		break;
		case A2B_PNP_APP_SYS_ERR:
		/* Print on entry only: this runs on every main-loop pass while in the error state */
		if(eLastHandled[nwIdx] != A2B_PNP_APP_SYS_ERR)
		{
			ADI_UART_PRINT("\n\rEntered System Error State\n\r");
		}
		if(nw[nwIdx].bSystemReTry == 1)
		{
			nw[nwIdx].bSystemReTry = 0;
			nw[nwIdx].eAppState = A2B_PNP_APP_WAIT_FOR_START;
		}
		break;
		default: break;

	}
	eLastHandled[nwIdx] = eState;
}

/// @brief initialization of default values
void app_default_inits()
{
	for(a2b_UInt8 i = 0; i< A2B_CONF_MAX_NUM_MASTER_NODES; i++)
	{
		nw[i].initParam.maxNumSubnodes = A2B_CONF_MAX_NUM_SLAVE_NODES;		
		/* Polling interval in ms */
		nw[i].g_MainNodeCfg.pollTime = A2B_APP_DEFAULT_INTERRUPT_QUERY_INTERVAL;
		/* Interval to attempt discovery for possible new nodes */
		nw[i].g_MainNodeCfg.periodicDiscInterval = A2B_APP_DEFAULT_BLIND_DISC_INTERVAL;

		/*creation of streams by connection is preferred */
		nw[i].bStreamBycon = 1;

		nw[i].initParam.maxPWconsumption = A2B_APP_DEFAULT_ALLOWED_PWR_CONSUMPTION;
		nw[i].initParam.maxCurrentConsumption = A2B_APP_DEFAULT_ALLOWED_CURRENT_CONSUMPTION;

		nw[i].initParam.BWErrorLevel =  A2B_APP_DEFAULT_BW_ERROR_LEVEL;
		nw[i].initParam.BWWarningLevel = A2B_APP_DEFAULT_BW_WARNING_LEVEL;

		nw[i].initParam.mainXcvrBInv = app_xcvrBInvSetting(A2B_APP_DEFAULT_MAIN_XCVRBINV);
		nw[i].initParam.subXcvrBInv = app_xcvrBInvSetting(A2B_APP_DEFAULT_SUB_XCVRBINV);
	}

}

/// @brief Map an XCVRBINV value (-1 = bus configuration, 0 = off, 1 = on) to the PnP setting
/// @param value -1, 0 or 1; anything else keeps the bus configuration value
/// @return A2B_PNP_XCVRBINV_x
a2b_UInt8 app_xcvrBInvSetting(int value)
{
	if (value == 0)
	{
		return A2B_PNP_XCVRBINV_OFF;
	}
	if (value == 1)
	{
		return A2B_PNP_XCVRBINV_ON;
	}
	return A2B_PNP_XCVRBINV_FROM_CFG;
}