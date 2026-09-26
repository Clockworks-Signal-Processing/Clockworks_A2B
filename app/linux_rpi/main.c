/*******************************************************************************
 * main.c — Linux/Raspberry Pi Zero 2W A2B PnP Application Entry Point
 * ClockWorks Signal Processing LLC
 *
 * Linux equivalent of app/RP2040/a2bapp_rp2040.c.
 *
 * Startup sequence:
 *   1. Print banner
 *   2. adi_a2b_SystemInit()      — stub (nothing to do on Linux)
 *   3. a2b_pal_gpio_init()       — open /dev/gpiochip0, request RESETn + IRQ
 *   4. a2b_pal_gpio_reset_ad2437() — pulse RESETn low 100 ms → release
 *   5. app_default_inits()       — set poll intervals, max sub-nodes, etc.
 *   6. getCmdsFromFile()         — issue startup commands through cmd_parse
 *   7. Main loop                 — app_state_process() + sub-node tracking
 *
 * BCF file path:
 *   Only used with "configType 2" (bus config from a file).  The default
 *   startup commands use "configType 0", the compiled-in bus configuration.
 *
 * GPIO pins (reference board, rev A): IRQ = GPIO13; RESETn is not
 * wired to the Pi.  See RESET_N_GPIO_PIN / IRQ_GPIO_PIN in pal_gpio.c.
 *
 * Command-line options (all optional; defaults are the compile-time values):
 *   -d <i2c-dev>     I2C device            (default /dev/i2c-1)
 *   -c <gpiochip>    GPIO chip             (default /dev/gpiochip0)
 *   -r <pin>         AD2437 RESETn line    (default: RESET_N_GPIO_PIN)
 *   -i <pin>         AD2437 IRQ line       (default: IRQ_GPIO_PIN)
 *   -h               usage
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include "adi_a2b_externs.h"
#include <a2bpnp.h>
#include "a2bapp_defs.h"
#include "a2bapp_common.h"
#include "cmd_queue.h"
#include "pal_gpio.h"
#include "pal_i2c.h"

/*---------------------------------------------------------------------------
 * BCF file path
 *
 * Used only with "configType 2".  Relative to the directory a2b_pnp is run
 * from; change to an absolute path (or use the "filePath" command) when
 * running on the Pi.
 *---------------------------------------------------------------------------*/
#define BCF_FILE_PATH   "../../../cfg/adi_a2b_system_autoconfig_RJ45.dat"

/*---------------------------------------------------------------------------
 * Startup command table
 *
 * Identical to the Pico version in a2bapp_rp2040.c.  Commands are processed
 * by ExecCmd() (cmd_parse.c) while app_state_process() runs in the background.
 *---------------------------------------------------------------------------*/
#define NUM_CMDS    9

static const char cmds[NUM_CMDS][40] =
{
    "enDbg",
    "configType 0",
    "maxNumNodes 16",
    "streamByConnection 1",
    "globalMute 0",
    "ignoreEEPROM 0",
    "autoApply 1",
    "start",
    "getNwInfo",
};

/*---------------------------------------------------------------------------
 * Forward declarations
 *---------------------------------------------------------------------------*/
static void getCmdsFromFile(void);
static void getSubNodeCount(uint8_t *nNode);
static int  parseArgs(int argc, char **argv, a2b_pal_gpio_cfg_t *gpio_cfg);

/* ExecCmd is defined in cmd_parse.c; no public header declares it */
extern void ExecCmd(void);

/* aStr is the global command buffer in cmd_parse.c */
extern char aStr[ADI_TERMINAL_CMD_SIZE];

/* nwIdx is the active network index, defined in a2bapp_common.c */
extern uint8_t nwIdx;

/*===========================================================================
 * main
 *===========================================================================*/
