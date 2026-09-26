/*******************************************************************************
Copyright (c) 2022 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
*******************************************************************************

   Name       : a2bapp_rp2040.c

   Description: This file is responsible for handling all the application level
                 functions

   Developed by: Consumer system application team, Bangalore, India

******************************************************************************/
/*============= I N C L U D E S =============*/


#include "adi_a2b_externs.h"
#include "assert.h"
#include <a2bpnp.h>
#include "a2bapp_defs.h"
#include "a2bapp_common.h"
#include <stdint.h>
#include "cmd_platform.h"
#include "cmd_queue.h"
#include "pico/malloc.h"
#include <string.h>
#include "pico/stdlib.h"
#include "stdio.h"

/*============= D E F I N E S =============*/

/*============= D A T A =============*/

#define BUFFER_SIZE 100
#define NUM_CMDS  15
extern char aStr[ADI_TERMINAL_CMD_SIZE];
char buffer[BUFFER_SIZE];

const char cmds[NUM_CMDS][40] =
{
"enDbg",   
"configType 0", 
"maxNumNodes 16",
"streamByConnection 1", 
"globalMute 0", 
"ignoreEEPROM 0",
"autoApply 1",
"xcvrBInv 0 -1",
"start",
"getNwInfo", 
'\0'
};

// not in any .h file???????
extern void ExecCmd(void);

/*============= C O D E =============*/
void platform_init();
a2b_UInt8 InterruptCallback(A2B_PNP_HANDLE hPnp, uint32_t Event, void *pArg);
a2b_UInt8 PnPAppCallback (A2B_PNP_HANDLE hPnp, uint32_t Event, void *pArg);
a2b_UInt8 ErrorCallback(A2B_PNP_HANDLE hPnp, uint32_t Event, void *pArg);
void a2b_pnp_PrintNWInfo(a2b_UInt8 nwIdx, uint8_t bPrintNWInfo, uint8_t bPrintAudioRt);
char* custom_fgets(char* buffer, int size, const char* source); 
static void prvSetupHardware( void );
void getSubNodeCount(uint8_t *nNode);
void getCmdsFromFile(void);
/** 
 * If you want to use command program arguments, then place them in the following string. 
 */
char __argv_string[] = "";
int usb_drive_sel();
int a2bServer_main();

int main()
{
    static uint8_t nSubNode = 0;
    uint8_t nNode = 0;
	a2b_UInt32 nResult = 0;

    //while(1);

     /* Open UART0 */
	//open_UART();
	stdio_init_all();
 
    sleep_ms(1000); // wonder what this is for?

    // Redirect standard input/output to USB VCP
    //stdio_set_translate_crlf(&stdio_usb, 0);
    //stdio_set_usb_output(true);
    welcomeScreen();
    platform_init();

    gpio_init(DEBUG_IO_1);
    gpio_set_dir(DEBUG_IO_1, GPIO_OUT);  // output
    gpio_put(DEBUG_IO_1, false); 
    
    // reset the AD2437 to clear out any odd state (someday the stack should do this) GP2 pin  AD2437_RESET_n
    gpio_init(AD2437_RESET_n);
    gpio_set_dir(AD2437_RESET_n, GPIO_OUT);  // output
    gpio_put(AD2437_RESET_n, false); 
    sleep_ms(100);
    gpio_put(AD2437_RESET_n, true); 
    app_default_inits();
    getCmdsFromFile();

   
	while(1)
	{
		app_state_process(0);
        
        getSubNodeCount(&nNode);

        if(nNode > nSubNode)
        {
            //a2b_pnp_AddConByNodeId(nw[nwIdx].hPnp, nNode - 1, 0, -1, nNode-1/*group same as nodeaddr*/);
            //a2b_pnp_AddConByNodeId(nw[nwIdx].hPnp, -1, nNode-1, nNode - 1, 0 /*group same as nodeaddr*/);
            a2b_pnp_AddConByNodeId(nw[nwIdx].hPnp, -1, 0, nNode - 1, 0 );

            // Enabling apply for safety
            nw[nwIdx].bApplyRequired = 1;
            nSubNode = nNode; 

            // a2b_pnp_WriteReg(nw[nwIdx].hPnp, -1, 0x53, 0x10);
            if (nNode  == 12) // just print this once to avoid clutter
            {
                a2b_pnp_GetNWInfo(nw[nwIdx].hPnp, &nw[nwIdx].A2BNetworkInfo);
                a2b_pnp_PrintNWInfo(nwIdx,true,true);
            }            
        }
        else if(nNode < nSubNode)
        {
            nSubNode = nNode; 
        }

		//add console execution

	}

    return 0;
}

void getSubNodeCount(uint8_t *nNode)
{
    a2b_pnp_GetNWInfo(nw[nwIdx].hPnp, &nw[nwIdx].A2BNetworkInfo);
    if (nw[nwIdx].A2BNetworkInfo)   /* NULL if the stack failed to start */
    {
        *nNode = nw[nwIdx].A2BNetworkInfo->nDeviceCount;
    }
}

void getCmdsFromFile(void)
{
    size_t len = 0;
    ssize_t read;
    char g_cCommand[50u] = {0};
    char g_cMsg[200u];
    int nArg1,numCommands = 0 ;

    while (numCommands < NUM_CMDS) {
        app_state_process(0);
        
            sscanf(cmds[numCommands], "%s", g_cCommand);
            memcpy(buffer, cmds[numCommands], BUFFER_SIZE);
            if(cmds[numCommands][0] == '%')
            {
                //ignore the command
            }
	        else if (!strcmp((const char*) g_cCommand, "wait"))
            {
                //Wait for user to enter the command
                fgets(buffer, BUFFER_SIZE, stdin);
            }
            else if (!strcmp((const char*) g_cCommand, "sleep"))
            {
                sscanf(buffer, "%s %i", g_cCommand, &nArg1);
                //Sleep(nArg1);
            }
            else if (!strcmp((const char*) g_cCommand, "deepDbg"))
            {   

            }
            else if (!strcmp((const char*) g_cCommand, "print"))
            {
                sscanf(buffer, "%s %199[^\n]", g_cCommand, g_cMsg);
                SetConsoleFont(BOLD_TEXT);
                SetConsoleFont(CYAN_TEXT);
                ADI_UART_PRINT("\n\r%s\n\r", g_cMsg);
                SetConsoleFont(NORMAL_TEXT);
            }
            else if((buffer[0] != '\0') && (buffer[0] != '\n'))
            {
                memcpy(aStr,buffer,BUFFER_SIZE );
                aStr[BUFFER_SIZE-1] = '\0';

                printf("\ncommand> %s", aStr);
                ExecCmd();   // have to chase down what this does
                
            }
            else if((buffer[0] == '\0') || (buffer[0] == '\n'))
            {
                break;
            }
            numCommands++;

    }
}

char* custom_fgets(char* buffer, int size, const char* source) {
    if (buffer == NULL || size <= 0 || source == NULL) {
        return NULL; // Invalid arguments
    }

    int i = 0;

    while (i < size - 1 && source[i] != '\0' && source[i] != '\n') {
        buffer[i] = source[i];
        i++;
    }

    buffer[i] = '\0'; // Null-terminate the string

    if (i > 0 || source[i] == '\n') {
        return buffer; // Successfully read a line
    } else {
        return NULL; // No characters read (end of string)
    }
}


