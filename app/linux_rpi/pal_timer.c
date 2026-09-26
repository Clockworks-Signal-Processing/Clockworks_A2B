/*******************************************************************************
 * pal_timer.c — Linux Timer PAL for A2B Stack
 * ClockWorks Signal Processing LLC
 *
 * Linux/Raspberry Pi Zero 2W peer implementation of the PAL timer functions.
 * Replaces the Pico SDK calls in app/RP2040/a2bstack-pal/adi_a2b_pal.c and
 * app/RP2040/a2bstack-pal/pal_timer.c.
 *
 * PAL function signatures read from:
 *   docs/AE_09_A2B_Stack_UserGuide.pdf, Table 3 (p.18–21, Rev 11.0)
 *   app/RP2040/a2bstack-pal/pal_timer.h
 *   app/RP2040/a2bstack-pal/adi_a2b_pal.c
 *   a2bstack/a2bstack/inc/a2b/pal.h
 *
 * Porting rule (CLAUDE.md §7):
 *   a2b_pal_TimerGetSysTimeFunc must return milliseconds using
 *   clock_gettime(CLOCK_MONOTONIC).  Use nanosleep() for any delays;
 *   do not use usleep() (deprecated).
 *
 * Two name sets are provided in this file:
 *   pal_timer*(...)      — abstract layer names, assigned to a2b_StackPal
 *                          struct fields (timerInit, timerGetSysTime,
 *                          timerShutdown) by adi_a2b_palInit() in main.c.
 *   a2b_pal_Timer*Func() — names declared in adi_a2b_externs.h, used for
 *                          direct calls from app/common/ and elsewhere.
 *
 * Both sets delegate to the same implementation so there is one code path.
 ******************************************************************************/

#include <time.h>
#include <stdio.h>

#include "a2b/ctypes.h"
#include "a2b/error.h"
#include "a2b/ecb.h"
#include "a2b/pal.h"
#include "adi_a2b_datatypes.h"
#include "adi_a2b_pal.h"

/*---------------------------------------------------------------------------
 * get_sys_time_ms — core monotonic millisecond counter
 *
 * Returns the number of milliseconds elapsed since an arbitrary fixed point
 * (the CLOCK_MONOTONIC epoch, which is typically system boot on Linux).
 * Monotonic clock is immune to wall-clock adjustments (NTP, adjtime, etc.)
 * so timer deltas remain stable during system operation.
 *
 * Return type is a2b_UInt32, which wraps around after ~49.7 days of uptime.
 * The A2B stack uses this value only for delta timing; wraparound is handled
 * correctly by unsigned subtraction as long as no single interval exceeds
 * ~49.7 days.
 *
 * Per Stack UG Rev 11.0 §5.3 (PnP UG), 1 ms granularity is sufficient.
 *---------------------------------------------------------------------------*/
static a2b_UInt32 get_sys_time_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        /* Should not fail for CLOCK_MONOTONIC — log and return 0 */
        perror("pal_timer: clock_gettime(CLOCK_MONOTONIC) failed");
        return 0u;
    }

    return (a2b_UInt32)((ts.tv_sec * 1000ULL) + (ts.tv_nsec / 1000000ULL));
}

/*===========================================================================
 * a2b_pal_TimerInitFunc
 *
 * Called during stack allocation to initialise the timer subsystem.
 * CLOCK_MONOTONIC requires no per-process initialisation on Linux.
 *
 * Signature from Table 3, Stack UG Rev 11.0 / adi_a2b_externs.h
 * (not mandatory).
 *===========================================================================*/
ADI_MEM_A2B_CODE_NO_CRIT
a2b_HResult a2b_pal_TimerInitFunc(A2B_ECB *ecb)
{
    A2B_UNUSED(ecb);
    return (a2b_HResult)A2B_RESULT_SUCCESS;
}

/*===========================================================================
 * a2b_pal_TimerGetSysTimeFunc
 *
 * Returns the current system time in milliseconds.  The stack calls this
 * on every tick to drive its internal timeout machinery.
 *
 * Implementation: clock_gettime(CLOCK_MONOTONIC) per CLAUDE.md §7.
 *
 * Signature from Table 3, Stack UG Rev 11.0 / adi_a2b_externs.h
 * (mandatory).
 *===========================================================================*/
ADI_MEM_A2B_CODE_CRIT
a2b_UInt32 a2b_pal_TimerGetSysTimeFunc(void)
{
    return get_sys_time_ms();
}

/*===========================================================================
 * a2b_pal_TimerShutdownFunc
 *
 * Called during stack destroy to shut down the timer subsystem.
 * Nothing to release for CLOCK_MONOTONIC.
 *
 * Signature from Table 3, Stack UG Rev 11.0 / adi_a2b_externs.h
 * (not mandatory).
 *===========================================================================*/
ADI_MEM_A2B_CODE_NO_CRIT
a2b_HResult a2b_pal_TimerShutdownFunc(A2B_ECB *ecb)
{
    A2B_UNUSED(ecb);
    return (a2b_HResult)A2B_RESULT_SUCCESS;
}

/*===========================================================================
 * Abstract layer wrappers — pal_timer.h interface
 *
 * These names are assigned to the a2b_StackPal struct fields by
 * adi_a2b_palInit():
 *     pal->timerInit       = pal_timerInit;
 *     pal->timerGetSysTime = pal_timerGetSysTime;
 *     pal->timerShutdown   = pal_timerShutdown;
 *===========================================================================*/

a2b_HResult pal_timerInit(A2B_ECB *ecb)
{
    return a2b_pal_TimerInitFunc(ecb);
}

a2b_UInt32 pal_timerGetSysTime(void)
{
    return a2b_pal_TimerGetSysTimeFunc();
}

a2b_HResult pal_timerShutdown(A2B_ECB *ecb)
{
    return a2b_pal_TimerShutdownFunc(ecb);
}
