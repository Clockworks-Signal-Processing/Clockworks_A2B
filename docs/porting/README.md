# Porting notes

RP2040, 64-bit Linux (Raspberry Pi) and 64-bit Windows are built today. These notes cover
what the platforms need to share the common core (`a2bstack/`, `a2bcommchannel/`,
`a2bpnp/`, `app/common/`).

## Shared-code rules (any platform)

- `app/common/cmd_parse.c` includes `a2bapp_platform.h`; each `app/<platform>/` provides one.
- `a2bpnp/a2bpnp.h` includes `pnp/a2bpnp_conf.h`, which each platform must provide
  (RP2040: `app/rp2040/a2bstack-pal/platform/pnp/`, Linux: `app/linux_rpi/platform/pnp/`,
  Windows: `app/windows/a2bstack-pal/platform/pnp/`).
- Pointer size and memory alignment come from the compiler in the platform `conf.h`
  (`UINTPTR_MAX`): 32-bit/4-byte on the RP2040, 64-bit/8-byte on arm64 Linux and Windows.
  The Linux and Windows `conf.h` do this; the RP2040 copy still hard-codes 32/4, which is correct there.
- Each platform's `adi_a2b_externs.h` declares `a2b_pal_FileOpen/Read/Close`: the shared BDD
  helper calls `a2b_pal_FileRead` for a bus configuration from a file (`configType 2`). The
  RP2040 has no file system, so its versions fail.
- Anything that carries a pointer through an integer uses `a2b_UIntPtr`, never `a2b_UInt32`
  (e.g. `adi_a2b_EnablePinInterrupt()`'s `CallBackParam`).
- The root `CMakeLists.txt` belongs to the Pico SDK / Pico VS Code extension. Other
  platforms are standalone CMake projects in their own directory (`cmake -S app/<platform>`).

## Raspberry Pi Linux (`app/linux_rpi/`)

- **State**: builds as native 64-bit arm64 for 64-bit Raspberry Pi OS trixie, libgpiod v2,
  in a Debian 13 WSL distro. See `app/linux_rpi/README.md` and the "Linux arm64" VS Code tasks.
- **Origin**: platform code from the `A2BPnP130RPiZero` repo (32-bit armhf, libgpiod v1,
  stock ADI core), moved onto this repo's core and converted to arm64 / libgpiod v2.
- **Verified on hardware**: a Pi Zero 2 W on the reference board discovered all 4 sub-nodes.
  The 64-bit audit compiled everything in the current feature set; code behind features that
  are off (`ENABLE_INTERRUPT_PROCESS`, `A2B_FEATURE_COMM_CH`) has not been compiled for 64-bit.
- **Port brief**: `linux-rpi-brief.md`, the `CLAUDE.md` from that repo. Historical: it
  specifies 32-bit armhf and libgpiod v1, both superseded by the 64-bit decision.

## Windows (`app/windows/`)

- **State**: builds as a native 64-bit console app with MSYS2 UCRT64 (gcc, cmake, ninja,
  pkgconf, libusb from MSYS2). See `app/windows/README.md` and the "Windows" VS Code tasks.
- **Origin**: ADI's win32 app, imported from `BrewLama/A2BPnP-win32`, where it was last built
  (2025-01-15) as 32-bit with MSYS2 mingw32 against ADI's **stock** 1.3.0 PnP core. Its
  changes to shared files (`win32-shared-changes.patch`) are already covered by the shared
  code here.
- **What changed for the port**:
  - The folder was renamed from `app/win32` to `app/windows`: the code builds 32- or 64-bit,
    and the other platform folders are named by platform too.
  - `app/windows/CMakeLists.txt` is now a standalone project, like `app/linux_rpi`. It
    compiles the core sources directly and finds libusb with pkg-config. `lib/win32/` (ADI's
    CMake files for building the stack and PnP as prebuilt libraries) was removed.
  - The platform `conf.h` derives the pointer size, and `CallBackParam` is `a2b_UIntPtr`.
  - New `platform/pnp/a2bpnp_conf.h` (two A2B chains, one per main node) and `a2bapp_platform.h`.
  - `features.h`: `ENABLE_AD243x_SUPPORT` on (AD2437); `A2B_FEATURE_OPTIMAL_RESPCYCS` stays off.
  - `a2bapp_server.c`:
    - The read/PnP thread handshake flag is `volatile`, so an optimized build can't hang in its wait loops.
    - The threads sleep 1 ms per loop (with `timeBeginPeriod(1)`) instead of each spinning a CPU core.
- **Known limitation**: the terminal output queue (`app/common/cmd_queue.c`) is shared by
  the PnP and output threads without a lock (ADI's design). Most output goes directly to
  `printf`, so only queued messages could be affected.
- **Hardware status** (ADI USBi on the WinUSB or libusbK driver, Clockworks AB0020 main board):
  - Verified: the USBi opens through libusb, I2C works (AD2437 at 0x68 reads vendor 0xAD;
    ADAU1761 at 0x39; EEPROM at 0x50), the codec is programmed, the stack starts and discovery runs.
  - Not yet verified: sub-node discovery. The current bench mixes connection types, so it
    probably needs different XCVRBINV settings per link. To be retried with a simpler, known
    setup and/or regenerated `.dat` files.
  - ADI's `.dat` files put the ADAU1761 at 0x38 (ADI's evaluation board), so they fail on the
    AB0020 at the first codec write. `cfg/ini_AB0020.txt` uses the compiled-in configuration
    (codec at 0x39) instead.
  - The console's `xcvrBInv <main> <sub>` command sets XCVRBINV for the main node and for all
    sub-nodes (any `configType`). A different value per sub-node isn't supported yet. See
    [XCVRBINV](../../README.md#xcvrbinv-b-side-polarity) in the main README.
