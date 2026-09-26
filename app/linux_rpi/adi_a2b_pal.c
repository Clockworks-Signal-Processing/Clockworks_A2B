/*******************************************************************************
 * adi_a2b_pal.c — Linux PAL Initialisation for A2B Stack
 * ClockWorks Signal Processing LLC
 *
 * Linux/Raspberry Pi Zero 2W equivalent of app/RP2040/a2bstack-pal/adi_a2b_pal.c.
 *
 * Provides:
 *   a2b_palInit()           — wires the a2b_StackPal function pointer table
 *   adi_a2b_palInit()       — alias for source compatibility with app/common/
 *   adi_a2b_SystemInit()    — one-time system initialisation (stub on Linux)
 *   Audio PAL stubs         — Pi Zero 2W has no A2B audio host; return success
 *   Memory manager wrappers — thin malloc/free wrappers (A2B_FEATURE_MEMORY_MANAGER
 *                             must NOT be defined)
 *   Version/build info      — static strings
 *   BCF file I/O            — when A2B_BCF_FROM_FILE_IO is defined
 *
 * Porting note: all Pico SDK calls (i2c_write_blocking, hardware/spi.h, etc.)
 * have been removed.  I2C is implemented via ioctl in pal_i2c.c; timer via
 * clock_gettime in pal_timer.c; GPIO via libgpiod in pal_gpio.c.
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "a2b/ctypes.h"
#include "a2b/error.h"
#include "a2b/ecb.h"
#include "a2b/pal.h"
#include "a2b/i2c.h"
#include "a2b/util.h"
#include "adi_a2b_datatypes.h"
#include "adi_a2b_pal.h"
#include "adi_a2b_externs.h"
#include "pal_timer.h"

/*===========================================================================
 * adi_a2b_SystemInit
 *
 * One-time system initialisation.  On the Pico this configured clocks and
 * UART; on Linux there is nothing to do — stdio is ready at process start.
 *===========================================================================*/
ADI_MEM_A2B_CODE_CRIT
a2b_HResult adi_a2b_SystemInit(void)
{
    printf("a2b: system init\n");
    return (a2b_HResult)A2B_RESULT_SUCCESS;
}

/*===========================================================================
 * Audio PAL stubs
 *
 * The Pi Zero 2W is the A2B main-node controller only; there is no A2B
 * audio host on this board (audio routing happens on sub-nodes).  These
 * stubs allow the stack to initialise without audio hardware.
 *===========================================================================*/
ADI_MEM_A2B_CODE_NO_CRIT
a2b_HResult a2b_pal_AudioInitFunc(A2B_ECB *ecb)
{
    A2B_UNUSED(ecb);
    return (a2b_HResult)A2B_RESULT_SUCCESS;
}

ADI_MEM_A2B_CODE_NO_CRIT
a2b_Handle a2b_pal_AudioOpenFunc(void)
{
    /* Pi Zero 2W has no A2B audio host.  Return a non-NULL sentinel so the
     * stack does not treat this as an allocation failure (stack.c:750-758
     * sets status = FAILURE if audioOpen returns NULL).               */
    return (a2b_Handle)1u;
}

ADI_MEM_A2B_CODE_NO_CRIT
a2b_HResult a2b_pal_AudioConfigFunc(a2b_Handle hnd,
                                    a2b_TdmSettings *tdmSettings)
{
    A2B_UNUSED(hnd);
    A2B_UNUSED(tdmSettings);
    return (a2b_HResult)A2B_RESULT_SUCCESS;
}

ADI_MEM_A2B_CODE_NO_CRIT
a2b_HResult a2b_pal_AudioCloseFunc(a2b_Handle hnd)
{
    A2B_UNUSED(hnd);
    return (a2b_HResult)A2B_RESULT_SUCCESS;
}

ADI_MEM_A2B_CODE_NO_CRIT
a2b_HResult a2b_pal_AudioShutdownFunc(A2B_ECB *ecb)
{
    A2B_UNUSED(ecb);
    return (a2b_HResult)A2B_RESULT_SUCCESS;
}

/*===========================================================================
 * Memory manager PAL wrappers
 *
 * Used when A2B_FEATURE_MEMORY_MANAGER is NOT defined (Linux userspace
 * always uses the system allocator).
 *===========================================================================*/
#ifndef A2B_FEATURE_MEMORY_MANAGER

ADI_MEM_A2B_CODE_NO_CRIT
a2b_HResult a2b_pal_memMgrInit(A2B_ECB *ecb)
{
    A2B_UNUSED(ecb);
    return (a2b_HResult)A2B_RESULT_SUCCESS;
}

ADI_MEM_A2B_CODE_NO_CRIT
a2b_Handle a2b_pal_memMgrOpen(a2b_Byte *heap, a2b_UInt32 heapSize)
{
    A2B_UNUSED(heap);
    A2B_UNUSED(heapSize);
    /* Sentinel non-NULL handle; Linux uses malloc/free, not a heap pool */
    return (a2b_Handle)1u;
}

