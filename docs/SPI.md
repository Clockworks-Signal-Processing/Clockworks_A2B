# SPI over A2B

How the C++ stack reaches SPI devices behind AD2433 and AD2437 sub-nodes: what the chips do,
how many bus slots it takes, how each host must be wired, the API, the bench console's
commands, the tests and what to check on a bench. Everything about SPI is on this page; the
other documents link here.

Status: built and tested on the simulator. **Not yet run on hardware.** The first bench is a
Pi Zero 2 W main-node board (AD2437) with a W25Q128 flash on an AB0020 sub-node (AD2437).
The AD2433 takes the same code and the same tests, untested on hardware.

## 1. What SPI over A2B is here

The AD2433 and AD2437 have an SPI port. On the main node it is an SPI slave the host talks
to; on a sub-node it can be an SPI master to that node's peripherals. Between them, the
chips carry SPI over the bus in a **data tunnel**: data slots reserved on every link from the
main node (the tunnel's **owner**) to one sub-node (its **far end**, the TRM's "tunnel
responder"). The host sends the main node a command on its SPI port; the main node carries
it down the tunnel; the far end runs it on its own SPI port and sends the answer back (AD2437
TRM Rev 0.2, chapter 5, "SPI Interface" and "SPI Over Distance").

**The main node is still controlled over I2C.** The stack's register access, discovery,
routing and I2C peripheral access are unchanged. SPI is used only for the tunnel's
transactions (and to read the main node's registers once, as a wiring check).

The six transaction types, and the calls that run them:

| TRM transaction | Command byte | Call |
|---|---|---|
| Atomic write (1-256 bytes) | 0x0C | `PeripheralWrite(node, PeripheralAddress::Spi(ss), ...)` |
| Atomic read (0-255 bytes out, then 1-256 in) | 0x0D, then the FIFO read 0x0B | `PeripheralWriteRead(...)`, `PeripheralRead(...)` with `Spi(ss)` |
| Full duplex (1-256 bytes, the answer one transaction later) | 0x09 | `SpiFullDuplex(ss, tx, rxPrevious, len)` |
| Extended full duplex (any length, one remote transfer) | 0x0E, ended by 0x09 | `SpiFullDuplex(..., SpiChunk::kMore)`, then `kLast` |
| Bulk write (1-256 bytes) | 0x06 | `SpiBulkWrite(ss, data, len)` |
| Extended bulk write (any length) | 0x0F, ended by 0x06 | `SpiBulkWrite(..., SpiChunk::kMore)`, then `kLast` |

Also: `SpiReadTunnelFifo()` (0x0B: the last atomic read's or full duplex's data),
`SpiAbort()` (0x0A), `SpiReadRegisters()` (0x01, the main node's own registers over SPI) and
`LastSpiStatus()`.

**Not built**, on purpose:
- **SPI host control** (the host running the whole bus over SPI instead of I2C): I2C does
  that already.
- **SPI remote register access and SPI-to-I2C** (commands 0x02, 0xC0-0xDF, 0x07, 0x08): the
  stack reaches sub-node registers and I2C peripherals over I2C, and ADI's anomaly 18000065
  (AD2437 Rev 1.3) says I2C and SPI used together on sub-node registers and remote I2C
  peripherals corrupt accesses. With the tunnel only, the two never meet.
- **SPI peripherals on node GPIO lines** (`PeripheralAddress` has no form for it yet), and the
  **SPI peripheral blocks of a module image** (still skipped at node set-up).
- **A tunnel owned by a sub-node**, or several tunnels at once: one tunnel, owned by the main
  node.
- **The register-based full duplex** (a second slave select, ADR2 or SIO2, on the main node).

## 2. Bandwidth: the tunnel takes data slots

The bits a superframe always has are its control and response frames (SCF, SRF). The chips
use them for register access and remote I2C, about one byte each way a superframe, and for
SPI remote register access and SPI-to-I2C. **SPI to an SPI device can't use them: it runs only
in the tunnel's data slots**, taken from the same pool as audio. So the tunnel's slots are
part of the routing plan:
- 2 to 12 slots each way (AD2437 TRM, "SPI Data Tunnels"), the same on every link between the
  main node and the far end;
- at least 6 bytes: 16- and 20-bit slots need 3 at least; 8-, 12- and 28-bit slots can't
  carry a tunnel;
- they count in each link's 32 data slots and in the response cycles, like audio slots.

The stack takes them when the tunnel is opened (`OpenSpiTunnel()`, `SpiTunnelConfig::slots`,
4 by default) and gives them back when it is closed. The TRM suggests more slots while
programming a peripheral and few or none during audio: open, program, close.

**How much a tunnel carries.** A tunnel's bytes a superframe are its slots times the slot size
(the main node's SLOTFMT) over 8 (TRM Tables 5-14 and 5-15). Each superframe's packet spends
some of them: a standard tunnel (up to 16 bytes) 3 bytes, 5 in a transaction's first
superframe; a jumbo tunnel (17-48 bytes) 4, and 6 in the first. Calculated at 48 kHz, one way:

| Slots each way (32-bit slots) | Tunnel bytes | Payload a superframe | Payload rate |
|---|---|---|---|
| 2 (the minimum) | 8 | 5 | about 1.9 Mbit/s |
| 3 | 12 | 9 | about 3.5 Mbit/s |
| 4 (the default) | 16 | 13 | about 5.0 Mbit/s |
| 12 (the most) | 48 (jumbo) | 44 | about 16.9 Mbit/s |

With 24-bit slots, 2 slots are 6 bytes, about 1.2 Mbit/s. The AD2437 data sheet's headline
is "high speed SPI (up to 10 Mbps) over distance".

A transaction's real rate is lower: an atomic transaction carries all of its command down
before the far end starts and its answer up after; every transaction is also a host SPI
transfer and the stack's status reads (SPIBUSY) around it; and the far end's SCK
(`clockDiv`: SYSCLK / (div + 1), 9.8 MHz at the default 4 at 48 kHz) and the host's SCK bound
it. The simulator's model of all that reads 64 KB with the extended full duplex at about
1.4 Mbit/s through a 2-slot tunnel and 2.7 Mbit/s through 4 slots, with 32-bit slots and host
SPI at 8 MHz. That is a model. **Measured on the bench** (section 7), with 24-bit slots and the
console's jobs: about 0.41 Mbit/s through 2 slots and 0.76 Mbit/s through 4, about a third and
a fifth of those slots' raw payload rate. Why is still open (the roadmap's "SPI throughput").

**Where the slots go** (`src/routing_planner.cpp` has it in full): downstream, the tunnel's
slots come first in the main node's run (its DTDNOFFS is 0; its DNSLOTS counts only its own),
every node between passes them on as ordinary slots (the TRM: nodes that don't enable the
tunnel pass its slots "like other bus slots that are not locally consumed"; an AD2428 or
AD2427 between works the same), and they arrive at the far end's slot 0. A far end that
passes slots on downstream passes the tunnel's with them (the TRM's "loosening" of its
restrictions, at the cost of their bandwidth on the links after it); the chain's last node as
the far end has no DNMASKEN and takes every other slot, as the TRM's last-node configuration
asks. Upstream, the far end sends the tunnel's slots after the slots it passes up and before
its own. These match the TRM's RTM example (Figure 5-62) register for register
(`test_routing_planner`).

## 3. Wiring, per host

### Raspberry Pi (the main-node board)

The host's SPI goes to the main node's SPI slave port. The main node's slave select is its
**ADR1** pin. On the Pi main-node board:

| Pi header pin | Pi signal | AD2437 pin | On the way |
|---|---|---|---|
| 19 | GPIO10, SPI0 MOSI | MOSI (18) | |
| 21 | GPIO9, SPI0 MISO | MISO (17) | |
| 23 | GPIO11, SPI0 SCLK | SCK (19) | R32, 33 ohm |
| 26 | GPIO7, SPI0 CE1 | ADR1 (22) | R28, 33 ohm; R29, 10 k to ground at ADR1 |
| 3, 5 | GPIO2/3, I2C1 | SDA, SCL | the control path, as before |
| 33 | GPIO13 | IRQ (24) | not used by the stack yet (it polls) |

- **The SPI mode is 0** (CPOL 0, CPHA 0): the main node's SPICFG from reset. An export that
  sets another mode on the main node needs the same on the host (`LinuxPlatform::OpenSpi()`'s
  `mode`; the console passes 0).
- **The clock:** at most 15 MHz (the AD2437 data sheet, Table 11: SCK period 66.67 ns as a
  slave). The console's default is 8 MHz (`--spi-hz`). The Pi's SPI clock is its core clock
  divided down, so spidev may run a little under the figure asked for.
- **ADR1 is also the main node's I2C address strap**, latched at its power-on reset
  (the data sheet: "the logic levels applied to the ADR2 and ADR1 pins at power-on reset").
  R29 holds it low (0x68), against the Pi's weak default pull-up on GPIO7 while the SPI driver
  isn't loaded. Once spidev runs, CE1 idles **high**: if the AD2437 is power-cycled or reset
  then, with the Pi running, it comes up at another I2C address and the bus doesn't start
  (`kI2cError`). Power the two together, or reset the Pi too. A board revision could move the
  slave select to CE0 or add a gate; that is a hardware decision.
- **Set-up on the Pi:** in `/boot/firmware/config.txt`, `dtparam=spi=on`, then reboot;
  `ls /dev/spidev0.*` shows `spidev0.0` (CE0) and `spidev0.1` (CE1); the user in the `spi`
  group (`sudo usermod -aG spi $USER`, then log in again).
- **The code:** `LinuxPlatform::OpenSpi(device, hz, mode)` and `SpiTransfer()`
  (`a2bcpp/platforms/linux/`): one `SPI_IOC_MESSAGE` a transfer, so the slave select stays
  asserted for all of it. The console opens `/dev/spidev0.1` at start-up if it is there
  (`--spi DEVICE` for another, `--spi none` for none; a `--spi` that can't be opened stops the
  console). **Wired another way:** another CE is another spidev device (`--spi
  /dev/spidev0.0` for CE0); another SPI controller is another spidev bus (`/dev/spidev1.x`,
  with its overlay in `config.txt`). A slave select on a plain GPIO needs the platform to drive
  it around the ioctl (with `SPI_NO_CS`): a change in `linux_platform.cpp`.

### RP2040

**Not supported yet**: `Rp2040Platform` has no `SpiTransfer()`, so the RP2040 bench console
answers every `spi` command "this host has no SPI to the main node". To add it:
`hardware_spi` (`spi_init()`, `spi_set_format()` for mode 0, `spi_write_read_blocking()`)
and a plain GPIO for the slave select, driven low for the whole transfer, in
`a2bcpp/platforms/rp2040/rp2040_platform.cpp`; the pins in `ab0310_wiring.h` (whether the
AB0310 wires the Pico's SPI to its AD2437 is to be checked); and the library in the Pico
projects' CMake (`examples/rp2040_example.cmake`, `hardware_spi`).

### Windows (the ADI USBi)

**Not supported.** The stack's USBi platform frames I2C only (it is a port of ADI's I2C PAL),
and the benches' USBi adapters are wired to the boards' I2C. Every `spi` and `spiflash` command
says "this host has no SPI to the main node" and sends nothing. ADI's newer adapter has SPI;
a platform for it would implement `SpiTransfer()`.

### A sub-node: its SPI devices

The far end is an SPI master with up to three slave select outputs:

| Slave select | Pin | Enabled by |
|---|---|---|
| SPISSEL0 (`ss 0`) | ADR1 | SPIPINCFG.SPIMSS0EN |
| SPISSEL1 (`ss 1`) | SIO2 | SPIPINCFG.SPIMSS1EN |
| SPISSEL2 (`ss 2`) | ADR2 | SPIPINCFG.SPIMSS2EN |

with SCK, MOSI and MISO on its SPI pins. The tunnel sets the far end up when it opens:
SPICFG (master, the mode, ENFDCS), SPICKDIV, SPIPINCFG (the slave selects asked for), and
takes them back to the module image's values when it closes. The SPI mode (0-3) and SCK are
the peripheral's: a W25Q flash takes mode 0 or 3, up to 133 MHz, far above what the AD2437
gives (49.152 MHz / (div + 1)).

**The AB0020 and CLKOUT2 (anomaly 18000047).** The AB0020's codec takes its MCLK from
CLKOUT2 on ADR2. With the SPI port on and PINCFG.GPIOSEL 0, ADR2/CLKOUT2 stops; that is why
the AB0020's image turns SPI off (SPICFG 0x02). With GPIOSEL 2, CLKOUT2 works (it is
ADR1/CLKOUT1 that stops then, which the AB0020 doesn't use as a clock). So the stack, opening
a tunnel to a node whose image puts CLKOUT2 on with GPIOSEL 0, gives it GPIOSEL 2 while the
tunnel is open (logged: "PINCFG.GPIOSEL 2 on its far end, to keep CLKOUT2 on ADR2") and its
own PINCFG back after. GPIOSEL 2 moves IO0 to SIO0, IO4 to ADR2 and IO5/IO6 to SCL/SDA (TRM
Table 5-27); a node whose image uses any of those as GPIO is refused. The bench's flash has
its chip select on **ADR1 (`ss 0`)**. That ADR1 still drives SPISSEL0 under GPIOSEL 2 isn't
stated in the anomaly list: check it on the bench (section 7).

The sub-node's ADR1 and ADR2 are its own I2C address straps too, read at its power-on; only a
local processor on that node's I2C port sees that address, not the bus.

## 4. The API

`a2b/network.h` and `a2b/spi_tunnel.h`. Every SPI call is made at once, between polls, from the
thread that polls, as the peripheral calls are; each keeps its command on the stack (up to 259
bytes plus its frame), on the app's call path, not on `Poll()`'s.

```cpp
// The host's SPI to the main node: a Platform call (default: not supported).
//   Result Platform::SpiTransfer(uint8_t bus, const uint8_t* tx, uint8_t* rx, size_t len);

uint8_t ids[3];
network.SpiReadRegisters(0x02, ids, 3);          // VENDOR, PRODUCT, VERSION over SPI

a2b::SpiTunnelConfig tunnel;
tunnel.node = 1;                                 // an AD2433 or AD2437 in the bus
tunnel.selects = 0x01;                           // SPISSEL0 (ADR1)
tunnel.slots = 4;                                // each way, 2-12
tunnel.clockDiv = 4;                             // SCK 9.8 MHz at 48 kHz
tunnel.mode = 0;
a2b::RoutingError why;
network.OpenSpiTunnel(tunnel, &why);             // written at the next idle poll
// ... EventType::kSpiTunnelOpened (after kRoutingApplied), or poll SpiTunnelReady()

const uint8_t jedec = 0x9F;
uint8_t id[3];
network.PeripheralWriteRead(1, a2b::PeripheralAddress::Spi(0), &jedec, 1, id, 3);  // EF 40 18

const uint8_t wren = 0x06;
network.PeripheralWrite(1, a2b::PeripheralAddress::Spi(0), &wren, 1);             // atomic write
network.SpiBulkWrite(0, program, 256, a2b::SpiChunk::kMore);                       // extended bulk
network.SpiBulkWrite(0, program + 256, 4, a2b::SpiChunk::kLast);
network.CloseSpiTunnel();
```

- **Opening** is checked at once (the node, its chip, the settings, the plan with the tunnel's
  slots: `RoutingError::kTunnelSlots`, `kTooManySlots`, ...) and written with a routing plan
  at the next poll between probes once the chain is up, as `ApplyRouting()` is; the tunnel's
  registers go with the plan's slot registers, before NEWSTRCT. A tunnel opened without
  `ApplyRouting()` has plans written for it alone (no connection routed, every crossbar at
  silence).
- **It follows its module** by chip ID: when the module leaves the bus the tunnel closes
  (`kSpiTunnelClosed`), and opens again when it comes back, at any position; after a restart
  (`Start()`, a PLL relock) it is written again once the chain is up.
- **Each transaction** waits for SPISTAT.SPIBUSY to clear before and after (the SPI status
  command, 0x04), up to `BoardTimings::spiTimeoutMs` (50), then reads SPIINT over I2C and
  clears it (and SPISTAT's error bits with SPIDTERR). An error bit gives `Result::kSpiError`,
  and `LastSpiStatus()` says which (section 8). `kInvalidArgument`: no tunnel ready to that
  node, a slave select it doesn't enable, a length out of range. `kNotSupported`: no SPI on the
  host (`SpiTransfer()`) or on the main node (an AD2428).
- **Full duplex**: the answer comes back with the next full duplex (`rxPrevious`; zeros on
  the first after another kind of transaction), or at once from `SpiReadTunnelFifo()`. Between
  full duplexes of different lengths, read the FIFO first (the TRM's note); the console's
  `SpiJob` (`examples/bench/spi_job.cpp`) shows the pattern for a stream of any length.
- **Extended** transactions keep the far end's slave select asserted; any other tunnel command
  to it before the closing `kLast` aborts the transfer (SPISTAT.DTABORT).
- **Probes for a new node give way to SPI.** A probe behind the tunnel's far end stops the
  tunnel while it runs: on the bench every transaction ended DTABORT once DISCVRY was out
  (section 7). So, while a tunnel is open:
  - no probe starts within `BoardTimings::spiProbeHoldMs` (1000) of the last SPI transaction;
    a job of one transaction a poll, or commands typed a few seconds apart, are never cut
    into. Hot-plug detection behind the tunnel resumes once SPI has been idle that long, so
    an app that uses SPI without a break finds no new node until it pauses;
  - a tunnel transaction that comes while a probe nothing has answered is out ends that
    probe first (logged "probe for sub-node N ended for an SPI transaction"; it counts as
    one that found nothing), then runs. The app sees nothing of it;
  - once a probe has found a node (DISCSTAT.DSCACT clear, the node being identified or set
    up), tunnel transactions return **`Result::kBusy`** and send nothing: the node joins, the
    tunnel is planned again (its far end is no longer the last node), and it works again;
  - **while the far end is the chain's last node and plays or sends audio, no probe is made
    behind it at all**, as with `pause` (logged "SPI tunnel at last sub-node N with audio: no
    probes", then "probes again"). The TRM's configuration for a tunnel ending at the last node
    (LDNSLOTS.DNMASKEN = 0, Figure 5-55) holds only while the node is last, and a probe clears
    NODE.LAST for its length: on the bench the far end's audio glitched at every probe
    (section 7). A node plugged in behind it is found once the tunnel is closed, moved to
    another node, or the audio there is unrouted. A far end without audio, or in the middle of
    the chain, is probed behind as above.
- **`Result::kBusy`** also while the tunnel's plan is due or being written (`OpenSpiTunnel()`
  called again, a node added or gone): `SpiTunnelReady()` is false meanwhile, and true again
  once the new plan is written. Try again at a later poll; the console's jobs do that for up
  to 3 s, and its raw commands say "busy: ... try again". `SpiReadTunnelFifo()` and
  `SpiReadRegisters()` touch only the main node and are never busy.

## 5. The bench console

| Command | What it does |
|---|---|
| `spi` | The tunnel (its slots and offsets) and the last transaction's status |
| `spi check` | The main node's VENDOR, PRODUCT, VERSION over SPI against I2C: the wiring, with no A2B link involved |
| `spi open N [ss S] [div D] [mode M] [slots T] [stretch]` | The tunnel to sub-node N (defaults: ss 0, div 4, mode 0, 4 slots) |
| `spi close` | Takes it off the bus |
| `spi write BYTE...` | Atomic write (1-256 bytes) |
| `spi read LEN [BYTE...]` | Atomic read: the bytes, then LEN read, printed in hex |
| `spi fd BYTE...` | Full duplex: the last one's data and this one's (FIFO) |
| `spi bulk BYTE...` | Bulk write |
| `spi dump FILE LEN [BYTE...]` | The bytes, then LEN bytes read into a raw `.bin`, one remote transfer (extended full duplex) |
| `spi send FILE [BYTE...]` | The bytes, then the file's, one remote transfer (extended bulk) |
| `spi abort` | The SPI abort; stops a job |
| `spiflash id [ID]` | The flash's JEDEC ID, maker and size; with ID (6 hex digits) a mismatch sets the exit code to 1 |
| `spiflash erase ADDR LEN` | Erases the 4 KB sectors that hold the range |
| `spiflash read ADDR LEN FILE` | Reads into a `.bin` (0x03, one remote transfer) |
| `spiflash program ADDR FILE` | Programs a `.bin` page by page (erase first; pages all 0xFF left out) |
| `spiflash verify ADDR FILE` | Compares the flash with a `.bin` |
| `wait node N [S]`, `wait spi [S]`, `wait running [S]`, `wait S` | Holds the queued commands until met, or S seconds (default 10; not met: exit code 1) |

Bytes are typed as the other commands take them (`0x9F`, or decimal); there is no limit
beyond the line's length and the 256 bytes of a transaction. The jobs (`spi dump|send`,
`spiflash` but `id`) run one transaction a poll, so the bus is served meanwhile, and print
their time and rate when they end. `dump`, `send`, `read`, `program` and `verify` need files:
not on the RP2040.

**The hello-world**, from the command line:

```bash
./a2b_bench --board ref --spi-flash-check 1=EF4018
```

starts the bus, waits for sub-node 1, opens the tunnel, reads the flash's JEDEC ID, checks it
is EF 40 18 (a W25Q128JV; EF 70 18 is its -IM/-JM part) and quits: exit code 0, or 1 if a step
failed. Without `=ID` it only prints the ID. `start` is added unless the command line has one.

## 6. Tests

| Test | What |
|---|---|
| `test_routing_planner` | The tunnel's slots: alone, the TRM's RTM example (Figure 5-62) register for register, a far end in the middle, the 32-slot limit |
| `test_sim_spi` | Scenario 22 on the simulator: the main node's registers over SPI; a host without SPI, an AD2428 main node; a failed host transfer; the six transaction types against a W25Q128-like flash through a **2-slot** and a **4-slot** tunnel (ID, erase, status by full duplex, a page by extended bulk and a short one by bulk, 64 KB by extended full duplex, with the simulator's timing); a tunnel alongside routed audio, moved and closed; the tunnel following its module, and a restart; through an AD2428 to an AD2433; slot sizes (16-bit slots need 3); CLKOUT2's GPIOSEL; errors (DTINVALID, FIFOUNF); the probes giving way: a transaction during a probe ends it (the simulator aborts one sent past the stack), no probe within the hold, a node plugged in behind the far end found only once SPI is idle, `kBusy` while it joins, a tunnel opened again not ready until its new plan is written; no probes behind a last-node far end with audio, until the tunnel is closed |
| `test_bench_spi` | The console: `spi check`, the tunnel and raw transactions, `spiflash` jobs from and to files (behind a last node that is probed: no DTABORT), a verify that differs, `spi abort`, `wait`, the exit code, a host without SPI; `wait spi` after a tunnel is opened again; a node joining behind the far end: a raw command busy, a job that waits it out |
| `test_sim_mixed_chips` | Scenario 11's SPI clause: refused on an AD2428 main node, and without a tunnel |

The simulator's SPI model (`a2bcpp/sim/sim_spi.cpp`, and the tunnel in `Bus::TraceAudio()`)
is written from the same TRM pages as the stack, apart from it: it checks that the stack does
what it means to, not that the TRM was read right. The hardware has the last word.

## 7. On the bench

The Pi main-node board, an AB0020 with an AD2437 module and the W25Q128 (CS on ADR1) at
sub-node N:

1. `spi check`, before `start`: the same IDs both ways. Different ones: the wiring, the mode,
   or SPI not enabled on the Pi. "SPI port is off": the board's export turns the main node's
   SPI off; `spi open` turns it on.
2. `./a2b_bench --board ref --spi-flash-check N=EF4018` (the export of the main-node board):
   exit code 0, and "matches". Also with `--trace FILE`: the SPIT lines are the SPI transfers.
3. The codec still clocked: with the tunnel open, audio routed to the AB0020 plays (GPIOSEL
   2), and the flash answers on ADR1.
4. At `slots 2` and at `slots 4` (`spi close`, `spi open N slots 4`): `spiflash erase 0 65536`,
   `spiflash program 0 test.bin`, `spiflash verify 0 test.bin`, `spiflash read 0 65536
   back.bin`, then `cmp test.bin back.bin`; note each job's rate against section 2's table.
5. A cable pulled during a job: the job fails with the error; the tunnel closes and opens
   again when the node is back.
6. Audio routed while the tunnel is open, and `spi close`: no audio drop beyond the plan's
   mute.
7. Probes and SPI: a loop of `spi read 3 0x9F` with `wait 1` between groups, over several
   probe periods, at 2 and 4 slots: no DTABORT; a node plugged in behind the far end is found
   within about 1.5 s of the last SPI command.

**What the bench showed (2026-11-05).** Pi main-node board, AB0020 + W25Q128 at sub-node 0
(the last node), host SPI at 1 and at 8 MHz, tunnels of 4, 12 and 2 slots: the ID, erase,
atomic writes of 1-20 bytes, bulk writes of 20 bytes and reads of up to 16 bytes all worked,
with the registers as planned (main DTCFG..DTUPOFFS `03 44 00 00`, the far end's `05 44 00
00`, its SPICFG..SPIPINCFG `01 00 04 00 00 10`, PINCFG `81`). Some transactions failed with
SPIINT 0x08 (SPIDTERR) and SPISTAT 0x40 (DTABORT), BECNT 0. The I2C trace placed every one
inside the stack's probe behind sub-node 0 (DISCVRY written; the probe runs up to 70 ms every
500 ms): transactions 11-12.5 ms into the probe worked, every one from 13.2 ms on aborted, and
sub-node 0 read NODE 0x60 (LAST cleared) meanwhile. Hence the probes giving way (section 4).

**With the probes giving way (2026-11-05).** The same runs at 8 and 1 MHz: every transaction
worked at 4, 12 and 2 slots, the stack ended a probe for a transaction twice in each run
("probe for sub-node 1 ended for an SPI transaction"), and there was no DTABORT; reads spaced
2 s apart, over many probe periods, all worked. `quit` now ends a2b_bench at once, also when
its stdin is a pipe held open (it used to hang there).

**The flash jobs (2026-11-06).** Host SPI at 8 MHz, 24-bit slots (SLOTFMT 0x44), 64 KB of
random data at 0x100000: erase, program, verify and read all worked at 2 and at 4 slots; the
read-back files were identical to the original (`cmp`), and BECNT stayed 0.

| Job, 64 KB | 2 slots | 4 slots |
|---|---|---|
| Erase (16 sectors of 4 KB) | 780 ms | 787 ms |
| Program (256 pages) | 1833 ms | 1395 ms |
| Verify | 1286 ms, 407 kbit/s | 685 ms, 765 kbit/s |
| Read | 1282 ms, 408 kbit/s | 698 ms, 751 kbit/s |

**Audio and the tunnel on one link (2026-11-06).** Three nodes: the AB0020 at sub-node 0 (no
EEPROM, `module 0 AB0020`) sends its input to the AB0020 at sub-node 1, which has the flash,
and the tunnel ends at sub-node 1, the last node. The plan was the TRM's (sub-node 0 passing
the 4 tunnel slots and adding its 2 audio slots; sub-node 1 taking the 2 audio slots after the
4 tunnel slots, LDNSLOTS 0x02, DTCFG 0x05), and BECNT stayed 0, but the audio glitched every
few seconds. `pause` stopped it and `resume` brought it back. With the tunnel ending at
sub-node 0 in the middle instead, sub-node 1 keeps its masked configuration (LDNSLOTS 0x80,
DNMASK 0x3), which a probe doesn't invalidate. The codec clock (CLKOUT2 on ADR2, GPIOSEL 2)
kept running with the tunnel open. Hence no probes behind a last-node far end with audio
(section 4); with that, the same setup played without a glitch.

The erase is the flash's own time (W25Q128JV tSE, 4 KB: 45 ms typical). A read is 256
transactions of 256 bytes: about 5.0 ms each at 2 slots and 2.7 ms at 4, against the
simulator's 1.4 ms and 0.8 ms at 32-bit slots. Not the flash's doing: its 0x03 read runs to
50 MHz and the far end clocks it at 9.8 MHz (0.2 ms for 256 bytes), and a page program
(tPP: 0.4 ms typical) is a small part of a page's time. The host's SPI at 15 MHz in place of
8 changed nothing at 4 slots: read 758 kbit/s, verify 755 kbit/s, program 1593 ms, erase
782 ms, identical read back, BECNT 0.

## 8. Troubleshooting

`LastSpiStatus()` (the console prints it by name):

| Bit | Means |
|---|---|
| SPIINT.BADCMD | A command while the main node was busy, a bad command, or a length that doesn't match the bytes sent |
| SPIINT.SPIDTERR | A tunnel transaction failed: see SPISTAT |
| SPISTAT.DTINVALID | The tunnel isn't set up right (a register, or the slot size) |
| SPISTAT.DTABORT | The far end didn't answer, or an extended transfer was broken by another command; also a probe for a new node behind the far end, which the stack keeps out of the way (section 4) |
| SPISTAT.DTBADPKT | A tunnel packet failed its CRC (a node between replaced or dropped its slots) |
| SPIINT.FIFOUNF | A FIFO read past the data the last read fetched (zeros returned) |
| SPIINT.FIFOOVF | A FIFO overflowed |
| timed out | SPIBUSY outlasted `spiTimeoutMs`; or the main node's SPI port is off and MISO floats high |
| host failed | `Platform::SpiTransfer()` failed (spidev) |

A flash that answers 00 00 00 or FF FF FF: no device on that slave select, the wrong slave
select (`spi open N ss S`), the wrong mode, or the far end's SPI pins not wired.
