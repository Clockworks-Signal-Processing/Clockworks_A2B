/**
 * @file ss_plus_file.h
 * @brief Reads a SigmaStudio+ bus configuration export (its adi_a2b_busconfig.c) at run
 *        time, into the structures ImportSsPlus() takes, so a host app can load a board
 *        without being rebuilt.
 *
 * Host only (Windows, Linux): it uses the heap and the C++ library. Embedded apps link
 * their export in instead.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_IMPORT_SS_PLUS_SS_PLUS_FILE_H_
#define A2BCPP_IMPORT_SS_PLUS_SS_PLUS_FILE_H_

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "a2b/types.h"
#include "ss_plus_import.h"

namespace a2b {

/// One SigmaStudio+ export read from its C file: the variables it defines, filled as the
/// C compiler would fill them, with sBusDescription to give to ImportSsPlus().
class SsPlusFile {
 public:
  SsPlusFile();  ///< Empty.
  ~SsPlusFile();

  SsPlusFile(const SsPlusFile&) = delete;
  SsPlusFile& operator=(const SsPlusFile&) = delete;

  Result Load(const char* path);               ///< Reads and parses an export's C file.
  Result Parse(const char* text, size_t len);  ///< Parses an export's text.

  const ADI_A2B_BCD* Bus() const;                    ///< Its sBusDescription, or nullptr.
  const std::string& Error() const;                  ///< Why the last load failed.
  const std::vector<std::string>& Warnings() const;  ///< What the last load ignored.

 private:
  struct Impl;  ///< The variables and the parser (ss_plus_file.cpp).

  std::unique_ptr<Impl> m_Impl;  ///< The loaded export.
};

}  // namespace a2b

#endif  // A2BCPP_IMPORT_SS_PLUS_SS_PLUS_FILE_H_
