/**
 * @file eeprom_job.cpp
 * @brief EepromJob: a sub-node's EEPROM read, written, verified or erased, a transfer at a
 *        time.
 *
 * The EEPROM is a 24xx part with 16-bit addresses (Clockworks' boards: a 24FC256, 32 KB,
 * 64-byte pages, 5 ms write cycle), reached as the node's I2C peripheral through
 * Network::PeripheralWrite() and PeripheralWriteRead(): a write is the two address bytes
 * (high first) and the data; a read writes the address, then reads after a repeated start.
 * Each Poll() makes at most one transfer, so the bus is polled between them and the
 * stack's own work goes on; a wait (the write cycle) is a time to poll past, not a sleep.
 *
 * **Writing** an image keeps the node safe if the job stops half way (a power cut, a
 * cable pulled, `eeprom abort`): the stack takes a node's image only if byte 0 is the
 * marker 0xAA, so
 * 1. the EEPROM is read over the image's length (only pages that differ are written);
 * 2. if it holds a marker and the image differs, 0xFF is written over the marker: from
 *    here the node reads as blank, never as a half-written image;
 * 3. each page that differs is written (byte 0 aside), with the write-cycle wait after it;
 * 4. everything is read back and compared (byte 0 aside);
 * 5. byte 0, the marker, is written last, and read back.
 *
 * After a write the EEPROM's part is busy for its write cycle and NAKs (the simulator's
 * model does too, sim/eeprom.cpp). The job waits EepromConfig::writeCycleMs, then tries
 * a transfer that fails again every EepromJob::kRetryMs ms, up to EepromJob::kMaxTries times: a
 * slower part still works, and a dead one fails in a few tens of ms. Reads go in chunks of
 * EepromConfig::readChunk bytes, well inside the ADI USBi's 0.3 s for a transfer's status
 * (porting.md); a page write is 66 bytes.
 *
 * **Reading** "to the image's end" follows the image's blocks as they come in
 * (WalkImage()); an EEPROM with no image (blank, or something else) is read whole.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "eeprom_job.h"

#include <algorithm>
#include <cstdio>

#include "eeprom_image.h"

namespace {

constexpr uint8_t kMarker = 0xAA;  ///< Byte 0 of an image.

/** printf into a std::string. */
template <typename... Args>
std::string Format(const char* format, Args... args) {
  char text[200];
  std::snprintf(text, sizeof(text), format, args...);
  return text;
}

}  // namespace

/**
 * Starts reading a node's EEPROM; Poll() does it.
 * @param node The sub-node (in the bus).
 * @param len Bytes to read from address 0; 0: to the end of the image there (the whole
 *        EEPROM if there is none). At most the EEPROM's size.
 * @param config The EEPROM.
 */
void EepromJob::StartRead(a2b::NodeAddr node, size_t len, const EepromConfig& config) {
  Begin(Kind::kRead, node, config);
  m_Step = Step::kRead;
  m_ToImageEnd = (len == 0);
  m_Target = m_ToImageEnd ? 0 : std::min(len, m_Config.size);
}

/**
 * Starts writing an image to a node's EEPROM: only the pages that differ, the marker
 * cleared first and written last (file comment), everything read back.
 * @param node The sub-node (in the bus).
 * @param image The bytes from address 0 (CheckImage() it first); copied.
 * @param config The EEPROM.
 */
void EepromJob::StartWrite(a2b::NodeAddr node, const std::vector<uint8_t>& image,
                           const EepromConfig& config) {
  Begin(Kind::kWrite, node, config);
  m_Image = image;
  if (m_Image.empty() || m_Image.size() > m_Config.size) {
    Fail(Format("an image of %u bytes for a %u-byte EEPROM", static_cast<unsigned>(m_Image.size()),
                static_cast<unsigned>(m_Config.size)));
    return;
  }
  m_Step = Step::kReadOld;
  m_Target = m_Image.size();
}

/**
 * Starts comparing a node's EEPROM with an image, byte 0 included.
 * @param node The sub-node (in the bus).
 * @param image The bytes from address 0; copied.
 * @param config The EEPROM.
 */
