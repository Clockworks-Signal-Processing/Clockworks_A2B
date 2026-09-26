# Preface

A lot of the work here was done by Claude Code, including creation of the documents.  I may stick my fingers in from time to time, you can tell as that will be the stuff that's confusing or wrong.

This is also very much a work in progress but easier to leave it public than try and sort out access.

The apps and the library itself are meant to be examples, not production ready code.  It's unsupported as-is code. If you find a bug and figure out how to fix it let Clockworks know.  For now we're pretty clueless about how we might maintain this so that others can contribute to it.  

Someday Analog Devices may release software for the AD2437 and the need for this goes away.

Written for Version 0.1.  26-Sep-2026.

------



# A2B_PnP

Clockworks Signal Processing's port of the Analog Devices A2B Plug-and-Play (PnP)
stack and console application. The goal is one shared core running on three platforms:

| Platform | Folder | Status |
|---|---|---|
| RP2040 (Raspberry Pi Pico SDK) | `app/rp2040/` | **Working.** Builds; tested on the AB0310 board with the RP2040 (AD2437 main node). |
| Raspberry Pi Linux (64-bit) | `app/linux_rpi/` | **Working.** Cross-built in Debian WSL; tested on a Pi Zero 2 W running 64-bit Raspberry Pi OS. See [app/linux_rpi/README.md](app/linux_rpi/README.md). |
| Windows console (64-bit) | `app/windows/` | **Builds** with MSYS2 UCRT64. On hardware, the USBi and the main node work; sub-node discovery is not verified yet. Uses the ADI USBi adapter through libusb; see [Windows](#windows-adi-usbi-adapter) and [app/windows/README.md](app/windows/README.md). |

## Layout

```
a2bstack/         ADI A2B stack, master/slave plugins, protobuf BDD parser
a2bcommchannel/   ADI communication channel
a2bpnp/           PnP layer (ADI 1.3.0 + connection-by-chip-ID and SWCTL additions)
app/common/       console / command application, shared by all platforms
app/test/         regression test support + cJSON
app/rp2040/       RP2040 platform: main, PAL (I2C, PIO SPI, timer, IRQ), bus config
app/windows/      Windows platform, 64-bit (see its README.md)
app/linux_rpi/    Linux / Raspberry Pi platform (see its README.md)
cfg/              network configuration files (.dat / .ini)
docs/             provenance of the code and porting notes
tools/            VS Code A2B menu extension (see below)
```

Each platform provides its own hardware layer (I2C/SPI access to the A2B transceiver,
timers, IRQ) and the console transport: a serial port on RP2040 and Linux, and the
Windows console on Windows. See `docs/PROVENANCE.md` for which files come from ADI
and what was changed.

## Setting up another PC

Anything that differs from PC to PC stays out of the repository. On each PC you work from:

1. Clone the repository. Any folder works; keep the path short for the RP2040 build.
2. Install the A2B menu ([Install](#install)), then reload VS Code.
3. In VS Code's **User** settings (File → Preferences → Settings → User, search for `a2b.`):
   - `a2b.piHost`, `a2b.piUser`: the Raspberry Pi this PC uses.
   - `a2b.msys2Root`: only if MSYS2 isn't in `C:\msys64`.
4. For each target you'll use on this PC:
   - **RP2040:** the Raspberry Pi Pico VS Code extension ([Building for RP2040](#building-for-rp2040)).
   - **Raspberry Pi:** Debian in WSL with the cross-compiler and `gdb-multiarch`, and this
     PC's SSH key on the Pi ([app/linux_rpi/README.md](app/linux_rpi/README.md#one-time-setup-windows)).
     Without the key, every Pi task asks for the Pi's password.
   - **Windows:** MSYS2 UCRT64, and the WinUSB driver for the USBi
     ([app/windows/README.md](app/windows/README.md)).

## Building for RP2040

Requirements: Raspberry Pi Pico SDK 2.0.0 with toolchain 13_2_Rel1, as installed by the
Raspberry Pi Pico VS Code extension under `~/.pico-sdk`.

- **VS Code:** open this folder with the Pico extension installed, then use *Compile
  Project*. *Run Project* (picotool) and the Cortex-Debug launch configurations use the
  built `build/A2B_PnP_RP2040.elf`.
- **Command line:**

  ```
  cmake -S . -B build -G Ninja
  cmake --build build
  ```

  Output: `build/A2B_PnP_RP2040.uf2`, plus `.elf`, `.bin` and `.dis`.

On Windows keep the checkout path short; the build fails when object-file paths
exceed Windows' 260-character limit.

## Windows (ADI USBi adapter)

The Windows version talks to the A2B main node through an **ADI USBi** USB-to-I2C
adapter, using the **libusb-1.0** library. It finds the adapter by its USB ID, 0456:7031
(`app/windows/a2bstack-pal/pal_i2c_usbi.c`).

- **Driver:** it does **not** use the ADI USBi driver that SigmaStudio installs.
  - libusb needs a libusb-compatible driver (WinUSB) bound to the USBi instead; [Zadig](https://zadig.akeo.ie/) is the usual tool for that.
  - While the USBi is bound to WinUSB, SigmaStudio can't use it.
  - To go back to SigmaStudio, reinstall the ADI driver for the USBi in Device Manager.
- **Firmware:** the USBi keeps its firmware. It appears directly as "Analog Devices USBi (programmed)", so nothing has to load firmware first.
- **Build:** 64-bit with MSYS2 UCRT64 (gcc, cmake, ninja, pkgconf, and libusb-1.0 from MSYS2). The build copies `libusb-1.0.dll` next to `a2b_pnp.exe`.

[app/windows/README.md](app/windows/README.md) has the setup (MSYS2, the WinUSB driver with Zadig), and how to build, run and debug, from VS Code or the command line.

## XCVRBINV (B-side polarity)

XCVRBINV is bit 4 (`0x10`) of each node's CONTROL register. It inverts the polarity of the
node's B-side transceiver, the port toward the next node downstream, so it has to match how
that link is wired. If it's wrong, discovery stops at that link: the nodes beyond it aren't
found.

- ADI's RJ45 boards swap the polarity on port B, so they need it on. The compiled-in bus
  configurations (`12_a2b_busconfig.c`) set it on for every node.
- The AB0310 + AB0331 (UTP link with UTP/RJ45 adapter) needs it off on the main node.
- A chain that mixes connection types can need a different value on each link.

### How the app sets it

The main node's value comes from the bus configuration in use: compiled-in (`configType 0`),
EEPROM or `.dat` file. By default every sub-node gets the main node's value. Two settings
change that, each `-1` (keep the bus configuration's value), `0` (off) or `1` (on):

- **The `xcvrBInv <main> <sub>` command**, before `start`. `<sub>` applies to every sub-node.
  For example, `xcvrBInv 0 1` turns it off on the main node and on for all sub-nodes.
- **Build-time defaults** `A2B_APP_DEFAULT_MAIN_XCVRBINV` and `A2B_APP_DEFAULT_SUB_XCVRBINV`
  in `app/common/a2bapp_common.h`, both `-1`. Define them (`-D`) to change a build's default.

A different value for each sub-node isn't supported yet.

With `enDbg` on, startup prints the values it uses, for example `CONTROL: main 0x10, sub-nodes 0x10`
(`0x10` = on, `0x00` = off).

If you get it wrong the main node will discover but nothing else will.  Discovery does not try different data polarities. A pox on the RJ45 connector having backwards data polarity.

### Where each platform sets it today

| Platform | Startup commands | Current setting |
|---|---|---|
| RP2040 | `cmds[]` in `app/rp2040/a2bapp_rp2040.c` | `xcvrBInv 0 -1`: off on the main node, the bus configuration's value (on) for the sub-nodes. This is for the AB0310 + AB0331 bench; remove the line for the usual hardware. |
| Raspberry Pi | `cmds[]` in `app/linux_rpi/main.c` (raise `NUM_CMDS` when adding a line) | No `xcvrBInv` line: on for every node, from the compiled-in configuration. |
| Windows | the startup script in `cfg\` named by `a2b.windowsStartupScript` | `ini_AB0020.txt` (the current startup script) has `xcvrBInv 0 1`: off on the main node, on for the sub-nodes. `ini_AB0020_binv.txt` has no `xcvrBInv` line: the bus configuration's value. |

## History

This repository started from a basic port of ADI's PnP code 1.3.0 release, but contains some code that was outside of that release.  There's some old code in this from 2024 for cable length detection but it is not reliable.  In fall of 2026 ADI released an algorithm for cable length detection but not integrated into a library.  That algorithm may eventually be incorporated into this library. 

The code is almost all from ADI, but is NOT supported by them. It is offered as-is.

The future may bring a partial or total rewrite of this library and the example applications.  



## VS Code A2B menu

`tools/vscode-a2b-menu` is a small VS Code extension that adds an **A2B** panel to the
activity bar (the icon on the far left: a square chained to two circles). The panel has
one section per target, each with the same four actions:

| Action | RP2040 | Raspberry Pi (Linux arm64) | Windows |
|---|---|---|---|
| **Clean** | Deletes `build/` and `build_release/` | Deletes `~/a2b-build/Debug`, `~/a2b-build/Release` (in Debian) and `app/linux_rpi/build/` | Deletes `app/windows/build/` |
| **Build** | Debug build in `build/` | Debug build, copied to `app/linux_rpi/build/` | Debug build in `app/windows/build/Debug/` |
| **Debug** | Builds, loads through the debug probe, stops at `main` | Builds, copies to the Pi, starts `gdbserver` there, stops at `main` | Builds, starts it under gdb in its own console window, stops at `main` |
| **Release** | Release build in `build_release/`, programmed through the debug probe, then runs | Release build, copied to the Pi and run there over SSH | Release build in `app/windows/build/Release/`, run in the task terminal |

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

### Use

Click the A2B icon, then click an action. Each action runs in a VS Code terminal, and
compiler errors appear in the Problems panel. Hover over an action to see the task or
debug configuration it runs.

**Stop All** (the square stop button in the panel's title bar, or Command Palette →
*A2B: Stop All Debug Sessions and Tasks*):
- Ends every debug session and every running task, even ones that no longer respond.
- Use it to get back to a clean state, for example after the Pi was power-cycled during a debug session.

The menu also protects against the common problems on its own:
- **Double-clicks:** clicking an action again while it's still starting does nothing.
- **Debug already running:** Debug offers to stop the running session and start again, instead of starting a second one.
- **Stale gdbserver:** before a new debug session, Debug ends any gdbserver connection left over from an earlier session.
- **Pi unreachable:** every Pi task gives up after 5 s if the Pi can't be reached, and ends about 15 s after it stops answering.
- **Missing settings:** anything that needs a setting that isn't set on this PC (such as the Pi's address) says which one, with a button that opens the settings, instead of running. This also applies to F5 / Run and Debug and Terminal → Run Task, as long as the extension is installed.

Before the first use of a target:

- **RP2040:**
  - Requires the Raspberry Pi Pico VS Code extension (see [Building for RP2040](#building-for-rp2040)).
  - Debug and Release need a CMSIS-DAP debug probe (for example a Raspberry Pi Debug Probe) connected to the board.
  - Program output comes over the Pico's USB serial port.
- **Raspberry Pi:**
  - Needs the one-time Debian WSL and SSH-key setup in [app/linux_rpi/README.md](app/linux_rpi/README.md).
  - Set the Pi's address and user as `a2b.piHost` and `a2b.piUser` in your VS Code User settings; they differ from PC to PC.
  - Program output appears in the task terminal, and is also saved on the Pi as `~/a2b_pnp.log`.
- **Windows:** needs MSYS2 UCRT64 and, for the hardware, the USBi on the WinUSB driver; see [app/windows/README.md](app/windows/README.md). If MSYS2 isn't in `C:\msys64`, set `a2b.msys2Root` in your VS Code user settings.

### How the menu is set up

The extension only runs existing tasks and launch configurations; everything it does is
also available from **Terminal → Run Task** and **Run and Debug**. The menu itself is
the `a2b.menu` setting in `.vscode/settings.json`. Each entry maps an action to either:
- `"task:<label>"`, a task in `.vscode/tasks.json`
- `"launch:<name>"`, a configuration in `.vscode/launch.json`

To change what an action does, edit the task or configuration it names, or point the
entry at a different one. An entry whose task or configuration doesn't exist shows as
"not set up yet". The menu updates as soon as the files are saved; the refresh button
in the panel's title bar also updates it.

# License

Files that were written by ADI retain their copyright message and refer to LICENSE_ADI_BSD.txt for the actual license.

If no other license is indicate the following (BSD 3 clause) applies:

Copyright (c) 2026, Clockworks Signal Processing LLC.

 All rights reserved. Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met: 

1. Redistributions of source code must retain the above copyright notice,   this list of conditions and the following disclaimer. 
2. Redistributions in binary form must reproduce the above copyright notice,   this list of conditions and the following disclaimer in the documentation   and/or other materials provided with the distribution. 
3. Neither the name of the copyright holder nor the names of its   contributors may be used to endorse or promote products derived from   this software without specific prior written permission. 



THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

