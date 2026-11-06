/**
 * @file main_bench.cpp
 * @brief The bench console on a desktop host:
 *        `a2b_bench [--usbi | --i2c DEVICE] [--spi DEVICE] [--spi-hz HZ] [--board NAME|FILE]
 *        [--ignore-eeprom] [--trace FILE] [--commands=LIST] [--spi-flash-check N[=ID]]
 *        [COMMAND]...`.
 *
 * Windows reaches the A2B board through the ADI USBi (libusb, WinUSB driver). Linux uses
 * the host's I2C through i2c-dev (/dev/i2c-1 on the Pi), or the USBi with `--usbi` when
 * the console was built with libusb (a USBi on a Linux host is untried: it may need the
 * USBi's firmware loaded and a udev rule). On Linux the host's SPI to the main node (the SPI
 * data tunnel, docs/SPI.md) is spidev, /dev/spidev0.1 unless `--spi` names another; without
 * it the `spi` commands say so. The options are shorthands for the first
 * commands; each COMMAND argument (quoted if it has spaces, e.g. "link 5 utp") runs next,
 * then the console reads commands from the keyboard. Commands are read on their own
 * thread, so the bus is polled every millisecond whatever is typed; while an `eeprom` job
 * runs, the next command waits for it (lines typed meanwhile are kept), except `eeprom
 * abort` and `quit`, which run at once (BenchConsole::RunsWhileBusy()). `--commands=LIST` is
 * several commands in one argument, separated by ';' (none if LIST is empty), for a
 * launcher that passes a setting as one argument: VS Code's "a2b.windowsBenchArgs".
 * `--spi-flash-check N[=ID]` runs, after them, the SPI tunnel's hello-world on sub-node N's
 * flash and exits: `start` (unless a command started the bus), `wait node N`, `spi open N`,
 * `wait spi`, `spiflash id [ID]`, `quit`. The exit code is 0, or 1 if a step failed or the ID
 * doesn't match (docs/SPI.md).
 *
 * Copyright 2026 by ClockWorks Signal Processing LLC
 * Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include "a2b_build_stamp.h"
#include "bench_console.h"

#ifdef A2B_CPP_USBI
#include "libusb_link.h"
#include "usbi_platform.h"
#endif
#ifdef __linux__
#include "linux_platform.h"
#endif
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

/** Lines typed at the console, from the reader thread to the main loop. */
struct Input {
  std::mutex lock;
  std::deque<std::string> lines;
};

/** The reader thread: queues each line typed, until the input ends. */
void ReadLines(Input* input) {
  std::string line;
  while (std::getline(std::cin, line)) {
    const std::lock_guard<std::mutex> guard(input->lock);
    input->lines.push_back(line);
  }
}

/**
 * Takes the next line to run, if there is one: the first one typed, or while an `eeprom`
 * job runs, the first that runs at once (`eeprom abort`, `quit`); the others stay queued.
 * @param input The lines typed.
 * @param busy An `eeprom` job is running.
 * @param line Set to the line.
 * @return True if @p line holds one.
 */
bool TakeLine(Input& input, bool busy, std::string& line) {
  const std::lock_guard<std::mutex> guard(input.lock);
  for (auto it = input.lines.begin(); it != input.lines.end(); ++it) {
    if (!busy || BenchConsole::RunsWhileBusy(it->c_str())) {
      line = *it;
      input.lines.erase(it);
      return true;
    }
  }
  return false;
}

/** Prints what the console printed; the prompt after a command. */
void Flush(BenchConsole& console, bool prompt) {
  const std::string text = console.TakeOutput();
  if (!text.empty()) {
    std::fputs(text.c_str(), stdout);
  }
  if (prompt) {
    std::fputs("A2B> ", stdout);
  }
  std::fflush(stdout);
}

