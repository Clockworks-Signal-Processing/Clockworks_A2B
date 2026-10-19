/**
 * @file console_text.h
 * @brief The bench console's command-line helpers, shared by bench_console.cpp and
 *        bench_console_files.cpp: words, numbers and hex.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_EXAMPLES_BENCH_CONSOLE_TEXT_H_
#define A2BCPP_EXAMPLES_BENCH_CONSOLE_TEXT_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace bench {

bool Same(const std::string& a, const char* b);                 ///< The same word, ignoring case.
std::vector<std::string> Split(const char* line);               ///< A command line's words.
bool ToNumber(const std::string& text, long max, long& value);  ///< A number from 0 to @p max.
std::string Hex(const uint8_t* data, size_t len);  ///< Bytes as hex, separated by spaces.

}  // namespace bench

#endif  // A2BCPP_EXAMPLES_BENCH_CONSOLE_TEXT_H_