ADI_MEM_A2B_CODE_NO_CRIT
void *a2b_pal_memMgrMalloc(a2b_Handle hnd, a2b_UInt32 size)
{
    A2B_UNUSED(hnd);
    return malloc((size_t)size);
}

ADI_MEM_A2B_CODE_NO_CRIT
void a2b_pal_memMgrFree(a2b_Handle hnd, void *p)
{
    A2B_UNUSED(hnd);
    free(p);
}

ADI_MEM_A2B_CODE_NO_CRIT
a2b_HResult a2b_pal_memMgrClose(a2b_Handle hnd)
{
    A2B_UNUSED(hnd);
    return (a2b_HResult)A2B_RESULT_SUCCESS;
}

ADI_MEM_A2B_CODE_NO_CRIT
a2b_HResult a2b_pal_memMgrShutdown(A2B_ECB *ecb)
{
    A2B_UNUSED(ecb);
    return (a2b_HResult)A2B_RESULT_SUCCESS;
}

#endif /* !A2B_FEATURE_MEMORY_MANAGER */

/*===========================================================================
 * Version / build info
 *===========================================================================*/
ADI_MEM_A2B_CODE_NO_CRIT
void a2b_pal_infoGetVersion(a2b_UInt32 *major,
                            a2b_UInt32 *minor,
                            a2b_UInt32 *release)
{
    if (major)  *major   = 1u;
    if (minor)  *minor   = 0u;
    if (release)*release = 0u;
}

ADI_MEM_A2B_CODE_NO_CRIT
void a2b_pal_infoGetBuild(a2b_UInt32        *buildNum,
                          const a2b_Char   **buildDate,
                          const a2b_Char   **buildOwner,
                          const a2b_Char   **buildSrcRev,
                          const a2b_Char   **buildHost)
{
    static const a2b_Char kDate[]     = __DATE__ " " __TIME__;
    static const a2b_Char kOwner[]    = "ClockWorks Signal Processing LLC";
    static const a2b_Char kSrcRev[]   = "linux-rpi-port";
    static const a2b_Char kHost[]     = "arm-linux-gnueabihf";

    if (buildNum)     *buildNum    = 0u;
    if (buildDate)    *buildDate   = kDate;
    if (buildOwner)   *buildOwner  = kOwner;
    if (buildSrcRev)  *buildSrcRev = kSrcRev;
    if (buildHost)    *buildHost   = kHost;
}

/*===========================================================================
 * BCF file I/O
 *
 * Used when A2B_BCF_FROM_FILE_IO is defined.  Wraps stdio to present the
 * offset-based interface the stack expects.
 *===========================================================================*/
#if defined(A2B_BCF_FROM_FILE_IO)

ADI_MEM_A2B_CODE_NO_CRIT
a2b_HResult a2b_pal_FileOpen(A2B_ECB *ecb, char *url)
{
    FILE *fp;

    if (!ecb || !url) {
        return (a2b_HResult)1u;
    }

    fp = fopen(url, "rb");
    if (!fp) {
        fprintf(stderr, "adi_a2b_pal: fopen(%s) failed\n", url);
        return (a2b_HResult)1u;
    }

    ecb->palEcb.fp = (a2b_Handle)fp;
    return (a2b_HResult)A2B_RESULT_SUCCESS;
}

ADI_MEM_A2B_CODE_NO_CRIT
a2b_HResult a2b_pal_FileRead(a2b_Handle hnd, a2b_UInt16 offset,
                             a2b_UInt16 nRead, a2b_Byte *rBuf)
{
    FILE *fp = (FILE *)hnd;
    size_t nActual;

    if (!fp || !rBuf) {
        return (a2b_HResult)1u;
    }

    if (fseek(fp, (long)offset, SEEK_SET) != 0) {
        return (a2b_HResult)1u;
    }

    nActual = fread(rBuf, 1u, (size_t)nRead, fp);
    return (nActual == (size_t)nRead)
        ? (a2b_HResult)A2B_RESULT_SUCCESS
        : (a2b_HResult)1u;
}

ADI_MEM_A2B_CODE_NO_CRIT
a2b_HResult a2b_pal_FileClose(A2B_ECB *ecb)
{
    FILE *fp;

    if (!ecb) {
        return (a2b_HResult)1u;
    }

    fp = (FILE *)ecb->palEcb.fp;
    if (fp) {
        fclose(fp);
        ecb->palEcb.fp = (a2b_Handle)A2B_NULL;
    }

    return (a2b_HResult)A2B_RESULT_SUCCESS;
}

#endif /* A2B_BCF_FROM_FILE_IO */

/*===========================================================================
 * a2b_palInit
 *
 * Wires the a2b_StackPal function pointer table with this platform's
 * implementations.  Called from a2bpnp_stkinterface_Init() (inside
 * a2b_pnp_Init()).
 *
 * Mirrors the Pico version in app/RP2040/a2bstack-pal/adi_a2b_pal.c.
 * Differences:
 *   - Pico SDK calls removed (hardware/i2c.h, pico/stdlib.h)
 *   - I2C bus speed set to 400 kHz (per CLAUDE.md §Hardware Context)
 *   - SPI slots remain NULL (Stage 1)
 *===========================================================================*/
