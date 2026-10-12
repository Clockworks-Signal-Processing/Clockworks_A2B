// clang-format off
/*=============================================================================
 *
 * Project: a2bcpp
 *
 * Copyright (c) 2023 - Analog Devices Inc. All Rights Reserved.
 * See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
 *
 *=============================================================================
 */
// clang-format on

/**
 * @file ss_plus_import.h
 * @brief Fills a BoardConfig from a SigmaStudio+ bus configuration export (the
 *        adi_a2b_busconfig.c file SigmaStudio+ writes). Main node only.
 *
 * Optional: only apps configured from SigmaStudio+ need it, and it needs ADI's
 * adi_a2b_busconfig.h and adi_a2b_datatypes.h, and a platform's a2b/ headers, to build
 * (A2B_SS_PLUS_HEADERS: by default tools/SS+reference/adi and the Linux platform's).
 *
 * C++ form by ClockWorks Signal Processing LLC.
 */

#ifndef A2BCPP_IMPORT_SS_PLUS_SS_PLUS_IMPORT_H_
#define A2BCPP_IMPORT_SS_PLUS_SS_PLUS_IMPORT_H_

#include <cstdint>

#include "a2b/board_config.h"
#include "a2b/op_list.h"
#include "a2b/types.h"

// ADI's headers, in the order the exports include them.
extern "C" {
// clang-format off
#include "adi_a2b_datatypes.h"
#include "adi_a2b_busconfig.h"
// clang-format on
}

namespace a2b {

Result ImportSsPlus(const ADI_A2B_BCD& bcd, uint8_t chain, BoardConfig& board,
                    OpList& hostScript);  ///< Fills @p board from one chain of an export.

}  // namespace a2b

#endif  // A2BCPP_IMPORT_SS_PLUS_SS_PLUS_IMPORT_H_
