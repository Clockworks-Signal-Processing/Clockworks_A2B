/*******************************************************************************
 * a2bapp_platform.h — RP2040 platform header for the shared app code
 *
 * app/common/cmd_parse.c includes "a2bapp_platform.h"; each platform under
 * app/<platform>/ provides its own, so the shared code has no Pico SDK
 * includes of its own.
 ******************************************************************************/
#ifndef A2BAPP_PLATFORM_H_
#define A2BAPP_PLATFORM_H_

#include "a2bapp_rp2040.h"  // adds time.h?  can't seem to include it directly
#include "pico/stdlib.h"    // apparently this needed too to get time.h to be loaded. very odd.

#endif /* A2BAPP_PLATFORM_H_ */
