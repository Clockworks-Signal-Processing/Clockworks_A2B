# A2B PnP C++ stack: roadmap

What the stack does, what it has been tested on, what it doesn't include, and what still needs a
hardware test. Why it is built the way it is: [design decisions](a2bcpp/docs/decisions.md),
which also holds the designs for the features below that are designed but not built. What the
hardware taught us: [hardware notes](a2bcpp/docs/hardware_notes.md).

## What it does

| Area | What is built |
|---|---|
| Main node | Start-up from a SigmaStudio+ export (compiled in or loaded at run time), host scripts before or after the transceiver, restarts |
| Chains | Blind discovery, hot-plug, drops and re-plug, rejections and the node review, the power budget, up to 16 sub-nodes (15 by default) |
| Sub-nodes | Module info from the EEPROM or from images in memory, peripheral scripts, 400 kHz I2C when the image asks, `NodeFactory` and the `SubNode` hooks |
| Links | Single-step UTP, RJ45 (B side inverted) and Custom (own supply, SWBYP) links, per position; two-step RJ45 and XLR links from an AD2437 (found at 5 V with SWBYP, the 24 V turned on after the review for a CFG-4 node, VBUS checked with VMTR), also on a UTP link that asks; the link from an image's B port; cable lengths per link |
| Audio | Routing on AD243x crossbars and AD242x TDM order, mixed chains, the routing mutes and `MuteNetwork()`, bandwidth, 44.1/48/96/192 kHz |
| Access | Raw register access, peripheral read/write/write-read on I2C, and on SPI through the data tunnel |
| SPI | The SPI data tunnel from an AD2433 or AD2437 main node to one sub-node's SPI devices: its slots in the routing plan, the six transaction types (atomic write and read, full duplex and bulk, and their extended forms), the main node's registers over SPI; the Pi's spidev; the bench console's `spi`, `spiflash`, `wait` and `--spi-flash-check` ([SPI](docs/SPI.md)). New in 0.6; tested on the Pi (below) |
| Errors | The error watch (`Network::SetErrorWatch()`, on in the bench console): bit errors counted on every node and summed before each BECNT clear, the SRF-missed and BECOVF interrupts, and a report of every node's error registers (SWSTAT, INTPND0-2, INTMSK0, BECCTL, BECNT, NODE; the console prints all but INTMSK0 and BECCTL) when a probe fails or before a drop; failed accesses named in the log with their time; interrupt types named; the console's `errors` command |
| Several buses | One `Network` per bus on one host |
| Provisioning | The image tool (`tools/a2b_eeprom`), `ImageModuleInfoSource`, the bench console's `eeprom` commands |
| Platforms | Linux (i2c-dev; spidev for SPI), RP2040 (Pico SDK), Windows over the ADI USBi (libusb) |
| Examples | The minimal example, the fixed app and the bench console |
| Tests | Unit tests, the simulator's scenarios, the import and example tests, two golden traces ([testing](a2bcpp/docs/testing.md)) |

## Tested on hardware

