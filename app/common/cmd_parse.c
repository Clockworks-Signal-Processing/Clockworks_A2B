/*******************************************************************************
Copyright (c) 2023 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
*******************************************************************************/

/******************Includes**************/
#define DO_CYCLE_COUNTS

#include "a2bapp_platform.h"	// per-platform: app/<platform>/a2bapp_platform.h
#include "adi_a2b_datatypes.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "spi.h"
#include "adi_a2b_externs.h"
#include "assert.h"
#include <stdint.h>
#include "cmd_platform.h"
#include "cmd_queue.h"
#include <a2bpnp.h>
#include "a2bapp_defs.h"
#include "a2bapp_common.h"
#include <stdlib.h>
#include "adi_a2b_driverprototypes.h"



#ifdef __GNUC__
#define GEN_STROK(s1, s2, s3)  strtok(s1, s2)
#else
#define GEN_STROK(s1, s2, s3)  strtok(s1, s2, s3)
#endif

#if A2B_APP_ROUTING_MODE == A2B_APP_USER_DRIVEN
void a2b_spiPeriWrRdCbNb(void* pCbParam, A2B_SPI_EVENT eEventType, a2b_Int8 nNodeAddr);
static void printUartBanner(void);
void setGpiod();
void tearDownGpiod();


volatile uint8_t g_uParseChar;
bool g_bTerminalEcho = TRUE;
uint8 IsArrowKey(uint8_t nChar);

char aStr[ADI_TERMINAL_CMD_SIZE];

char aStrBuf[ADI_TERMINAL_CMD_HIS_SIZE][ADI_TERMINAL_CMD_SIZE];
uint8_t nCmdStrInd;
uint8_t gnCurrBufInd;
uint8_t gnCurrCmdInd;
uint8_t nwIdx = 0;
uint8 aReadData[32*1024] = {0u};
char filePath[50u] = {0};

/*Arguments for the sscanf function*/
uint32 nArg1 = 0u;
uint32 nArg2 = 0u;
uint32 nArg3 = 0u;
uint32 nArg4 = 0u;
float nArg5;
static a2bpnp_StreamInfo AudioStream;

static bool adi_uart_IsCR(uint8_t uChar);
static bool adi_uart_IsBackSpace(uint8_t uChar);
static bool adi_uart_isUp(uint8_t uChar);
static a2b_UInt8 adi_uart_isDown(a2b_UInt8 uChar);
static a2b_UInt8 adi_uart_isRight(a2b_UInt8 uChar);
static a2b_UInt8 adi_uart_isLeft(uint8_t uChar);
void ExecCmd(void);
void cmd_DisplaySupportedCommands(void);
void testSpi(void);

/*Function to check if the character is Carriage return*/
static bool adi_uart_IsCR(uint8_t uChar)
{
	if (uChar==0xD) return true;
	else return false;
}

/*Function to check if the character is Backspace*/
static bool adi_uart_IsBackSpace(uint8_t uChar)
{
	if(uChar == 0x08u)
		return 1u;
	else
		return 0u;
}

/*Up arrow key handling*/
static bool adi_uart_isUp(uint8_t uChar)
{
	uint8 bRet = 0u;
	if((aStr[nCmdStrInd-3]== 0x1B) && ((aStr[nCmdStrInd-2] == 0x41) || (aStr[nCmdStrInd-1] == 0x41)))
	{
		bRet = 1u;
	}
	return (bRet);
}

/*Down arrow key handling*/
static a2b_UInt8 adi_uart_isDown(a2b_UInt8 uChar)
{
	a2b_UInt8 bRet = 0u;
	if((aStr[nCmdStrInd-3]== 0x1B) && ((aStr[nCmdStrInd-2] == 0x42) || (aStr[nCmdStrInd-1] == 0x42)))
	{
		bRet = 1u;
	}
	if((aStr[nCmdStrInd-2]== 0x1B) && (aStr[nCmdStrInd-1] == 0x42))
	{
		bRet = 1u;
	}
	return (bRet);
}

/*Right arrow key handling*/
static a2b_UInt8 adi_uart_isRight(a2b_UInt8 uChar)
{
	uint8 bRet = 0u;
	if((aStr[nCmdStrInd-3]== 0x1B) && ((aStr[nCmdStrInd-2] == 0x43) || (aStr[nCmdStrInd-1] == 0x43)))
	{
		aStr[--nCmdStrInd] = '\0';
		aStr[--nCmdStrInd] = '\0';
		aStr[--nCmdStrInd] = '\0';
		bRet = 1u;
	}
	if((aStr[nCmdStrInd-2]== 0x1B) && (aStr[nCmdStrInd-1] == 0x43))
	{
		aStr[--nCmdStrInd] = '\0';
		aStr[--nCmdStrInd] = '\0';
		bRet = 1u;
	}
	return (bRet);
}

static a2b_UInt8 adi_uart_isLeft(uint8_t uChar)
{
	uint8 bRet = 0u;
	if((aStr[nCmdStrInd-3]== 0x1B) && ((aStr[nCmdStrInd-2] == 0x42) || (aStr[nCmdStrInd-1] == 0x42)))
	{
		aStr[--nCmdStrInd] = '\0';
		aStr[--nCmdStrInd] = '\0';
		aStr[--nCmdStrInd] = '\0';
		bRet = 1u;
	}
	if((aStr[nCmdStrInd-2]== 0x1B) && (aStr[nCmdStrInd-1] == 0x42))
	{
		aStr[--nCmdStrInd] = '\0';
		aStr[--nCmdStrInd] = '\0';
		bRet = 1u;
	}
	return (bRet);
}