void EepromJob::StartVerify(a2b::NodeAddr node, const std::vector<uint8_t>& image,
                            const EepromConfig& config) {
  Begin(Kind::kVerify, node, config);
  m_Image = image;
  if (m_Image.empty() || m_Image.size() > m_Config.size) {
    Fail(Format("an image of %u bytes for a %u-byte EEPROM", static_cast<unsigned>(m_Image.size()),
                static_cast<unsigned>(m_Config.size)));
    return;
  }
  m_Step = Step::kVerify;
  m_Data.assign(m_Image.size(), 0xFF);
  m_Target = m_Image.size();
}

/**
 * Starts clearing a node's marker (0xFF at address 0): the stack then finds no module
 * info there, as on a blank EEPROM. The rest of the image stays.
 * @param node The sub-node (in the bus).
 * @param config The EEPROM.
 */
void EepromJob::StartErase(a2b::NodeAddr node, const EepromConfig& config) {
  Begin(Kind::kErase, node, config);
  m_Image.assign(1, 0xFF);
  m_Data.assign(1, kMarker);
  m_Step = Step::kMarker;
}

/** Stops the job; a write stopped half way leaves the node blank (file comment). */
void EepromJob::Abort() {
  if (m_State == State::kRunning) {
    Fail("stopped (eeprom abort)");
  }
}

/**
 * Runs the job on: at most one transfer, none during a wait.
 * @param network The bus the node is on.
 * @param nowMs Platform::NowMs().
 * @return State::kRunning until it is done or has failed.
 */
EepromJob::State EepromJob::Poll(a2b::Network& network, uint32_t nowMs) {
  if (m_State != State::kRunning) {
    return m_State;
  }
  if (m_Waiting) {
    if (!a2b::TimeReached(nowMs, m_WaitUntil)) {
      return m_State;
    }
    m_Waiting = false;
  }
  if (m_Kind == Kind::kRead) {
    PollRead(network, nowMs);
  } else {
    PollWrite(network, nowMs);
  }
  return m_State;
}

/**
 * What a finished job did, in words.
 * @return One line, or an empty string for a job not finished.
 */
std::string EepromJob::Summary() const {
  if (m_State != State::kDone) {
    return std::string();
  }
  const std::string where = Format("sub-node %u's EEPROM (0x%02X)", static_cast<unsigned>(m_Node),
                                   static_cast<unsigned>(m_Config.i2cAddr));
  std::string text;
  switch (m_Kind) {
    case Kind::kRead:
      text = Format("Read %u bytes from ", static_cast<unsigned>(m_Data.size())) + where;
      break;
    case Kind::kWrite:
      text = Format("Wrote %u bytes to ", static_cast<unsigned>(m_Image.size())) + where +
             Format(": %u page(s) written, %u already right; verified",
                    static_cast<unsigned>(m_PagesWritten), static_cast<unsigned>(m_PagesSkipped));
      break;
    case Kind::kVerify:
      text = where + Format(" holds the image (%u bytes)", static_cast<unsigned>(m_Image.size()));
      break;
    case Kind::kErase:
      text = "Marker cleared on " + where + ": the node has no module info now";
      break;
    case Kind::kNone:
      break;
  }
  if (m_Retries > 0) {
    text += Format(" (%d transfer(s) tried again)", m_Retries);
  }
  return text;
}

/** Resets the job for a new one. */
void EepromJob::Begin(Kind kind, a2b::NodeAddr node, const EepromConfig& config) {
  m_Kind = kind;
  m_State = State::kRunning;
  m_Node = node;
  m_Config = config;
  m_Config.pageSize = std::max<size_t>(m_Config.pageSize, 1);
  m_Config.readChunk = std::max<size_t>(m_Config.readChunk, 1);
  m_Image.clear();
  m_Data.clear();
  m_Target = 0;
  m_ToImageEnd = false;
  m_WholeEeprom = false;
  m_At = 0;
  m_PageEnd = 0;
  m_Waiting = false;
  m_Tries = 0;
  m_Retries = 0;
  m_PagesWritten = 0;
  m_PagesSkipped = 0;
  m_MarkerCleared = false;
  m_Error.clear();
  m_Note.clear();
}

/**
 * Makes one transfer at EEPROM address @p addr: a write of @p len bytes, or a read of
 * @p readLen. A failure is tried again after #kRetryMs, up to #kMaxTries times.
 * @return True if it was made; false if not (yet): the caller tries again at the next poll
 *         (the job fails after the last try).
 */