| Host | Configurations |
|---|---|
| Raspberry Pi (Linux, i2c-dev) | AD2437 main node (`ref`): a chain of four from their EEPROMs; hot-plug at the end; a node's 24 V removed and restored; a chain of six with UTP and RJ45 links and cables pulled at three places. Mixed chain behind the AD2437 (AB0020/AD2428, AB0204/AD2427, two AB0020/AD2437, one on its own supply with `link 2 custom`): EEPROMs read, written, verified and booted from; a write stopped by a pulled cable; codec programs at 100 and 400 kHz; real cable lengths; audio between sub-nodes |
| Raspberry Pi, two-step | The reference board with four AD2437 sub-nodes from their EEPROMs, every link two-step RJ45 (module info read at 5 V), and a fifth node: both steps, a node refused without its 24 V and joining once it is back, power-good low, single-step links still working, the fifth node dropped and found again |
| Windows (ADI USBi) | AB0020 with an AD2437 main node and up to four AD2437 sub-nodes (UTP and RJ45 links); AB0020 with an AD2428 main node and AD2428, AD2427 and AD2437 sub-nodes (an AD2437 on its own supply with `link N custom`); routing on both, a node re-plugged while routed; EEPROMs written, verified and booted from, three ADAU1761 programmed from their EEPROMs |
| Windows (ADI USBi), two-step | AB0310 with an AD2437 main node and four AD2437 sub-nodes on custom boards, every link two-step RJ45, with the console's module info and with the AB0312 image: both steps, a node refused without its 24 V and joining once it is back, power-good low, single-step links still working, a fifth node plugged in, dropped and found again. A mixed chain of six: an AB0020 with an AD2428, an AB0204 with an AD2427 and four AD2437 nodes, the first on its own supply (`custom` links, RJ45/UTP converter boards): two-step only between the AD2437 nodes |
| RP2040 (AB0310 board) | The fixed app: an AB0312 and four AD2437 modules on RJ45 links, both connections routed with nothing typed. The bench console: four sub-nodes found and audio routed |
| RP2040 (AB0310 board), two-step | The bench console with four AD2437 sub-nodes from their EEPROMs, every link two-step RJ45, and a fifth node: a node refused without its 24 V and joining once it is back, power-good low, single-step links still working, the fifth node dropped and found again |
| Raspberry Pi, SPI over A2B (0.6) | AD2437 main node (`ref`), host SPI on spidev0.1 (CE1 to ADR1) at 1, 8 and 15 MHz, a W25Q128 on an AB0020 (AD2437) at the tunnel's far end, first as the only sub-node, then in the middle and at the end of a chain of two: the tunnel at 2, 4 and 12 slots; all six transaction types (`spi write`, `read`, `fd`, `bulk`, `dump`, `send`); the flash's ID (`--spi-flash-check`, also with a wrong ID), and 64 KB erased, programmed, verified and read back identical at 2 and 4 slots; probes for a new node giving way to SPI; a node plugged in behind the far end, and cables pulled behind and before it during jobs; the AB0020's codec clock kept with the tunnel open (GPIOSEL 2); audio between the two AB0020 on the same link as the tunnel, without a glitch ([SPI](docs/SPI.md#7-on-the-bench)) |
| The error watch | On from the start in the bench console on both sites' Pi benches and on the USBi, over repeated start-ups (`tools/drop_test`, ten starts a run, 2026-10-20): the drop reports, the failed-access lines, `errors` and `errors report`. Not yet run on the RP2040 |

## The error watch: results and what's left

