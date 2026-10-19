# Porting guide

To run the stack on a new host, implement `a2b::Platform` (`a2bcpp/include/a2b/platform.h`).
One `Platform` object may serve several A2B buses.

The repository has these platforms, which are also examples to start from:

| Platform | Host | Where |
|---|---|---|
| `LinuxPlatform` | Linux i2c-dev, e.g. the Raspberry Pi | `a2bcpp/platforms/linux/` |
| `Rp2040Platform` | RP2040 with the Pico SDK | `a2bcpp/platforms/rp2040/` |
| `UsbiPlatform` | The ADI USBi USB-to-I2C adapter, through libusb-1.0 (Windows; it builds on Linux too) | `a2bcpp/platforms/usbi/` |
| `sim::Simulator` | Simulated A2B buses, for the tests | `a2bcpp/sim/` |

Only `Rp2040Platform` drives a reset line (`SetResetPin()`); no platform reads an IRQ line or a
power-good input yet. The
[quick start](quick_start.md) builds the minimal example app with the Linux and RP2040
platforms; the [bench console](bench_console.md) runs on the USBi, on Linux and on the
RP2040.

## What to implement

| Method | Required | What it does |
|---|---|---|
| `I2cWrite(addr, data, len)` | yes | One write transfer ending in a stop |
| `I2cRead(addr, data, len)` | yes | One read transfer |
| `I2cWriteRead(addr, writeData, writeLen, readData, readLen)` | yes | A write, a repeated start, then a read |
| `NowMs()` | yes | A free-running millisecond counter |
| `SetMainNodeReset(bus, asserted)` | no | Drives the main node's reset line (RESETn, active low) |
| `IrqPending(bus)` | no | Reads the main node's IRQ line |
| `PowerGood(bus)` | no | Reads the power-good signal for two-step discovery |
| `Log(level, text)` | no | Writes a log message |
| `Lock()` / `Unlock()` | no | Guards an I2C controller shared between threads |

## The contract

**I2C**
- Addresses are 7-bit, without the read/write bit.
- Return `Result::kOk` or `Result::kI2cError`. Report a NAK, a bus error or a driver
  timeout as `kI2cError`, without retrying: the stack decides what a failure means.