/*Function to process the received character*/
static void adi_uart_ProcessChar(uint8_t uChar)
{
	uint8_t i = false, nTempCmdInd;
	static bool bUpPressed = FALSE;
	static bool bDownPressed = FALSE;
	if(adi_uart_IsCR(uChar))
	{
		/* Carriage Return handling */
		aStr[nCmdStrInd] = '\0';
		if(nCmdStrInd != 0)
		{
			nCmdStrInd = 0;
			ExecCmd();

			nTempCmdInd = ((gnCurrBufInd + ADI_TERMINAL_CMD_HIS_SIZE) - 1u) % ADI_TERMINAL_CMD_HIS_SIZE;
			if(strcmp(aStrBuf[nTempCmdInd],aStr) != 0)
			{
				strncpy(aStrBuf[gnCurrBufInd++],aStr, ADI_TERMINAL_CMD_SIZE);
				gnCurrBufInd %= ADI_TERMINAL_CMD_HIS_SIZE;
				gnCurrCmdInd = ((gnCurrBufInd + ADI_TERMINAL_CMD_HIS_SIZE) - 1u) % ADI_TERMINAL_CMD_HIS_SIZE;;
			}
		}
		else
		{
			if(bUpPressed == TRUE)
			{
				gnCurrCmdInd++;
				if(bDownPressed == TRUE)
				{
					gnCurrCmdInd--;
				}
			}
		}
		bUpPressed = FALSE;
		bDownPressed = FALSE;
	}
	else if (adi_uart_IsBackSpace(uChar))
	{
		/* Back-Space handling */
		if(nCmdStrInd > 0)
		{
			ADI_UART_PRINT("%c",uChar);
			ADI_UART_PRINT(" ");
			aStr[--nCmdStrInd] = '\0';
		}
	}
	else
	{
		aStr[nCmdStrInd++] = uChar;
	}

	if(adi_uart_isUp(uChar))
	{
		bUpPressed = TRUE;
		if(bDownPressed == TRUE)
		{
			gnCurrCmdInd = ((gnCurrCmdInd + ADI_TERMINAL_CMD_HIS_SIZE) - 1) % ADI_TERMINAL_CMD_HIS_SIZE;
			bDownPressed = FALSE;
		}

		memcpy(aStr, aStrBuf[gnCurrCmdInd],ADI_TERMINAL_CMD_SIZE);
		for(i = 0; i < nCmdStrInd-2; i++)
		{
			ADI_UART_PRINT("%c",0x08);
			ADI_UART_PRINT(" ");
			ADI_UART_PRINT("%c",0x08);
		}
		nCmdStrInd = strlen(aStr);
		ADI_UART_PRINT("\rA2B>");
		ADI_UART_PRINT("%s",aStr);
		gnCurrCmdInd = ((gnCurrCmdInd + ADI_TERMINAL_CMD_HIS_SIZE) - 1) % ADI_TERMINAL_CMD_HIS_SIZE;
	}


	if((adi_uart_isDown(uChar)) && (bUpPressed == TRUE))
	{

		/* Down arrow key handling */
		if(bDownPressed == 0u)
		{
			gnCurrCmdInd = (gnCurrCmdInd + 1) % ADI_TERMINAL_CMD_HIS_SIZE;
			bDownPressed = 1u;
		}
		gnCurrCmdInd = (gnCurrCmdInd + 1) % ADI_TERMINAL_CMD_HIS_SIZE;
		strncpy(aStr, aStrBuf[gnCurrCmdInd],ADI_TERMINAL_CMD_HIS_SIZE);
		for(i = 0; i < nCmdStrInd-2; i++)
		{
			ADI_UART_PRINT("%c",0x08);
			ADI_UART_PRINT(" ");
			ADI_UART_PRINT("%c",0x08);
		}
		nCmdStrInd = strlen(aStr);
		ADI_UART_PRINT("\rA2B>");
		ADI_UART_PRINT("%s",aStr);
	}
	if(adi_uart_isRight(uChar))
	{

	}
	if(adi_uart_isLeft(uChar))
	{

	}
}
/// @brief  Console execution function. currently used in the embedded targets with UART
/// @param  
void adi_a2b_consoleExecution(void)
{
	static bool bEscSeqStart = false;
	if (adi_terminal_RxDequeue((uint8_t*)&g_uParseChar))
	{
		adi_uart_ProcessChar(g_uParseChar);

		if (g_bTerminalEcho)
		{
			if (g_uParseChar=='\r')
			{
				ADI_UART_PRINT("\r\nA2B>");
			}
			else if(g_uParseChar == 0x1B)
			{
				bEscSeqStart = true;
			}
			else if((bEscSeqStart == 1u) && (IsArrowKey(g_uParseChar) == 1u))
			{
				bEscSeqStart = 0u;
			}
			else if ((bEscSeqStart == 1u) && (g_uParseChar == 0x5Bu))
			{

			}
			else
			{
				ADI_UART_PRINT("%c",g_uParseChar);
			}
		}
	}
}
uint8 IsArrowKey(uint8_t nChar)
{
	uint8 bRet;
	switch(nChar)
	{
	case 'A':
	case 'B':
	case 'C':
	case 'D':
		bRet = 1u;
		break;
	default:
		bRet = 0u;
		break;
	}
	return(bRet);
}

