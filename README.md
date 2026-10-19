------

Clockworks generated text. No AI allowed until after "AI Section"

------



# Preface

A lot of the work here was done by Claude Code, including creation of the documents.  I may stick my fingers in from time to time, you can tell as that will be the stuff that's confusing or wrong.

This is also very much a work in progress but even in its semi-incomplete state offers a fair amount of useful functionality.

The apps and the library itself are meant to be examples, not production ready code.  It's unsupported as-is code. If you find a bug and figure out how to fix it let Clockworks know.  We're pretty clueless about how we might maintain this so that others can contribute to it.  

See `ROADMAP.md` for current code status.

# What is it?

There are three types of software here.  The first is an A^2^B stack for controlling a network of ADI A2B devices: AD2427, AD2428, AD2433, and AD2437 at present.  It's generic C/C++ code intended for 32 bit (embedded ARM) and 64 bit (Windows, embedded Linux) use.  You normally would not change this unless you need some feature that's not supported.  We intentionally don't call this a driver as it's intended to be compiled directly into your application.

The second piece are some console programs that show how to use the stack and can also be useful during development and testing.  These might be the basis for your own host application.

The last piece (not available yet) are utility programs, at the moment they generate the file(s) needed to program the per node EPPROM that's used as part of the discovery process to tell the main node what the hardware is. You normally would not change this unless you need some feature that's not supported.

## Why should I use the Clockworks A^2^B software?

The currently released Analog Devices (ADI) software for A^2^B only allows for fixed hardware configurations and is heavily dependent on using SigmaStudio+ (SS+) to set up the files used to describe the configuration.  There was a version of that ADI A^2^B software called the Plug-and-Play stack which instead discovered the network at run time.  Clockworks took that code and created a package with support for the code running on the same three hosts that  this package uses.  The actual A^2^B software was left unchanged. You can find that package at: https://github.com/Clockworks-Signal-Processing/PnP_A2B

That PnP package used the concept of a config EEPROM to allow the host software to figure out what a discovered A^2^B node does and has for peripherals. In the PnP package that was not fully implemented and there were no separate utilities to set the fields.  ADI does have a doc to describe the fields.

The ADI PnP package also had a host console program to serve as an example of using the stack as well as being useful to set up and run an A^2^B network not using SS+.

The Clockworks A^2^B software borrows heavily from the ADI PnP one.  Mostly the Clockworks software was a chance to look at what was done, see what useful, and focus on some specific use cases and not try and solve all potential ones.

One architectural change was to use C++ in an effort to keep the code simpler and easier to work with.

The biggest change was taking an AI-first approach where instead of you the developer actually editing this software you let some LLM coding-specific tool do it.  The goal being that your coding tool can quickly figure out how to integrate this software into your host application.  

# What hardware can I run this on?

The generic answer is of course any hardware using the supported chips.  If you want to see things go out of the box the host console application programs have built in support for Clockworks AB0020, AB0310, and AB0312 boards populated with AD2428, AD2433 (with UTP added), or AD2437 modules.  If an AD2427 is discovered the system assumes it's a microphone board with 4 PDM mics.  Clockworks offers 4 different boards in that configuration.

For the RP2040 host the AB0310 board is assumed by the console programs.

For the RPi Pi Zero (or any other RPi board intended to run Linux) it's assumed to connect to I2C1 and may optionally have a reset line, IRQ line, and SPI connections.

## How can I make it do something on some A^2^B hardware?

First of course you need to get some supported hardware, or make modifications to support your own hardware.  If your hardware has the configuration  EEPROM per node then you may not need to make any actual stack changes.  If your main node has peripheral chips then you'll need to use Sigma Studio+ to export the main node configuration, this software does not look for an EEPROM on the main node.

See `bench_console.md` for how to use the console program - also referred to as the 'bench' example.

Probably the most useful thing to read after this document (including the AI generated section below) is `quick_start.md`.  Don't get your hopes up based on the document being called *quick start.*  Understanding  A^2^B takes time. Using this package we hope it takes *less time* than the alternatives.



## How do I edit the code?

It is assumed you will use some sort of AI tool.  To help with the process you should instruct your AI to read all *.md files in this repo to understand what the code does and the issues that you want to avoid when developing add-ons. The ADI datasheets are included as those need to be referenced.  You may need to refresh those directly from ADI.

