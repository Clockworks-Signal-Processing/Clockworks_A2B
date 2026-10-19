# Windows setup (ADI USBi)

The bench console (`a2b_bench.exe`, `a2bcpp/examples/bench`) talks to the A2B main node
through an **ADI USBi** USB-to-I2C adapter, using **libusb-1.0**. It is built as a 64-bit
program with MSYS2's UCRT64 toolchain. This page covers the one-time setup, then how VS Code
builds and runs it. For what the console does, see the [bench console guide](bench_console.md).

## One-time setup

### MSYS2 (UCRT64)

1. Install MSYS2 from <https://www.msys2.org>. The default location is `C:\msys64`.
2. In an **MSYS2 UCRT64** shell, update it. Run this until it reports nothing to do; the
   first run may close the shell:

   ```bash
   pacman -Syu
   ```

3. Install the tools:

   ```bash
   pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-libusb mingw-w64-ucrt-x86_64-gdb
   ```

If MSYS2 isn't in `C:\msys64`, set `"a2b.msys2Root"` in your VS Code **user** settings, for
example `"a2b.msys2Root": "D:/msys64"`. The build script also takes `-Msys2Root`, or the
`MSYS2_ROOT` environment variable.

If `pacman` stops at "checking available disk space" (this can happen with mapped network
drives), run it with that check turned off:

```bash
sed '/^CheckSpace/d' /etc/pacman.conf > /tmp/pacman-nospace.conf
pacman --config /tmp/pacman-nospace.conf -Syu
```

### USBi driver (WinUSB)

The program finds the USBi by its USB ID, 0456:7031. Device Manager lists it as "Analog
Devices USBi (programmed)".

SigmaStudio installs a Cypress driver (CYUSB) for the USBi, which libusb can't use. The USBi
needs the **WinUSB** driver instead:

1. Download Zadig from <https://zadig.akeo.ie> and run it.
2. Choose Options → List All Devices, then pick **Analog Devices USBi (programmed)**. Check
   that the USB ID shows 0456 7031.
3. Set the driver on the right to **WinUSB** and click **Replace Driver**.

While the USBi uses WinUSB, SigmaStudio can't use it. To switch back, open Device Manager,
right-click the USBi, then choose Update driver → Browse my computer → Let me pick from a
list, and pick the Analog Devices (Cypress) driver.

The USBi can stop answering until it is power-cycled; if a run can't reach the adapter, try
that first.

## Build, run and debug in VS Code

The **A2B** sidebar menu's "Windows bench (USBi)" section runs these tasks, which are also in
Terminal → Run Task… and Run and Debug:

| Menu | Runs | What it does |
|---|---|---|
| Clean | **Windows bench: Clean** | Deletes `build/a2b-bench-windows/` |
| Build | **Windows bench: Build (Debug)** | Builds `build/a2b-bench-windows/Debug/examples/bench/a2b_bench.exe` |
| Debug | **Windows bench: Debug** (launch configuration) | Builds, then starts the program under gdb in its own console window, stopped at `main` |
| Release | **Windows bench: Release (run)** | Builds the optimized `.../Release/examples/bench/a2b_bench.exe` and runs it in the task terminal |

The builds run `a2bcpp/tools/build_windows.ps1` (`Debug`, `Release` or `clean`). The program
starts the bus on the board set in `a2b.windowsBoard` (default `AB0020`, the Clockworks
AB0020; `ref`, `AB0310` or a SigmaStudio+ export's C file also work), after any console
commands in `a2b.windowsBenchArgs` (separated by `;`, for example
`ignoreEEPROM 1; link 2 custom`). Both settings differ from PC to PC, so set them in your VS
Code **User** settings. Then it reads commands at the `A2B>` prompt (`help` lists them; `quit`
exits).

## Without VS Code

From the repository root, in PowerShell:

```powershell
a2bcpp\tools\build_windows.ps1 Release       # or Debug, or clean
build\a2b-bench-windows\Release\examples\bench\a2b_bench.exe --board AB0020 start
```

The build copies `libusb-1.0.dll` next to the program; that and Windows' own DLLs are all it
needs, so it also runs outside MSYS2.
