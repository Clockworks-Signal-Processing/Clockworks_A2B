/**
 * @file spi_job.h
 * @brief The bench console's long SPI jobs through the data tunnel (docs/SPI.md): a stream
 *        read or write of any length, and a SPI NOR flash's read, erase, program and verify,
 *        one transaction per poll so the bus is served meanwhile.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef A2BCPP_EXAMPLES_BENCH_SPI_JOB_H_
#define A2BCPP_EXAMPLES_BENCH_SPI_JOB_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "a2b/network.h"
#include "a2b/types.h"

/// One long SPI job on the tunnel's far end, run by Poll().
class SpiJob {
 public:
  /** @brief What the job does. */
  enum class Kind : uint8_t {
    kNone,     ///< Nothing started.
    kStream,   ///< Header bytes out, then bytes read (extended full duplex): `spi dump`.
    kSend,     ///< Bytes out (extended bulk write): `spi send`.
    kErase,    ///< A flash's 4 KB sectors erased (0x06, 0x20, then 0x05 until not busy).
    kProgram,  ///< A flash's pages programmed (0x06, 0x02 by bulk write, 0x05 until done).
    kVerify,   ///< A flash read (0x03) and compared.
  };

  /** @brief Where the job is. */
  enum class State : uint8_t {
    kIdle,     ///< Nothing started.
    kRunning,  ///< Poll again.
    kDone,     ///< Finished; see Data(), Summary().
    kFailed,   ///< Stopped; see Error().
  };

  /** @brief The longest a flash may stay busy after a program or an erase. */
  static constexpr uint32_t kBusyTimeoutMs = 3000;
  /** The longest the stack may answer Result::kBusy (a node joining, the tunnel replanned)
      before the job gives up. */
  static constexpr uint32_t kStackBusyMs = 3000;

  void StartStream(a2b::NodeAddr node, uint8_t select, const std::vector<uint8_t>& header,
                   size_t len, uint32_t nowMs);  ///< Header out, then @p len bytes read.
  void StartSend(a2b::NodeAddr node, uint8_t select, const std::vector<uint8_t>& data,
                 uint32_t nowMs);  ///< @p data out in one remote transfer.
  void StartErase(a2b::NodeAddr node, uint8_t select, uint32_t address, size_t len,
                  uint32_t nowMs);  ///< The flash's sectors that hold the range, erased.
  void StartProgram(a2b::NodeAddr node, uint8_t select, uint32_t address,
                    const std::vector<uint8_t>& data, uint32_t nowMs);  ///< Programs @p data.
  void StartVerify(a2b::NodeAddr node, uint8_t select, uint32_t address,
                   const std::vector<uint8_t>& data, uint32_t nowMs);  ///< Compares @p data.
  void Abort();                                                        ///< Stops; the job fails.
  State Poll(a2b::Network& network, uint32_t nowMs);                   ///< At most a transaction.

  Kind GetKind() const { return m_Kind; }                      ///< What the job does.
  bool Running() const { return m_State == State::kRunning; }  ///< Not finished.
  a2b::NodeAddr Node() const { return m_Node; }                ///< The tunnel's far end.
  const std::vector<uint8_t>& Data() const { return m_Data; }  ///< What a stream read.
  const std::string& Error() const { return m_Error; }         ///< Why it failed.
  std::string Summary() const;  ///< What it did, with its time and rate.

 private:
  /** @brief The flash jobs' steps for one sector or page. */
  enum class Step : uint8_t {
    kEnable,  ///< Write enable (0x06), atomic.
    kWrite,   ///< The erase or program command.
    kBusy,    ///< Status register 1 (0x05) until BUSY is clear.
  };

  void Begin(Kind kind, a2b::NodeAddr node, uint8_t select, uint32_t nowMs);
  State Fail(const std::string& why);                     ///< Stops with @p why.
  State Finish(uint32_t nowMs);                           ///< Done.
  bool PollStream(a2b::Network& network);                 ///< One full duplex (or a FIFO read).
  bool PollSend(a2b::Network& network);                   ///< One bulk write.
  bool PollFlash(a2b::Network& network, uint32_t nowMs);  ///< One step of an erase or program.
  bool NextUnit();                 ///< To the next sector or page; false at the end.
  bool Retry(a2b::Result result);  ///< Result::kBusy: the same step at the next poll.

  Kind m_Kind = Kind::kNone;
  State m_State = State::kIdle;
  a2b::NodeAddr m_Node = 0;
  uint8_t m_Select = 0;
  std::vector<uint8_t> m_Out;   ///< What a stream or send sends.
  std::vector<uint8_t> m_In;    ///< What a stream reads back, as it comes.
  size_t m_Header = 0;          ///< A stream's bytes before the data.
  size_t m_Done = 0;            ///< Bytes sent so far.
  size_t m_Last = 0;            ///< The last full duplex's length (its data is still to come).
  std::vector<uint8_t> m_Data;  ///< A stream's data; a program's or verify's image.
  uint32_t m_Address = 0;       ///< A flash job's start.
  size_t m_Unit = 0;            ///< Offset of the sector or page in hand.
  size_t m_UnitLen = 0;         ///< Its bytes.
  size_t m_End = 0;             ///< The flash job's bytes.
  Step m_Step = Step::kEnable;
  uint32_t m_BusySince = 0;  ///< The command that made the flash busy.
  size_t m_Units = 0;        ///< Sectors erased, pages programmed.
  size_t m_Skipped = 0;      ///< Pages left out (all 0xFF).
  uint32_t m_NowMs = 0;      ///< This poll's time.
  bool m_Waiting = false;    ///< The stack answered Result::kBusy...
  uint32_t m_WaitSince = 0;  ///< ...since then.
  bool m_Fetched = false;    ///< A stream's FIFO read is done; its full duplex is next.
  uint32_t m_StartMs = 0;
  uint32_t m_EndMs = 0;
  std::string m_Error;
};

#endif  // A2BCPP_EXAMPLES_BENCH_SPI_JOB_H_