- `I2cWriteRead` must not put a stop between the write and the read.
- Register transfers are short: a register write is at most 17 bytes (register address
  plus `Transport::kMaxRegBurst` values). Peripheral transfers are as long as the app or
  the module asks: a sub-node's module-info image is read 144 bytes at a time, and a
  peripheral script from a module's EEPROM writes each unit in one transfer, up to
  4094 bytes plus the address (ADI's ADAU1701 program is 4090). The driver must take
  transfers that long in one piece (Linux i2c-dev takes up to 8192 bytes per message).
- Transfers to a sub-node's peripherals are clock-stretched by the main node until the
  sub-node has done them (the TRMs: "the clock stretching feature is required for the I2C
  interface of the host"). A driver timeout must allow for a slow peripheral behind a
  100 kHz sub-node port: about 0.4 s for a 4 KB write.
- Each bus uses two addresses on the host I2C bus: the main node's, and the bus address
  that reaches the sub-nodes and their peripherals. The app gives both in the bus's
  configuration; the bus address is usually the main node's plus one.

**Time**
- `NowMs()` may wrap at 2^32. The stack only takes differences, with `ElapsedMs()` and
  `TimeReached()` from `a2b/types.h`.
- The stack never asks the platform for a delay. Waits are deadlines checked on each
  `Poll()`, so `Poll()` never blocks for longer than its I2C transfers.

**GPIO**
- Each GPIO call has a `bus` parameter: the bus index given in that bus's
  configuration, so a host with several buses can pick the right line.
- `SetMainNodeReset(bus, true)` drives the main node's RESETn low, `false` releases it.
  The stack pulses it once at each `Network::Start()`: low for
  `BoardTimings::resetPulseMs` (100 ms), then high, then it waits 2 ms (tACCESS) before
  the first access. Return `kOk`, or `kNotSupported` (the default) without a line; then
  the stack goes straight on. Any other result is logged and the start-up goes on
  without the pulse. `Rp2040Platform::SetResetPin()` is an example.
- Either way, the stack then resets the main node through its registers, with the
  SUSTAIN wait (the start-up in the [main-node configuration guide](main_node_config.md)):
  a host without a reset line, or one restarted without a power cycle, finds the main
  node still running.
- Without an IRQ line, `IrqPending` returns true (the default), and the stack reads the
  interrupt registers every `BoardTimings::interruptPollMs` (20 ms). With one, it reads
  them only while the line is active.
- Without a power-good input, `PowerGood` returns true (the default).

**Threads**
- All calls on one bus come from one thread.
- If several threads share an I2C controller (e.g. two buses polled from two threads),
  implement `Lock()`/`Unlock()` with a mutex. The stack holds the lock for each whole
  register or peripheral access, which may be several transfers. The other calls
  (`NowMs()`, `Log()`, the GPIO calls) then come from those threads too, so they must be
  thread-safe. One thread polling every bus in turn needs none of this
  ([architecture guide](architecture.md), "Several buses").

## The USBi platform

`a2bcpp/platforms/usbi/` is derived from ADI's Windows PAL (`pal_i2c_usbi.c`, ADI licence) and
has two parts, so the USB framing can be tested without the adapter:

| Part | What it does | Library |
|---|---|---|
| `UsbiLink` (`usbi_link.h`) | The USB side: one control transfer, and a sleep | (interface) |
| `UsbiPlatform` | The USBi's I2C framing over a `UsbiLink`; time from the host's steady clock; logs to stderr | `a2bcpp_usbi` (no libusb) |
| `LibusbLink` | Finds the USBi by its USB ID (0456:7031) and opens it with libusb-1.0; raises Windows' timer resolution to 1 ms while it is open | `a2bcpp_usbi_libusb` |

```cpp
#include "libusb_link.h"
#include "usbi_platform.h"

a2b::LibusbLink link;
if (link.Open() != a2b::Result::kOk) {   // the first USBi found; Open(1) for the second
  std::printf("%s\n", link.Error());      // not found, or can't be opened, with a hint
}
a2b::UsbiPlatform platform(link);        // a Platform like any other
```

Each I2C transfer is a USB vendor request, then status requests, as in ADI's PAL
(the unit test `test_usbi_platform` checks every byte of them):

| Transfer | Requests |
|---|---|
| Write | 0xB2 to the device (bmRequestType 0x40), wValue the 7-bit address, wIndex 0 (I2C), the bytes in the data stage; then 2 ms, then CHECK_WRITE (0xB5, from the device: 0xC0) every 10 ms, up to 30 more times, while the USBi answers NONE or WRITING |
| Read | 0xB4 from the device with the bytes; then 2 ms, then CHECK_READ (0xB6) the same way while it answers NONE |
| Write-read | 0xB3 with the bytes to write and its write status; 1 ms; then the read |

Only a SUCCESS status (1) is `kOk`; a NAK (FAILED), a bus error (ERROR), an exception, no
answer within the 30 checks, or a short USB transfer is `kI2cError`. The USB timeout is
500 ms, 2 s for a write or read of more than 16 bytes. A transfer takes at least 2 ms
(3 ms more for a write-read) plus the USB round trips, so a register read costs about
6-8 ms: the stack's 20 ms interrupt poll and its probes still fit, as they did for
ADI's app. Transfers are limited to 6000 bytes (ADI's PAL's chunk size). Until one
transfer has worked, the first failure is logged once with a hint (is the USBi connected
to the board, is the board powered).

Limits:
- The USBi has no RESETn or IRQ line for the main node: the stack resets it through
  CONTROL (with the SUSTAIN wait) and reads the interrupt registers every 20 ms.
- The status is checked 2 ms after a transfer, then every 10 ms up to 30 more times: about
  0.3 s of waits, plus the USB requests. A write that a slow peripheral clock-stretches for
  longer is reported as failed, as in ADI's PAL. Behind the USBi a remote write takes about
  217 µs per byte at a sub-node's 100 kHz and 162 µs at 400 kHz ([timing](timing.md)): the
  AB0020's 1732-byte codec write took 375 ms at 100 kHz and still passed, with little to
  spare, and a 4 KB unit would fail. The bench console's EEPROM writes go a 64-byte page at a
  time and its reads 256 bytes at a time; the image tool warns about a unit that would take
  over 0.25 s, and its AB0020 example splits that write and runs at 400 kHz.
- One bus per USBi. A second USBi is `Open(1)`; two buses on one USBi are two main nodes
  at different addresses, as on any host.
- On Windows the USBi needs the WinUSB driver (Zadig), not SigmaStudio's Cypress driver;
  [setup_windows.md](setup_windows.md) has the steps. SigmaStudio can't use the USBi while it has WinUSB.
- A USBi on a Linux host is untried: the platform builds there (the build checks),
  but the USBi may need its firmware loaded (fxload) and a udev rule.

## Bring-up

1. Build the unit tests on your host (see the [testing guide](testing.md)).
2. Run the simulator tests (`a2bcpp/tests/scenario/`, same scripts). They run the stack
   itself (`Network`) against simulated nodes. `a2b::sim::Simulator` is also a reference
   `Platform`: a missing device NAKs, and nothing blocks.
3. Run the minimal example app with your platform (`a2bcpp/examples/minimal/`: its
   `MinimalApp` takes any `Platform`).
4. On hardware, capture an I2C trace of the new stack (`--trace FILE` in the examples) and
   check it with `trace_diff.py` against a golden or another run (`a2bcpp/tools/README.md`).
5. On a microcontroller, check the RAM budget ([RAM on the RP2040](ram.md) shows the
   method).
