/*******************************************************************************
Copyright (c) 2022 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
*******************************************************************************

   Name       : a2bapp_win32.c

   Description: This file is responsible for handling all the application level
                 functions

   Functions  : main()
                PnPAppCallback()

   Developed by: Automotive Software and Systems team, Bangalore, India

******************************************************************************/
/*============= I N C L U D E S =============*/

#include "a2bapp_win32.h"
#include "adi_a2b_externs.h"
#include "assert.h"
#include <a2bpnp.h>
#include "a2bapp_defs.h"
#include "a2bapp_common.h"
#include <conio.h>
#include <stdint.h>
#include "cmd_platform.h"
#include "cmd_queue.h"
#include <windows.h>

/*============= D E F I N E S =============*/

/*============= D A T A =============*/


/*============= C O D E =============*/
a2b_UInt8 InterruptCallback(A2B_PNP_HANDLE hPnp, uint32_t Event, void *pArg);
a2b_UInt8 PnPAppCallback (A2B_PNP_HANDLE hPnp, uint32_t Event, void *pArg);
a2b_UInt8 ErrorCallback(A2B_PNP_HANDLE hPnp, uint32_t Event, void *pArg);
void a2b_pnp_PrintNWInfo(a2b_UInt8 nwIdx, uint8_t bPrintNWInfo, uint8_t bPrintAudioRt);

/** 
 * If you want to use command program arguments, then place them in the following string. 
 */
int usb_drive_sel();
int a2bServer_main();
char* initFileName = A2B_NULL;

int main(int argc, char *argv[])
{

	a2b_UInt32 nResult = 0;

    if(argc > 1)
    {
      initFileName  = argv[1];
    }

   /* Ensuring the terminal is color coded */
	HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD dwModeOut = 0;
    GetConsoleMode(hConsole, &dwModeOut);
    dwModeOut |= ENABLE_VIRTUAL_TERMINAL_PROCESSING| ENABLE_PROCESSED_OUTPUT ;
    SetConsoleMode(hConsole, dwModeOut);

	HANDLE hConsoleIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD dwModeIn = 0;
    GetConsoleMode(hConsoleIn, &dwModeIn);
    dwModeIn |= ENABLE_VIRTUAL_TERMINAL_INPUT ;
    SetConsoleMode(hConsoleIn, dwModeIn);

    /* Code to switch driver to lib USB, as of now not used*/
	//usb_drive_sel();
	//driver_switch();

    //Initialize the queue 
	adi_terminal_InitTxQueue();

	//start the thread
	a2bServer_main();

	while(1)
	{
     
	  /* control should not come here !*/

	}

    return 0;
}