/** Prints the usage. */
void Usage() {
  std::fprintf(stderr,
               "a2b_bench [--usbi | --i2c DEVICE] [--spi DEVICE] [--spi-hz HZ] "
               "[--board NAME|FILE] [--ignore-eeprom] [--trace FILE] [--commands=LIST] "
               "[COMMAND]...\n"
               "  --usbi           the ADI USBi (the default on Windows)\n"
               "  --i2c DEVICE     the host's I2C, e.g. /dev/i2c-1 (the default on Linux)\n"
               "  --spi DEVICE     the host's SPI to the main node (Linux; default\n"
               "                   /dev/spidev0.1, used if it opens; none: no SPI)\n"
               "  --spi-hz HZ      its clock (default 8000000, at most 15000000)\n"
               "  --board X        board X: a built-in board or an export's C file\n"
               "  --ignore-eeprom  ignoreEEPROM 1\n"
               "  --trace FILE     i2cTrace on FILE\n"
               "  --commands=LIST  console commands separated by ';', e.g.\n"
               "                   \"--commands=ignoreEEPROM 1; link 2 custom\" (none if empty)\n"
               "  --spi-flash-check N[=ID]\n"
               "                   after the commands: open the SPI tunnel to sub-node N, read\n"
               "                   its flash's JEDEC ID (and match it with ID, e.g. EF4018),\n"
               "                   then quit; exit code 1 if anything failed\n"
               "  COMMAND          a console command, e.g. start or \"link 5 utp\"\n");
}

/**
 * Adds the commands of a `--commands=` list to @p commands: the text between ';', without
 * the spaces around it; empty ones (an empty list) add nothing.
 * @param list The list.
 * @param commands Where the commands go, in order.
 */
void AddCommandList(const char* list, std::deque<std::string>& commands) {
  std::string command;
  for (const char* p = list;; ++p) {
    if (*p != ';' && *p != '\0') {
      command += *p;
      continue;
    }
    const size_t first = command.find_first_not_of(" \t");
    if (first != std::string::npos) {
      commands.push_back(command.substr(first, command.find_last_not_of(" \t") - first + 1));
    }
    command.clear();
    if (*p == '\0') {
      return;
    }
  }
}

/**
 * Prints the command line as the program got it (an argument with a space or a quote in
 * quotes), so a launcher's mistake shows: VS Code doesn't show a debug launch's.
 * @param argc Argument count.
 * @param argv The arguments.
 */
void PrintCommandLine(int argc, char** argv) {
  std::string line = "command line:";
  for (int i = 0; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool quote = arg.empty() || arg.find_first_of(" \t\"") != std::string::npos;
    line += quote ? " \"" + arg + "\"" : " " + arg;
  }
  std::printf("%s\n", line.c_str());
}

/**
 * Takes the value option argv[i] needs, argv[i + 1]: there must be one, and it mustn't be
 * another option, or "--board --ignore-eeprom AB0020" would load a board file named
 * "--ignore-eeprom".
 * @param argc Argument count.
 * @param argv The arguments.
 * @param i The option's index; moved to its value's.
 * @param what What the value is, for the message.
 * @return The value, or nullptr with a message printed.
 */
const char* OptionValue(int argc, char** argv, int& i, const char* what) {
  if (i + 1 >= argc || std::strncmp(argv[i + 1], "--", 2) == 0) {
    std::fprintf(stderr, "a2b_bench: %s needs %s%s%s\n", argv[i], what,
                 (i + 1 < argc) ? ", not " : "", (i + 1 < argc) ? argv[i + 1] : "");
    return nullptr;
  }
  return argv[++i];
}

/**
 * Ends main() with an error. On Windows, a console window of the program's own (VS Code's
 * external console for a debug launch, a double-click) closes as it exits and takes the
 * message with it, so there it waits for Enter first.
 * @return 1, main()'s exit code.
 */
int Fail() {
#ifdef _WIN32
  DWORD processes[2];
  if (GetConsoleProcessList(processes, 2) == 1) {
    std::fprintf(stderr, "Press Enter to exit\n");
    std::getchar();
  }
#endif
  return 1;
}