void a2b_palInit(struct a2b_StackPal *pal, A2B_ECB *ecb)
{
    a2b_UInt8 nIndex = 0u;

    if (A2B_NULL == pal) {
        return;
    }

    nIndex = ecb->palEcb.nChainIndex;
    a2b_memset(pal, 0, sizeof(*pal));
    a2b_memset(ecb, 0, sizeof(*ecb));
    ecb->palEcb.nChainIndex = nIndex;

#ifndef A2B_FEATURE_MEMORY_MANAGER
    pal->memMgrInit     = a2b_pal_memMgrInit;
    pal->memMgrOpen     = a2b_pal_memMgrOpen;
    pal->memMgrMalloc   = a2b_pal_memMgrMalloc;
    pal->memMgrFree     = a2b_pal_memMgrFree;
    pal->memMgrClose    = a2b_pal_memMgrClose;
    pal->memMgrShutdown = a2b_pal_memMgrShutdown;
#endif

    pal->timerInit       = pal_timerInit;
    pal->timerGetSysTime = pal_timerGetSysTime;
    pal->timerShutdown   = pal_timerShutdown;

    pal->i2cInit      = a2b_pal_I2cInit;
    pal->i2cOpen      = a2b_pal_I2cOpenFunc;
    pal->i2cClose     = a2b_pal_I2cCloseFunc;
    pal->i2cRead      = a2b_pal_I2cReadFunc;
    pal->i2cWrite     = a2b_pal_I2cWriteFunc;
    pal->i2cWriteRead = a2b_pal_I2cWriteReadFunc;
    pal->i2cShutdown  = a2b_pal_I2cShutdownFunc;

    /* SPI — Stage 1: not implemented (data tunnel only needed in service mode) */
    pal->spiInit      = A2B_NULL;
    pal->spiOpen      = A2B_NULL;
    pal->spiClose     = A2B_NULL;
    pal->spiRead      = A2B_NULL;
    pal->spiWrite     = A2B_NULL;
    pal->spiWriteRead = A2B_NULL;
    pal->spiFd        = A2B_NULL;
    pal->spiShutdown  = A2B_NULL;

    pal->audioInit     = a2b_pal_AudioInitFunc;
    pal->audioOpen     = a2b_pal_AudioOpenFunc;
    pal->audioClose    = a2b_pal_AudioCloseFunc;
    pal->audioConfig   = a2b_pal_AudioConfigFunc;
    pal->audioShutdown = a2b_pal_AudioShutdownFunc;

    pal->getVersion = a2b_pal_infoGetVersion;
    pal->getBuild   = a2b_pal_infoGetBuild;

#if defined(A2B_BCF_FROM_FILE_IO)
    pal->fileRead = a2b_pal_FileRead;
#endif

    if (A2B_NULL != ecb) {
        ecb->baseEcb.i2cAddrFmt    = A2B_I2C_ADDR_FMT_7BIT;
        /* 400 kHz — requires dtparam=i2c_arm_baudrate=400000 in config.txt
         * (/boot/firmware/config.txt on Bookworm and later) */
        ecb->baseEcb.i2cBusSpeed   = A2B_I2C_BUS_SPEED_400KHZ;
        ecb->baseEcb.i2cMasterAddr = A2B_CONF_DEFAULT_MASTER_NODE_I2C_ADDR;
    }
}

/*===========================================================================
 * adi_a2b_palInit
 *
 * Source-compatibility alias for a2b_palInit().
 * The Pico adi_a2b_externs.h declares this name; kept for link compatibility.
 *===========================================================================*/
void adi_a2b_palInit(struct a2b_StackPal *pal, A2B_ECB *ecb)
{
    a2b_palInit(pal, ecb);
}

/*===========================================================================
 * adi_a2b_Delay
 *
 * Blocks for nTime milliseconds using nanosleep() (per CLAUDE.md §7).
 * Called from cmd_parse.c / a2bapp_common.c.
 *===========================================================================*/
void adi_a2b_Delay(uint32_t nTime)
{
    struct timespec ts;
    ts.tv_sec  = nTime / 1000u;
    ts.tv_nsec = (long)(nTime % 1000u) * 1000000L;
    nanosleep(&ts, NULL);
}

/*===========================================================================
 * adi_a2b_Timer* stubs
 *
 * The A2B stack timer is driven by pal_timerGetSysTime() (polled).
 * These hardware timer functions are referenced by adi_a2b_driverprototypes.h
 * but not called on Linux; stubs are provided for link compatibility.
 *===========================================================================*/
uint32_t adi_a2b_TimerOpen(uint32_t nTimerNo, void *pUserArgument)
{
    (void)nTimerNo; (void)pUserArgument;
    return 0u;
}

uint32_t adi_a2b_TimerStop(uint32_t nTimerNo)
{
    (void)nTimerNo;
    return 0u;
}

uint32_t adi_a2b_TimerClose(uint32_t nTimerNo)
{
    (void)nTimerNo;
    return 0u;
}

uint32_t adi_a2b_TimerStart(uint32_t nTimerNo, uint32_t nTime)
{
    (void)nTimerNo; (void)nTime;
    return 0u;
}