int main(int argc, char **argv)
{
    static uint8_t nSubNode = 0;
    uint8_t nNode = 0;
    a2b_pal_gpio_cfg_t gpio_cfg;

    /* Command line: device paths and pins (I2C device is applied inside) */
    a2b_pal_gpio_get_defaults(&gpio_cfg);
    if (parseArgs(argc, argv, &gpio_cfg) != 0) {
        return 2;
    }

    /* Banner */
    printf("\n\rA2B PnP Stack — Linux/RPi Zero 2W port\n\r");
    printf("ClockWorks Signal Processing LLC\n\r\n\r");

    /* System initialisation (prints "a2b: system init" on Linux) */
    adi_a2b_SystemInit();

    /* GPIO — open the chip, request RESETn + IRQ lines if assigned
     * Defaults are in pal_gpio.c (RESET_N_GPIO_PIN / IRQ_GPIO_PIN); override
     * with -r / -i.  pal_gpio.c prints the specific cause on failure. */
    if (a2b_pal_gpio_init(&gpio_cfg) < 0) {
        return 1;
    }

    /* Reset the AD2437 to clear any latched state.
     * Pico equivalent: gpio_put(AD2437_RESET_n, false); sleep_ms(100);
     *                  gpio_put(AD2437_RESET_n, true); */
    a2b_pal_gpio_reset_ad2437();

    /* Set the BCF file path for this build before the state machine runs.
     * app_state_process() overwrites NULL with the Pico Windows path, so
     * set explicitly for Linux. */
    nw[0].g_MainNodeCfg.pFilePath = (uint8_t *)BCF_FILE_PATH;

    /* Initialise app defaults: poll interval, max sub-nodes, etc. */
    app_default_inits();

    /* Issue startup commands through the command parser */
    getCmdsFromFile();

    /* -----------------------------------------------------------------------
     * Main loop
     *
     * Sleeps 1 ms per pass so it doesn't spin a CPU core at 100% (the RP2040
     * loop can spin; a Linux process shouldn't).  The stack's own timing is
     * coarser (A2B_APP_DEFAULT_INTERRUPT_QUERY_INTERVAL is 20 ms).
     * ----------------------------------------------------------------------- */
    while (1)
    {
        static const struct timespec loop_pause = { 0, 1000000L };   /* 1 ms */

        nanosleep(&loop_pause, NULL);
        app_state_process(0);

        getSubNodeCount(&nNode);

        if (nNode > nSubNode)
        {
            /* New sub-node discovered — add it to the default routing group */
            a2b_pnp_AddConByNodeId(nw[nwIdx].hPnp, -1, 0, nNode - 1, 0);

            nw[nwIdx].bApplyRequired = 1;
            nSubNode = nNode;

            if (nNode == 12)
            {
                /* Print network info once when the expected chain is complete */
                a2b_pnp_GetNWInfo(nw[nwIdx].hPnp, &nw[nwIdx].A2BNetworkInfo);
                a2b_pnp_PrintNWInfo(nwIdx, true, true);
            }
        }
        else if (nNode < nSubNode)
        {
            /* Sub-node count decreased — bus drop or re-discovery */
            nSubNode = nNode;
        }
    }

    /* Unreachable — included for completeness */
    a2b_pal_gpio_shutdown();
    return 0;
}

/*===========================================================================
 * getCmdsFromFile
 *
 * Iterates through the cmds[] startup table, processing each command through
 * ExecCmd() while calling app_state_process() so the stack can run between
 * commands.
 *
 * Equivalent to getCmdsFromFile() in a2bapp_rp2040.c, minus the Pico SDK
 * dependencies.
 *===========================================================================*/