/// @brief Services user commands 
/// @param  
void ExecCmd(void)
{
	uint8 nArg1_u8 = 0u, nNode = 0u;
	char g_cCommand[50u] = {0};
	char ArgS1[50u] = {0};
	char ArgS2[50u] = {0};
	char ArgS3[50u] = {0};

	char *token[16];
	char *token1;
	char *token2;
	char *rest;
	char suffix[50] = {0};
	char fileName[50] = {0};
	a2b_UInt8 num;
	a2b_Int8 n = 0;
    static a2b_UInt16 streamIdx = 0u;
	static bool bExported = A2B_FALSE;


	sscanf(aStr, "%s", g_cCommand);
	if (!strcmp((const char*) g_cCommand, "start"))
	{
		sscanf(aStr, "%s %i %i", g_cCommand, &nArg1, &nArg1);
		nw[nwIdx].bUserReady = 1;
		ADI_UART_PRINT("\r\n");

	}
	else if (!strcmp((const char*) g_cCommand, "test"))
	{
		sscanf(aStr, "%s %i", g_cCommand, &nArg1);
		nw[nwIdx].bTestRegression = nArg1;
		// err, why is RP2040 code trying to read a file?
		// TODO, make sense of this
		#if 0
		if(nw[nwIdx].bTestRegression)
		{
			int ret = JSONOpen("..\\..\\..\\cfg\\ini_REGRESSION_RJ45.json");
			switch(ret)
			{
				case -1:
				ADI_UART_PRINT("\n\rTEST:: JSON Open Failed\n\r");
				break;

				case -2:
				ADI_UART_PRINT("\n\rTEST:: JSON Memory Allocation Failed\n\r");
				break;

				case -3:
				ADI_UART_PRINT("\n\rTEST:: JSON Read Failed\n\r");
				break;

				case -4:
				ADI_UART_PRINT("\n\rTEST:: JSON Parse Failed\n\r");
				break;
			}
		}
		#endif
	}
	else if (!strcmp((const char*) g_cCommand, "recover"))
	{
		nw[nwIdx].bSystemReTry = 1;
	}
	else if (!strcmp((const char*) g_cCommand, "setNetwork"))
	{
		sscanf(aStr, "%s %i", g_cCommand, &nArg1);
		nwIdx = nArg1;
	}
	else if (!strcmp((const char*) g_cCommand, "ignoreEEPROM"))
	{
		sscanf(aStr, "%s %i", g_cCommand, &nArg1);
		if(nArg1 == 1 || nArg1 == 0)
		{
			nw[nwIdx].ignoreE2Prom = nArg1;
		}
		else
		{
			SetConsoleFont(RED_TEXT);
			ADI_UART_PRINT("\n\rInvalid command\n\r");
			SetConsoleFont(NORMAL_TEXT);
		}
	}
	else if (!strcmp((const char*) g_cCommand, "streamByConnection"))
	{
		sscanf(aStr, "%s %i", g_cCommand, &nArg1);
		if(nArg1 == 1 || nArg1 == 0)
		{
			nw[nwIdx].bStreamBycon = nArg1;
			SetConsoleFont(CYAN_TEXT);
			ADI_UART_PRINT("\n\rNote: This command takes effect only during startup.\n\r");
			SetConsoleFont(NORMAL_TEXT);
		}
		else
		{
			SetConsoleFont(RED_TEXT);
			ADI_UART_PRINT("\n\rInvalid command\n\r");
			SetConsoleFont(NORMAL_TEXT);
		}
	}
	else if (!strcmp((const char*) g_cCommand, "globalMute"))
	{
		sscanf(aStr, "%s %i", g_cCommand, &nArg1);
		if(nArg1 == 1 || nArg1 == 0)
		{
			nw[nwIdx].bGlobalMute = nArg1;
		}
		else
		{
			SetConsoleFont(RED_TEXT);
			ADI_UART_PRINT("\n\rInvalid command\n\r");
			SetConsoleFont(NORMAL_TEXT);
		}
	}
	else if (!strcmp((const char*) g_cCommand, "discInterval"))
	{
		sscanf(aStr, "%s %i", g_cCommand, &nArg1);
		nw[nwIdx].g_MainNodeCfg.periodicDiscInterval = nArg1;
	}
	else if (!strcmp((const char*) g_cCommand, "intrptPollInterval"))
	{
		sscanf(aStr, "%s %i", g_cCommand, &nArg1);
		nw[nwIdx].g_MainNodeCfg.pollTime = nArg1;
	}
	else if (!strcmp((const char*) g_cCommand, "maxNumNodes"))
	{
		sscanf(aStr, "%s %i", g_cCommand, &nArg1);
		nw[nwIdx].initParam.maxNumSubnodes = nArg1;
	}
	else if (!strcmp((const char*) g_cCommand, "xcvrBInv"))
	{
		int nMain = 0, nSub = 0;
		if ((sscanf(aStr, "%s %i %i", g_cCommand, &nMain, &nSub) == 3) &&
			(nMain >= -1) && (nMain <= 1) && (nSub >= -1) && (nSub <= 1))
		{
			nw[nwIdx].initParam.mainXcvrBInv = app_xcvrBInvSetting(nMain);
			nw[nwIdx].initParam.subXcvrBInv = app_xcvrBInvSetting(nSub);
		}
		else
		{
			SetConsoleFont(RED_TEXT);
			ADI_UART_PRINT("\n\rInvalid command\n\r");
			SetConsoleFont(NORMAL_TEXT);
		}
	}
	else if (!strcmp((const char*) g_cCommand, "filePath"))
	{
		sscanf(aStr, "%s %s", g_cCommand, suffix);
		snprintf(filePath, sizeof(filePath), "../../../cfg/%s", suffix);	// "/" works on Windows too
		nw[nwIdx].g_MainNodeCfg.pFilePath = (a2b_UInt8 *)filePath;
	}
	else if (!strcmp((const char*) g_cCommand, "updateEEPROM"))
	{
		sscanf(aStr, "%s %s %d %i", g_cCommand, fileName, &nArg1, &nArg2);
		if(a2b_pnp_UpdatePnPModuleInfo(nw[nwIdx].hPnp, fileName, nArg1, nArg2) == 0)
		{
			ADI_UART_PRINT("\r\nEEPROM Updated!\r\n");
		}
		else
		{
			SetConsoleFont(RED_TEXT);
			ADI_UART_PRINT("\r\nEEPROM NOT Updated!\r\n");
			SetConsoleFont(NORMAL_TEXT);
		}		
	}
	else if (!strcmp((const char*) g_cCommand, "updateEEPROMAll"))
	{
		sscanf(aStr, "%s %s %i", g_cCommand, fileName, &nArg2);
		if(a2b_pnp_UpdatePnPModuleInfoAll(nw[nwIdx].hPnp, fileName, nArg2) == 0)
		{
			ADI_UART_PRINT("\r\nAll EEPROM Updated!\r\n");
		}
		else
		{
			SetConsoleFont(RED_TEXT);
			ADI_UART_PRINT("\r\nEEPROM NOT Updated!\r\n");
			SetConsoleFont(NORMAL_TEXT);
		}		
	}
	else if (!strcmp((const char*) g_cCommand, "configType"))
	{
		sscanf(aStr, "%s %i", g_cCommand, &nArg1);
		if(nArg1 == 2 || nArg1 == 1 || nArg1 == 0)
		{
			nw[nwIdx].g_MainNodeCfg.mainNodeCfg = nArg1;
		}
		else
		{
			SetConsoleFont(RED_TEXT);
			ADI_UART_PRINT("\n\rInvalid command\n\r");
			SetConsoleFont(NORMAL_TEXT);
		}
	}
	else if (!strcmp((const char*) g_cCommand, "clear"))
	{
		ADI_UART_PRINT("\033[2J");   /* clear screen */
		ADI_UART_PRINT("\033[0;0H"); /* set cursor to 0,0 */
	}
	else if (!strcmp((const char*) g_cCommand, "pause"))
	{
		sscanf(aStr, "%s %i", g_cCommand, &nArg1);
		a2b_pnp_PauseNewNodeDisc(nw[nwIdx].hPnp);

	}
	else if (!strcmp((const char*) g_cCommand, "resume"))
	{
		/*nArg1 is the node to read the value from and nArg2 is the register addres*/
		sscanf(aStr, "%s %i", g_cCommand, &nArg1);
		a2b_pnp_ResumeNewNodeDisc(nw[nwIdx].hPnp);

	}
	else if (!strcmp((const char*) g_cCommand, "clearAllCon"))
	{
		sscanf(aStr, "%s %i", g_cCommand, &nArg1);
		a2b_pnp_ClearAllCon(nw[nwIdx].hPnp);
		nw[nwIdx].bApplyRequired = nw[nwIdx].enAutoApply;

	}
	else if (!strcmp((const char*) g_cCommand, "getNwInfo"))
	{
		/*nArg1 is the node to read the value from and nArg2 is the register addres*/
		sscanf(aStr, "%s %i", g_cCommand, &nArg1);
		a2b_pnp_GetNWInfo(nw[nwIdx].hPnp, &nw[nwIdx].A2BNetworkInfo);
		a2b_pnp_PrintNWInfo(nwIdx,true,false);  
		if(nw[nwIdx].bTestRegression)
		{
			printf("Not implemented");
			//int ret = a2bapp_verify_getNwInfo();
		}
	}
	else if (!strcmp((const char*) g_cCommand, "getNodeInfo"))
	{
		/*nArg1 is the node to read the value from and nArg2 is the register addres*/
		sscanf(aStr, "%s %i", g_cCommand, &nArg2);
		a2b_pnp_GetNWInfo(nw[nwIdx].hPnp, &nw[nwIdx].A2BNetworkInfo);
		if(nArg2 == -1)
		{
			print_main_node_info(nwIdx);
		}
		else
		{
			print_node_info(nwIdx, nArg2);
		}
	}
	else if (!strcmp((const char*) g_cCommand, "getNodePwrInfo"))
	{
		a2bpnp_DevicePwrInfo pwrInfo;
		sscanf(aStr, "%s %i", g_cCommand, &nArg2);
		a2b_pnp_GetDevicePwrInfo(nw[nwIdx].hPnp, nArg2, &pwrInfo);
		SetConsoleFont(NORMAL_TEXT);
		ADI_UART_PRINT("\n\r Maximum power budget : %d mW",  pwrInfo.maxPwr);
		ADI_UART_PRINT("\n\r Minimum power budget : %d mW\n\r",  pwrInfo.minPwr);
	}
	
	else if (!strcmp((const char*) g_cCommand, "addStream"))
	{
		if(nw[nwIdx].bStreamBycon)
		{
			SetConsoleFont(RED_TEXT);
			ADI_UART_PRINT("\n\r 'addStream' command is not enabled. Use 'addCon' command instead \n\r");
			SetConsoleFont(NORMAL_TEXT);
			return;
		}

		sscanf(aStr, "%s %s %s %s", g_cCommand, ArgS1, ArgS2, ArgS3);//ArgS1: Source, ArgS2: Destination, ArgS3: Name

		if(ArgS1[0] == 0x00 || ArgS2[0] == 0x00 || ArgS3[0] == 0x00)
		{
			SetConsoleFont(RED_TEXT);
			ADI_UART_PRINT("\n\rInvalid command\n\r");
			SetConsoleFont(NORMAL_TEXT);
		}
		else
		{
			a2b_pnp_GetNWInfo(nw[nwIdx].hPnp, &nw[nwIdx].A2BNetworkInfo);
			memset(&AudioStream, '\0', sizeof(AudioStream));
			/*nArg1 is the node to read the value from and nArg2 is the register addres*/

			token1 = GEN_STROK(ArgS1,":", &context);
			token2 = GEN_STROK(NULL, ":", &context);
			AudioStream.SourceNodeIdx = atoi(token1);
			AudioStream.srcRxGroup = atoi(token2);
			if( (AudioStream.SourceNodeIdx >= 0) && (AudioStream.SourceNodeIdx < 16))
			{
				AudioStream.nNumChannels = nw[nwIdx].A2BNetworkInfo->DevInfo[AudioStream.SourceNodeIdx].RxGroups[AudioStream.srcRxGroup].nGroupChannels;
			}
			else
			{
				AudioStream.SourceNodeIdx = -1;
				AudioStream.nNumChannels = nw[nwIdx].A2BNetworkInfo->MainNodeInfo.RxGroups[AudioStream.srcRxGroup].nGroupChannels;
			}

			rest = ArgS2;
			token[n++] = GEN_STROK(rest, ",", &rest);
			n--;
			while(token[n] != NULL)
			{
				token[++n] = GEN_STROK(NULL, ",", &rest);
			}
			while (--n >= 0)
			{
				token1 = GEN_STROK(token[n], ":", &context);
				token2 = GEN_STROK(NULL, "\n", &context);
				AudioStream.DestNodeIdx[nNode] = atoi(token1);
				AudioStream.destTxGroup[nNode] = atoi(token2);
				nNode++;
			}
			AudioStream.nDestNodes = nNode;
			nNode = 0u;
			AudioStream.streamIdx = streamIdx;
			memcpy(AudioStream.StreamName, ArgS3, strlen(ArgS3));
			streamIdx++;
			a2b_pnp_SetAudioRt(nw[nwIdx].hPnp, &AudioStream, 1u, true);
		}
	}
	else if (!strcmp((const char*) g_cCommand, "addCon"))
	{
		sscanf(aStr, "%s %s %s", g_cCommand, ArgS1, ArgS2 );
		token1 = GEN_STROK(ArgS1,":", &context);
		token2 = GEN_STROK(NULL, ":", &context);
		int srcNodeId = atoi(token1);
		int srcGroupNum = atoi(token2);

		token1 = GEN_STROK(ArgS2,":", &context);
		token2 = GEN_STROK(NULL, ":", &context);

		int dstNodeId = atoi(token1);
		int dstgroupNum = atoi(token2);

		if(nw[nwIdx].bStreamBycon == A2B_TRUE)
 	    {
			a2b_pnp_AddConByNodeId(nw[nwIdx].hPnp, srcNodeId, srcGroupNum, dstNodeId, dstgroupNum);
		}
		else{
			ADI_UART_PRINT("Creating Stream By Connection Not Enabled!");
		}

		nw[nwIdx].bApplyRequired = nw[nwIdx].enAutoApply;
	}
	else if (!strcmp((const char*) g_cCommand, "exportCon"))
	{
		sscanf(aStr, "%s", g_cCommand );

		if(nw[nwIdx].bStreamBycon == A2B_TRUE)
 	    {
			connPairByChipId NWCon[A2B_PNP_CFG_MAX_NUM_NW_CONNECTION] = {0};
			a2b_UInt8 streamCnt;
			if(!a2b_pnp_GetConnectionList(nw[nwIdx].hPnp, NWCon, &streamCnt))
			{
				prepareAudioConJSON(NWCon, streamCnt);
				bExported = A2B_TRUE;
				ADI_UART_PRINT("Exported to 'cfg/connection.json'!");
			}
			else
			{
				ADI_UART_PRINT("File Export Failed.");
			}
		}
		else{
			ADI_UART_PRINT("Creating Stream By Connection Not Enabled!");
		}

	}
	else if (!strcmp((const char*) g_cCommand, "getRouting"))
	{
		sscanf(aStr, "%s %i", g_cCommand, &nArg1 );
		a2b_pnp_GetNWInfo(nw[nwIdx].hPnp, &nw[nwIdx].A2BNetworkInfo);
		print_audio_routing(nwIdx);

	}
	/*Command to switch between I2C and SPI*/
	else if (!strcmp((const char*) g_cCommand, "addTunnel"))
	{
		sscanf(aStr, "%s %i", g_cCommand, &nArg1 );
		a2b_pnp_GetDefaultDTStream(nw[nwIdx].hPnp, &nw[nwIdx].DTStream, &num);

		nw[nwIdx].DTStream.nDtDnSlots = (nArg1 > 2)  ? nArg1 : 2;
		nw[nwIdx].DTStream.nDtUpSlots = (nArg1 > 2)  ? nArg1 : 2;
		a2b_pnp_SetSPIDTRt(nw[nwIdx].hPnp, &nw[nwIdx].DTStream, num, true);

	}
	else if (!strcmp((const char*) g_cCommand, "removeTunnel"))
	{
		sscanf(aStr, "%s %i", g_cCommand, &nArg1 );
		a2b_pnp_SetSPIDTRt(nw[nwIdx].hPnp, NULL, 0, true);
	}
	else if (!strcmp((const char*) g_cCommand, "autoApply"))
	{
		nw[nwIdx].enAutoApply = 1;
	}
	else if (!strcmp((const char*) g_cCommand, "clearStream"))
	{
		if(nw[nwIdx].bStreamBycon)
		{
			ADI_UART_PRINT("ClearStream option is not Enabled. Use 'clearAllcon' instead");
			return;
		}
		sscanf(aStr, "%s %i", g_cCommand, &nArg1 );
		a2b_pnp_SetAudioRt(nw[nwIdx].hPnp, NULL, 0, false);
		streamIdx = 0u;
	}
	else if (!strcmp((const char*) g_cCommand, "clearTunnel"))
	{
		sscanf(aStr, "%s %i", g_cCommand, &nArg1 );
		a2b_pnp_ClearTunnel(nw[nwIdx].hPnp, &nw[nwIdx].DTStream);
	}
	else if (!strcmp((const char*) g_cCommand, "applyRouting"))
	{
		sscanf(aStr, "%s %i", g_cCommand, &nArg1 );
		nw[nwIdx].bApplyRequired = 1;
	}
	else if (!strcmp((const char*) g_cCommand, "enDbg"))
	{
		sscanf(aStr, "%s %i", g_cCommand, &nArg1 );
	    nw[nwIdx].bEnableDebug  = 1;

	}
	else if (!strcmp((const char*) g_cCommand, "disDbg"))
	{
		sscanf(aStr, "%s %i", g_cCommand, &nArg1 );
	    nw[nwIdx].bEnableDebug  = 0;

	}
	else if (!strcmp((const char*) g_cCommand, "getBw"))
	{
		int nodeNum, per;
		sscanf(aStr, "%s %i", g_cCommand, &nArg1 );
		a2b_pnp_GetNwBW(nw[nwIdx].hPnp, (a2b_Int32*)&nodeNum, (a2b_UInt32*)&per);
		SetConsoleFont(GREEN_TEXT);
		ADI_UART_PRINT("\r\n");
		ADI_UART_PRINT("Network Bandwidth : %d%%,\nMax contributing node %d", per, nodeNum);
		ADI_UART_PRINT("\r\n");
		SetConsoleFont(NORMAL_TEXT);

	}
	else if (!strcmp((const char*) g_cCommand, "getDiscTime"))
	{
		int time;
		sscanf(aStr, "%s %i", g_cCommand, &nArg1);
		a2b_pnp_GetDiscTimeInfo(nw[nwIdx].hPnp, nArg1, (a2b_UInt32 *)&time );

		ADI_UART_PRINT("\r\n");
		ADI_UART_PRINT("Sub node discovery time(in ms) : %d ", time);
		ADI_UART_PRINT("\r\n");
	}
	else if (!strcmp((const char*) g_cCommand, "getVendorID"))
	{
		a2b_UInt8 vendorId[8u]; 
		sscanf(aStr, "%s %i", g_cCommand, &nArg1);
		a2b_pnp_GetVendorID(nw[nwIdx].hPnp, nArg1, vendorId);

		ADI_UART_PRINT("\r\n");
		ADI_UART_PRINT("Sub node %d vendor ID : %c%c%c%c%c%c%c%c ", nArg1, vendorId[0], vendorId[1], vendorId[2], vendorId[3], vendorId[4], vendorId[5], vendorId[6], vendorId[7]);
		ADI_UART_PRINT("\r\n");
	}
	else if (!strcmp((const char*) g_cCommand, "quit"))
	{
		ADI_UART_PRINT("\r\n");
		ADI_UART_PRINT("Exiting..");
		ADI_UART_PRINT("\r\n");
		ADI_UART_PRINT("Connection Export Required?:");
		if((nw[nwIdx].bStreamBycon == A2B_TRUE) && !bExported)
 	    {
			ADI_UART_PRINT("YES\r\n");
			connPairByChipId NWStream[A2B_PNP_CFG_MAX_NUM_NW_CONNECTION] = {0};
			a2b_UInt8 connectionCnt;
			if(!a2b_pnp_GetConnectionList(nw[nwIdx].hPnp, &NWStream[0], &connectionCnt))
			{
				prepareAudioConJSON(NWStream, connectionCnt);
				bExported = A2B_TRUE;
				ADI_UART_PRINT("Exported to 'cfg/connection.json'!");
			}
			else
			{
				ADI_UART_PRINT("File Export Failed.");
			}
		}
		else
		{
			ADI_UART_PRINT("NO\r\n");
		}
		exit(0);
	}
	else if (!strcmp((const char*) g_cCommand, "ver"))
	{
		a2b_UInt32 major,minor,patch; 
		ADI_UART_PRINT("\r\n");
		a2b_pnp_GetVersion( &major,	&minor, &patch);
		
		ADI_UART_PRINT("PnP Software Version: %d.%d.%d",major,minor,patch);
		ADI_UART_PRINT("\r\n");
	}
	else if (!strcmp((const char*) g_cCommand, "rdm"))
	{
		sscanf(aStr, "%s %i", g_cCommand, &nArg2 );
		a2b_pnp_ReadReg(nw[nwIdx].hPnp, -1, nArg2, &nArg1_u8 );
		ADI_UART_PRINT("\r\n");
		ADI_UART_PRINT("Main node reg value : 0x%x ", nArg1_u8);
		ADI_UART_PRINT("\r\n");

	}
	else if (!strcmp((const char*) g_cCommand, "rds"))
	{
		sscanf(aStr, "%s %i %i", g_cCommand, &nArg2, &nArg3 );
		a2b_pnp_ReadReg(nw[nwIdx].hPnp, nArg2, nArg3, &nArg1_u8 );
		ADI_UART_PRINT("\r\n");
		ADI_UART_PRINT("Sub node reg value : 0x%x ", nArg1_u8);
		ADI_UART_PRINT("\r\n");

	}
	else if (!strcmp((const char*) g_cCommand, "wrm"))
	{
		sscanf(aStr, "%s %i %i", g_cCommand, &nArg2, &nArg3);
		a2b_pnp_WriteReg(nw[nwIdx].hPnp, -1, nArg2, nArg3 );

	}
	else if (!strcmp((const char*) g_cCommand, "wrs"))
	{
		sscanf(aStr, "%s %i %i %i", g_cCommand, &nArg2, &nArg3, &nArg4 );
		a2b_pnp_WriteReg(nw[nwIdx].hPnp, nArg2, nArg3, nArg4 );

	}
	else if (!strcmp((const char*) g_cCommand, "dspControl"))
	{
		sscanf(aStr, "%s %i", g_cCommand, &nArg1);
		nw[nwIdx].bMixerDSPCntrl = nArg1;
	}
	else if (!strcmp((const char*) g_cCommand, "tsgpiod"))
	{
		setGpiod();
	}
	else if (!strcmp((const char*) g_cCommand, "tcgpiod"))
	{
		tearDownGpiod();
	}
	else if (!strcmp((const char*) g_cCommand, "tspie2Prom"))
	{
		testSpi();
	}
	/*Command to display supported commands*/
	else if ( (!strcmp((const char*) g_cCommand, "help")) || (!strcmp((const char*) g_cCommand, "?")))
	{
		cmd_DisplaySupportedCommands();
	}
	else
	{
		SetConsoleFont(RED_TEXT);
		ADI_UART_PRINT("\n\rInvalid command\n\r");
		SetConsoleFont(NORMAL_TEXT);
	}

}