Particularly the `a2bcpp\docs\` directory should be reviewed.

For full details see `a2bcpp\docs\cookbook.md`



------

Written for Version 0.3.  19-Oct-2026.

Copyright 2026 Clockworks Signal Processing LLC  https://clk.works

The remainder of the documentation, unless noted otherwise, was created by Claude, either Sonnet or Opus. It may have been edited by a human.

------



------

"AI Section"  text below here was created by the AI tools.

------



# A2B_PnP

Clockworks Signal Processing's C++ stack for networks of Analog Devices A2B devices (AD2427,
AD2428, AD2433 and AD2437), with example programs for three platforms:

| Platform | Programs | Status |
|---|---|---|
| RP2040 (Raspberry Pi Pico SDK) | the fixed app (AB0310 and AB0312 bench, nothing typed), the bench console `a2b_bench` (typed commands on the UART console, no file commands) and the minimal example, in `a2bcpp/examples/fixed/rp2040`, `a2bcpp/examples/bench/rp2040` and `a2bcpp/examples/minimal/rp2040` | The fixed app and the bench console **work** on the AB0310 board (an AD2437 main node, AD2437 modules on RJ45 links, audio routed). The minimal example builds but has not been run on its board. See [Building for RP2040](#building-for-rp2040). |
| Raspberry Pi Linux (64-bit) | the bench console `a2b_bench` and the minimal example, cross-built in Debian WSL | **Works.** Tested on a Pi Zero 2 W running 64-bit Raspberry Pi OS. See [a2bcpp/docs/setup_pi.md](a2bcpp/docs/setup_pi.md). |
| Windows console (64-bit) | the bench console `a2b_bench.exe`, built with MSYS2 UCRT64 | **Works** through the ADI USBi adapter and libusb: AD2437 and AD2428 main nodes, mixed chains of the four chips. See [Windows](#windows-adi-usbi-adapter) and [a2bcpp/docs/setup_windows.md](a2bcpp/docs/setup_windows.md). |

EEPROM provisioning works on the USBi and the Pi: the image tool, `tools/a2b_eeprom`, builds
and reads module-info images (a GUI and a command line), and the bench console writes them to
the sub-nodes' EEPROMs ([provisioning.md](a2bcpp/docs/provisioning.md)).

Not included: two-step power-up on AD2437 links, power diagnostics, SPI, dual buses, spread
spectrum, GPIO over distance and a C facade. The [roadmap](ROADMAP.md) lists
them, what has been tested on hardware, the known issues and what still needs a test.

## Layout

```
a2bcpp/           the C++ stack: include/ (public API), src/, sim/ (simulator), platforms/,
                  import/ (SigmaStudio+), examples/ (minimal, fixed, bench, and clockworks:
                  the Clockworks board profiles they share), tests/, tools/, docs/
adi/              the ADI files the build still needs: stack headers, SigmaStudio+ exports,
                  module-info images (see docs/PROVENANCE.md)
docs/             PROVENANCE.md, ADI_chips/ (data sheets and TRMs), ADI_PnP/ (ADI's PnP
                  documents)
ROADMAP.md        what is not included, the known issues and what is not yet tested
tools/            a2b_eeprom/ (the EEPROM image tool: builds and reads module-info images);
                  vscode-a2b-menu/ (VS Code A2B menu extension); SS+reference/ (the
                  SigmaStudio+ projects and exports of the Clockworks boards)
