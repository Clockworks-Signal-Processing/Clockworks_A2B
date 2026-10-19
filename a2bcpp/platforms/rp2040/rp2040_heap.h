/**
 * @file rp2040_heap.h
 * @brief The heap in use on the RP2040 (rp2040_heap.c), for the examples' RAM figures.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_PLATFORMS_RP2040_RP2040_HEAP_H_
#define A2BCPP_PLATFORMS_RP2040_RP2040_HEAP_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

size_t Rp2040HeapInUse(void);  ///< The heap in use in bytes: newlib's mallinfo().uordblks.

#ifdef __cplusplus
}
#endif

#endif  // A2BCPP_PLATFORMS_RP2040_RP2040_HEAP_H_