static void getCmdsFromFile(void)
{
    char g_cCommand[50u] = {0};
    static char buffer[100];
    int numCommands = 0;

    while (numCommands < NUM_CMDS)
    {
        app_state_process(0);

        sscanf(cmds[numCommands], "%49s", g_cCommand);
        memcpy(buffer, cmds[numCommands], sizeof(buffer) - 1u);
        buffer[sizeof(buffer) - 1u] = '\0';

        if (buffer[0] == '%')
        {
            /* Comment — ignore */
        }
        else if (!strcmp(g_cCommand, "wait"))
        {
            fgets(buffer, (int)sizeof(buffer), stdin);
        }
        else if (!strcmp(g_cCommand, "sleep"))
        {
            /* Sleep N ms — not used in current startup table */
            int nArg1 = 0;
            struct timespec ts;
            sscanf(buffer, "%49s %i", g_cCommand, &nArg1);
            ts.tv_sec  = nArg1 / 1000;
            ts.tv_nsec = (long)(nArg1 % 1000) * 1000000L;
            nanosleep(&ts, NULL);
        }
        else if ((buffer[0] != '\0') && (buffer[0] != '\n'))
        {
            memcpy(aStr, buffer, sizeof(buffer));
            aStr[ADI_TERMINAL_CMD_SIZE - 1] = '\0';

            printf("\ncommand> %s", aStr);
            ExecCmd();
        }
        else if ((buffer[0] == '\0') || (buffer[0] == '\n'))
        {
            break;
        }

        numCommands++;
    }
}

/*===========================================================================
 * getSubNodeCount
 *
 * Returns the current number of discovered sub-nodes via the PnP API.
 *===========================================================================*/
static void getSubNodeCount(uint8_t *nNode)
{
    a2b_pnp_GetNWInfo(nw[nwIdx].hPnp, &nw[nwIdx].A2BNetworkInfo);
    if (nw[nwIdx].A2BNetworkInfo)
    {
        *nNode = nw[nwIdx].A2BNetworkInfo->nDeviceCount;
    }
}

/*===========================================================================
 * usage / parsePin / parseArgs
 *
 * Command-line overrides for the device paths and pin numbers, so another
 * board or wiring doesn't need a rebuild.  The I2C device is handed to
 * pal_i2c.c here; GPIO settings are returned in gpio_cfg.
 *
 * Returns 0 to continue, non-zero to exit (bad option, or -h).
 *===========================================================================*/
static void usage(const char *prog)
{
    fprintf(stderr,
        "Usage: %s [-d i2c-dev] [-c gpiochip] [-r reset-pin] [-i irq-pin]\n"
        "  -d  I2C device               (default /dev/i2c-1)\n"
        "  -c  GPIO chip                (default /dev/gpiochip0)\n"
        "  -r  AD2437 RESETn line, -1 = not connected (default: pal_gpio.c)\n"
        "  -i  AD2437 IRQ line,    -1 = not connected (default: pal_gpio.c)\n",
        prog);
}

static int parsePin(const char *arg, int *pin)
{
    char *end;
    long v = strtol(arg, &end, 0);

    if (*arg == '\0' || *end != '\0' || v < A2B_GPIO_PIN_UNASSIGNED || v > 1023)
    {
        return -1;
    }
    *pin = (int)v;
    return 0;
}

static int parseArgs(int argc, char **argv, a2b_pal_gpio_cfg_t *gpio_cfg)
{
    int opt;

    while ((opt = getopt(argc, argv, "d:c:r:i:h")) != -1)
    {
        switch (opt)
        {
        case 'd':
            a2b_pal_i2c_set_device(optarg);
            break;
        case 'c':
            gpio_cfg->chip_path = optarg;
            break;
        case 'r':
            if (parsePin(optarg, &gpio_cfg->reset_n_pin) != 0)
            {
                fprintf(stderr, "%s: bad RESETn pin '%s'\n", argv[0], optarg);
                usage(argv[0]);
                return -1;
            }
            break;
        case 'i':
            if (parsePin(optarg, &gpio_cfg->irq_pin) != 0)
            {
                fprintf(stderr, "%s: bad IRQ pin '%s'\n", argv[0], optarg);
                usage(argv[0]);
                return -1;
            }
            break;
        case 'h':
        default:
            usage(argv[0]);
            return -1;
        }
    }
    if (optind < argc)
    {
        fprintf(stderr, "%s: unexpected argument '%s'\n", argv[0], argv[optind]);
        usage(argv[0]);
        return -1;
    }
    return 0;
}