/*Function to display the supported UART commands
 * This functions need to be updated manually when a new command is added
 **/
void cmd_DisplaySupportedCommands(void)
{

	ADI_UART_PRINT("\r\n help / ?                                                        Prints this list");
	ADI_UART_PRINT("\r\n addCon <source node: group> <dest node: group>            Adds new connection by position");
	ADI_UART_PRINT("\r\n addStream <source node: group> <dest node: group> <Name>                               Add Audio stream(Destinations can be comma seperated w/o spaces)");
	ADI_UART_PRINT("\r\n addTunnel <num slots>                                                   Add new tunnel");
	ADI_UART_PRINT("\r\n applyRouting                                                Apply new audio and tunnel Routing");
	ADI_UART_PRINT("\r\n clear                                                       Clears the display");
	ADI_UART_PRINT("\r\n clearStream                                                 Clears the streamlist");
	ADI_UART_PRINT("\r\n clearAllCon                                                Clears all the connections");
	ADI_UART_PRINT("\r\n clearTunnel                                                 Remove tunnel");
	ADI_UART_PRINT("\r\n configType <type>                                           Main node Configuration source. 0 - BCF, 1 - EEPROM, 2 - FILE");
	ADI_UART_PRINT("\r\n discInterval <value>                                        Update discovery interval value");
	ADI_UART_PRINT("\r\n disDbg                                                      Disable debug logs");
	ADI_UART_PRINT("\r\n enDbg                                                       Enable debug logs");
	ADI_UART_PRINT("\r\n filePath <filename>                                         Name of .dat file for configuration (optional)");
	ADI_UART_PRINT("\r\n getBw                                                       Get bandwidth of network");
	ADI_UART_PRINT("\r\n getDiscTime <nodeNum>                                       Discovery time of a node");
	ADI_UART_PRINT("\r\n getNwInfo                                                   Gets Network Info");    
	ADI_UART_PRINT("\r\n getNodeInfo <nodeNum>                                       Gets node level Info"); 
	ADI_UART_PRINT("\r\n getNodePwrInfo <nodeNum>									 Gets node lvel power Info");  
	ADI_UART_PRINT("\r\n getVendorID <nodeNum>                                       Get Vendor ID");
	ADI_UART_PRINT("\r\n globalMute <0/1>                                            Mute globally in case of stream reconfiguration");
	ADI_UART_PRINT("\r\n ignoreEEPROM <0/1>                                          Ignore reading of EEPROM");
	ADI_UART_PRINT("\r\n intrptPollInterval <value>                                  Update Interrupt Polling interval");
	ADI_UART_PRINT("\r\n maxNumNodes <value>                                         Update maximum number of sub nodes");
	ADI_UART_PRINT("\r\n pause                                                       Pauses PnP discovery");
	ADI_UART_PRINT("\r\n quit                                                        Quit this application");
	ADI_UART_PRINT("\r\n resume                                                      Resumes the PnP discovery");
	ADI_UART_PRINT("\r\n rdm <regAddr>                                               Reads the value from the specified A2B register (Main)");
	ADI_UART_PRINT("\r\n rds <nodeNum> <regAddr>                                     Reads the value from the specified A2B register (Sub)");
	ADI_UART_PRINT("\r\n setNetwork <nw ID>                                          Set the default network to execute further commands");
	ADI_UART_PRINT("\r\n start                                                       Starts the PnP application and network discovery");
	ADI_UART_PRINT("\r\n streamByConnection <0/1>                                    Enable creating streams by connections");
	ADI_UART_PRINT("\r\n updateEEPROM <filename> <nodeNum> <EEPROM Addr>             Update EEPROM of a node");
	ADI_UART_PRINT("\r\n updateEEPROMAll <filename> <EEPROM Addr>                    Update EEPROM of all the nodes");
	ADI_UART_PRINT("\r\n ver                                                         PnP Software Version");
	ADI_UART_PRINT("\r\n wrm <regAddr> <Val>                                         Writes the specified value into the given A2B register (Main)");
	ADI_UART_PRINT("\r\n wrs <nodeNum> <regAddr> <Val>                               Writes the specified value into the given A2B register (Sub)");
	ADI_UART_PRINT("\r\n xcvrBInv <main> <sub>                                       CONTROL.XCVRBINV for main/sub nodes: -1 bus config, 0 off, 1 on (before start)\r\n");

}