bool EepromJob::Transfer(a2b::Network& network, uint32_t nowMs, uint32_t addr, const uint8_t* data,
                         size_t len, uint8_t* readBuf, size_t readLen) {
  const a2b::PeripheralAddress eeprom = a2b::PeripheralAddress::I2c(m_Config.i2cAddr);
  std::vector<uint8_t> out = {static_cast<uint8_t>(addr >> 8), static_cast<uint8_t>(addr)};
  a2b::Result result = a2b::Result::kOk;
  if (readBuf != nullptr) {
    result = network.PeripheralWriteRead(m_Node, eeprom, out.data(), out.size(), readBuf, readLen);
  } else {
    out.insert(out.end(), data, data + len);
    result = network.PeripheralWrite(m_Node, eeprom, out.data(), out.size());
  }
  if (result == a2b::Result::kOk) {
    m_Tries = 0;
    return true;
  }
  if (++m_Tries >= kMaxTries) {
    Fail(
        Format("no answer at EEPROM address 0x%04X after %d tries (is the node in the bus, the "
               "EEPROM at 0x%02X?)",
               static_cast<unsigned>(addr), m_Tries, static_cast<unsigned>(m_Config.i2cAddr)));
    return false;
  }
  ++m_Retries;
  Wait(nowMs, kRetryMs);
  return false;
}

/** No transfer for @p ms. */
void EepromJob::Wait(uint32_t nowMs, uint32_t ms) {
  m_WaitUntil = nowMs + ms;
  m_Waiting = true;
}

/** Stops with @p why. */
EepromJob::State EepromJob::Fail(const std::string& why) {
  m_Error = why;
  m_Waiting = false;
  m_State = State::kFailed;
  return m_State;
}

/** Finishes. */
EepromJob::State EepromJob::Finish() {
  m_State = State::kDone;
  return m_State;
}

/** One read: the next chunk; at the image's end (or the length asked) the job is done. */
void EepromJob::PollRead(a2b::Network& network, uint32_t nowMs) {
  const size_t limit = (m_Target != 0) ? m_Target : m_Config.size;
  const size_t len = std::min(m_Config.readChunk, limit - m_At);
  std::vector<uint8_t> chunk(len);
  if (!Transfer(network, nowMs, static_cast<uint32_t>(m_At), nullptr, 0, chunk.data(), len)) {
    return;
  }
  m_Data.insert(m_Data.end(), chunk.begin(), chunk.end());
  m_At += len;
  if (m_ToImageEnd && m_Target == 0) {
    size_t end = 0;
    std::string why;
    const ImageWalk walk =
        WalkImage(m_Data.data(), m_Data.size(), m_At >= m_Config.size, false, end, why);
    if (walk == ImageWalk::kComplete) {
      m_Target = end;
    } else if (walk == ImageWalk::kInvalid) {
      m_Target = m_Config.size;
      m_WholeEeprom = true;
      m_Note = "no image there (" + why + "): the whole EEPROM read";
    }
  }
  if (m_Target != 0 && m_At >= m_Target) {
    m_Data.resize(m_Target);
    Finish();
  }
}

/**
 * Finds the next page with bytes to write, from #m_At: [#m_At, #m_PageEnd) is then that
 * page's part of the image (byte 0 left for the marker step).
 * @return False when no page is left.
 */
bool EepromJob::NextPage() {
  const size_t page = m_Config.pageSize;
  while (m_At < m_Image.size()) {
    const size_t start = std::max<size_t>(m_At, 1);
    const size_t end = std::min(m_Image.size(), (m_At / page + 1) * page);
    if (start < end &&
        !std::equal(m_Image.begin() + start, m_Image.begin() + end, m_Data.begin() + start)) {
      m_At = start;
      m_PageEnd = end;
      return true;
    }
    if (start < end) {
      ++m_PagesSkipped;
    }
    m_At = end;
  }
  return false;
}

