# A2B PnP — 64-bit Linux / Raspberry Pi (`app/linux_rpi`)

Builds `a2b_pnp` as a native **64-bit (arm64)** program for 64-bit Raspberry Pi
OS, cross-compiled on Windows in a **Debian WSL** distro. It uses the same
shared core as the RP2040 build (`a2bstack/`, `a2bpnp/`, `a2bcommchannel/`,
`app/common/`); only the platform layer here is Linux-specific.

Reference board: Pi Zero 2 W running Raspberry Pi OS **trixie (Debian 13), 64-bit**.
Any 64-bit Pi (3, 4, 5, Zero 2 W, CM3/4) running 64-bit Pi OS works.

## One-time setup (Windows)

### Debian WSL matching the Pi

The build host's Debian release must match the Pi's, so the C library (glibc)
matches; otherwise the Pi reports `GLIBC_2.xx not found`. For trixie that's
Debian 13. Check both sides with `cat /etc/debian_version`.

In **Debian** (`wsl -d Debian`):

```bash
sudo dpkg --add-architecture arm64
sudo apt-get update
sudo apt-get install crossbuild-essential-arm64 cmake pkg-config libgpiod-dev:arm64
```

Check: `PKG_CONFIG_LIBDIR=/usr/lib/aarch64-linux-gnu/pkgconfig pkg-config --modversion libgpiod`
should print **2.x**. (1.x means the v1 library, which this port does not use.)

WSL's default distro may be something else (e.g. Ubuntu). The VS Code tasks
name Debian explicitly, so this doesn't matter for them; to change the default
anyway: `wsl --set-default Debian`.

### SSH key on the Pi (once per PC, for each Pi)

Deploy, Run and Debug use Windows' `ssh`/`scp`, which log in with a key from
`%USERPROFILE%\.ssh`. A key belongs to one PC: each PC you work from needs its own key
installed on each Pi it uses. Until then every task asks for the Pi's password (Debug
asks three times).

In PowerShell on the PC (replace `<user>` and `<pi-address>`):

1. See whether the PC already has a key (`id_ed25519.pub` or `id_rsa.pub`):
   ```powershell
   dir $env:USERPROFILE\.ssh\id_*.pub
   ```
2. If it doesn't, create one. Press Enter at each prompt: with a passphrase, every task
   would ask for it instead.
   ```powershell
   ssh-keygen -t ed25519
   ```
3. Install it on the Pi. This asks for the Pi's password one last time. If the key from
   step 1 is `id_rsa.pub`, use that name.
   ```powershell
   type $env:USERPROFILE\.ssh\id_ed25519.pub | ssh <user>@<pi-address> "mkdir -p ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 700 ~/.ssh && chmod 600 ~/.ssh/authorized_keys"
   ```
4. Check: this prints `ok` without asking for anything.
   ```powershell
   ssh -o BatchMode=yes <user>@<pi-address> echo ok
   ```

The first connection to a Pi asks whether to trust its host key; answer `yes`. If ssh
warns `REMOTE HOST IDENTIFICATION HAS CHANGED` instead (the Pi was re-imaged, or a
different Pi now has that address), remove the old key with `ssh-keygen -R <pi-address>`
and connect again.

## Build, deploy, run from VS Code

Open the repository folder (`A2B_PnP`) in VS Code on Windows, as for the RP2040
build (not "Reopen in WSL"). The RP2040 is the default for Ctrl+Shift+B and F5;
the Linux build is reached only through the tasks and the debug entry below.

The Pi's address and user are the settings `a2b.piHost` and `a2b.piUser`. They differ
from PC to PC, so set them in each PC's VS Code **User** settings (File → Preferences →
Settings → User, search for `a2b.pi`). Don't put them in `.vscode/settings.json`: a value
there would override every PC's own. If they're missing, the A2B menu says so.

The **A2B** sidebar menu (`tools/vscode-a2b-menu`) has Clean / Build / Debug / Release
for the Pi under "Raspberry Pi (Linux arm64)". It runs these tasks, which are also in
Terminal → Run Task…:

