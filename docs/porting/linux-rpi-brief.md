> **Imported** from `CLAUDE.md` in the `A2BPnP130RPiZero` repo (commit e61ffa2); only the
> project name was removed from the title.
> In this repo the Linux app folder is `app/linux_rpi/` (not `apps/linux_rpi/`), and the RP2040
> PAL it refers to is `app/rp2040/a2bstack-pal/`. The `docs/` files it mentions were never
> committed anywhere.

# CLAUDE.md — A2B PnP Stack Linux/RPi Port
**ClockWorks Signal Processing LLC**

---

## Start Here

Before writing any code, read `docs/A2B_Project_Summary.md` in full.
That document is the authoritative reference for this project's architecture,
constraints, library choices, and porting approach. All decisions must be
consistent with it.

Also read the relevant ADI documentation in `docs/adi_extracted/` before
implementing any PAL function. Key files:
- `AE_09_A2B_Stack_UserGuide.txt` — PAL function signatures and requirements
- `A2B_PnP_UserGuide.txt` — PnP porting constraints, Linux notes (§5.6.1)
- `Raspberry_Pi_Zero_2_W_Development_Environment.txt` — toolchain requirements

**One-time setup prerequisite:** Before this file is useful, `docs/adi_extracted/`
must be populated by running the extraction script in `PROJECT_DOCS_INDEX.md`.
If that directory is empty or missing, stop and run the extraction first.
Claude Code reads PAL signatures and register values from those text files —
it must not derive them from memory.

---

## What This Port Is

This project is a port of the existing RP2040/Pico C console app to Linux
on a Raspberry Pi Zero 2W (32-bit Raspberry Pi OS, armhf).

**Only these things change:**
- The PAL implementation (`a2bstack-pal/adi_a2b_pal.c`) — Pico SDK calls
  replaced with Linux equivalents
- The application directory — a new `apps/linux_rpi/` peer to the existing
  Pico app directory
- The build system — `CMakeLists.txt` and `toolchain-pi.cmake` replacing
  `pico_sdk_import.cmake` and Pico-specific CMake functions

**Nothing else changes.** The stack core, plugins, and protobuf parser are
copied as-is. PAL function signatures are unchanged.

---

## Directories — Do Not Modify

These are ADI-owned. Copy them as-is per Stack UG §3.2. Never modify:

```
a2bstack/
a2bplugin-master/
a2bplugin-slave/
a2bstack-protobuf/
```

The PAL file that gets replaced is `a2bstack-pal/adi_a2b_pal.c`. The existing
Pico version of this file is the reference — read it before implementing the
Linux version. The Linux PAL is a new peer implementation, not an in-place
edit of the Pico file.

---

## Critical Implementation Rules

Violating any of these will produce code that either does not compile,
does not link, or fails at runtime on the AD2437.

### 1. `a2b_pal_I2cWriteReadFunc` — single ioctl, atomic

This function performs an atomic repeated-start I2C transaction. It must
be implemented as a **single `ioctl(I2C_RDWR)` call with two `i2c_msg`
structs** — the first for the write (register address), the second with
`I2C_M_RD` set for the read.

```c
struct i2c_msg msgs[2];
struct i2c_rdwr_ioctl_data msgset;

msgs[0].addr  = addr;
msgs[0].flags = 0;           // write
msgs[0].len   = wlen;
msgs[0].buf   = wbuf;

msgs[1].addr  = addr;
msgs[1].flags = I2C_M_RD;   // read
msgs[1].len   = rlen;
msgs[1].buf   = rbuf;

msgset.msgs  = msgs;
msgset.nmsgs = 2;

ioctl(fd, I2C_RDWR, &msgset);
```

**Two separate `ioctl` calls are not equivalent** and will fail for
register reads on the AD2437. Do not implement this as write followed
by read.

### 2. `a2b_pal_I2cReadFunc` — single ioctl, read only

This function performs a read-only I2C transaction. It must be implemented
as a **single `ioctl(I2C_RDWR)` call with one `i2c_msg` struct** with
`I2C_M_RD` set. It is not a write followed by a read.

