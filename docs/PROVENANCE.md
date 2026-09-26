# Provenance

Where the code in this repository comes from, relative to the ADI release
`ADI_A2B_PnP_Software_SRC-Rel1.3.0` (`Target/software`). Comparisons ignore line
endings and whitespace.

In every ADI file header, the license lines under the ADI copyright line have been replaced
with a pointer to `LICENSE_ADI_BSD.txt` (2026-09-26). The comparisons below don't count
that change.

## Sources imported

| Part | Imported from |
|---|---|
| Shared core, `app/common`, `app/test`, `app/rp2040`, `cfg` | `PnP_RP2040`, branch `lappy`, commit c21bfff (2026-03-21), folder `software/` |
| `app/win32` (now `app/windows`), `lib/win32` (since removed), 4 extra `cfg` files | `BrewLama/A2BPnP-win32` commit e1dca33 (2024-12-20), folder `software/` (local clone `D:\Projects\A2BPnP-win32\Target`; last built 2025-01-15 with MSYS2 mingw32 gcc) |
| `app/linux_rpi` platform code | `A2BPnP130RPiZero` branch `linux_rpi-portability`, commit 7af07af (`apps/linux_rpi/`), converted here to 64-bit arm64 and libgpiod v2 |
| `docs/porting/linux-rpi-brief.md` | `A2BPnP130RPiZero` commit e61ffa2 (`CLAUDE.md`) |

`Clockworks-Signal-Processing/A2BPnP-win32` (commit 8a8f038, 2024-11-19) is **not** the win32
source. It is an older snapshot from before the `BrewLama/A2BPnP-win32` history was restarted
on 2024-11-21: stock ADI main-node groups, `"ctypes.h"`-style includes instead of `"a2b/..."`,
no `12_a2b_busconfig.c`, and committed build output.

The RP2040 code in `lappy` started (commit b9e554c, 2024-11-05) from an RP2040 package
supplied by ADI, which was already ahead of the public 1.3.0 release in the PnP layer.
Later commits changed very little in the shared code.

## Shared core

| Folder | vs ADI 1.3.0 |
|---|---|
| `a2bstack/a2bstack` (71 files) | identical |
| `a2bstack/a2bplugin-slave` (7) | identical |
| `a2bstack/a2bplugin-master` (19) | identical except `src/discovery.c` |
| `a2bstack/a2bstack-protobuf` (15) | identical except `src/a2b_bdd_helper.c` |
| `a2bcommchannel` (7) | identical |
| `a2bpnp` (11) | 9 files modified; `a2bpnp_stack_interface.*` identical |

### What differs

- **`discovery.c`**
  - `a2b_UpdateSlotConfiguration()` now returns a status, and discovery stops on failure.
  - The B-side power-switch (SWCTL) value is read from the BDD's node index 0 instead of `nodeAddr`.
- **`a2b_bdd_helper.c`**
  - AD241x-only register setup is disabled (`#if 0` / commented out).
  - The BCF file-read path was replaced with `assert(0)`, since the RP2040 has no file system. Restored after this import, because Windows reads its bus configuration from `.dat` files; the RP2040's `a2b_pal_FileOpen/Read` now report failure instead.
