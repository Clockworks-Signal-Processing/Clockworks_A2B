/*******************************************************************************
 * cmd_platform.h — Linux console platform definitions for A2B PnP app
 * ClockWorks Signal Processing LLC
 *
 * Linux/Raspberry Pi Zero 2W equivalent of app/RP2040/cmd_platform.h.
 *
 * On the Pico, PAL_PRINTF enqueues into a UART TX FIFO (adi_terminal_TxEnqueue).
 * On Linux, output goes directly to stdout — no FIFO needed.
 ******************************************************************************/

#ifndef __CMD_PLATFORM_H_
#define __CMD_PLATFORM_H_

#include <stdio.h>
#include <stdbool.h>

/* Route terminal output to stdout on Linux */
#define PAL_PRINTF(x)       fputs((const char *)(x), stdout)

/* Must match UART_BUFFER_SIZE expected by cmd_queue.c terminal_printf() */
#define UART_BUFFER_SIZE    (1024u)

#endif /* __CMD_PLATFORM_H_ */
