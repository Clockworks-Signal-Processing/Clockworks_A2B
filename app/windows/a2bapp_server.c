/*******************************************************************************
Copyright (c) 2023 - Analog Devices Inc. All Rights Reserved.
See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
*******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <windows.h>
#include <string.h>
#include "adi_a2b_externs.h"
#include "cmd_platform.h"
#include "cmd_queue.h"
#include <a2bpnp.h>
#include "a2bapp_defs.h"
#include "a2bapp_common.h"

#define MULTI_MAIN_APP_ENABLE  0

#define PIPE_NAME_READ "\\\\.\\pipe\\client_server_pipe"
#define PIPE_NAME_WRITE "\\\\.\\pipe\\server_client_pipe"
void printUartBanner(void);
#define BUFFER_SIZE 100
char initFileFull[100] = "..\\..\\..\\cfg\\";

void ExecCmd(void);
extern char aStr[ADI_TERMINAL_CMD_SIZE];
typedef struct {
    HANDLE pipe;
} PipeThreadArgs;

/* Set by the read thread when aStr holds a command, cleared by the PnP thread once it
 * has run it. volatile: the two threads poll it. */
volatile a2b_Bool enable_flag = 0;
a2b_Bool waitflag;
char buffer[BUFFER_SIZE];
extern char* initFileName;
extern a2b_Bool bi2cLog;

void getCmdsFromFile(char* fileName)
{
	FILE * fp;
    size_t len = 0;
    ssize_t read;
    char g_cCommand[50u] = {0};
    char g_cMsg[200u];
    int nArg1;

    fp = fopen(fileName, "r");
    if (fp == NULL)
    {
        return;
    }

    while ((fgets(buffer, BUFFER_SIZE, fp)) != NULL) {
            sscanf(buffer, "%s", g_cCommand);
            if(buffer[0] == '%')
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
                Sleep(nArg1);
            }
            else if (!strcmp((const char*) g_cCommand, "deepDbg"))
            {   
                //enable deep debug
                bi2cLog = 1;
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
                int bytesRead = strlen(buffer);
                memcpy(aStr,buffer,bytesRead );
                aStr[bytesRead] = '\0';

                printf("\nA2B> %s", aStr);

                enable_flag = 1;
                while(enable_flag == 1)
                {
                    Sleep(1);
                }
            }

    }
}


//Accepts the commands from user & executes
DWORD WINAPI read_thread(LPVOID lpParam) {
    PipeThreadArgs* thread_args = (PipeThreadArgs*)lpParam;
  
     if(initFileName == A2B_NULL)
     {
        getCmdsFromFile("..\\..\\..\\cfg\\ini_EVAL_RJ45.txt");
     }
     else
     {
        strcat(initFileFull, initFileName);
        getCmdsFromFile(initFileFull);
     }

    while (1) {
        memset(buffer, 0, BUFFER_SIZE);

       // Get user input
        printf("A2B> ");
        fgets(buffer, BUFFER_SIZE, stdin);
        buffer[strcspn(buffer, "\n")] = '\0';  // Remove trailing newline

        if( strlen(buffer) == 0)
        {
            continue;
        }
        int bytesRead = strlen(buffer);
        memcpy(aStr,buffer,bytesRead );
        aStr[bytesRead] = '\0';
        if(aStr[0] != '\0')
        {
            enable_flag = 1;

            while(enable_flag == 1)
            {
                Sleep(1);
            }
        }
    }

    return 0;
}

//Thread for PnP execution. State based representation.
DWORD WINAPI pnp_thread(LPVOID lpParam) {
    PipeThreadArgs* thread_args = (PipeThreadArgs*)lpParam;

    while (1) {
        		app_state_process(0);
                if(enable_flag == 1)
                {
                    ExecCmd();
                    enable_flag = 0;
                }
                
#if A2B_CONF_MAX_NUM_MASTER_NODES > 1
                app_state_process(1);
#endif
		/*-----------------------------------------------------------*/
		/* Add your other continuous monitoring application code here */
		/*-----------------------------------------------------------*/

		/* (DEBUG purpose) set eEnableDisc to resume the paused discovery after node rejection  */

        /* Don't hold a CPU core at 100% (1 ms with timeBeginPeriod(1), see a2bServer_main) */
        Sleep(1);
    }

    return 0;
}
//Reads the information & puts to the console
DWORD WINAPI write_thread(LPVOID lpParam) {
    PipeThreadArgs* thread_args = (PipeThreadArgs*)lpParam;
    char *buffer;

    while (1) {
        
        // Write data to the named pipe
        DWORD bytesWritten;
 
        //Deque the message and send
       if( adi_terminal_TxDequeue((uint8_t **)&buffer, (uint32_t*)&bytesWritten) )
       {
            if(bytesWritten != 0)
            {
               printf("%s", buffer);
            }
       }
       else
       {
            Sleep(1);
       }

    }

    return 0;
}
//Creates threads adn starts the execution
int a2bServer_main() {
    PipeThreadArgs thread_args;
    HANDLE read_thread_handle, write_thread_handle, pnp_task_thread_handle;

    app_default_inits();

    /* 1 ms scheduler resolution, so the threads' Sleep(1) is 1 ms rather than ~15 ms */
    timeBeginPeriod(1);

    // Set up arguments for read thread
    write_thread_handle = CreateThread(NULL, 0, write_thread, (LPVOID)&thread_args, 0, NULL);

    // Create separate threads for reading and writing to the client
    read_thread_handle = CreateThread(NULL, 0, read_thread, (LPVOID)&thread_args, 0, NULL);    

    pnp_task_thread_handle = CreateThread(NULL, 0, pnp_thread, (LPVOID)&thread_args, 0, NULL);

    welcomeScreen();

    WaitForSingleObject(read_thread_handle, INFINITE);
    WaitForSingleObject(pnp_task_thread_handle, INFINITE);
    WaitForSingleObject(write_thread_handle, INFINITE);

    CloseHandle(read_thread_handle);
    CloseHandle(pnp_task_thread_handle);
    CloseHandle(write_thread_handle);
    timeEndPeriod(1);

    return 0;
}