/**
 * Ends the program after `quit`: prints what is left, closes the host (@p closeHost), and
 * exits with _Exit(), skipping the exit-time clean-up. That clean-up waits on stdin while the
 * reader thread is blocked reading it: on a stdin that never ends (a pipe a shell or ssh
 * holds open), a plain return from main() never finished. `quit` has stopped the bus and
 * closed the trace already; the statics are left as they are.
 * @param console The console; its exit code is the program's.
 * @param closeHost Closes the host's I2C (and SPI) or the USBi.
 */
[[noreturn]] void Quit(BenchConsole& console, void (*closeHost)()) {
  Flush(console, false);
  std::fflush(stdout);
  std::fflush(stderr);
  closeHost();
  std::_Exit(console.ExitCode());
}

}  // namespace

/**
 * Opens the host's I2C, runs the commands given, then reads commands until `quit` (which
 * exits through Quit()).
 * @param argc Argument count.
 * @param argv The options and commands (see the file's description).
 * @return 1 if the host's I2C can't be opened or an option is wrong (Fail()).
 */
int main(int argc, char** argv) {
  std::printf("a2b_bench built %s\n", A2B_BUILD_STAMP);
  PrintCommandLine(argc, argv);
  std::fflush(stdout);
#ifdef _WIN32
  bool useUsbi = true;
#else
  bool useUsbi = false;
#endif
  const char* device = "/dev/i2c-1";
  const char* spiDevice = "/dev/spidev0.1";
  bool spiNamed = false;  // --spi given: failing to open it is an error.
  long spiHz = 8000000;
  std::string flashCheck;  // --spi-flash-check: "N" or "N=ID".
  std::deque<std::string> commands;
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    const char* value = nullptr;
    if (std::strcmp(arg, "--usbi") == 0) {
      useUsbi = true;
    } else if (std::strcmp(arg, "--i2c") == 0) {
      value = OptionValue(argc, argv, i, "an I2C device, e.g. /dev/i2c-1");
      if (value == nullptr) {
        Usage();
        return Fail();
      }
      useUsbi = false;
      device = value;
    } else if (std::strcmp(arg, "--spi") == 0) {
      value = OptionValue(argc, argv, i, "an SPI device, e.g. /dev/spidev0.1, or none");
      if (value == nullptr) {
        Usage();
        return Fail();
      }
      spiDevice = value;
      spiNamed = true;
    } else if (std::strcmp(arg, "--spi-hz") == 0) {
      value = OptionValue(argc, argv, i, "a clock in Hz, e.g. 8000000");
      char* end = nullptr;
      spiHz = (value != nullptr) ? std::strtol(value, &end, 0) : 0;
      if (value == nullptr || *end != '\0' || spiHz < 1000 || spiHz > 15000000) {
        std::fprintf(stderr, "a2b_bench: --spi-hz needs 1000 to 15000000\n");
        Usage();
        return Fail();
      }
    } else if (std::strncmp(arg, "--spi-flash-check", 17) == 0) {
      value = (arg[17] == '=') ? arg + 18 : OptionValue(argc, argv, i, "a sub-node, e.g. 0=EF4018");
      if (value == nullptr || *value == '\0') {
        Usage();
        return Fail();
      }
      flashCheck = value;
    } else if (std::strcmp(arg, "--board") == 0) {
      value = OptionValue(argc, argv, i, "a built-in board or an export's C file");
      if (value == nullptr) {
        Usage();
        return Fail();
      }
      commands.push_back(std::string("board \"") + value + "\"");
    } else if (std::strcmp(arg, "--ignore-eeprom") == 0) {
      commands.push_back("ignoreEEPROM 1");
    } else if (std::strncmp(arg, "--commands=", 11) == 0) {
      AddCommandList(arg + 11, commands);
    } else if (std::strcmp(arg, "--trace") == 0) {
      value = OptionValue(argc, argv, i, "a trace file");
      if (value == nullptr) {
        Usage();
        return Fail();
      }
      commands.push_back(std::string("i2cTrace on \"") + value + "\"");
    } else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0 ||
               (arg[0] == '-' && arg[1] == '-')) {
      Usage();
      return Fail();
    } else {
      commands.push_back(arg);
    }
  }

  if (!flashCheck.empty()) {
    const size_t equals = flashCheck.find('=');
    const std::string node = flashCheck.substr(0, equals);
    const std::string id = (equals == std::string::npos) ? "" : " " + flashCheck.substr(equals + 1);
    bool started = false;
    for (const std::string& command : commands) {
      started = started || command == "start";
    }
    if (!started) {
      commands.push_back("start");
    }
    commands.push_back("wait node " + node + " 15");
    commands.push_back("spi open " + node);
    commands.push_back("wait spi 5");
    commands.push_back("spiflash id" + id);
    commands.push_back("quit");
  }

  a2b::Platform* host = nullptr;
