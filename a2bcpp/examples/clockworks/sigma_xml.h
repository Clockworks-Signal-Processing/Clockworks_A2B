/**
 * @file sigma_xml.h
 * @brief Reads a SigmaStudio XML program export (the `<ROM>` file it writes for a codec,
 *        e.g. an ADAU1761's) into I2C writes and waits, for the bench console's module
 *        images (`module N AB0020 FILE`).
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_EXAMPLES_BENCH_SIGMA_XML_H_
#define A2BCPP_EXAMPLES_BENCH_SIGMA_XML_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/// One action of a SigmaStudio XML program: an I2C write or a wait.
struct SigmaAction {
  bool wait = false;           ///< A `delay` action, else a `writeXbytes`.
  uint8_t i2cAddr = 0;         ///< The write's 7-bit I2C address.
  std::vector<uint8_t> bytes;  ///< The write: its sub-address (high byte first), then data.
  uint16_t waitMs = 0;         ///< The wait.
};

/// A SigmaStudio XML program: its writes and waits in order, and the reads skipped.
struct SigmaProgram {
  std::vector<SigmaAction> actions;  ///< The writes and waits.
  size_t readsSkipped = 0;           ///< `read` actions, which a peripheral script can't run.
};

/// The most bytes one write may have: a peripheral-script unit holds 4095, one of them its
/// CRC-8 (a2bcpp/src/module_script.cpp).
inline constexpr size_t kMaxSigmaWrite = 4094;

/// The longest wait: a peripheral-script wait unit's 12-bit size.
inline constexpr uint16_t kMaxSigmaWaitMs = 4095;

bool ParseSigmaXml(const std::string& text, uint8_t defaultI2cAddr, SigmaProgram& program,
                   std::string& error);  ///< Reads a program from its text.
bool ReadSigmaXml(const std::string& path, uint8_t defaultI2cAddr, SigmaProgram& program,
                  std::string& error);  ///< Reads a program from a file.

#endif  // A2BCPP_EXAMPLES_BENCH_SIGMA_XML_H_
