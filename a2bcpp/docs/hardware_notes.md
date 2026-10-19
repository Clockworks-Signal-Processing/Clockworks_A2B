# Hardware notes and lessons learned

What the chips, boards and adapters taught us. Each note says what to expect, why, and what the
stack does about it. Most come from the TRMs (`docs/ADI_chips`) and were then confirmed, or
corrected, on the bench. The design decisions are in [decisions.md](decisions.md), the waits in
[timing.md](timing.md).

## Start-up and resets

### SUSTAIN after a soft reset
On an AD243x main node a soft reset unlocks the PLL, and the node then spends 1024 SYNC periods
in SUSTAIN: about 21.3 ms at 48 kHz, about 23.2 ms at 44.1 kHz. **INV and MSTR written inside
SUSTAIN leave the node unusable until it is power-cycled.**

The stack therefore always starts the main node the same way, on every chip: soft reset with
CONTROL.MSTR = 0, a wait of at least 25 ms (a hard minimum, whatever `nRediscWaitTime` says),
I2SGCFG.INV if used, then MSTR, then wait for MSTR_RUNNING. Hosts with no RESETn pulse at
start-up (the Pi, the USBi on Windows) find the main node still running from the last session,
so the reset runs even where a RESETn line exists. `trace_diff.py` holds the minimum with a
default `mingap` rule.

### What each reset keeps
From the TRMs, and modelled in the simulator ([main_node_config.md](main_node_config.md) has
the table):
- A soft reset keeps CONTROL and BMMCFG on the AD2437, and PLLCTL too on the AD2431/32/33/35.
- The AD242x TRM says a soft reset returns every register to its reset value, and a PLL unlock
  keeps BMMCFG and CONTROL.MSTR.
- A soft reset with MSTR = 0 unlocks a running main node's PLL (SUSTAIN on AD243x only); with
  MSTR = 1 the PLL stays locked; in POWER-UP it does nothing.
- Once the PLL is locked, MSTR writes have no effect.
- A soft reset with MSTR kept resets the main node's registers and loses the chain (AD2437 TRM,
  "Sustain Mode"). INTMSK2 (DSCDONE) is reset too, so nothing is discovered until `Start()`.
- Power-on values that aren't 0: CHIP 0x50, RESPCYCS 0x40, PINCFG 0x01, the AD243x crossbar,
  RXMASK and VMAX. I2SRRATE's power-on value is 0x01 on the AD242x too.

### RESETn
Where the platform has the line: low for `BoardTimings::resetPulseMs` (100; the data sheets'
tRST is 10 us), then 2 ms (tACCESS, 1.5 ms in the AD2437 and AD2431W-35W data sheets) before the
first access. Not on a PLL relock. The AD242x has no reset pin.

### Host peripherals before or after the transceiver
Some host peripherals have to be configured *before* the main node is touched. The AB0020's
ADAU1761 gives the AD2437 its SYNC, so a host script that runs after MSTR_RUNNING never lets the
chip start. SigmaStudio+ marks each host peripheral as configured before or after the
transceiver (`bPostDiscCfg`), and the stack follows it (`BoardConfig::hostOpsBeforeMainNode`,
run at `Start()`, not after a PLL relock). ADI's stack configured every host peripheral first.

## Chips

- **The AD2427 is not main-node capable** (AD2426/27/28 data sheet Rev. F, Table 1). It also has
  no I2S/TDM port, only PDM (`ChipCaps::hasI2s`): with a PDM pin on, the BCLK pin is an output,
  typically a 3.072 MHz clock (64 x SYNC) for TDM2/32-bit or TDM4/16-bit.
- **Sub-node count:** every main node takes 16 sub-nodes by the current data sheets (an older
  AD242x revision gave 10 for the AD2428). The 16th node has given trouble on hardware, so the
  default is 15.
- **Register page 1** (0x100-0x1FF) exists on the AD243x only (`ChipCaps::hasPage1`); the AD242x
  register map ends at 0x9B. `rdm 0x100` on an AD242x must be refused.
- **SWSTAT2/SWCTL2 and the power configuration** (CFG0/CFG4) exist on the AD2433 and AD2437
  only. XCVRBINV is on every chip, so it is a link setting, not a capability.
