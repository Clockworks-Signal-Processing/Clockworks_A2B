/**
 * @file main_rp2040.cpp
 * @brief The bench console on the RP2040: the same commands at an `A2B>` prompt on the
 *        Pico's UART console (uart0 on GP0/GP1, 230400 baud, the Debug Probe's UART port),
 *        driving the A2B bus on the AB0310 board's wiring (i2c0 on GPIO16/17 at 400 kHz, the
 *        main node's RESETn on GP2).
 *
 * The console is built without files (A2B_BENCH_FILES 0): no `eeprom` commands, no
 * `i2cTrace`, no boards or programs from files. It starts with the AB0310 picked
 * (`board AB0310`, kStartup); `start` starts the bus. One core does everything: commands
 * run between polls (the stack takes one thread), the bus is polled every millisecond by
 * absolute time, and what the console prints waits in a buffer that is sent while the UART
 * has room, so a long listing never holds up a poll. `quit` stops the bus and the console
 * keeps running (`start` starts it again). `heap`, this program's own, prints the heap in
 * use.
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "a2b_build_stamp.h"
#include "ab0310_wiring.h"
#include "bench_console.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"
#include "rp2040_heap.h"
#include "rp2040_platform.h"

namespace {

/// The commands run at start-up, each shown after a prompt as if typed.
constexpr const char* kStartup[] = {"board AB0310"};

constexpr uint64_t kPollUs = 1000;      ///< The bus is polled every millisecond.
constexpr size_t kMaxLine = 256;        ///< The longest command line kept.
constexpr size_t kMaxPending = 32768;   ///< Output not sent yet, at most; more is dropped.
constexpr size_t kCompactAfter = 4096;  ///< Sent output kept before the buffer is compacted.

/// What goes to the UART: kept here and sent while the UART has room, never waiting for it.
class Output {
 public:
  /** Adds @p text, each "\n" as "\r\n"; drops it (and says so once) if too much waits. */
  void Add(const std::string& text) {
    if (m_Text.size() - m_Sent + text.size() > kMaxPending) {
      m_Dropped = true;
      return;
    }
    if (m_Dropped) {
      m_Dropped = false;
      m_Text += "\r\n[output dropped: the UART fell behind]\r\n";
    }
    for (const char c : text) {
      if (c == '\n') {
        m_Text += '\r';
      }
      m_Text += c;
    }
  }

  /** Sends what the UART's FIFO takes now. */
  void Drain() {
    while (m_Sent < m_Text.size() && uart_is_writable(uart0)) {
      uart_putc_raw(uart0, m_Text[m_Sent++]);
    }
    if (m_Sent == m_Text.size()) {
      m_Text.clear();
      m_Sent = 0;
    } else if (m_Sent > kCompactAfter) {
      m_Text.erase(0, m_Sent);
      m_Sent = 0;
    }
  }

 private:
  std::string m_Text;      ///< Waiting, from m_Sent on.
  size_t m_Sent = 0;       ///< Bytes of m_Text sent.
  bool m_Dropped = false;  ///< Some output was dropped.
};

/// The RP2040 platform with the stack's log lines in the console's output, in order with it.
class ConsolePlatform : public a2b::Rp2040Platform {
 public:
  explicit ConsolePlatform(Output& output)
      : Rp2040Platform(i2c0, a2b::ab0310::kSdaPin, a2b::ab0310::kSclPin), m_Output(output) {}

  void Log(a2b::LogLevel level, const char* text) override {
    static const char* const kPrefixes[] = {"error", "warning", "info", "debug"};
    m_Output.Add(std::string(kPrefixes[static_cast<int>(level)]) + ": " + text + "\n");
  }

 private:
  Output& m_Output;
};