The stack and the bench console catch and report what the nodes see (the error watch;
[architecture](a2bcpp/docs/architecture.md#finding-the-chain), the console's `errors`
command).

- **The bench runs found the drops' cause** ([testing](a2bcpp/docs/testing.md#catching-a-drop)):
  short links left at the 4 m default cable length. With lengths close to the real ones there
  were no drops ([hardware notes](a2bcpp/docs/hardware_notes.md#routing-and-audio)). The
  dropped node never answered the late read, so the chain check's drop decision stays.
- **Not done:** the per-error interrupts (CRC, decoding, header count, parity) stay masked,
  as they would flood the 20 ms INTTYPE poll on a noisy link (BECNT counts them); VMTR is
  not read in the report; the AD242x end-of-chain open-line interrupt is still only hidden
  by the console ([power diagnostics](a2bcpp/docs/decisions.md#power-diagnostics)).
- **BECNT counts header-count errors** (BECCTL's ENHDCNT), which the TRMs advise against: a
  header-count error mostly raises a CRC error for the same line error, so one error can
  count twice and a sum is an upper bound. The watch takes it so that nothing goes
  uncounted, which suits comparing links; clearing the bit would make the figure a count.

## Features not included

Each is one or two lines; where a chip register exists, its call is there and returns
`kNotSupported`.

- **Power diagnostics:** telling the faults apart after a power-fault interrupt, the benign open
  line at the end of an AD242x chain from a real cable fault in particular
  ([design](a2bcpp/docs/decisions.md#power-diagnostics)).
- **AD2437 dual parallel buses** (`BusPair`) ([design](a2bcpp/docs/decisions.md#dual-buses-buspair)).
- **The C facade** with ADI's `a2bpnp.h` names ([design](a2bcpp/docs/decisions.md#the-c-facade)).
- **SPI throughput:** on the bench the console's flash jobs read at about 0.41 Mbit/s through 2
  tunnel slots and 0.76 Mbit/s through 4 (24-bit slots, host SPI at 8 MHz): about a third and a
  fifth of the slots' raw payload rate, and well under the simulator's model
  ([SPI](docs/SPI.md#7-on-the-bench)). Not the flash (its 0x03 read runs to 50 MHz; the far end
  clocks it at 9.8 MHz, 0.2 ms of a 2.7 ms transaction) and not the host's SPI (the same rate
  at 15 MHz as at 8). Fitting the two slot counts gives about 1.5 ms a transaction that doesn't
  depend on the tunnel, and the tunnel's part about twice the model's. A `--trace` of the same
  jobs (SPIT and I2CT time stamps) would split it. Likely causes:
  - (~55%) the tunnel carrying less a superframe than the model takes from the TRM's tables;
  - (~40%) the cost of each transaction: one transaction a poll and the console loop's 1 ms
    sleep, the SPIBUSY status polls, and the SPIINT read and clear over I2C at 400 kHz;
  - (~5%) something else.
- **SPI beyond the data tunnel:** SPI host control, SPI remote register access and SPI-to-I2C
  (ADI's anomaly 18000065 is a reason to keep I2C for those), SPI peripherals on node GPIO slave
  selects, a tunnel owned by a sub-node or several tunnels, an image's SPI peripheral blocks
  (still skipped), and SPI on the USBi and the RP2040 ([SPI](docs/SPI.md)).
- **Spread-spectrum clocking** on every node (ADI's `a2b_ConfigSpreadSpectrum`; the main node's
  PLLCTL already comes from its export): `Network::SetSpreadSpectrum()`. Not in the AD2437 TRM
  Rev 0.2.
- **GPIO over distance:** `Network::SetupGpioOverDistance()`, `ClearGpioOverDistance()`,
  `ReadVirtualIoPort()`, without the bugs in ADI's versions; and the simulator's model and console
  commands for them.
- **Custom B port types** (RJ45-CUSTOM, RJ45-CUSTOM-INV, CUSTOM): `CustomBPortLink()` has no
  handler, so the chain ends behind such a node ("Unsupported B port type").
- **SigmaStudio+ sub-node types** matched against the module type in the EEPROM.
- **ADI's `.dat` configuration files** as an import.
- **ADI's cable-length algorithm** as a `RespCycles()` policy, and an automatic retry of the
  other polarity in discovery.
- **A supplier-block reader** for `OnNodeIdentified()`, if apps need the vendor data during the
  review (the bench console reads it with `eeprom info`).
- **A USBi on a Linux host:** ADI ships no Linux driver; it may need the USBi's firmware loaded
  (fxload) and a udev rule.
- **A differential routing test** against ADI's `a2bpnp_calc.c` on 10k topologies, with the
  intended-difference rules ([decisions](a2bcpp/docs/decisions.md#bugs-fixed-not-ported)).
  It needs ADI's PnP layer (release 1.3.0), which is not part of this repository, and is best
  run on frozen code.

## Known issues

- **No hot-plug behind an SPI tunnel's last-node far end with audio:** while the tunnel ends
  at the chain's last node and that node plays or sends audio, the stack makes no probe behind
  it, because each probe glitched the node's audio on the bench (the TRM's last-node tunnel
  configuration holds only while the node is last). A node plugged in there is found once the
  tunnel is closed or moved ([SPI](docs/SPI.md#4-the-api)).
- **A node dropped after an EEPROM write to the last node:** after an AB0312 image was written
  to the last node with the EEPROMs read, the probe for the next position failed and the chain
  check dropped the node, which then came back from its new image. The cause is not known. Avoid
  it with `ignoreEEPROM 1` while writing.
- **A start from the EEPROMs of four custom AD2437 boards:** the EEPROM at 0x50-0x54 NAKed
  right after CHIPID, two-step and single-step alike, and the node was rejected for want of
  module info, though the same EEPROMs were read and written after node set-up. The cause is
  not known (the EEPROM's supply, perhaps).
- **A node and the nodes behind it dropped at start-up or while idle, then came back:**
  short links left at the 4 m default cable length. Give each link a length close to its real
  one ([hardware notes](a2bcpp/docs/hardware_notes.md#routing-and-audio)); the stack can't
  measure cables.
- **An AB0312 (or AB0310) with the AB0331 module** (an AD2437 on UTP): the AB0312's image
  says RJ45, as it is built for the AB0371, and the EEPROM on the base board can't tell the two
  AD2437 modules apart, so the link after it runs two-step and inverted. Set that link
  (`link N utp`), or write the board's EEPROM with an image whose B port is UTP
  ([boards](a2bcpp/docs/boards.md)).
- **The EEPROM write-cycle wait:** at the default 6 ms, nearly every page write's first try is
  NAKed and retried on the Pi (about one retry a page; harmless, but slower). Raise it with
  `eeprom config cycle MS` if that matters.
- **A rejected node on its own supply** (a link not powered over the A2B pair) stays discovered,
  so its position isn't probed again until `Start()`.
- **No image can say that the next board has its own supply:** the app sets `link N custom` on
  every start ([hardware notes](a2bcpp/docs/hardware_notes.md#open-line-inttype-12-and-13)).
- **`SubNode::TweakRegisters()` is not seen by routing:** routing reads the module info's port
  settings, not the tweaked ones; override `RoutingPolicy::MapGroups()` too when a hook changes
  I2SCFG, I2SGCFG, I2SRATE or PDMCTL.

## What needs testing on hardware

Built and tested on the simulator, not yet on hardware:
- **SPI over A2B with an AD2433** at either end (the Pi bench's AD2437 runs are under "Tested
  on hardware"; [SPI](docs/SPI.md)).
- **An AD2433 main node**, and an AD2433 in a routed chain (an AD2433 sub-node in a mixed chain
  has been found without issues).
- **An AD2437 main node with AD242x boards** (the mixed chain turned round).
- **Several buses** on one host.
- **The local mute's two silence assumptions:** that TX crossbar location 31 and an empty RX
  frame buffer location play silence. They need a crossbar node that plays more than one pair, or
  an RX group that doesn't start at its first channel (a wider TDM board with a codec, or a scope
  on an AD2437's DTX pins). `muteMode global` (DATCTL) is the documented mute meanwhile.
- **Mute and sample rates:** `mute on`/`off` by ear or scope, `muteMode global` on an AD242x
  chain, and a 96 kHz stream between two AB0020 with I2SRATE 5 in their images.
- **The bench console's register commands on an AD2428 main node** (`rdm 0x100` must be refused:
  no page 1).
- **The RP2040 minimal example** on its board.
- **`ram_report.py` rerun** on a PC with the Arm toolchain, to confirm the 32-bit Impl rows
  after the error watch: they grew by 304 bytes where a 64-bit host measures 296
  ([RAM on the RP2040](a2bcpp/docs/ram.md)), and after 0.6's SPI tunnel, which added 56 bytes
  on a 64-bit host.
- **EEPROMs:** `eeprom config cycle 0` on the Pi or RP2040 (does a write-cycle NAK raise the main
  node's I2C error interrupt?); `eeprom erase`; a backup written back; the 20 m image on a long
  cable; the boards' power-good GPIO and EEPROM write protect ([provisioning](a2bcpp/docs/provisioning.md)).
- **The SigmaStudio+ import of an AD2428 export** in `test_ss_plus_file`, and an export's host
  operations against a real board ([main- and sub-node programming](a2bcpp/docs/main_and_sub_node_programming.md)).
- **Earlier releases' fixes** that change behaviour on the bench and have not been re-checked
  there: `eeprom abort` typed after the write has cleared the marker (the node left blank, and
  the console saying so; an abort is taken at once on the Pi), as in
  [testing](a2bcpp/docs/testing.md#bench-checks); a cable length over 30 m accepted; the RP2040
  console's input with a pasted block of commands.
- **0.5's error watch** on the bench, beyond the drop hunt it was written for:
  `errors off` putting each node's BECCTL and INTMSK0 back; `errors clear` on a running chain;
  a SWSTAT cable fault named on an AD242x sub-node (codes 4 and 5, which only those chips
  report; the simulator covers the naming, no bench has raised one).
- **The real cable lengths on the USBi bench**: they stopped the drops on both sites' Pi
  benches, and the USBi's mixed chain has not been run with them
  ([hardware notes](a2bcpp/docs/hardware_notes.md#routing-and-audio)).
- **Two-step RJ45 links**, as in [testing](a2bcpp/docs/testing.md#two-step-rj45-links), on
  every host: a link cut between the first nodes; a start from the EEPROMs of the custom
  boards on the USBi (Known issues, above); power-good read on an AB0312 with IO7 an input
  (the bench's boards were custom ones, power-good on GPIO7). A node that isn't CFG-4 kept at
  5 V has no hardware to run on (only the AB0371 module, an AD2437, is used on RJ45:
  [boards](a2bcpp/docs/boards.md)) and is covered on the simulator only.
- **XLR links** (no XLR hardware yet).

## Documents not written

Interconnects, C API compatibility notes and the migration map (with the
facade), multiple and dual buses (with `BusPair`), troubleshooting, and a changelog.
