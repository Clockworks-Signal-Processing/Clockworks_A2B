# Timing

Every wait and timeout the stack uses, with its value, where the value comes from, and the
`trace_diff.py` rule that checks it in an I2C trace. The stack never delays inside a call. Each
wait is a deadline, checked on every `Network::Poll()`, so a wait ends at the first poll after
its time. Poll every few milliseconds.

The values in `BoardTimings` are per bus (`a2bcpp/include/a2b/board_config.h`).
`ImportSsPlus()` sets `discoveryStartDelayMs` and `rediscoveryWaitMs` from the SigmaStudio+
export. "ADI's stack" below is the stack this one replaces; the goldens are traces captured from
it ([testing.md](testing.md)).

## Main-node start-up and the first sub-node

| Wait | Value | Kind | Source | Trace rule |
|---|---|---|---|---|
| Soft reset to I2SGCFG.INV and CONTROL.MSTR | `rediscoveryWaitMs` (export: `nRediscWaitTime`, 250 on the bench), **never less than 25 ms** | Hard minimum on AD243x | SUSTAIN: 1024 SYNC periods, 21.3 ms at 48 kHz, 23.2 ms at 44.1 kHz (AD2437 and AD243x TRMs, "Soft Reset"; ADI's `A2B_SW_RESET_DELAY`) | `mingap M CONTROL&0x04 M CONTROL&0x80 25` and `mingap M CONTROL&0x04 M I2SGCFG&0x80 25` (default rules) |
| RESETn held low, at `Network::Start()` on a platform with the line | `resetPulseMs` (100) | Minimum | tRST is 10 us (AD2437 and AD2431W-35W data sheets); ADI's RP2040 app held it 100 ms | none (a GPIO, not in the I2C trace) |
| RESETn released to the first access | 2 ms | Hard minimum | tACCESS is 1.5 ms (the same data sheets) | none |
| CONTROL.MSTR to MSTR_RUNNING | up to `discoveryStartDelayMs` (export: `nDiscoveryStartDelay`, 25 if 0); then one last interrupt check, then `NetworkError::kMainNodeNotRunning` | Timeout | tPLK is 360 SYNC periods, 7.5 ms at 48 kHz (TRMs, "Main Node Bring-Up"); the goldens show 2.2-22.6 ms; ADI's stack failed at 25 ms, after a last-chance read | none (interrupt-driven) |
| Host script waits (`Op::Wait`) | as the export says (the AB0020 codec: 100 ms) | Hard minimum | SigmaStudio+ delay units | the reference's waits, `--delay-ratio` |
| Interrupt checks (INTSRC read) | every `interruptPollMs` (20), at once after an interrupt; gated by `Platform::IrqPending()` | Period | ADI's interrupt poll period (goldens: INTSRC every ~20 ms) | `--jitter-ms` (25) allows one poll interval |

Soft reset, SUSTAIN and MSTR are the start-up sequence in the [main-node configuration
guide](main_node_config.md), under "Start-up"; why the 25 ms matters is in the [hardware
notes](hardware_notes.md#sustain-after-a-soft-reset).

## Discovery and chains

| Wait | Value | Kind | Source | Trace rule |
|---|---|---|---|---|
| DISCVRY to DSCDONE | up to `discoveryTimeoutMs` (70); then one last interrupt check, then ENDDSC | Timeout | ADI's `A2B_DISCOVERY_DELAY` 50, or 70 behind a medium- or high-power AD243x. The stack can't know the node in advance, so it uses 70. The goldens show DSCDONE 15-36 ms after DISCVRY | none (interrupt-driven) |
| A node's power configuration overridden (SWCTL2, SWCTL.CFG_DET_OV) to its switch closing (ENSW) | 100 ms | Hard minimum | The TRMs' discovery flows ("WAIT 100 ms delay"); ADI's `A2B_HIGH_PWR_SWITCH_DELAY` | `mingap M SWCTL=0x12 M SWCTL&0x01 100` and the same for `S*`, per node (default rules) |
| A node found and configured to the probe for the next position | none: the next poll | - | The TRMs' flows discover the chain node after node. ADI's PnP layer probed again only after routing, at its 500 ms period | none |
| A probe that found nothing to the next probe, at the end of the chain | `probeIntervalMs` (500) | Period | ADI's idle probe period (goldens: DISCVRY every 500 ms) | none |
| A failed probe or set-up to the next probe | `probeIntervalMs` (500) | Period | New: ADI's stack stopped on a failed transfer | none |
| Two-step: the node found at 5 V and reviewed to the switch opened and SWBYP cleared, then the 24 V switch closed and DISCVRY again | none: the same poll, then the next | - | ADI's `a2b_dscvryNodeDiscovered` goes straight on to the second step (AD2437 TRM, Figures 7-2 and 7-3, no wait between the steps) | none |
| A VBUS reading (VMTR_VLTG1) under 4 V to the node's rejection | read again at each poll for 10 ms | Timeout | ADI read it once, straight after VMTR_VEN, and the bench's G13 trace has the value there; the window is a margin | none |
| A two-step link's power-good low to the next try | `probeIntervalMs` (500) | Period | ADI's stack ended discovery | none |

The overridden configuration and the wait happen at most once per node and start-up: the check
is made before a node's first probe (the main node's, then each sub-node's as it becomes the
last). The goldens have no override, since their AD2437 nodes detect CFG-4 as the export and the
module info expect (SWSTAT2 0xA0).

## Module info and peripheral scripts

| Wait | Value | Kind | Source | Trace rule |
|---|---|---|---|---|
| A peripheral script's type C unit | its payload size, in ms (ADI's ADAU1701: 16 ms before its first write) | Hard minimum | The module's image (ADI's `a2b_pnp_periphCfgProcessing`) | the reference's waits, `--delay-ratio` |
| A node's `OnConfigured()` waits (`Op::Wait`) | as the app says | Hard minimum | The app | none |

