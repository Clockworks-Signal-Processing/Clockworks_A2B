# Raspberry Pi setup (64-bit Linux, I2C)

The bench console (`a2b_bench`, `a2bcpp/examples/bench`) runs on a 64-bit Raspberry Pi and
talks to the A2B main node over the Pi's I2C bus (`/dev/i2c-1`). It is cross-compiled on
Windows in a **Debian WSL** distro and deployed over SSH. This page covers the one-time
setup, then how VS Code builds, deploys, runs and debugs it. For what the console does, see
the [bench console guide](bench_console.md).

Reference board: a Pi Zero 2 W running Raspberry Pi OS **trixie (Debian 13), 64-bit**. Any
64-bit Pi (3, 4, 5, Zero 2 W, CM3/4) running 64-bit Pi OS works. The hardware is described
in the repository [README](../../README.md).

## One-time setup on the PC

### Debian WSL matching the Pi

The build host's Debian release must match the Pi's, so the C library (glibc) matches;
otherwise the Pi reports `GLIBC_2.xx not found`. For trixie that's Debian 13. Check both
sides with `cat /etc/debian_version`.

In **Debian** (`wsl -d Debian`):

```bash
sudo dpkg --add-architecture arm64
sudo apt-get update
sudo apt-get install crossbuild-essential-arm64 cmake
```

WSL's default distro may be something else (for example Ubuntu). The VS Code tasks name
Debian explicitly, so this doesn't matter for them; to change the default anyway:
`wsl --set-default Debian`.

### SSH key on the Pi (once per PC, for each Pi)

Deploy, Run and Debug use Windows' `ssh`/`scp`, which log in with a key from
`%USERPROFILE%\.ssh`. A key belongs to one PC: each PC you work from needs its own key
installed on each Pi it uses. Until then every task asks for the Pi's password (Debug asks
three times).

In PowerShell on the PC (replace `<user>` and `<pi-address>`):

1. See whether the PC already has a key (`id_ed25519.pub` or `id_rsa.pub`):
   ```powershell
   dir $env:USERPROFILE\.ssh\id_*.pub
   ```
2. If it doesn't, create one. Press Enter at each prompt: with a passphrase, every task would
   ask for it instead.
   ```powershell
   ssh-keygen -t ed25519
   ```
3. Install it on the Pi. This asks for the Pi's password one last time. If the key from step 1
   is `id_rsa.pub`, use that name.
   ```powershell
   type $env:USERPROFILE\.ssh\id_ed25519.pub | ssh <user>@<pi-address> "mkdir -p ~/.ssh && cat >> ~/.ssh/authorized_keys && chmod 700 ~/.ssh && chmod 600 ~/.ssh/authorized_keys"
   ```
4. Check: this prints `ok` without asking for anything.
   ```powershell
   ssh -o BatchMode=yes <user>@<pi-address> echo ok
   ```

The first connection to a Pi asks whether to trust its host key; answer `yes`. If ssh warns
`REMOTE HOST IDENTIFICATION HAS CHANGED` instead (the Pi was re-imaged, or a different Pi
now has that address), remove the old key with `ssh-keygen -R <pi-address>` and connect
again.

## One-time setup on the Pi

1. Enable I2C at 400 kHz in `/boot/firmware/config.txt`, then reboot:
   ```
   dtparam=i2c_arm=on
   dtparam=i2c_arm_baudrate=400000
   ```
   Check with `ls /dev/i2c-*`.
2. Give the user access to I2C, then log out and back in:
   ```bash
   sudo usermod -aG i2c $USER
   ```
3. For debugging only: `sudo apt install gdbserver`.

The Pi platform drives no reset or IRQ line: the stack resets the main node in software
and polls its interrupt registers.

## Build, deploy, run from VS Code

Open the repository folder in VS Code on Windows (not "Reopen in WSL").

