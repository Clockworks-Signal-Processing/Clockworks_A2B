/**
 * @file console_text.cpp
 * @brief The bench console's command-line helpers (console_text.h).
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "console_text.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace bench {

/** True if @p a and @p b are the same word, ignoring case. */
bool Same(const std::string& a, const char* b) {
  if (a.size() != std::strlen(b)) {
    return false;
  }
  for (size_t i = 0; i < a.size(); ++i) {
    const char x = (a[i] >= 'A' && a[i] <= 'Z') ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
    const char y = (b[i] >= 'A' && b[i] <= 'Z') ? static_cast<char>(b[i] - 'A' + 'a') : b[i];
    if (x != y) {
      return false;
    }
  }
  return true;
}

/**
 * Splits a command line into words at white space; "double quotes" keep spaces in a word
 * (a Windows path, for instance).
 */
std::vector<std::string> Split(const char* line) {
  std::vector<std::string> words;
  const char* p = line;
  for (;;) {
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
      ++p;
    }
    if (*p == '\0') {
      return words;
    }
    std::string word;
    if (*p == '"') {
      ++p;
      while (*p != '\0' && *p != '"') {
        word += *p++;
      }
      if (*p == '"') {
        ++p;
      }
    } else {
      while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
        word += *p++;
      }
    }
    words.push_back(word);
  }
}

/** Reads a whole number from 0 to @p max; false if @p text isn't one. */
bool ToNumber(const std::string& text, long max, long& value) {
  if (text.empty()) {
    return false;
  }
  char* end = nullptr;
  value = std::strtol(text.c_str(), &end, 0);
  return *end == '\0' && value >= 0 && value <= max;
}

/** Formats @p len bytes as hex, separated by spaces. */
std::string Hex(const uint8_t* data, size_t len) {
  std::string text;
  char byte[4];
  for (size_t i = 0; i < len; ++i) {
    std::snprintf(byte, sizeof(byte), i == 0 ? "%02X" : " %02X", data[i]);
    text += byte;
  }
  return text;
}

}  // namespace bench