Two things take time inside one `Poll()`: the EEPROM reads, and each write unit of a peripheral
script, which is read from the source and written in one transfer.
- **The EEPROM reads:** a sub-node's I2C port runs at 100 kHz from reset, so the 144-byte start of
  an image takes about 15 ms.
- **The peripheral scripts:** they run at 100 kHz, or at 400 kHz when the image sets
  I2CCFG.DATARATE (the stack writes it before them).
- `Poll()` runs at most one unit. By the TRMs' figures (113 µs per byte at 100 kHz, 45 µs at
  400 kHz), ADI's 4090-byte ADAU1701 unit keeps one `Poll()` busy for about 0.8 s at 100 kHz
  (0.4 s to read it, 0.4 s to write it), as ADI's blocking calls did, and about 0.4 s at 400 kHz,
  which ADI's RJ45 image sets. A host adds its own time per byte (below). The node counts as
  found only after its scripts and its `OnConfigured()` operations, so the next probe waits for
  them.

**Measured behind an ADI USBi** (Windows; AD2428 main node, AD2428 and AD2437 sub-nodes, the
AB0020's ADAU1761 program, 6076 bytes of data, from its EEPROM):

| | 100 kHz | 400 kHz |
|---|---|---|
| A remote read, per byte | 215 µs | 147 µs |
| A remote write, per byte | 217 µs | 162 µs |
| A remote transfer of a few bytes | about 8 ms | about 8 ms |
| The 1732-byte program write | 375 ms | 281 ms |
| The whole program, read and written unit by unit | 4.8 s | 3.95 s |

The 400 kHz port saves 68 µs per byte, as the TRMs' figures say; the rest of each byte's time
is the USBi's (its own I2C to the main node, and USB). The USBi checks a transfer's status for
about 0.3 s ([porting](porting.md)), so the image tool warns about a unit that would take over
0.25 s at these rates.

**Measured on the Pi** (i2c-dev at 400 kHz to an AD2437 main node, AB0020 sub-nodes with AD2428
and AD2437 modules, the same program as the 8376-byte image holds it: 6078 bytes, the split write
adding a 2-byte sub-address; times from the I2C trace, a transfer's start to the next
transfer's start):

| | 100 kHz | 400 kHz |
|---|---|---|
| A remote read, per byte (256-byte transfers, 39.4 ms each) | 157 µs | not measured |
| A remote write, per byte (1022-byte units) | 148.5 µs | 85.9 µs |
| The whole program (39 writes, one 100 ms wait) | 2.057 s | 1.262 s |
| The 144-byte start of the image and its groups (9 transfers, 319 bytes) | 47 ms | n/a |

- A 32 KB EEPROM read takes 5.13 s (128 transfers); the first 8.4 KB takes 1.33 s.
- Against the USBi, the Pi's per-byte times are about 27% lower at 100 kHz (reads 157 against
  215 µs) and about 47% lower at 400 kHz (writes 86 against 162 µs), and the whole program
  takes about a third of the time at 400 kHz and under half at 100 kHz. The Pi has no 8 ms
  per transfer and no status window.
- **EEPROM page writes.** At the default 6 ms write-cycle wait a 64-byte page write (a
  66-byte transfer) is followed by the next one 11.0 ms later. That try is NAKed, and the
  retry 2 ms after it succeeds, so successful page writes land about 13 ms apart. A 3-byte
  marker write needs no retry. The EEPROM is therefore busy for about 13 ms after a page write
  starts: the 64 bytes take about 6 ms on the node's 100 kHz port, then the 24FC256's write
  cycle of up to 5 ms. This is a reading of the traces, not a measurement of the part.
  Writing 18 pages cost 17 retries on each of two nodes and 14 on the third, with no interrupt
  reported by the main node. A longer wait than 6 ms would remove them; they cost about 2 ms
  a page.
- The three nodes' programs ran in the same 1.26 s, and a trace of a start from the EEPROMs
  holds no failed transfer except the AB0204's five searches at 0x50-0x54.

## A running chain: checks, drops, rejections and BECNT

| Wait | Value | Kind | Source | Trace rule |
|---|---|---|---|---|
| Chain checks (main node VENDOR, PRODUCT and CONTROL; each sub-node's VENDOR and PRODUCT) | every `chainCheckMs` (500), between probes; at once after an interrupt that may mean a drop, or a failed transfer | Period | The TRMs' "Bus Drop Detection" flow (a periodic read of every node); ADI's stack read VENDOR of each node before each probe, every 500 ms, and not while probing was paused | none: the extra reads would need a rule in a golden replay, which was not built |
| A read in a chain check | up to 3 tries, back to back | Retries | The TRMs: retry a failed access 2-4 times before checking for a dropped node | none |
| A dropped node's upstream switch opened to the next probe there | `probeIntervalMs` (500), **never less than 250 ms** | Minimum | The AD2437 TRM's bus drop flow waits 250 ms after SWCTL = 0 (the AD242x TRM's, 100 ms) before discovery, so the dropped part starts from power-up | none |
| A rejected node to the next probe of its position | `rejectedProbeMs` (2000) | Period | New: the position is probed again for a different module; the same node (its chip ID) is switched off again at once | none |
| BECNT cleared (0 written to every node) | every `becntResetMs` (1000), between probes; 0 for none | Period | The TRMs ("Line Diagnostics After Discovery": write 0 to BECNT once every second); ADI's `a2b_app_handle_becovf`, a broadcast every 1 s | none (a broadcast write of 0x1F) |

A node is also dropped at once when an interrupt says so: SRF missed from the new last node (if
its `InterruptMask()` enables it), a power fault, an I2C or interrupt messaging error, or a
sub-node INTTYPE read error (0xFD).

What the bench showed:
- On the Pi bench the last of four nodes was switched off by removing the 24 V at the node before
  it, which kept running from the A2B pair and reported a power fault (INTTYPE 15, its VBUS
  missing) with the default masks. The drop was handled 3 ms after that interrupt was read, so
  within one interrupt poll (20 ms), and the node was found again by the first probe after the
  24 V came back (DSCDONE 28 ms after DISCVRY).
- A cable pulled between two nodes (a chain of six) is found by whichever comes first, the next
  chain check or the next probe at the end of the chain, which touches every node and gives up at
  the first that doesn't answer ("probe for sub-node n failed; checking the chain"), so within
  about half a second; in the bench's trace the probe met it first each time, and the drop was
  handled 9 ms after that probe began. No pull raised an interrupt. A power fault (INTTYPE 15)
  came only on a re-plug, from the node before the cable, while a probe had its switch closed: the
  connector making contact, as with the 24 V jumper. Nodes plugged back in were found by the next
  probe (DSCDONE 20-25 ms after DISCVRY).
- The TRMs: a node becomes the last after 32 missed SRFs (under a millisecond), so by the next
  interrupt check the nodes after it no longer answer.

## Routing

| Wait | Value | Kind | Source | Trace rule |
|---|---|---|---|---|
| CONTROL.NEWSTRCT to the next transfer of any kind | 2 ticks of the millisecond clock (1 ms or more) | Hard minimum | The TRMs: a new structure (slot registers, RESPCYCS) applies within 5 superframes (0.1 ms at 48 kHz), and the main node holds off its INTTYPE reads of the sub-nodes meanwhile; ADI's `a2b_delayForNewStruct`, `A2B_DELAY_FOR_NEWSTRUCT_IN_MS` 1. After sub-node 0's probe (position 0's RESPCYCS and NEWSTRCT, before its switch is closed and DISCVRY written) and after a routing plan's slot registers, before its crossbars | none in the traces; the simulator counts every access made within 1 ms of a NEWSTRCT (`Bus::AccessesInNewStructSettle()`, 0 in every routing scenario) |
| `ApplyRouting()` or a change of the chain to the plan's first write | none: the first poll between probes once `kRunning` | - | New: ADI's PnP layer routed from its own loop, and probed again only after routing | none |
| A failed routing write to the next try | `probeIntervalMs` (500), after a chain check | Period | New: ADI's stack stopped on a failed transfer | none |

The settle is two ticks because the clock is in milliseconds: a NEWSTRCT written just before a
tick would have only part of a millisecond before the next tick.

## Why a `mingap` rule for SUSTAIN

ADI's stack writes two soft resets 12.6 ms apart at start-up (goldens `boot4` and G13). A `delay`
rule measures from a matching write to the *next* compared event, so it would flag the first
reset. A `mingap` rule measures from the *last* soft reset to the next write of INV or MSTR,
whatever comes between. Both rules are in `a2bcpp/tools/trace_rules_default.txt`, so every
comparison checks them:

- the current goldens (captured after ADI's stack was fixed to wait) keep them: INV and MSTR come
  250 ms after the second reset;
- ADI's earlier boot trace breaks both, with INV and MSTR 0.1 ms after the reset. That is the bug
  that left the main node unusable until a power cycle;
- the new stack keeps them, on the simulator and by construction (`Network` never waits less than
  25 ms).

## Not yet implemented

ADI's stack has these waits; the C++ stack gets them with the feature:

| Wait | ADI's value | Feature |
|---|---|---|
| Power-diagnostic settles | 100 or 250 ms | power diagnostics |

Not ported: the high-power switch mode-4 open-detect timer. ADI's PnP build compiles it out
(`#ifndef A2B_FEATURE_PNP` in `a2b_onDiscTimeout`); only its reads of SWSTAT2 and SWCTL after a
discovery timeout remain, a trace difference with no rule.