- **Switch mode:** SWCTL 0x11 in the AD243x TRMs' flows, 0x01 in the AD242x TRM's.
- **The AD2433 has the voltage monitor** (VMTR) too.
- **No crossbar on the AD242x:** neither RXMASK nor TXXBAR is in its register map. It has the
  AD243x's slot registers and passing on.
- **VMTR interrupts** must not be enabled on AD243x sub-nodes when the main node is an AD242x
  (AD2431-35 TRM, "Considerations for Systems with Other Transceiver Nodes"). The stack enables
  none; read their VMTR registers instead.
- **An AD243x node's SWSTAT2** is checked before its switch closes, whatever the main node.
- **GPIO0's interrupt-enable and output value** are its own; ADI's conversion read GPIO1's.
- **Spread spectrum** is not in the AD2437 TRM Rev 0.2 (ADI's stack programs PLLCTL for it);
  it is not supported yet.
- **SPI** needs an AD2433 or AD2437 main node. On the AB0020 the image must turn SPI **off**:
  CLKOUT2 is not functional when SPI is enabled with the default GPIO select, so CLKOUT2 never
  reaches ADR2 and the codec gets no MCLK.

## Links, power and discovery

### Open line: INTTYPE 12 and 13
`INTTYPE 12` is PWRERR, open line (AD242x TRM, Table 5-1); 13 is the same at a low VIN. It has
two faces.

1. **Benign: nothing is plugged in behind the last AD242x node.** Each blind probe closes that
   node's switch onto an open B side, and the node reports INTTYPE 12 every time. This is
   expected. The bench console hides types 12 and 13 from the last node (`interrupts quiet`, the
   default; `interrupts all` shows them). The stack itself passes them on as `kInterrupt` events;
   telling an open line from a real cable fault is planned (power diagnostics, in the roadmap).
2. **Real: the switch never finishes closing.** An AD2428's switch does not finish closing onto
   a node that draws no bias current, for example an own-supply AD2437 behind a galvanically
   isolated A side. Symptom: INTTYPE 12 from the AD2428 and no DSCDONE, so the next node is
   never found. An AD24xx passes the next node no frames until SWSTAT.FIN. **Fix: `link N
   custom`**, which sets CONTROL.SWBYP on the upstream node instead of ENSW (AD2428 TRM,
   "Data-Only and Power-Only Bus Operation"). A node on its own supply that still draws the bias
   current (its A side DC-coupled) is found over a UTP link too.

The same happens with an AD2437 behind an AD2427. A link not powered over the A2B pair must set
SWBYP on the node before it: leaving the switch open without SWBYP gives no link at all (an early
`Custom` link did that). The simulator models it (`LinkSpec::isolated`), and the bench console
prints a hint when the last sub-node reports an open line.

**No image can say that the next board has its own supply:** it is the downstream board's
property, not the upstream module's. The app sets the link (`link N custom` in the bench
console) on every start; `link all default` clears it (`link all board` keeps it).

