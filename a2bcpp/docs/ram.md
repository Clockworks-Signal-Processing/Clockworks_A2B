# RAM on the RP2040

The RP2040 has 264 KB of SRAM. The C++ stack must use no more than ADI's stack does (a
condition of the C facade, [decisions](decisions.md#the-c-facade)), and less is the goal.
This page records what the current version uses. `a2bcpp/tools/ram_report.py` reruns the
numbers; rerun it when a change adds per-bus state.

## Current figures

Measured with the Arm GNU Toolchain 13.2.rel1 (Debian's `gcc-arm-none-eabi` 13.2.1, the Pico SDK
2.0.0's version). The two optimisation levels agree on every size.

| What | Bytes |
|---|---:|
| Static RAM (`.data` + `.bss`) | 0 |
| **`Network`, per bus** | **15,368** |
| of which `Network::kImplSize` | 15,360 |
| of which the Impl uses | 15,160 (200 left on the RP2040) |
| On 64-bit hosts, the Impl | 17,024 of 17,408 |
| `BoardConfig`, per bus (the app's; the main node's `ChannelGroups` in it) | 708 |
| **Two buses: two `Network`s** | **30,736** |

`Network` keeps all of a bus's state in fixed storage inside the object, so these are the whole
cost: no heap. The app also keeps each bus's `BoardConfig` and host script.

### Where a `Network` spends its bytes

The main components, from the last time the report listed them (the rest is small state: the
transport caches, the interrupt poller, the chain-check and BECNT deadlines, the mute state and
bandwidth):

| Component | Bytes |
|---|---:|
| Peripheral-script buffer (a unit is written in one transfer: ADI's ADAU1701 program is 4090 B; the size field allows 4095) | 4096 |
| `SubNodeInfo` × 16 (each with its `ChannelGroups`, 136 B; the image's target chip, B port and cable length added 4 B) | 224 × 16 |
| `SubNode` slots, 16 × `kSubNodeSlotSize` (64), and their pointers | 1024 + 64 |
| `RoutingPlan` × 2 (written, being built) | 1020 × 2 |
| `RoutingPlanner` working arrays, its input and the response-cycle input | 812 + 272 + 80 |
| `NodeSetup` (the node's `RegisterImage` 484, `OnConfigured()` ops 8 × 20, `ModuleScript` 88) | 776 |
| `ConnectionTable` (32 connections) | 580 |
| The main node's start-up `RegisterImage` | 484 |
| Event queue, 32 × `Event` (12) | 384 |
| `Discovery`, and the links' settings (17 × 14) | 212 + 238 |
| `LinkProfile` × 16 and flags (the app's per-link profiles) | 96 + 16 |
| Cable lengths (`SetCableLength()`) | 32 |
| `Transport`, `ScriptRunner`, `InterruptPoller` | 40 + 60 + 24 |
| `EepromModuleInfoSource` | 28 |

If RAM gets tight, the groups (8 each way per node) and the second plan are where it is: a plan
could be built straight into the registers' writes at the cost of `Routing()`, and each
`SubNodeInfo` keeps the node's parsed register list (48 B of it), which could move out.

The minimal example's RP2040 build (Debug, Pico SDK 2.0.0, stdio and I2C drivers included)
has 37,540 bytes of static RAM: `.bss` 30,632 (its app object: one `BoardConfig`, a 64-op host
script, the `Network`) and `.data` 6,908. The fixed app's figures are below, beside the bench
console's.

### Stack

Worst path, in bytes, plus the platform's I2C driver depth:

| Path | `-Og` | `-O3` |
|---|---:|---:|
| **Two buses, one thread** (`a2b_PollTwoBuses`), through a peripheral script's EEPROM read (`ModuleScript::RunUnit`) | **456** | **392** |
| Two buses, through the EEPROM scan (`Discovery::Identify`) | 440 | 368 |
| Two buses, as the call graph gives it (the routing plan, the local mute) | 448 | 416 |
| One bus (`Network::Poll`), through `ModuleScript::RunUnit` | 440 | 376 |
| One bus, as the call graph gives it | 432 | 400 |

With one thread polling two buses, the high-water mark is 456 B plus the I2C driver. An app's
own `RoutingPolicy` adds its depth in place of `ComputeResponseCycles()`'s (232 B `-Og`, 176 B
`-O3`); an app's own module-info source adds its depth in place of the EEPROM source's; and a
handler that calls `PeripheralWrite()` adds about 170 B to its own frame. With a thread per bus,
each thread needs the single-bus figure plus its driver.

`SetAudioRoutes()` keeps a table of 32 connection ends on the stack (336 B at `-Og`). It is an
app call, not on `Poll()`'s path, but an app calling it from a small task stack should count it.

## The bench console on the RP2040

The bench console's Pico build (`a2bcpp/examples/bench/rp2040`, [bench
console](bench_console.md#on-the-rp2040)), measured from its ELF (`arm-none-eabi-size -A`, the
`.bin` for flash), Pico SDK 2.0.0, toolchain 13_2_Rel1:

| What | Debug (`-Og`) | Release (`-O3`) |
|---|---:|---:|
| Flash (the `.bin`) | 233,032 | 280,876 |
| `.data` | 18,520 | 18,360 |
| `.bss` | 37,112 | 37,120 |
| **Static RAM** (`.data` + `.bss`) | **55,632** | **55,480** |
| Left for the heap (`.heap`) | 206,320 | 206,472 |

Of the `.bss`, the `BenchConsole` is 23,688 bytes (its `Network`, `BoardConfig`, 256-op host
script and the console's settings) and the four built-in exports' node structures 11,424. The
fixed app, built with them for comparison: 161,976 bytes of flash and 28,884 of static RAM
(Debug).

The console uses the heap (its strings, the command line's words, the codec programs of
`module N BOARD`, the output waiting for the UART); the stack doesn't. **Heap in use: 33,032
bytes**, read on the bench with the console's `heap` command
(`Rp2040HeapInUse()`, newlib's `mallinfo().uordblks`) after the AB0310 run with four sub-nodes
found and audio routed. That leaves about 173 KB of the 206 KB heap. It is not broken down
by user; the fixed app, which keeps no console strings or board lists, used 1,460 bytes.

Stack: the Pico SDK's main stack is 2 KB (`.stack_dummy`), with the rest of the 4 KB
SCRATCH_Y bank and SCRATCH_X (unused with one core) below it before anything else. The
deepest paths, from `-fstack-usage` at `-Og`, are about 1.5 KB: a command, `start` importing
the board (`main` 232, the host's `Run` 224, `Start` 104, `ImportBoard` 744,
`ImportSsPlus` 176, `AddUnit` 104, ...), and a poll whose event is printed (`Network::Poll`
432 as above, `OnEvent` 104, `ReportNode` 264, the SDK's `vsnprintf` 112 and number
formatting about 220), each plus `malloc`'s and the I2C driver's.

## How it is measured

`a2bcpp/tools/ram_report.py` compiles the stack for the Cortex-M0+ with the Pico SDK's Arm GNU
toolchain, at `-Og` (the RP2040 Debug build) and `-O3` (its Release build). It runs nothing on
the target. It reports:

- **Static RAM:** `.data` plus `.bss` of each object file.
- **Object sizes:** each class that holds per-bus state is listed in
  `a2bcpp/tools/ram_probe.cpp`. The report reads its `sizeof` back with `nm`.
- **Stack:** each function's worst path, from GCC's call graph (`-fcallgraph-info=su`). The call
  graph doesn't follow calls through a virtual function (the `Platform` methods, marked
  "+ Platform") or into the C library ("+ memcpy"). Add the platform's I2C driver depth to those
  rows. The stack's log lines are built without the C library's printf, whose stack the report
  can't see.
- **Paths through a virtual call:** the deepest path from `Network::Poll` (and from
  `a2b_PollTwoBuses`, below) to each place that calls the module-info source or the listener, plus
  the callee's worst path: the EEPROM source for the module-info source (`GetModuleInfo` 264 / 192,
  `ReadModuleInfo` 240 / 152), and `a2b_HandlerWritesOtherBus` in `ram_probe.cpp` (a handler that
  writes to a peripheral on another bus) for `OnEvent()`.

From the repository root (the toolchain is found on `PATH` or in `~/.pico-sdk/toolchain/*/bin`):

```
python a2bcpp/tools/ram_report.py
```

## Two buses

A `Network` per bus holds all that bus's state in fixed storage ([design
rules](decisions.md#design-rules)), so the RAM for two buses is twice the per-bus figure. The
stack high-water mark depends on how the app polls them:

- **One thread polls both buses in turn** (the RP2040 app): `ram_probe.cpp`'s `a2b_PollTwoBuses`
  does that, and the report gives its worst path, with the paths through the module-info source
  and the listener. The two `Poll()` calls never nest; a handler on one bus may call the other
  bus's functions, but not its `Poll()`.
- **One thread per bus:** each thread's stack needs the single-bus depth (`Network::Poll`).

## ADI's stack, for comparison

| What | Bytes | Source |
|---|---:|---|
| `gApp_Info` (one chain) | 107,744 | ADI's stack with a RP2040 console app |
| `.bss` + `.data`, whole RP2040 app | about 163 KB | ADI's stack with a RP2040 console app |
| `.bss`, whole RP2040 app (Debug build) | 150,800 | `arm-none-eabi-size` on the app |

Two buses of the C++ stack (30,736 B) are under a third of ADI's `gApp_Info` for one chain, and the whole
example's `.bss` about a fifth of ADI's app.