/** Moves on after the current step's last transfer. */
void EepromJob::NextStep(uint32_t nowMs) {
  (void)nowMs;
  switch (m_Step) {
    case Step::kReadOld:
      if (m_Data == m_Image) {
        m_PagesSkipped = (m_Image.size() + m_Config.pageSize - 1) / m_Config.pageSize;
        m_Note = "the EEPROM already held this image: nothing written";
        Finish();
        return;
      }
      m_At = 0;
      m_Step = (m_Data[0] == kMarker) ? Step::kInvalidate : Step::kPages;
      if (m_Step == Step::kPages && !NextPage()) {
        m_Step = Step::kVerify;
      }
      return;
    case Step::kInvalidate:
      m_At = 0;
      m_Step = Step::kPages;
      if (!NextPage()) {
        m_Step = Step::kVerify;
        m_At = 0;
      }
      return;
    case Step::kPages:
      if (!NextPage()) {
        m_Step = Step::kVerify;
        m_At = 0;
      }
      return;
    case Step::kVerify:
      if (m_Kind == Kind::kVerify) {
        Finish();
        return;
      }
      m_Step = (m_Data[0] == m_Image[0]) ? Step::kCheckMarker : Step::kMarker;
      return;
    case Step::kMarker:
      m_Step = Step::kCheckMarker;
      return;
    case Step::kCheckMarker:
    case Step::kRead:
      Finish();
      return;
  }
}

/** One transfer of a write, verify or erase. */
void EepromJob::PollWrite(a2b::Network& network, uint32_t nowMs) {
  switch (m_Step) {
    case Step::kReadOld: {
      const size_t len = std::min(m_Config.readChunk, m_Target - m_At);
      std::vector<uint8_t> chunk(len);
      if (Transfer(network, nowMs, static_cast<uint32_t>(m_At), nullptr, 0, chunk.data(), len)) {
        m_Data.insert(m_Data.end(), chunk.begin(), chunk.end());
        m_At += len;
        if (m_At >= m_Target) {
          NextStep(nowMs);
        }
      }
      return;
    }
    case Step::kInvalidate: {
      const uint8_t blank = 0xFF;
      if (Transfer(network, nowMs, 0, &blank, 1, nullptr, 0)) {
        m_Data[0] = blank;
        m_MarkerCleared = true;
        Wait(nowMs, m_Config.writeCycleMs);
        NextStep(nowMs);
      }
      return;
    }
    case Step::kPages:
      if (Transfer(network, nowMs, static_cast<uint32_t>(m_At), &m_Image[m_At], m_PageEnd - m_At,
                   nullptr, 0)) {
        std::copy(m_Image.begin() + m_At, m_Image.begin() + m_PageEnd, m_Data.begin() + m_At);
        ++m_PagesWritten;
        m_At = m_PageEnd;
        Wait(nowMs, m_Config.writeCycleMs);
        NextStep(nowMs);
      }
      return;
    case Step::kVerify: {
      const size_t len = std::min(m_Config.readChunk, m_Image.size() - m_At);
      std::vector<uint8_t> chunk(len);
      if (!Transfer(network, nowMs, static_cast<uint32_t>(m_At), nullptr, 0, chunk.data(), len)) {
        return;
      }
      for (size_t i = 0; i < len; ++i) {
        const size_t at = m_At + i;
        m_Data[at] = chunk[i];
        if ((at != 0 || m_Kind == Kind::kVerify) && chunk[i] != m_Image[at]) {
          Fail(Format("0x%04X holds 0x%02X, not 0x%02X%s", static_cast<unsigned>(at), chunk[i],
                      m_Image[at],
                      m_Kind == Kind::kWrite ? " after writing (write-protected?)" : ""));
          return;
        }
      }
      m_At += len;
      if (m_At >= m_Image.size()) {
        NextStep(nowMs);
      }
      return;
    }
    case Step::kMarker:
      if (Transfer(network, nowMs, 0, &m_Image[0], 1, nullptr, 0)) {
        m_Data[0] = m_Image[0];
        m_MarkerCleared = false;
        Wait(nowMs, m_Config.writeCycleMs);
        NextStep(nowMs);
      }
      return;
    case Step::kCheckMarker: {
      uint8_t marker = 0;
      if (!Transfer(network, nowMs, 0, nullptr, 0, &marker, 1)) {
        return;
      }
      if (marker != m_Image[0]) {
        Fail(Format("byte 0 holds 0x%02X, not 0x%02X after writing (write-protected?)", marker,
                    m_Image[0]));
        return;
      }
      NextStep(nowMs);
      return;
    }
    case Step::kRead:
      Finish();
      return;
  }
}