#endif

static void printUartBanner(void)
{
//    ADI_UART_PRINT("        #####  #### ########   \n\r");
//    ADI_UART_PRINT("       ###### ##  # #########  \n\r");
//    ADI_UART_PRINT("      ###  ##   ##  ##     ##  \n\r");
//    ADI_UART_PRINT("     ########  ##   ########   \n\r");
//    ADI_UART_PRINT("    #### #### ##### ##     ##  \n\r");
//    ADI_UART_PRINT("   ####  ####################  \n\r");
//    ADI_UART_PRINT("  ####   ###################   \n\r");

	SetConsoleFont(CYAN_TEXT);
	ADI_UART_PRINT("        #####");
	SetConsoleFont(GREEN_TEXT);
	ADI_UART_PRINT("  ####");
	SetConsoleFont(CYAN_TEXT);
	ADI_UART_PRINT(" ########   \n\r");

	SetConsoleFont(CYAN_TEXT);
	ADI_UART_PRINT("       ######");
	SetConsoleFont(GREEN_TEXT);
	ADI_UART_PRINT(" ##  #");
	SetConsoleFont(CYAN_TEXT);
	ADI_UART_PRINT(" #########  \n\r");

	SetConsoleFont(CYAN_TEXT);
	ADI_UART_PRINT("      ###  ##");
	SetConsoleFont(GREEN_TEXT);
	ADI_UART_PRINT("   ##");
	SetConsoleFont(CYAN_TEXT);
	ADI_UART_PRINT("  ##     ##  \n\r");

	SetConsoleFont(CYAN_TEXT);
	ADI_UART_PRINT("     ########");
	SetConsoleFont(GREEN_TEXT);
	ADI_UART_PRINT("  ##");
	SetConsoleFont(CYAN_TEXT);
	ADI_UART_PRINT("   ########   \n\r");

	SetConsoleFont(CYAN_TEXT);
	ADI_UART_PRINT("    #### ####");
	SetConsoleFont(GREEN_TEXT);
	ADI_UART_PRINT(" #####");
	SetConsoleFont(CYAN_TEXT);
	ADI_UART_PRINT(" ##     ##  \n\r");

	SetConsoleFont(CYAN_TEXT);
    ADI_UART_PRINT("   ####  ####################  \n\r");
    ADI_UART_PRINT("  ####   ###################   \n\r");
	SetConsoleFont(GREEN_TEXT);
}