The Pi's address and user are the settings `a2b.piHost` and `a2b.piUser`. They differ from PC
to PC, so set them in each PC's VS Code **User** settings (File → Preferences → Settings →
User, search for `a2b.pi`). Don't put them in `.vscode/settings.json`: a value there would
override every PC's own. If they're missing, the A2B menu says so. The board the program
starts is `a2b.piBoard` (default `ref`, the Pi's export).

The **A2B** sidebar menu (`tools/vscode-a2b-menu`) has Clean / Build / Debug / Release for
the Pi under "Raspberry Pi bench". It runs these tasks, which are also in Terminal → Run
Task…:

| Task | What it does |
|---|---|
| **Pi bench: Build (Debug)** | Builds in Debian. The build directory is `~/a2b-cpp-build/a2bcpp-Debug` inside Debian (WSL1 can't set file permissions on Windows drives, which CMake needs); the finished `a2b_bench` is copied to `build/a2b-bench-pi/Debug/`. |
| **Pi bench: Build (Release)** | The same, optimized: `~/a2b-cpp-build/a2bcpp-Release`, copied to `build/a2b-bench-pi/Release/`. |
| **Pi bench: Clean** | Removes both build directories and `build/a2b-bench-pi/`. |
| **Pi bench: Deploy to Pi** | Builds, stops any running copy on the Pi, then copies `a2b_bench` to the Pi's home directory. |
| **Pi bench: Run on Pi** | Runs `./a2b_bench --board <a2b.piBoard> start` on the Pi over SSH, in the task terminal (type commands at the `A2B>` prompt; `quit` or Ctrl+C ends it). |
| **Pi bench: Release (deploy and run)** | Builds Release, stops any running copy, copies it to the Pi and runs it. |
| **Pi bench: Stop on Pi** | Stops `a2b_bench` and `gdbserver` on the Pi (they would hold the same I2C bus). |
| **Pi bench: Get log from Pi** | Copies `~/a2b_bench.log` from the Pi (the output of the last Run or debug session) to `build/a2b-bench-pi/a2b_bench.log`. |
| **Pi bench: Run Trace on Pi**, **Get traces from Pi** | Deploys and runs with `--trace traces/boot.txt` (every I2C transfer, from the start), and copies `~/traces` to `build/a2b-bench-pi/traces`. |

The builds run `a2bcpp/tools/build_pi.sh` (`Debug`, `Release` or `clean`) in Debian, with
`a2bcpp/tools/toolchain-pi.cmake`. Compiler errors in the build task link back to the source
files.

### Debugging on the Pi

One-time: `sudo apt install gdb-multiarch` in Debian, and `gdbserver` on the Pi (above).

Run and Debug → pick **Pi bench: Debug (gdbserver)** → F5. It builds, deploys, starts
`gdbserver` on the Pi and connects `gdb-multiarch` (running in Debian) to it; execution stops
at `main`. The program's own output appears in the "Start gdbserver on Pi" task terminal, not
the Debug Console. Shift+F5 ends the session and the program.

It works with the repository in any folder on drives C: to F: (the drives listed in
`sourceFileMap` in `launch.json`).

### IntelliSense

With a C++ file open, click the configuration name at the bottom right of the status bar and
pick **Linux arm64** to see the code as the Linux build does (64-bit pointers, Linux headers
from the Debian distro), or **RP2040 fixed** / **RP2040 minimal** for the RP2040 views.

### From a terminal instead

In Debian, from the repository root:

```bash
a2bcpp/tools/build_pi.sh Release       # or Debug, or clean
file build/a2b-bench-pi/Release/a2b_bench   # expect: ELF 64-bit LSB ... ARM aarch64
```

## Common errors

| Symptom | Cause / fix |
|---|---|
| CMake: `aarch64-linux-gnu-gcc/g++ not found on PATH` | Install `crossbuild-essential-arm64` in Debian (above) |
| CMake: `Operation not permitted` | The build directory is on a Windows drive under WSL1; use `~/a2b-cpp-build/...` |
| Pi: `GLIBC_2.xx not found` | The build host's Debian is newer than the Pi's OS; match releases |
| Pi: `cannot execute binary file` / `Exec format error` | The Pi runs 32-bit Pi OS; this build needs 64-bit |
| `open(/dev/i2c-1) failed` | The message says whether it's permissions or I2C not enabled |

## Porting notes

Two details of the Linux i2c-dev interface matter if you write your own Linux host:

- A write followed by a read must be one `ioctl(I2C_RDWR)` carrying two messages, so the bus
  sees a repeated start.
- A read alone must also be a single `ioctl`.
