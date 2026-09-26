# A2B PnP — 64-bit Windows (`app/windows`)

The Windows console version of the A2B PnP app. It talks to the A2B main node through an
**ADI USBi** USB-to-I2C adapter using **libusb-1.0**, and is built as a 64-bit program with
MSYS2's UCRT64 toolchain.

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

The app finds the USBi by its USB ID, 0456:7031. Device Manager lists it as "Analog Devices
USBi (programmed)".

SigmaStudio installs a Cypress driver (CYUSB) for the USBi, which libusb can't use. The USBi
needs the **WinUSB** driver instead:

1. Download Zadig from <https://zadig.akeo.ie> and run it.
2. Choose Options → List All Devices, then pick **Analog Devices USBi (programmed)**. Check
   that the USB ID shows 0456 7031.
3. Set the driver on the right to **WinUSB** and click **Replace Driver**.

While the USBi uses WinUSB, SigmaStudio can't use it. To switch back, open Device Manager,
right-click the USBi, then choose Update driver → Browse my computer → Let me pick from a
list, and pick the Analog Devices (Cypress) driver.

## Build, run and debug in VS Code

The **A2B** sidebar menu's Windows section runs these, and they're also in Terminal → Run
Task… and Run and Debug:

| Menu | Runs | What it does |
|---|---|---|
| Clean | **Windows: Clean** | Deletes `app/windows/build/` |
| Build | **Windows: Build (Debug)** | Builds `app/windows/build/Debug/a2b_pnp.exe` |
| Debug | **Windows Debug** (launch configuration) | Builds, then starts the program under gdb in its own console window, stopped at `main` |
| Release | **Windows: Release (run)** | Builds `app/windows/build/Release/a2b_pnp.exe` (optimized) and runs it in the task terminal |

The program prints to its console and reads commands at the `A2B>` prompt (`help` lists
them; `quit` exits).

## Command line

From the repository root, in PowerShell:

```powershell
app\windows\build.ps1 Debug          # or Release, or clean
cd app\windows\build
.\Debug\a2b_pnp.exe                # optional argument: a startup script in cfg\
```

Run it from `app\windows\build`, because its default paths to the configuration files are
`..\..\..\cfg`. The build copies `libusb-1.0.dll` next to `a2b_pnp.exe`, which is the only
DLL it needs besides Windows' own. So the program also runs outside MSYS2.

## Startup script and bus configuration

At startup the program runs a script of console commands from `cfg\`. It runs
`ini_EVAL_RJ45.txt` unless a different file name is given as the first argument.

- **`ini_AB0020.txt`** (used by the VS Code Windows Debug and Release): for the Clockworks
  AB0020 main board. It uses `configType 0`, the compiled-in configuration in
  `12_a2b_busconfig.c`. That's the same as the RP2040/Linux one, except that it also
  configures the main board's ADAU1761 codec (at I2C address 0x39; it supplies the A2B
  chip's frame sync and bit clock, so it's programmed first) and EEPROM (0x50).
- **`ini_EVAL_RJ45.txt`** and the other `ini_*.txt`: ADI's scripts for ADI's evaluation
  boards. They use `configType 2`, which loads the bus configuration from a `.dat` file in
  `cfg\` (named by `filePath`). ADI's `.dat` files put the ADAU1761 at 0x38, so they don't
  work on the AB0020.

On `quit` the program saves its audio connections to `cfg\connection.json`, and loads them
again at the next start. The file isn't tracked by git.

## Configuration notes

In `a2bstack-pal/platform/a2b/`:
- up to 2 main nodes (one USBi each) and 16 sub-nodes per network
- AD243x support on (the AD2437)
- `A2B_FEATURE_OPTIMAL_RESPCYCS` off: unlike Linux, the Windows app uses the configured
  RESPCYCS rather than probing for it