void welcomeScreen()
{
	ADI_UART_PRINT("\033[2J");   /* clear screen */
	ADI_UART_PRINT("\033[0;0H"); /* set cursor to 0,0 */

	SetConsoleFont(GREEN_TEXT);
	printUartBanner();
	ADI_UART_PRINT("\n\r ===========================================================");
	ADI_UART_PRINT("\n\r |                                                         |");
	ADI_UART_PRINT("\n\r |           A2B Plug and Play Application                 |");
	ADI_UART_PRINT("\n\r |           Build Time : %s, %s            |", __DATE__, __TIME__);
	ADI_UART_PRINT("\n\r |                                                         |");
	ADI_UART_PRINT("\n\r ===========================================================");
	ADI_UART_PRINT("\n\r");
	ADI_UART_PRINT("\e[0m");

	/* Configure a2b system */
	ADI_UART_PRINT("\n\rA2B>");

}

void SetConsoleFont(CONSOLE_FONT eFont)
{
#ifdef ADI_UART_ENABLE
	switch(eFont)
	{
	case NORMAL_TEXT:
		ADI_UART_PRINT (_NORMALTEXT_);
		break;

	case BOLD_TEXT:
		ADI_UART_PRINT (_BOLDTEXT_);
		break;

	case UNDERLINE_TEXT:
		ADI_UART_PRINT (_UNDERLINETEXT_);
		break;

	case BLINK_TEXT:
		ADI_UART_PRINT (_BLINKTEXT_);
		break;

	case RED_TEXT:
		ADI_UART_PRINT (_IREDTEXT_);
		break;

	case GREEN_TEXT:
		ADI_UART_PRINT (_GREENTEXT_);
		break;

	case YELLOW_TEXT:
		ADI_UART_PRINT (_YELLOWTEXT_);
		break;

	case BLUE_TEXT:
		ADI_UART_PRINT (_BLUETEXT_);
		break;

	case MAGENTA_TEXT:
		ADI_UART_PRINT (_MAGENTATEXT_);
		break;

	case CYAN_TEXT:
		ADI_UART_PRINT (_CYANTEXT_);
		break;

	case WHITE_TEXT:
		ADI_UART_PRINT (_WHITETEXT_);
		break;

	case BLACK_TEXT:
		ADI_UART_PRINT (_BLACKTEXT_);
		break;

	default:
		ADI_UART_PRINT (_NORMALTEXT_);
		break;
	}
#endif
}

/**********************************TEST CODES  START ****************************************************/
a2bpnp_GpiodParam param0[6];
a2bpnp_GpiodParam param1[6];
uint8_t idx = 0;
void setGpiod(void)
{
	//Input virtual port 1
	param0[0].gpioNum = 2;
	param0[0].invertEnable = 0;
	param0[0].nodeId = 0;

	param1[0].gpioNum = 4;
	param1[0].invertEnable = 0;
	param1[0].nodeId = 0;


	//virtual port 2
	param1[1].gpioNum = 2;
	param1[1].invertEnable = 0;
	param1[1].nodeId = 1;


	param0[1].gpioNum = 4;
	param0[1].invertEnable = 0;
	param0[1].nodeId = 1;


	// a2b_pnp_SetupGPIOOverDistance(hPnp, 2, param0, 2);
	// a2b_pnp_SetupGPIOOverDistance(hPnp, 1, param1, 2);
}

void tearDownGpiod()
{
	// a2b_pnp_ClearGPIOOverDistance(hPnp, 2, param0, 2);
	// a2b_pnp_ClearGPIOOverDistance(hPnp, 1, param1, 2);

}