```

## Documentation

In `a2bcpp/docs/`, in a reading order:

| # | Page | What it covers |
|---|---|---|
| 1 | [quick_start.md](a2bcpp/docs/quick_start.md) | Bringing up a bus with the minimal example; building for each host |
| 2 | [main_and_sub_node_programming.md](a2bcpp/docs/main_and_sub_node_programming.md) | Where the main node's and the sub-nodes' settings come from, in short |
| 3 | [bench_console.md](a2bcpp/docs/bench_console.md) | The bench console: trying boards and chains by typed commands |
| 4 | [hardware_notes.md](a2bcpp/docs/hardware_notes.md) | What the chips, boards and adapters taught us: read before changing the stack |
| 5 | [architecture.md](a2bcpp/docs/architecture.md) | How the stack is built and runs |
| 6 | [cookbook.md](a2bcpp/docs/cookbook.md) | Customising modules, links, routing and events |
| 7 | [provisioning.md](a2bcpp/docs/provisioning.md) | The module-info image (EEPROM) format, the image tool and writing EEPROMs |
| 8 | [main_node_config.md](a2bcpp/docs/main_node_config.md), [timing.md](a2bcpp/docs/timing.md), [porting.md](a2bcpp/docs/porting.md) | The main node's settings in full; every wait; a `Platform` for another host |
| 9 | [decisions.md](a2bcpp/docs/decisions.md), [testing.md](a2bcpp/docs/testing.md), [ram.md](a2bcpp/docs/ram.md) | Requirements, rationale, policies and the designs not built; the tests and the simulator; RAM on the RP2040 |
| - | [setup_pi.md](a2bcpp/docs/setup_pi.md), [setup_windows.md](a2bcpp/docs/setup_windows.md) | Setting up a PC for the Pi and for Windows |

The API reference is Doxygen's (`cd a2bcpp && doxygen`; [testing.md](a2bcpp/docs/testing.md#doxygen)).

### Words used here

| Word | Meaning |
|---|---|
| bench | A lab test set-up: A2B boards, modules and cables on a desk |
| bench console | `a2b_bench`, the interactive test program for a bench ([guide](a2bcpp/docs/bench_console.md)) |
| fixed app | The example that runs one fixed set-up (an AB0310 with an AB0312) with nothing typed |
| `ref` board | The reference board of ADI's Pi and RP2040 apps: the bench console's built-in board from `adi/exports/rp2040` |
| main node, sub-node | ADI's current names for the A2B master and slave nodes |
| module info, image | A sub-node's settings, channel groups and peripheral programs, as its EEPROM holds them |
| External (target chip) | An image for a base board whose A2B transceiver is a plug-in module: any supported chip takes it |
| golden trace | An I2C trace captured from ADI's stack, which the C++ stack's traffic was checked against |
| scenario N | A numbered simulator test case ([testing.md](a2bcpp/docs/testing.md#simulator-scenarios)) |

`docs/PROVENANCE.md` says which files come from ADI. The applications and the library are
examples, not production code.

## Setting up another PC

Anything that differs from PC to PC stays out of the repository. On each PC you work from:

1. Clone the repository. Any folder works; keep the path short for the RP2040 build.
2. Install the A2B menu ([Install](#install)), then reload VS Code.
3. In VS Code's **User** settings (File → Preferences → Settings → User, search for `a2b.`):
   - `a2b.piHost`, `a2b.piUser`: the Raspberry Pi this PC uses.
   - `a2b.msys2Root`: only if MSYS2 isn't in `C:\msys64`.
   - `a2b.piBoard`, `a2b.windowsBoard`: only to start another board in the bench console (defaults `ref` and `AB0020`, set in `.vscode/settings.json`).
4. For each target you'll use on this PC:
   - **RP2040:** the Raspberry Pi Pico VS Code extension ([Building for RP2040](#building-for-rp2040)).
   - **Raspberry Pi:** Debian in WSL with the cross-compiler and `gdb-multiarch`, and this
     PC's SSH key on the Pi ([a2bcpp/docs/setup_pi.md](a2bcpp/docs/setup_pi.md)).
     Without the key, every Pi task asks for the Pi's password.
   - **Windows:** MSYS2 UCRT64, and the WinUSB driver for the USBi
     ([a2bcpp/docs/setup_windows.md](a2bcpp/docs/setup_windows.md)).

## Building for RP2040

Requirements: Raspberry Pi Pico SDK 2.0.0 with toolchain 13_2_Rel1, as installed by the
Raspberry Pi Pico VS Code extension under `~/.pico-sdk`.

Three programs of the C++ stack, each a Pico SDK project of its own:

| Example | Folder | Program |
|---|---|---|
| fixed (AB0310 and AB0312 bench, nothing typed) | `a2bcpp/examples/fixed/rp2040` | `a2b_fixed.elf` |
| bench console (the `A2B>` commands on the UART console, AB0310 wiring; no file commands) | `a2bcpp/examples/bench/rp2040` | `a2b_bench.elf` |
| minimal (the least an app needs) | `a2bcpp/examples/minimal/rp2040` | `a2b_minimal.elf` |

The bench console is the Pi's and Windows' `a2b_bench` without what needs a file system
(`eeprom`, `i2cTrace`, boards and programs from files); the
[bench console guide](a2bcpp/docs/bench_console.md#on-the-rp2040) has the details.

- **VS Code:** open this folder with the Pico extension installed, then use the A2B menu's
  **RP2040 fixed**, **RP2040 bench** and **RP2040 minimal** sections (or Terminal → Run Task,
  `RP2040 fixed: ...` / `RP2040 bench: ...` / `RP2040 minimal: ...`, and F5 on
  `RP2040 fixed: Debug` or `RP2040 bench: Debug`). Debug builds go in
  `build/a2b-<example>-rp2040`, Release builds in `build/a2b-<example>-rp2040-release`.
- **Command line** (example `fixed`; Debug shown, add `-DCMAKE_BUILD_TYPE=Release` and another
  `-B` folder for Release):

  ```
  cmake -S a2bcpp/examples/fixed/rp2040 -B build/a2b-fixed-rp2040 -G Ninja -DCMAKE_BUILD_TYPE=Debug
  cmake --build build/a2b-fixed-rp2040
  ```

  Output: `build/a2b-fixed-rp2040/a2b_fixed.elf`, plus `.uf2`. Run these in VS Code's terminal,
  where the Pico extension puts `cmake`, `ninja` and the toolchain on `PATH`. Programming
  and debugging need a CMSIS-DAP debug probe; [a2bcpp/docs/quick_start.md](a2bcpp/docs/quick_start.md)
  has the details (wiring, UART console, the OpenOCD command).

On Windows keep the checkout path short; the build fails when object-file paths
exceed Windows' 260-character limit.

## Windows (ADI USBi adapter)

The Windows version talks to the A2B main node through an **ADI USBi** USB-to-I2C
adapter, using the **libusb-1.0** library. It finds the adapter by its USB ID, 0456:7031
(`a2bcpp/platforms/usbi/`).

- **Driver:** it does **not** use the ADI USBi driver that SigmaStudio installs.
  - libusb needs a libusb-compatible driver (WinUSB) bound to the USBi instead; [Zadig](https://zadig.akeo.ie/) is the usual tool for that.
  - While the USBi is bound to WinUSB, SigmaStudio can't use it.
  - To go back to SigmaStudio, reinstall the ADI driver for the USBi in Device Manager.
- **Firmware:** the USBi keeps its firmware. It appears directly as "Analog Devices USBi (programmed)", so nothing has to load firmware first.
- **Build:** 64-bit with MSYS2 UCRT64 (gcc, cmake, ninja, pkgconf, and libusb-1.0 from MSYS2). The build copies `libusb-1.0.dll` next to the program.

VS Code builds and runs the C++ stack's bench console, `a2b_bench.exe` (`a2bcpp/examples/bench`),
with `a2bcpp/tools/build_windows.ps1` into `build/a2b-bench-windows/<Debug|Release>/examples/bench/`;
the [bench console guide](a2bcpp/docs/bench_console.md) has its commands.
[a2bcpp/docs/setup_windows.md](a2bcpp/docs/setup_windows.md) has the setup (MSYS2, the WinUSB
driver with Zadig).

## XCVRBINV (B-side polarity)

XCVRBINV is bit 4 (`0x10`) of each node's CONTROL register. It inverts the polarity of the
node's B-side transceiver, the port toward the next node downstream, so it has to match how
that link is wired. If it's wrong, discovery stops at that link: the nodes beyond it aren't
found.

- ADI's RJ45 boards swap the polarity on port B, so they need it on. The Pi's SigmaStudio+
  export (`adi/exports/rp2040`) sets it on for every node.
- The AB0310 + AB0331 (UTP link with UTP/RJ45 adapter) needs it off on the main node.
- The AB0020 is UTP on both ports: every link needs it off.
- A chain that mixes connection types can need a different value on each link.

### How the app sets it

The polarity is a per-link setting, the `bSideInverted` field of a `LinkProfile`. The main node's
link takes its value from the SigmaStudio+ export (`BoardConfig::defaultLink`: inverted if the
export's main-node CONTROL sets XCVRBINV). Every other link takes, in order, the app's
`Network::SetLinkProfile(n, profile)`, the upstream node's `SubNode::DownstreamLink()`, then the
board default. In the bench console, `link N utp`, `link N rj45` and `link N custom [inverted]`
set it per position. The presets are `Utp` and `Custom` (straight) and `Rj45` (inverted); see
[cookbook.md](a2bcpp/docs/cookbook.md#link-profiles) and
[hardware_notes.md](a2bcpp/docs/hardware_notes.md#xcvrbinv-and-the-order-of-writes).

If you get it wrong the main node will discover but nothing else will.  Discovery does not try different data polarities. A pox on the RJ45 connector having backwards data polarity.

## History

This repository started from a basic port of ADI's PnP code 1.3.0 release, but contains some code that was outside of that release.  There's some old code in this from 2024 for cable length detection but it is not reliable.  In fall of 2026 ADI released an algorithm for cable length detection but not integrated into a library.  That algorithm may eventually be incorporated into this library. 



## VS Code A2B menu

`tools/vscode-a2b-menu` is a small VS Code extension that adds an **A2B** panel to the
activity bar (the icon on the far left: a square chained to two circles). The panel has
one section per target, each with the same four actions:

| Action | RP2040 fixed / bench / minimal | Raspberry Pi bench | Windows bench (USBi) |
|---|---|---|---|
| **Clean** | Deletes `build/a2b-<example>-rp2040` and `build/a2b-<example>-rp2040-release` | Deletes `~/a2b-cpp-build/a2bcpp-Debug`, `a2bcpp-Release` (in Debian) and `build/a2b-bench-pi/` | Deletes `build/a2b-bench-windows/` |
| **Build** | Debug build in `build/a2b-<example>-rp2040` | Debug build, copied to `build/a2b-bench-pi/Debug/` | Debug build in `build/a2b-bench-windows/Debug/` |
| **Debug** | Builds, loads through the debug probe, stops at `main` | Builds, copies to the Pi, starts `gdbserver` there, stops at `main` | Builds, starts it under gdb in its own console window, stops at `main` |
| **Release** | Release build in `build/a2b-<example>-rp2040-release`, programmed through the debug probe, then runs | Release build, copied to the Pi and run there over SSH | Release build in `build/a2b-bench-windows/Release/`, run in the task terminal |

The Pi and Windows entries run the bench console, `a2b_bench` (`a2bcpp/examples/bench`), which
starts the bus on the board set in `a2b.piBoard` / `a2b.windowsBoard` and then reads commands at
the `A2B>` prompt ([bench console guide](a2bcpp/docs/bench_console.md)). The Pi section also has
**Trace** (runs it with `--trace traces/boot.txt`) and **Get traces** (copies `~/traces` from the Pi).
The RP2040 bench entry builds the same console for the Pico: it starts with the AB0310 board
picked, and its `A2B>` prompt is on the Pico's UART console.

The extension only runs existing tasks and launch configurations; everything it does is also
available from **Terminal → Run Task** and **Run and Debug**. The details (Stop All, the settings,
how the menu is defined) are in [tools/vscode-a2b-menu/README.md](tools/vscode-a2b-menu/README.md).

### Install

Once on each machine, from the repository folder:

```
powershell -ExecutionPolicy Bypass -File tools\vscode-a2b-menu\install.ps1
```

Then, in VS Code: Command Palette (Ctrl+Shift+P) → **Developer: Reload Window**. The A2B
icon appears in this workspace only.

The script packages the extension as a `.vsix` in `%TEMP%` and installs it with
`code --install-extension`, so VS Code's `code` command must be on the PATH (the VS Code
installer adds it). It needs no Node.js or other tools. Run it again after changing
the extension. To remove it: Extensions view → **A2B Menu** → Uninstall.

Before the first use of a target:

- **RP2040:** requires the Raspberry Pi Pico VS Code extension (see [Building for RP2040](#building-for-rp2040)). Debug and Release need a CMSIS-DAP debug probe (for example a Raspberry Pi Debug Probe) connected to the board. Program output comes over the Pico's uart0 (GP0/GP1, 230400 baud), the Debug Probe's UART port.
- **Raspberry Pi:** needs the one-time Debian WSL and SSH-key setup in [a2bcpp/docs/setup_pi.md](a2bcpp/docs/setup_pi.md). Set the Pi's address and user as `a2b.piHost` and `a2b.piUser` in your VS Code User settings; they differ from PC to PC. The program is `~/a2b_bench` on the Pi. Its output appears in the task terminal, and is also saved on the Pi as `~/a2b_bench.log`.
- **Windows:** needs MSYS2 UCRT64 (with pkgconf and libusb) and, for the hardware, the USBi on the WinUSB driver; see [a2bcpp/docs/setup_windows.md](a2bcpp/docs/setup_windows.md). If MSYS2 isn't in `C:\msys64`, set `a2b.msys2Root` in your VS Code user settings.

# License

Files that were written by ADI retain their copyright message and refer to LICENSE_ADI_BSD.txt for the actual license.

If no other license is indicate the following (BSD 3 clause) applies:

Copyright (c) 2026, Clockworks Signal Processing LLC.

 All rights reserved. Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met: 

1. Redistributions of source code must retain the above copyright notice,   this list of conditions and the following disclaimer. 
2. Redistributions in binary form must reproduce the above copyright notice,   this list of conditions and the following disclaimer in the documentation   and/or other materials provided with the distribution. 
3. Neither the name of the copyright holder nor the names of its   contributors may be used to endorse or promote products derived from   this software without specific prior written permission. 



THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