| Task | What it does |
|---|---|
| **Linux arm64: Build (Debug)** | Builds in Debian. The build directory is `~/a2b-build/Debug` inside Debian (WSL1 can't set file permissions on Windows drives, which CMake needs); the finished `a2b_pnp` is copied to `app/linux_rpi/build/`. |
| **Linux arm64: Build (Release)** | The same, optimized: `~/a2b-build/Release`, copied to `app/linux_rpi/build/release/`. |
| **Linux arm64: Clean** | Removes both build directories and `app/linux_rpi/build/`. |
| **Linux arm64: Deploy to Pi** | Builds, stops any running copy on the Pi, then copies `a2b_pnp` to the Pi's home directory. |
| **Linux arm64: Run on Pi** | Runs `./a2b_pnp` on the Pi over SSH, in the task terminal. Ctrl+C stops it. |
| **Linux arm64: Release (deploy and run)** | Builds Release, stops any running copy, copies it to the Pi (same `~/a2b_pnp`) and runs it. |
| **Linux arm64: Stop on Pi** | Stops `a2b_pnp` / `gdbserver` on the Pi. |
| **Linux arm64: Get log from Pi** | Copies `~/a2b_pnp.log` from the Pi (the output of the last Run or debug session) to `app/linux_rpi/build/a2b_pnp.log`. |

The builds run `app/linux_rpi/build.sh` (`Debug`, `Release` or `clean`) in Debian.

Compiler errors in the build task link back to the source files.

### Debugging on the Pi

One-time: `sudo apt install gdb-multiarch` in Debian, and `sudo apt install gdbserver`
on the Pi.

Run and Debug → pick **Pi Debug (gdbserver, arm64)** → F5. It builds, deploys, starts
`gdbserver` on the Pi and connects `gdb-multiarch` (running in Debian) to it; execution
stops at `main`. The program's own output appears in the "Start gdbserver on Pi" task
terminal, not the Debug Console. Shift+F5 ends the session and the program.

It works with the repository in any folder on drives C: to F: (the drives listed in
`sourceFileMap` in `launch.json`).

### IntelliSense

With a C file open, click the configuration name at the bottom right of the status bar
and pick **Linux arm64** to see the code as the Linux build does (64-bit pointers, Linux
headers from the Debian distro), or **Pico** for the RP2040 view.

### From a terminal instead

In Debian, from the repository (Windows' `D:\Projects\A2B_PnP` is `/mnt/d/Projects/A2B_PnP` there):

```bash
app/linux_rpi/build.sh Debug       # or Release, or clean
file app/linux_rpi/build/a2b_pnp   # expect: ELF 64-bit LSB ... ARM aarch64
```

The script runs `cmake -S app/linux_rpi -B ~/a2b-build/<type>` with
`toolchain-pi.cmake`, then builds and copies the result as described above.

## Pi setup

1. Enable I2C at 400 kHz in `/boot/firmware/config.txt`, then reboot:
   ```
   dtparam=i2c_arm=on
   dtparam=i2c_arm_baudrate=400000
   ```
   Check with `ls /dev/i2c-*`. At startup the program reports the bus speed
   and warns if it is below 400 kHz.
2. Give the user access to I2C and GPIO, then log out and back in:
   ```bash
   sudo usermod -aG i2c,gpio $USER
   ```
3. libgpiod v2 (`libgpiod3`) ships with Raspberry Pi OS trixie; nothing to install.

## Run options

```bash
./a2b_pnp [-d i2c-dev] [-c gpiochip] [-r reset-pin] [-i irq-pin]
```

| Option | Default | Meaning |
|---|---|---|
| `-d` | `/dev/i2c-1` | I2C bus the AD2437 is on |
| `-c` | `/dev/gpiochip0` | GPIO chip (list with `gpiodetect`) |
| `-r` | unassigned: RESETn isn't wired to the Pi on the reference board (rev A) | AD2437 RESETn line; `-1` = not connected |
| `-i` | `13` (Pi header pin 33 on the reference board, rev A) | AD2437 IRQ line; `-1` = not connected |

The IRQ line is only used when the stack runs in interrupt mode (`ENABLE_INTERRUPT_PROCESS`,
currently off; the stack polls the AD2437 over I2C). With no RESETn pin, power-cycle the
AD2437 before each run. Defaults are `RESET_N_GPIO_PIN` / `IRQ_GPIO_PIN` in `pal_gpio.c`.

## Configuration notes

- `platform/a2b/conf.h` picks pointer size and memory alignment from the
  compiler (64-bit here, 32-bit on the RP2040).
- `platform/a2b/features.h` defines `A2B_FEATURE_OPTIMAL_RESPCYCS`, required for
  field installations with cables longer than 5 m; a short-cable bench test
  passes either way.
- The bus configuration is the compiled-in `app/rp2040/12_a2b_busconfig.c`,
  shared with the RP2040 build (`configType 0` in the startup commands).

## Common errors

| Symptom | Cause / fix |
|---|---|
| CMake: `aarch64-linux-gnu-gcc/g++ not found on PATH` | Install `crossbuild-essential-arm64` in Debian (above) |
| CMake: `libgpiod v2 for arm64 not found` | Install `libgpiod-dev:arm64` in Debian (above) |
| CMake: `Operation not permitted` | Build directory is on a Windows drive under WSL1; use `~/a2b-build/...` |
| Pi: `GLIBC_2.xx not found` | Build host Debian is newer than the Pi's OS; match releases |
| Pi: `cannot execute binary file` / `Exec format error` | Pi runs 32-bit Pi OS; this build needs 64-bit |
| `pal_i2c: open(/dev/i2c-1) failed` | The message says whether it's permissions or I2C not enabled |
| `pal_gpio: ... Permission denied` | Add the user to the `gpio` group (Pi setup step 2) |
