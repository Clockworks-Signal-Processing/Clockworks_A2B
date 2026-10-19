/*
 * The RP2040 examples' C helpers: the heap in use (rp2040_heap.h), from newlib's allocator,
 * and a stub for newlib's entropy hook. In C because newlib's <malloc.h> and its C++ <cstdlib>
 * declare the allocator with different exception specifications.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "rp2040_heap.h"

#include <errno.h>
#include <malloc.h>
#include <stddef.h>

/* The heap in use in bytes. */
size_t Rp2040HeapInUse(void) { return (size_t)mallinfo().uordblks; }

/*
 * No entropy source: -1 with errno ENOSYS. libstdc++'s prebuilt string code refers to
 * std::random_device, which the app never uses and the linker discards; newlib's own stub
 * for this would draw a link warning.
 */
int _getentropy(void* buf, size_t len) {
  (void)buf;
  (void)len;
  errno = ENOSYS;
  return -1;
}