#define SPI_REMOTE_EEPROM_WR_EN_CMD					(0x06U)
void testSpi(void)
{

	a2b_UInt8 		anWBuf_Cmd[1] 				 = {SPI_REMOTE_EEPROM_WR_EN_CMD};
	a2b_UInt8       wrtBuf[5] = {0x02, 0x10, 0x00, 0xAD, 0xAB};
	a2b_UInt8		WrBufferForRead[3] = {0x03, 0x10, 0x00};
	a2b_UInt8       rdBuffer[2] = {0};
	a2b_SpiConfig cfg;

	cfg.eApiMode = A2B_API_BLOCKING;
	cfg.eSpiMode =  A2B_SPI_ATOMIC;
	cfg.pCbParam = A2B_NULL;
	cfg.pfStatCb = A2B_NULL;

	a2b_SpiWrRdParams rdWrParams;

	rdWrParams.nNode = 0;
	rdWrParams.nAddrWidth = 0;
	rdWrParams.nWriteBytes = 1;
	rdWrParams.nSlaveSel = 1;
	rdWrParams.nAddrOffset = 0;
	rdWrParams.nReadBytes = 0;
	rdWrParams.pRBuf = 0;
	rdWrParams.nAddrIncrement = 1;
	rdWrParams.pWBuf = anWBuf_Cmd;

	//Enable Write command
	// res = a2b_pnp_SPIDTWriteReadPeri(hPnp, &rdWrParams);

	//Write to EEPROM
	rdWrParams.pWBuf = wrtBuf;
	rdWrParams.nAddrWidth = 2;
	rdWrParams.nAddrOffset = 1;
	rdWrParams.nWriteBytes = 5;
	rdWrParams.nReadBytes = 0;
	rdWrParams.pRBuf = 0;
	// res |= a2b_pnp_SPIDTWriteReadPeri(hPnp, &rdWrParams);

	adi_a2b_Delay(500);
	//Read from EEPROM
	rdWrParams.pWBuf = WrBufferForRead;
	rdWrParams.nWriteBytes = 3;
	rdWrParams.nReadBytes = 2;
	rdWrParams.pRBuf = rdBuffer;
	// res |= a2b_pnp_SPIDTWriteReadPeri(hPnp, &rdWrParams);

	if( (rdBuffer[0] == 0xAD) && (rdBuffer[1] == 0xAB))
	{
		ADI_UART_PRINT("\n\rEEPROM Verified\n\r");
	}

}

/*************************TEST Code End******************************************************/

void a2b_pnp_PrintNWInfo(a2b_UInt8 nwIdx, uint8_t bPrintNWInfo, uint8_t bPrintAudioRt)
{
	uint8_t NodeIdx;

	/* Not set until a2b_pnp_GetNWInfo() succeeds, i.e. never if the stack failed to start */
	if(nw[nwIdx].A2BNetworkInfo == NULL)
	{
		ADI_UART_PRINT("\n\rNo network information: the A2B stack is not running.\n\r");
		return;
	}

	// just to keep screen less cluttered as we don't care about this now

	if(bPrintNWInfo)
	{

		//main node
		print_main_node_info(nwIdx);

		//SUB Nodes
		for(NodeIdx = 0; NodeIdx<nw[nwIdx].A2BNetworkInfo->nDeviceCount; NodeIdx++)
		{
			print_node_info(nwIdx, NodeIdx);
		}
	}

	SetConsoleFont(CYAN_TEXT);
	if(bPrintAudioRt)
	{
		print_audio_routing(nwIdx);
	}
	SetConsoleFont(NORMAL_TEXT);
}

