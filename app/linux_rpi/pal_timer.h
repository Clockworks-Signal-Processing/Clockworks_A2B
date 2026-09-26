/*******************************************************************************
 * pal_timer.h — Linux Timer PAL for A2B Stack (public interface)
 * ClockWorks Signal Processing LLC
 *
 * Identical API to app/RP2040/a2bstack-pal/pal_timer.h.
 ******************************************************************************/

#ifndef A2B_PAL_TIMER_H_
#define A2B_PAL_TIMER_H_

#include "a2b/macros.h"
#include "a2b/ctypes.h"
#include "a2b/ecb.h"
#include "a2b/pal.h"

A2B_BEGIN_DECLS

A2B_EXPORT A2B_DSO_LOCAL a2b_HResult pal_timerInit(A2B_ECB *ecb);
A2B_EXPORT A2B_DSO_LOCAL a2b_HResult pal_timerShutdown(A2B_ECB *ecb);
A2B_EXPORT A2B_DSO_LOCAL a2b_UInt32  pal_timerGetSysTime(void);

A2B_END_DECLS

#endif /* A2B_PAL_TIMER_H_ */