### XCVRBINV and the order of writes
A node's XCVRBINV is set before its SWCTL.ENSW, and every later CONTROL write (NEWSTRCT, for
example) must keep it. DSCDONE comes only when polarity, ENSW (or the node's own supply) and the
response-cycle window are right. A node that is discovered but on the wrong polarity is found,
yet nothing behind it is. Boards differ: the AB0020 is UTP on both ports, although the export
copied from the reference board sets XCVRBINV, so its links must all be UTP.

- An RJ45-to-UTP adapter on the main node's output makes the link UTP: no XCVRBINV, whatever
  the board's export says.
- The AB0310 with an AB0331 (a UTP link through a UTP/RJ45 adapter) needs XCVRBINV off on the
  main node.
- A chain that mixes connection types can need a different value on each link. Discovery
  doesn't try the other polarity: on the wrong one, the main node is found and nothing else.

### Discovery
- **NODEADR points at node n - 1** when DISCVRY is written for node n (TRMs, DISCVRY register).
- **Before a node first powers the next link** it gets INTMSK0 = PWRERR and INTMSK1 = 0, CONTROL
  with the link's XCVRBINV, and, on an AD243x, the power-configuration check:
  SWSTAT2.HPSW_CFG_DET against the module info's power configuration; if they differ, SWCTL2.HPSW_CFG
  and SWCTL.CFG_DET_OV, then 100 ms before ENSW.
- **Nodes before the new one** go to switch mode 2 around the probe and the identification
  (SWCTL 0x21/0x11 in ADI's stack); a probe that finds nothing leaves the last node's switch
  open in its mode.
- **A sub-node's I2C port** runs at 100 kHz and 48 kHz frame rate at reset (I2CCFG 0). Remote
  peripheral access sets the sub-node's CHIP, then NODEADR.PERI. Remote I2C transfers have no
  length limit (the 32-byte limits are SPI's). The host's I2C must allow clock stretching.
- **I2CCFG.FRAMERATE** (1 = 44.1 kHz, sub-nodes only) is set by the stack when
  `routing.frameRateHz` is 44100; the EEPROM read before it runs about 8% slow, harmlessly.
- **I2CCFG.DATARATE** (1 = 400 kHz, sub-nodes only) is set by the stack when the node's image
  asks for it. The write comes after the image's start and groups are read at 100 kHz, and
  before the peripheral programs ([provisioning.md](provisioning.md)). Remote writes take about
  113 µs a byte at 100 kHz and 45 µs at 400 kHz (TRM, remote I2C latency).

### Drops and power faults
- A dropped node NAKs; the stack retries a read 2-4 times (the TRMs' advice; it uses 3), takes the
  node and those behind it out, opens the switch before it, and probes the position again no
  sooner than 250 ms later (AD2437 TRM, "Node Drop Detection").
- A node becomes the last after missing SRFs (32, per the TRMs); an app that wants the
  interrupt can add SRF missed to its `InterruptMask()`. The stack's default masks are PWRERR only.
- **A pulled cable raises no interrupt**; the chain check finds it. **Removing a node's 24 V**
  is a different case: a node that kept running from the A2B pair sees its missing VBUS as a fault
  and reports INTTYPE 15 (undetermined cable fault) once.
- **A power fault on reconnect is contact bounce**, not chip behaviour: a jumper does not make or
  break cleanly, so a second INTTYPE 15 can come when the 24 V is restored. Don't read it as a
  node problem.
- A read of INTTYPE clears the interrupt it reports, which the stack then never sees. Reading it
  from the app is the app's risk.
- An interrupt-messaging error (0x80) or a sub-node INTTYPE read error (0xFD) means a node
  dropped; the chain is checked at once.

## Routing and audio

- **Auto-broadcast registers.** SLOTFMT, DATCTL and I2SRRATE written to the main node are copied
  only to the sub-nodes discovered when they are written, not to a later one. Each plan therefore
  writes them again before NEWSTRCT. (Found on the bench; the simulator models it.)
- **NEWSTRCT settle.** After each NEWSTRCT the stack sends nothing for 1 ms or more (two ticks).
  The TRMs apply a new structure within 5 superframes (0.1 ms at 48 kHz), and the main node holds
  off its own INTTYPE reads of the sub-nodes meanwhile, so an INTTYPE read inside the settle could
  give 0x80.
- **Cable lengths.** Give the real length, or leave the 4 m default on short cables. A
  declared length *longer* than the cable breaks discovery: 10 m declared on a 1 m link needed
  several tries and rejected a node, and 15 m or 20 m found nothing past sub-node 0. The real
  lengths (4 m, then 1 m) moved the main node's RESPCYCS window by one cycle and the sub-nodes'
  values by one or two. 110 ft (33.5 m) of cable ran clean on an AD2428 chain with
  SigmaStudio+, past the data sheets' 15 m (AD242x) and 30 m (AD2437), so the stack enforces
  no limit: it refuses only a length whose response cycles can't be written. A length changed
  on a running, routed bus replans at once; with a wrong length that plan's write once failed
  on an I2C error after NEWSTRCT and the chain dropped and was found again. Set lengths before
  `Start()`.
- **Response cycles** use the AD2431-35 TRM's Formula B (integer arithmetic, as the RP2040 has no
  floating point). The cable length is 4 m per hop unless the app or the upstream node's image
  (its B-side cable, in metres) gives one; each node adjusts its response time
  during discovery (-4 to +15 bit times). The AD2437 TRM Rev 0.2 subtracts 7 instead of 8, and
  one cable delay instead of two, in RESPCYCS_UP, and rounds the main node's value down: under
  one cycle apart. RESPOFFS 254 (SigmaStudio+'s "accurate", used unless "conservative" is set)
  gives a few more cycles than the stack's value; both lie in the valid window (29-211). ADI's
  AD242x Formula A (DISCVRY the main node's value less 4 per position) is not used.
- **TDM order on the AD242x.** An AD242x's RX frame buffer is all its RX channels in order, so a
  run carries every channel from the lowest to the highest it sends (the main node, which has no
  offset register, from channel 0). Its TX channel n plays location n, so the slots it plays, in
  bus order, must fill its TX channels from 0 with no gap. Downstream the main node's slots come
  first, then each sub-node's in chain order; upstream the last sub-node's first.
  How PDM and TDM channels share the buffer when both are on isn't documented.
- **Mixed chains.** The TRMs say nothing per chip about routing: every node passes slots on in
  the order it receives them, so the TDM order of an AD242x sink is the bus order whatever lies
  between. Formula B covers "AD2426/27/28/3x or mixed" systems.
- **I2S port off means no SYNC.** A sub-node image that leaves the I2S port off gives "no SYNC"
  from that node. The routing refuses a connection whose channels aren't on a node's port
  (`RoutingError::kNoSuchChannel`).
- **Sample rates.** A sub-node's 96 or 192 kHz is its own port at 2x or 4x the superframe rate
  (I2SRATE 5 or 6), so a 96 kHz stereo stream is four slots per superframe on that node.

### Muting
RXMASK takes effect at once, like TXXBAR ("RX crossbar settings take effect without setting
CONTROL.NEWSTRCT", AD2437 TRM). From a plan's NEWSTRCT until its crossbar write the new slots can
carry the old frame buffer, so a sink whose crossbar doesn't change can play the wrong channel for
that moment. The local mute therefore also covers each node that plays a slot whose channel its
source's new RXMASK moves, and mutes the whole bus for that plan when a node that needs it can't
be muted alone.

Two assumptions of the local mute are **not in the TRMs and not yet confirmed on hardware**:
TX crossbar location 31 plays silence, and an unfilled RX frame buffer location sends silence.
The mute the TRMs document is DATCTL's DNS and UPS (the data slots' enables), the same on the
AD242x and AD243x; `muteMode global` uses it. See [cookbook.md](cookbook.md#routing).

### Raw register access: the stack's registers
The registers the stack owns are documented in `network.cpp` and the cookbook ("Raw register
access"). In short: a switch opened drops the nodes after it; MSTR cleared starts the bus again;
a soft reset with MSTR kept loses the chain; MMRPAGE is refused (the address picks the page);
NODEADR and CHIP may be written, and the `Transport` rewrites them before its next access.

## Boards

- **AB0020** (main or sub-node, ADAU1761 codec at I2C 0x39): UTP on both ports. The codec gives
  the AD2437 its SYNC and bit clock, so it is programmed first. SPI must be off in its image.
  Its sub-node program is 38 writes of 6076 bytes in all (the largest 1732 B, with its
  sub-address), one 100 ms delay and one read of its PLL register, which a peripheral script
  skips. Behind an ADI USBi the largest write took 375 ms at 100 kHz and 281 ms at 400 kHz,
  at the edge of the USBi's status window (Host adapters, below), so the image tool's example
  splits it in two and sets 400 kHz; [timing](timing.md) has the figures. On the Pi the whole
  program takes 1.26 s at 400 kHz and 2.06 s at 100 kHz.
  A codec needs its program again after a re-plug unless the module's peripheral script (or
  EEPROM) supplies it.
- **`ignoreEEPROM 1`'s image** turns the I2S port on but programs no codec: an AB0020 sub-node
  needs `module N AB0020` for its codec program, or its EEPROM written with the image tool's
  `AB0020.dat`.
- **The Clockworks boards' EEPROM is on the base board** (AB0020, AB0310, AB0312); the plug-in
  A2B module has none, so their images name no chip (target chip External) and the stack
  reads the chip from the module. An AB0312's RJ45 B port is two-step with an AD2437 module
  (which the stack doesn't run: the chain ends there unless the link is set, `link N rj45`)
  and UTP with any other ([provisioning](provisioning.md)).
- **A node's power configuration is its chip's, not its image's.** An image holds one power
  configuration, but a board with a plug-in module may carry an AD2437 (CFG-4) or an AD2428 or
  AD2433 (CFG-0). Taking the image's once gave an AD2428 module CFG-4, and would have
  overridden an AD2433's; the stack now uses the chip's own and ignores the image's byte
  ([decisions](decisions.md#policy-decisions)).
- **The Clockworks boards' EEPROM** is a Microchip 24FC256 or equivalent: 32 KB, 64-byte
  write pages, 5 ms write cycle, 16-bit addresses, at 0x50. ADI's specification recommends
  "256KB" with 2-byte addresses and names the M24512 (64 KB): it means 256 Kbit.
- **The EEPROM's write cycle, seen through a sub-node:** it is busy about 13 ms after a page
  write starts. At the bench console's default 6 ms wait, nearly every page's first write is
  NAKed and the retry 2 ms later succeeds (on the Pi, 17 retries for 18 pages); the main node
  raises no I2C error interrupt for these NAKs. Behind a USBi no NAK shows at all: each transfer
  takes about 8 ms, longer than the write cycle ([provisioning](provisioning.md)).
- **AB0310/AB0312:** RJ45 links (XCVRBINV); the AB0020's I2S settings; SYNC and BCLK from the
  board; BCLK is for 32-bit slots.
- **AB0204** (an AD2427 with four PDM MEMS microphones): the microphones' 3.072 MHz clock on the
  BCLK pin and the PDM high-pass filter on.
- **A SigmaStudio+ project for the AD2428 AB0020 needed fixes** before its export imported
  correctly: the main node's GPIO3/GPIO5 as DTX0/DRX0 (else I2SCFG is 0 and the port check
  refuses every connection), I2S SYNC as the codec programs expect (I2SGCFG 0xE0, not pulse SYNC),
  DATCTL 0x03 (SigmaStudio+ had 0x00: no data on the bus), and IO7 driven high as on the AD2437
  project. The main node's CLKOUT2 stays off (CLK2CFG 0x00) and the audio works.
- **An export that names a constant ADI's header lacks** (for example `A2B_GPIO_2_AS_CLKOUT2`) is
  refused only where the import reads it; elsewhere the loader takes it as 0 with a warning.
- **A CRLF export** (a Windows checkout) must load: `#endif\r` once left every `#if` open.

## Host adapters

- **ADI USBi** (USB ID 0456:7031): no RESETn or IRQ line for the main node, so the interrupt
  registers are read every 20 ms and the soft-reset start-up is used. SigmaStudio's Cypress
  driver (CYUSB) cannot be used by libusb; the USBi needs WinUSB ([setup_windows.md](setup_windows.md)).
  The USBi sometimes stops answering (every first transfer NAKed) until it and the board have
  been power-cycled; the bench console's hint names the address.
- **Known limit, as in ADI's USBi PAL:** the status is polled for about 0.3 s, so a transfer
  clock-stretched longer (a 4 KB script unit behind a 100 kHz sub-node port) reads as failed.
  The image tool warns about a unit over 2 KB, and the bench console's EEPROM reads and writes
  stay small (256 and 66 bytes).
- **USBi transfer time:** each transfer costs about 8 ms whatever its length, on top of about
  215 µs a byte at 100 kHz ([timing](timing.md)). It hides an EEPROM's write-cycle NAKs (above),
  so test write-cycle handling on the Pi or RP2040.
- **Raspberry Pi (i2c-dev):** a write followed by a read must be one `ioctl(I2C_RDWR)` with two
  messages, and a read alone one `ioctl` ([setup_pi.md](setup_pi.md)). The Pi has no reset or IRQ
  line wired on the reference board: the stack resets the main node in software and polls.
  The ADI stack's `A2B_FEATURE_OPTIMAL_RESPCYCS` search was needed for cables longer than 5 m;
  a short-cable bench passes either way.
- **RP2040:** the fixed app and the bench console run on the AB0310 board, with the Pico SDK's
  malloc panicking when out of memory. The Pico projects take the SDK from `PICO_SDK_PATH` if
  it is set, else from the VS Code extension's `~/.pico-sdk`; the console UART runs at 230400.
- **Host I2C must allow clock stretching** (sub-node peripherals stretch the clock).
