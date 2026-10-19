/**
 * @file eeprom_job.h
 * @brief EepromJob: reads, writes, verifies or erases a sub-node's module-info EEPROM
 *        through the Network's peripheral calls, a transfer at a time from the app's poll
 *        loop, so the bus is served meanwhile. Application code (decisions.md: provisioning
 *        is the app's), for the bench console's `eeprom` commands.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_EXAMPLES_BENCH_EEPROM_JOB_H_
#define A2BCPP_EXAMPLES_BENCH_EEPROM_JOB_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "a2b/network.h"
#include "a2b/types.h"

/// The EEPROM on a sub-node: a 24xx part with 16-bit addresses.
struct EepromConfig {
  uint8_t i2cAddr = 0x50;     ///< Its 7-bit I2C address.
  size_t size = 32768;        ///< Its bytes (Clockworks' 24FC256: 32 KB).
  size_t pageSize = 64;       ///< Its write page (24FC256: 64 bytes).
  uint32_t writeCycleMs = 6;  ///< The wait after each write (24FC256's tWC: 5 ms at most).
  size_t readChunk = 256;     ///< Bytes per read transfer (well inside the USBi's 0.3 s).
};

/// One read, write, verify or erase of a node's EEPROM, run by Poll().
class EepromJob {
 public:
  /** @brief What the job does. */
  enum class Kind : uint8_t {
    kNone,    ///< Nothing started.
    kRead,    ///< Read to the image's end (or the whole EEPROM, or a length).
    kWrite,   ///< Write an image, verify it, then write its marker.
    kVerify,  ///< Compare the EEPROM with an image.
    kErase,   ///< Clear the marker: the node then has no module info.
  };

  /** @brief Where the job is. */
  enum class State : uint8_t {
    kIdle,     ///< Nothing started.
    kRunning,  ///< Poll again.
    kDone,     ///< Finished; see Data(), Summary().
    kFailed,   ///< Stopped; see Error().
  };

  /** @brief The most tries of one transfer: a busy EEPROM NAKs until its write is done. */
  static constexpr int kMaxTries = 8;
  /** @brief The wait between tries, in ms. */
  static constexpr uint32_t kRetryMs = 2;

  void StartRead(a2b::NodeAddr node, size_t len,
                 const EepromConfig& config);  ///< Reads; @p len 0: to the image's end.
  void StartWrite(a2b::NodeAddr node, const std::vector<uint8_t>& image,
                  const EepromConfig& config);  ///< Writes, verifies, writes the marker.
  void StartVerify(a2b::NodeAddr node, const std::vector<uint8_t>& image,
                   const EepromConfig& config);  ///< Compares.
  void StartErase(a2b::NodeAddr node,
                  const EepromConfig& config);        ///< Clears the marker.
  void Abort();                                       ///< Stops; the job fails.
  State Poll(a2b::Network& network, uint32_t nowMs);  ///< At most one transfer.

  Kind GetKind() const { return m_Kind; }                      ///< What the job does.
  State GetState() const { return m_State; }                   ///< Where it is.
  bool Running() const { return m_State == State::kRunning; }  ///< Not finished.
  a2b::NodeAddr Node() const { return m_Node; }                ///< The node.
  const EepromConfig& Config() const { return m_Config; }      ///< The EEPROM.
  const std::vector<uint8_t>& Data() const { return m_Data; }  ///< What a read read.
  const std::string& Error() const { return m_Error; }         ///< Why it failed.
  std::string Summary() const;                                 ///< What it did, in words.
  size_t PagesWritten() const { return m_PagesWritten; }       ///< Pages written.
  size_t PagesSkipped() const { return m_PagesSkipped; }       ///< Pages already right.
  int Retries() const { return m_Retries; }                    ///< Transfers tried again.
  bool WholeEeprom() const { return m_WholeEeprom; }           ///< A read went to the EEPROM's end.
  std::string Note() const { return m_Note; }                  ///< What a finished job wants known.
  /// A write cleared the image's marker: until it is written again the node is blank.
  bool MarkerCleared() const { return m_MarkerCleared; }

 private:
  /** @brief The job's steps, in order (a write uses them all). */
  enum class Step : uint8_t {
    kReadOld,      ///< Read what the EEPROM holds over the image's length.
    kInvalidate,   ///< Write 0xFF over the marker, so a half-written image reads as blank.
    kPages,        ///< Write each page that differs (byte 0 left to the end).
    kVerify,       ///< Read back and compare (byte 0 too, unless writing).
    kMarker,       ///< Write byte 0, the marker.
    kCheckMarker,  ///< Read it back.
    kRead,         ///< A read job's reads.
  };

  void Begin(Kind kind, a2b::NodeAddr node, const EepromConfig& config);
  bool Transfer(a2b::Network& network, uint32_t nowMs, uint32_t addr, const uint8_t* data,
                size_t len, uint8_t* readBuf, size_t readLen);  ///< One transfer, tried again.
  void Wait(uint32_t nowMs, uint32_t ms);                       ///< No transfer until then.
  State Fail(const std::string& why);                           ///< Stops with @p why.
  State Finish();                                               ///< Done.
  void NextStep(uint32_t nowMs);  ///< After the current step's last transfer.
  bool NextPage();                ///< To the next page to write; false at the end.
  void PollRead(a2b::Network& network, uint32_t nowMs);   ///< One read transfer.
  void PollWrite(a2b::Network& network, uint32_t nowMs);  ///< One write job transfer.

  Kind m_Kind = Kind::kNone;
  State m_State = State::kIdle;
  Step m_Step = Step::kRead;
  a2b::NodeAddr m_Node = 0;
  EepromConfig m_Config;
  std::vector<uint8_t> m_Image;  ///< What a write or verify wants there.
  std::vector<uint8_t> m_Data;   ///< What the EEPROM holds, as read.
  size_t m_Target = 0;           ///< A read's length; 0 until the image's end is known.
  bool m_ToImageEnd = false;     ///< A read goes to the image's end.
  bool m_WholeEeprom = false;    ///< A read found no image and read the whole EEPROM.
  size_t m_At = 0;               ///< Next byte to read or write.
  size_t m_PageEnd = 0;          ///< End of the bytes being written.
  uint32_t m_WaitUntil = 0;      ///< No transfer before this time.
  bool m_Waiting = false;        ///< A wait is in force.
  int m_Tries = 0;               ///< Tries of the current transfer.
  int m_Retries = 0;             ///< Transfers tried again, in all.
  size_t m_PagesWritten = 0;
  size_t m_PagesSkipped = 0;
  bool m_MarkerCleared = false;  ///< The marker was written blank; the image's isn't back.
  std::string m_Error;
  std::string m_Note;
};

#endif  // A2BCPP_EXAMPLES_BENCH_EEPROM_JOB_H_