void print_audio_routing(a2b_UInt8 nwIdx)
{
	uint8_t NodeIdx;
	uint8_t StreamIdx;

	ADI_UART_PRINT("\n\r******************Audio Routing Details****************\n\r");
	if(nw[nwIdx].A2BNetworkInfo->nStreamCount)
	{
		//ADI_UART_PRINT("Audio Stream Information\n\r");
		for(StreamIdx = 0; StreamIdx<nw[nwIdx].A2BNetworkInfo->nStreamCount; StreamIdx++)
		{
			ADI_UART_PRINT("\n\rStream %d:  Source Node: %d RxGroup: %d --> ", StreamIdx, nw[nwIdx].A2BNetworkInfo->NWStream[StreamIdx].SourceNodeIdx, nw[nwIdx].A2BNetworkInfo->NWStream[StreamIdx].srcRxGroup);
			ADI_UART_PRINT("Destination: ");
			for(NodeIdx=0; NodeIdx<nw[nwIdx].A2BNetworkInfo->NWStream[StreamIdx].nDestNodes; NodeIdx++)
			{
				ADI_UART_PRINT("Node:%d : Group:%d  ", nw[nwIdx].A2BNetworkInfo->NWStream[StreamIdx].DestNodeIdx[NodeIdx], nw[nwIdx].A2BNetworkInfo->NWStream[StreamIdx].destTxGroup[NodeIdx]);
			}
			ADI_UART_PRINT("\n\r");
		}
	}
	else
	{
		ADI_UART_PRINT("\n\rNo Audio Streams defined\n\r");
	}

}
void print_main_node_info(a2b_UInt8 nwIdx)
{
	char PrintStr[64] = {0};

	SetConsoleFont(UNDERLINE_TEXT);
	ADI_UART_PRINT("\n\n\rMain Node (-1)");
	ADI_UART_PRINT("Device Name: %s\n\r", nw[nwIdx].A2BNetworkInfo->MainNodeInfo.DeviceName);
	SetConsoleFont(NORMAL_TEXT);

	SetConsoleFont(CYAN_TEXT);
	ADI_UART_PRINT("\n\rData Tunnel: ");
	if(nw[nwIdx].A2BNetworkInfo->MainNodeInfo.dtEnabled>0)
	{
		SetConsoleFont(GREEN_TEXT);
		ADI_UART_PRINT("CAN BE USED\n\r");
	}
	else
	{
		SetConsoleFont(RED_TEXT);
		ADI_UART_PRINT("NOT REQUIRED\n\r");
	}
	SetConsoleFont(NORMAL_TEXT);

	SetConsoleFont(MAGENTA_TEXT);
	//Print Device Types
	ADI_UART_PRINT("\n\rSupported Device Type(s): ");
	for(uint8_t i=0; i<nw[nwIdx].A2BNetworkInfo->MainNodeInfo.nDeviceType; i++)
	{
		memset(PrintStr, '\0', sizeof(PrintStr));
		switch(nw[nwIdx].A2BNetworkInfo->MainNodeInfo.DeviceType[i])
		{
		case 0u:
			sprintf(PrintStr, " PDM MIC ");
			ADI_UART_PRINT("%s", PrintStr);
			break;
		case 1u:
			sprintf(PrintStr, " Analog MIC ");
			ADI_UART_PRINT("%s", PrintStr);
			break;
		case 2u:
			sprintf(PrintStr, " AUDIO OUT ");
			ADI_UART_PRINT("%s", PrintStr);
			break;
		case 3u:
			sprintf(PrintStr, " AUDIO IN ");
			ADI_UART_PRINT("%s", PrintStr);
			break;
		case 4u:
			sprintf(PrintStr, " MIDI ");
			ADI_UART_PRINT("%s", PrintStr);
			break;
		case 5u:
			sprintf(PrintStr, " SENSOR ");
			ADI_UART_PRINT("%s", PrintStr);
			break;
		default:
			break;
		}
	}
	SetConsoleFont(NORMAL_TEXT);

	//Print Tx Group details
	SetConsoleFont(YELLOW_TEXT);
	for(uint8_t i=0; i<nw[nwIdx].A2BNetworkInfo->MainNodeInfo.nTxGroups; i++)
	{
		ADI_UART_PRINT("\n\n\rTx Group %d Name: %s\n\r", nw[nwIdx].A2BNetworkInfo->MainNodeInfo.TxGroups[i].GroupID, nw[nwIdx].A2BNetworkInfo->MainNodeInfo.TxGroups[i].GroupName);
		ADI_UART_PRINT("\r	Group Channels: ");
		for(uint8_t j=0; j<nw[nwIdx].A2BNetworkInfo->MainNodeInfo.TxGroups[i].nGroupChannels; j++)
		{
			ADI_UART_PRINT("%d  ", nw[nwIdx].A2BNetworkInfo->MainNodeInfo.TxGroups[i].GroupChannels[j]);
		}
	}
	SetConsoleFont(NORMAL_TEXT);

	//Print Rx Group details
	SetConsoleFont(BLUE_TEXT);
	for(uint8_t i=0; i<nw[nwIdx].A2BNetworkInfo->MainNodeInfo.nRxGroups; i++)
	{
		ADI_UART_PRINT("\n\n\rRx Group %d Name: %s\n\r", nw[nwIdx].A2BNetworkInfo->MainNodeInfo.RxGroups[i].GroupID, nw[nwIdx].A2BNetworkInfo->MainNodeInfo.RxGroups[i].GroupName);
		ADI_UART_PRINT("\r	Group Channels: ");
		for(uint8_t j=0; j<nw[nwIdx].A2BNetworkInfo->MainNodeInfo.RxGroups[i].nGroupChannels; j++)
		{
			ADI_UART_PRINT("%d  ", nw[nwIdx].A2BNetworkInfo->MainNodeInfo.RxGroups[i].GroupChannels[j]);
		}
	}
	SetConsoleFont(NORMAL_TEXT);
}
void print_node_info(a2b_UInt8 nwIdx, a2b_UInt8 NodeIdx)
{
	char PrintStr[64] = {0};

	SetConsoleFont(UNDERLINE_TEXT);
	ADI_UART_PRINT("\n\n\rSub Node %d - ", NodeIdx);
	ADI_UART_PRINT("Device Name: %s\n\r", nw[nwIdx].A2BNetworkInfo->DevInfo[NodeIdx].DeviceName);
	SetConsoleFont(NORMAL_TEXT);

	SetConsoleFont(CYAN_TEXT);
	ADI_UART_PRINT("\n\rData Tunnel: ");
	if(nw[nwIdx].A2BNetworkInfo->DevInfo[NodeIdx].dtEnabled>0)
	{
		SetConsoleFont(GREEN_TEXT);
		ADI_UART_PRINT("CAN BE USED\n\r");
	}
	else
	{
		SetConsoleFont(RED_TEXT);
		ADI_UART_PRINT("NOT REQUIRED\n\r");
	}
	SetConsoleFont(NORMAL_TEXT);

	SetConsoleFont(MAGENTA_TEXT);
	//Print Device Types
	ADI_UART_PRINT("\n\rSupported Device Type(s): ");
	for(uint8_t i=0; i<nw[nwIdx].A2BNetworkInfo->DevInfo[NodeIdx].nDeviceType; i++)
	{
		memset(PrintStr, '\0',sizeof(PrintStr));
		switch(nw[nwIdx].A2BNetworkInfo->DevInfo[NodeIdx].DeviceType[i])
		{
		case 0u:
			sprintf(PrintStr, " PDM MIC ");
			ADI_UART_PRINT("%s", PrintStr);
			break;
		case 1u:
			sprintf(PrintStr, " Analog MIC ");
			ADI_UART_PRINT("%s", PrintStr);
			break;
		case 2u:
			sprintf(PrintStr, " AUDIO OUT ");
			ADI_UART_PRINT("%s", PrintStr);
			break;
		case 3u:
			sprintf(PrintStr, " AUDIO IN ");
			ADI_UART_PRINT("%s", PrintStr);
			break;
		case 4u:
			sprintf(PrintStr, " MIDI ");
			ADI_UART_PRINT("%s", PrintStr);
			break;
		case 5u:
			sprintf(PrintStr, " SENSOR ");
			ADI_UART_PRINT("%s", PrintStr);
			break;
		default:
			break;
		}
	}
	SetConsoleFont(NORMAL_TEXT);

	//Print Tx Group details
	SetConsoleFont(YELLOW_TEXT);
	for(uint8_t i=0; i<nw[nwIdx].A2BNetworkInfo->DevInfo[NodeIdx].nTxGroups; i++)
	{
		ADI_UART_PRINT("\n\n\rTx Group %d Name: %s\n\r", nw[nwIdx].A2BNetworkInfo->DevInfo[NodeIdx].TxGroups[i].GroupID, nw[nwIdx].A2BNetworkInfo->DevInfo[NodeIdx].TxGroups[i].GroupName);
		ADI_UART_PRINT("\r	Group Channels: ");
		for(uint8_t j=0; j<nw[nwIdx].A2BNetworkInfo->DevInfo[NodeIdx].TxGroups[i].nGroupChannels; j++)
		{
			ADI_UART_PRINT("%d  ", nw[nwIdx].A2BNetworkInfo->DevInfo[NodeIdx].TxGroups[i].GroupChannels[j]);
		}
	}
	SetConsoleFont(NORMAL_TEXT);

	//Print Rx Group details
	SetConsoleFont(BLUE_TEXT);
	for(uint8_t i=0; i<nw[nwIdx].A2BNetworkInfo->DevInfo[NodeIdx].nRxGroups; i++)
	{
		ADI_UART_PRINT("\n\n\rRx Group %d Name: %s\n\r", nw[nwIdx].A2BNetworkInfo->DevInfo[NodeIdx].RxGroups[i].GroupID, nw[nwIdx].A2BNetworkInfo->DevInfo[NodeIdx].RxGroups[i].GroupName);
		ADI_UART_PRINT("\r	Group Channels: ");
		for(uint8_t j=0; j<nw[nwIdx].A2BNetworkInfo->DevInfo[NodeIdx].RxGroups[i].nGroupChannels; j++)
		{
			ADI_UART_PRINT("%d  ", nw[nwIdx].A2BNetworkInfo->DevInfo[NodeIdx].RxGroups[i].GroupChannels[j]);
		}
	}
	SetConsoleFont(NORMAL_TEXT);

}

void a2b_printNodeRejection(a2bpnp_NodeRejectionInfo* pRejection)
{
	switch(pRejection->rejection)
	{
	case A2B_PNP_EXCEED_MAX_NODE_COUNT:
	ADI_UART_PRINT("\n\rNode Rejected: %d, %s\n\r", pRejection->NodeId, "Exceeded max node count");
	break;
	case A2B_PNP_INVALID_MODULE_HEADER:
	ADI_UART_PRINT("\n\rNode Rejected: %d, %s\n\r", pRejection->NodeId, "No or invalid module info");
	break;
	case A2B_PNP_INVALID_VENDOR_ID :
	ADI_UART_PRINT("\n\rNode Rejected: %d, %s\n\r", pRejection->NodeId, "Invalid vendor ID");
	break;
	case A2B_PNP_EXCEED_PWR_BUDGET:
	ADI_UART_PRINT("\n\rNode Rejected: %d, %s\n\r", pRejection->NodeId, "Exceeded power budget");
	break;
	case A2B_PNP_EXCEED_BW_BUDGET:
	case A2B_PNP_VENDOR_SPECIFIC:
	default:
	ADI_UART_PRINT("\n\rNode Rejected: %d, %s\n\r", pRejection->NodeId, "Misc");
	}
	

}



