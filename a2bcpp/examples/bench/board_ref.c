/*
 * The bench console's built-in board "ref": the reference board's SigmaStudio+ export
 * (adi/exports/rp2040/12_a2b_busconfig.c, as the Pi and RP2040 apps use it), with its
 * sBusDescription renamed so that several exports fit in one program.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#define sBusDescription a2b_bench_board_ref
#include "../../../adi/exports/rp2040/12_a2b_busconfig.c"