/// Collects a typed line: echo, backspace or DEL, CR, LF or CR LF ends it; other control
/// characters and terminal escape sequences (ESC [ ... and ESC O x: the arrow keys, Home,
/// End) are ignored.
class LineEditor {
 public:
  /**
   * Takes one character.
   * @param c The character.
   * @param output Where the echo goes.
   * @param line The line, when this ends one.
   * @return True when @p line holds a line.
   */
  bool Take(int c, Output& output, std::string& line) {
    const bool afterCr = m_AfterCr;
    m_AfterCr = false;
    if (m_Escape == 1) {  // ESC [ starts a sequence, ESC O takes one byte more.
      m_Escape = (c == '[') ? 2 : ((c == 'O') ? 3 : 0);
      return false;
    }
    if (m_Escape == 3) {
      m_Escape = 0;
      return false;
    }
    if (m_Escape == 2) {  // A sequence ends with a byte from 0x40 to 0x7E.
      m_Escape = (c >= 0x40 && c <= 0x7E) ? 0 : 2;
      return false;
    }
    if (c == '\r' || c == '\n') {
      if (c == '\n' && afterCr) {
        return false;  // The LF of a CR LF.
      }
      m_AfterCr = (c == '\r');
      output.Add("\n");
      line.swap(m_Line);
      m_Line.clear();
      return true;
    }
    if (c == 0x1B) {
      m_Escape = 1;
    } else if (c == '\b' || c == 0x7F) {
      if (!m_Line.empty()) {
        m_Line.pop_back();
        output.Add("\b \b");
      }
    } else if (c >= 0x20 && c < 0x7F && m_Line.size() < kMaxLine) {
      m_Line += static_cast<char>(c);
      output.Add(std::string(1, static_cast<char>(c)));
    }
    return false;
  }

 private:
  std::string m_Line;      ///< Typed so far.
  bool m_AfterCr = false;  ///< The last character ended a line with CR.
  int m_Escape = 0;        ///< In an escape sequence: 1 after ESC, 2 after ESC [, 3 after ESC O.
};

/** @p line, trimmed and in lower case. */
std::string Word(const std::string& line) {
  const size_t first = line.find_first_not_of(' ');
  if (first == std::string::npos) {
    return std::string();
  }
  std::string word = line.substr(first, line.find_last_not_of(' ') - first + 1);
  for (char& c : word) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return word;
}

/**
 * Runs one command line: the console's, or this program's `heap` (listed after `help`);
 * `quit` stops the bus and the console goes on.
 * @param console The console.
 * @param line The line.
 * @param output Where its output goes, and the prompt after it.
 */
void Run(BenchConsole& console, const std::string& line, Output& output) {
  const std::string word = Word(line);
  if (word == "heap") {
    char text[64];
    std::snprintf(text, sizeof(text), "Heap in use: %u bytes\n",
                  static_cast<unsigned>(Rp2040HeapInUse()));
    output.Add(text);
  } else {
    const bool running = console.Execute(line.c_str());
    output.Add(console.TakeOutput());
    if (word == "help" || word == "?") {
      output.Add("heap                     the heap in use (this RP2040 program's)\n");
    }
    if (!running) {
      output.Add("Bus stopped; the console keeps running (start starts it again)\n");
    }
  }
  output.Add("A2B> ");
}

}  // namespace

/**
 * Sets up stdio and I2C, runs the start-up commands, then reads commands and polls the bus
 * every millisecond.
 * @return Never returns.
 */
int main() {
  stdio_init_all();
  std::printf("a2b_bench built %s\n", A2B_BUILD_STAMP);
  std::printf("I2C: i2c0 on GP%u/GP%u at 400 kHz, the main node's RESETn on GP%u\n",
              a2b::ab0310::kSdaPin, a2b::ab0310::kSclPin, a2b::ab0310::kResetPin);
  static Output output;
  static ConsolePlatform platform(output);
  a2b::ab0310::SetUp(platform);
  static BenchConsole console(platform);
  output.Add("A2B bench console (RP2040, no files): help lists the commands\n");
  for (const char* command : kStartup) {
    output.Add(std::string("A2B> ") + command + "\n");
    (void)console.Execute(command);
    output.Add(console.TakeOutput());
  }
  output.Add("A2B> ");

  static LineEditor editor;
  std::string line;
  uint64_t nextPollUs = time_us_64();
  for (;;) {
    // Every character waiting: the UART's FIFO holds 32, about 1.4 ms of a paste.
    for (int c = getchar_timeout_us(0); c != PICO_ERROR_TIMEOUT; c = getchar_timeout_us(0)) {
      if (editor.Take(c, output, line)) {
        Run(console, line, output);
      }
    }
    const uint64_t now = time_us_64();
    if (now >= nextPollUs) {
      console.Poll();
      output.Add(console.TakeOutput());
      nextPollUs += kPollUs;
      if (nextPollUs <= now) {  // Late (a long command): the next poll a period on, not a burst.
        nextPollUs = now + kPollUs;
      }
    }
    output.Drain();
  }
}