- **`a2bpnp/*`**
  - Positionless audio connections by chip ID: `a2b_pnp_AddConByChipId()`, `a2b_pnp_GetConnectionList()` and `a2b_pnp_ImportConnections()`, plus `ConnectionMngClass::addConnectionByChipID()`, `getListOfConnection()`, `getListOfExistingConnection()` and `IsNodePartofAnyConnection()`.
  - A new event, `A2B_PNP_CONN_IMPORT`. Connections are re-applied when a node that is part of one rejoins.
  - SWCTL handling around discovery: `a2b_pnp_PrepareSwctlforBD()` and `a2b_pnp_RestoreSwctlAftrBD()`.
  - Sizes such as the maximum number of peripherals and streams, and name/buffer lengths, moved from `a2bpnp.h` into the platform header `platform/pnp/a2bpnp_conf.h`, so each platform can size its RAM.
  - Extra debug logging, including the computed RESPCYCS value.
  - Added after this import: CONTROL.XCVRBINV can be set separately for the main node and the sub-nodes (`mainXcvrBInv`/`subXcvrBInv` in `a2bpnp_AppInitParams`; the app's `A2B_APP_DEFAULT_*_XCVRBINV` defines and `xcvrBInv` command). By default the bus configuration value is kept, and the sub-nodes copy the main node's, as before.

## Console application (`app/common`, `app/test`)

- **`a2bapp_common.c/.h`**
  - JSON export and import of the connection list (`prepareAudioConJSON()`, `parseAudioConJSON()`, via cJSON).
  - Test-mode flag.
  - Main-node group table set up for a 12-channel group G0 plus two 2-channel groups.
- **`cmd_parse.c`**
  - `exportCon` and `test` commands.
  - Includes the RP2040 platform header, so this file isn't platform-neutral yet.
- **`cmd_queue.h`**: `ADI_UART_PRINT` maps to `printf` instead of `terminal_printf`.
- **`app/test/`**: not in ADI 1.3.0. Regression-test support and cJSON.

**Fixed later:** Tx group G2 in the main-node table in `a2bapp_common.c` was imported as `.GroupChannels = {14,55}` (the typo arrived in commit d0aed1c, 2024-12-18). The Windows copy and Rx G2 have `{14,15}`; it was corrected to `{14,15}` after this import.

## Platform folders

- **`app/rp2040/`** (none of it in ADI 1.3.0 as-is)
  - Adapted from ADI's sc59x and win32 PAL: `adi_a2b_pal.c`, `adi_a2b_init.c`, `adi_a2b_externs.h`, `adi_a2b_datatypes.h`, `platform/a2b/conf.h`, `features.h` and `palecb.h`.
  - Identical to ADI's: `adi_a2b_pal.h`, `adi_a2b_driverprototypes.h`, `ctypes.h`, `a2bapp_defs.h` and `cmd_platform.h`.
  - New:
    - `a2bapp_rp2040.c/.h` (main, board pin map, AD2437 reset)
    - `adi_a2b_irq.c`, `pio_spi.c/.h` and the PIO programs
    - `12_a2b_busconfig.c`, `platform_inits.c`
    - `platform/pnp/a2bpnp_conf.h`
  - Settings: `ENABLE_AD243x_SUPPORT` is on, and `A2B_CONF_MAX_NUM_SLAVE_NODES` is 16.
- **`app/win32/`**: ADI's win32 app. Differences: `CMakeLists.txt`, `adi_a2b_multimain.c`, and an added `12_a2b_busconfig.c`.
  - Ported to 64-bit after this import and renamed `app/windows/`; `lib/win32/` was removed. See `docs/porting/README.md` (Windows).
- **`app/linux_rpi/`**: new scaffolding only at import; the Linux platform code was added
  later from `A2BPnP130RPiZero` and converted to 64-bit (see the table above and
  `docs/porting/README.md`).

## Changes made during this import

- Folders reorganized: `app/RP2040/common` and `app/RP2040/midi2a2b_platform` became `app/rp2040`.
- The RP2040 build moved into `app/rp2040/rp2040.cmake`, and the output was renamed `A2B_PnP_RP2040`.
- Two `#include` paths changed, in `cmd_parse.c` and `platform_inits.c`.
- `.vscode` paths made machine-independent.
- Left out:
  - build output and `.orig` files
  - the unused `FreeRTOS_Kernel_import.cmake`, the duplicate `spi.pio` and the duplicate `pico_sdk_import.cmake`
  - `ADI_EVM_adi_a2b_busconfig.c` and `CSP_EVM_adi_a2b_busconfig.c`, which were entirely `#if 0`
  - prebuilt Windows libraries and executables
- Verification: the RP2040 build was compared with a build of `lappy` c21bfff, both with source paths stripped and a fixed build time.
  - The only differences were link order and the program-name string.
  - All 1,748 functions compile to identical instructions, and `.rodata` is identical.