```c
struct i2c_msg msg;
struct i2c_rdwr_ioctl_data msgset;

msg.addr  = addr;
msg.flags = I2C_M_RD;
msg.len   = rlen;
msg.buf   = rbuf;

msgset.msgs  = &msg;
msgset.nmsgs = 1;

ioctl(fd, I2C_RDWR, &msgset);
```

### 3. `A2B_FEATURE_OPTIMAL_RESPCYCS` — must be defined

In `features.h`, this macro must be defined unconditionally. Every field
installation of this system will have cables longer than 5m. Without this
flag, discovery will work on the bench with short cables and fail
intermittently in the field.

**Verification note:** This cannot be confirmed with short bench cables —
a bench test passes regardless of whether the flag is set. Functional
verification requires cables ≥5m in the test setup.

### 4. GPIO — libgpiod v1 API only

Use the `libgpiod` v1 API (`gpiod_chip_open`, `gpiod_line_request_output`,
`gpiod_line_set_value`, `gpiod_line_event_wait`, etc.).

Do NOT use:
- sysfs GPIO (`/sys/class/gpio/`)
- `pigpio`
- `wiringPi`
- `bcm2835`
- Any library not listed in the dev environment doc

`libgpiod` v1.6.3 is pre-installed on Raspberry Pi OS. Do not require v2.

**GPIO pin numbers for AD2437 RESETn and IRQ are TBD pending PCB design.**
Use `#define` constants for these pin numbers — do not hardcode them.
Add a comment marking them as TBD so they are easy to find.

### 5. I2C — kernel interface only

Use `/dev/i2c-1` via `open()` and `ioctl()` with `linux/i2c-dev.h`.
This corresponds to GPIO2 (SDA) and GPIO3 (SCL) — the hardware I2C pins.
Requires `dtparam=i2c_arm=on` in `/boot/config.txt`.

Verify the bus is present before first run: `ls /dev/i2c-*` and
`i2cdetect -l`. The bus number can shift if software I2C overlays are active.

Do NOT use:
- `libi2c`
- `wiringPi`
- Any other I2C wrapper library

### 6. Build system — CMake only

Use CMake with a `toolchain-pi.cmake` cross-compilation toolchain file.
Target `arm-linux-gnueabihf-gcc` via `crossbuild-essential-armhf`.

Do NOT use:
- Plain Makefiles
- Autotools
- SCons
- Any other build system

The `CMakeLists.txt` must have Debug and Release build types and produce
a binary named `a2b_pnp`.

### 7. Timer — CLOCK_MONOTONIC

`a2b_pal_TimerGetSysTimeFunc` must return milliseconds using
`clock_gettime(CLOCK_MONOTONIC)`:

```c
struct timespec ts;
clock_gettime(CLOCK_MONOTONIC, &ts);
return (uint32_t)((ts.tv_sec * 1000ULL) + (ts.tv_nsec / 1000000ULL));
```

Use `nanosleep()` for delays. Do not use `usleep()` (deprecated).

### 8. SPI — kernel interface only

SPI is used in this project for subnode firmware updates via the A2B data
tunnel, operating only when the system is in offline service mode.

For Stage 1, implement SPI PAL functions as working stubs that return
success — this allows the stack to initialize without the full SPI
implementation. Do not attempt to implement the full SPI data tunnel in
Stage 1.

When full implementation is needed (Stage 2+), use `/dev/spidev*` via
`open()` and `ioctl(SPI_IOC_MESSAGE)`. Do not use any SPI wrapper library.

### 9. 32-bit target guard

Add the following compile-time assert to the PAL source. If someone
accidentally builds with a 64-bit toolchain, `A2B_CONF_POINTER_SIZE = 4`
will be silently wrong and the stack will misbehave at runtime.

```c
_Static_assert(sizeof(void*) == 4, "Must build for 32-bit ARM — check toolchain");
```

---

## `features.h` — Verify Before First Build