#ifdef A2B_CPP_USBI
  static a2b::LibusbLink link;
  static a2b::UsbiPlatform usbi(link);
  if (useUsbi) {
    if (link.Open() != a2b::Result::kOk) {
      std::fprintf(stderr, "%s\n", link.Error());
      return Fail();
    }
    std::printf("ADI USBi open\n");
    host = &usbi;
  }
#endif
#ifdef __linux__
  static a2b::LinuxPlatform linuxI2c;
  if (!useUsbi) {
    if (linuxI2c.Open(device) != a2b::Result::kOk) {
      std::fprintf(stderr, "Cannot open %s (is I2C enabled, and may this user use it?)\n", device);
      return Fail();
    }
    std::printf("I2C %s open\n", device);
    host = &linuxI2c;
    if (std::strcmp(spiDevice, "none") != 0) {
      if (linuxI2c.OpenSpi(spiDevice, static_cast<uint32_t>(spiHz), 0) == a2b::Result::kOk) {
        std::printf("SPI %s open at %ld Hz\n", spiDevice, spiHz);
      } else if (spiNamed) {
        std::fprintf(stderr, "Cannot open %s (is SPI enabled, and may this user use it?)\n",
                     spiDevice);
        return Fail();
      } else {
        std::printf("No SPI (%s doesn't open): the spi commands are unavailable\n", spiDevice);
      }
    }
  }
#else
  (void)device;
  (void)spiNamed;
  if (std::strcmp(spiDevice, "/dev/spidev0.1") != 0 || spiHz != 8000000) {
    std::printf("--spi and --spi-hz do nothing here: this host has no SPI to the main node\n");
  }
#endif
  if (host == nullptr) {
    std::fprintf(stderr, "%s is not built into this console\n",
                 useUsbi ? "The USBi (libusb)" : "The host's I2C");
    return Fail();
  }

  static BenchConsole console(*host);
  void (*const closeHost)() = [] {
#ifdef A2B_CPP_USBI
    link.Close();
#endif
#ifdef __linux__
    linuxI2c.CloseSpi();
    linuxI2c.Close();
#endif
  };
  std::printf("A2B bench console: help lists the commands\n");
  // Typed lines are read from the start, so `eeprom abort` reaches a start-up command's job.
  static Input input;
  std::thread(ReadLines, &input).detach();
  bool running = true;
  while (running && !commands.empty()) {
    std::printf("A2B> %s\n", commands.front().c_str());
    running = console.Execute(commands.front().c_str());
    commands.pop_front();
    Flush(console, false);
    while (running && console.Busy()) {  // An `eeprom` job: the next command waits for it.
      std::string line;
      if (TakeLine(input, true, line)) {
        running = console.Execute(line.c_str());
      }
      console.Poll();
      Flush(console, false);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  if (!running) {
    Quit(console, closeHost);
  }
  Flush(console, true);

  while (running) {
    std::string line;
    if (TakeLine(input, console.Busy(), line)) {
      running = console.Execute(line.c_str());
      Flush(console, running && !console.Busy());  // No prompt while a job holds the input.
    }
    const bool busy = console.Busy();
    console.Poll();
    Flush(console, busy && !console.Busy());  // The prompt again when a job ends.
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  Quit(console, closeHost);
}