| Macro | Required Setting |
|---|---|
| `A2B_FEATURE_OPTIMAL_RESPCYCS` | **Must be defined** |
| `A2B_FEATURE_COMM_CH` | Carry forward from Pico app; do NOT enable until mailbox message format is defined |
| `A2B_FEATURE_MEMORY_MANAGER` | Carry forward from Pico app |
| `A2B_CONF_POINTER_SIZE` | Must be 4 (32-bit ARM target) |
| `A2B_CONF_MAX_NUM_MASTER_NODES` | Set per existing Pico app |
| `A2B_CONF_MAX_NUM_SLAVE_NODES` | Set per existing Pico app |

---

## Porting Reference Table

| Pico SDK | Linux Replacement |
|---|---|
| `i2c_write_blocking()` | `ioctl(I2C_RDWR)` single write msg |
| `i2c_read_blocking()` | `ioctl(I2C_RDWR)` single read msg (`I2C_M_RD`) |
| Repeated-start via `nostop` | Single `ioctl` with two `i2c_msg` structs |
| `time_us_64()` | `clock_gettime(CLOCK_MONOTONIC)` |
| `sleep_ms()` | `nanosleep()` |
| `gpio_put()` / `gpio_get()` | `gpiod_line_set_value()` / `gpiod_line_get_value()` |
| `pico_sdk_import.cmake` | `toolchain-pi.cmake` |
| `stdio_init_all()` | Remove — plain stdio works |
| Pico-specific `target_link_libraries` | `-lgpiod` (if GPIO used) |

---

## New Directory Structure

```
apps/
  pico/               ← existing, do not touch
  linux_rpi/          ← create this
    main.c
    pal_i2c.c
    pal_timer.c
    pal_gpio.c        ← GPIO used for RESETn / IRQ; pin #defines are TBD placeholders
    CMakeLists.txt
    toolchain-pi.cmake
```

Shared common source files (command parsing, etc.) are referenced by
relative path from `apps/linux_rpi/CMakeLists.txt`. Do not copy them.

---

## Hardware Context (Main Node)

- AD2437 connects to Pi Zero 2W via `/dev/i2c-1` (GPIO2=SDA, GPIO3=SCL)
- AD2437 `RESETn` connects to a Pi Zero GPIO — assert low to force re-discovery.
  **Pin number TBD — use `#define RESET_N_GPIO_PIN  <TBD>` in `pal_gpio.c`.**
- AD2437 `IRQ` connects to a Pi Zero GPIO — signals stack that status has changed.
  **Pin number TBD — use `#define IRQ_GPIO_PIN  <TBD>` in `pal_gpio.c`.**
- I2C bus speed: 400 kHz — requires `dtparam=i2c_arm=on` and
  `dtparam=i2c_arm_baudrate=400000` in `/boot/config.txt`. Default is 100 kHz
  if the baudrate line is absent. Verify: `i2cdetect -l`.
- `A2B_CONTROL.XCVRBINV` must be set on all nodes — ADI RJ45 boards swap
  data polarity on Port B (see A2B plan Tables 1 and 2)
- Discovery is one-shot on power-up; re-discovery is triggered by asserting RESETn

---

## What to Flag, Not Fix

If you encounter any of the following, flag them and ask before proceeding:

- Any Pico PAL code that has no obvious Linux equivalent
- Any hardcoded paths, addresses, or bus numbers that may differ on the Pi
- Any Pico-specific memory allocation patterns that may not be appropriate
  for Linux userspace
- Any register maps or initialization sequences — do not reimplement these
  from memory; read them from `docs/adi_extracted/ad2437-trm-0_2.txt`
- Any GPIO pin number needed for RESETn or IRQ — these are TBD; use the
  `#define` placeholders and flag that the values need to be filled in

---

## Code Generation Rule

Do not reproduce register maps, table data, or initialization sequences
from memory. Either:
1. Read the relevant values from `docs/adi_extracted/` and cite the source, or
2. Derive every entry algorithmically from first principles and show the
   derivation, or
3. Write code that generates the table programmatically and self-verifies it.

If a PAL function signature is needed, read it from the existing source
files or from `docs/adi_extracted/AE_09_A2B_Stack_UserGuide.txt`. Do not guess.
