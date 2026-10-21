# Bench console

A *bench* here is a lab test set-up: A2B boards, modules and cables on a desk. The bench
console, `a2b_bench` (`a2bcpp/examples/bench/`), is the interactive program for trying one.
It drives one A2B bus with the C++ stack from typed commands: from Windows through the ADI USBi, from
Linux through the host's I2C (the Pi), or on an RP2040 with no PC beside the bus, through
the Pico's own I2C (without the file commands; see "On the RP2040" below). The
[minimal example](quick_start.md) shows the least an app needs; the console is for testing.

It can do the following:
- pick a built-in board, or load a SigmaStudio+ export at run time;
- start and stop the bus, and show its status;
- set a link profile and a cable length per position;
- ignore the EEPROMs, give a position a Clockworks board's module info, or give it an image
  file;
- read, write, verify and erase sub-nodes' module-info EEPROMs;
- trace the I2C to a file;
- read and write any node's registers and peripherals;
- pause and resume discovery, and reject a node;
- connect and route channel groups, and mute.

Not included: GPIO over distance, and the other features in the
[roadmap](../../ROADMAP.md)'s list.

## Build and run

### Windows (USBi)

Once: MSYS2 UCRT64 with the tools and libusb, and the WinUSB driver for the USBi (Zadig); see
[setup_windows.md](setup_windows.md) for the steps:

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-libusb
```

Then, in PowerShell from the repository root, build the tests and the console and run the
tests (add `-Msys2Root D:\msys64` if MSYS2 is not in `C:\msys64`):

```powershell
a2bcpp\tools\run_tests.ps1
build\a2bcpp-windows\examples\bench\a2b_bench.exe
```

The build copies `libusb-1.0.dll` next to `a2b_bench.exe`, the only DLL it needs besides
Windows' own, so it also runs outside MSYS2. CMake reports
`libusb ...: the USBi platform's libusb link is built`; without libusb or pkgconf the
tests still build, but the console doesn't. Close SigmaStudio, or any other program using
the USBi, first. `a2b_bench` opens the first USBi it finds and says why if it can't:
not found (not plugged in, or not the WinUSB driver), or can't be opened (in use).

From VS Code, the A2B menu's "Windows bench (USBi)" section runs the build, the program
(Release) and gdb (Debug); the build is `a2bcpp\tools\build_windows.ps1 Debug|Release`, into
`build\a2b-bench-windows\<type>\examples\bench`, without the tests. The program starts the
board set in `a2b.windowsBoard` (default `AB0020`) and its `start` command.

### Linux (the Pi)

Built with the minimal example (the [quick start](quick_start.md), "Raspberry Pi"); the
program is `build/a2bcpp-pi/examples/bench/a2b_bench`. It opens `/dev/i2c-1` unless given
`--i2c DEVICE`. Stop any other program using the same I2C bus first.

From VS Code, the A2B menu's "Raspberry Pi bench" section cross-builds it in Debian WSL
(`a2bcpp/tools/build_pi.sh Debug|Release`, into `build/a2b-bench-pi/<type>`), copies it to
the Pi as `~/a2b_bench`, and runs it (`--board <a2b.piBoard> start`, default `ref`) or
debugs it through `gdbserver`; the Pi's address and user are the User settings `a2b.piHost`
and `a2b.piUser` ([setup_pi.md](setup_pi.md)). With libusb
(`libusb-1.0-0-dev`) the Linux build also has `--usbi`; a USBi on a Linux host is untried
(it may need its firmware loaded and a udev rule).

### On the RP2040

`a2bcpp/examples/bench/rp2040` builds the console for a Raspberry Pi Pico as `a2b_bench.elf`
(and `.uf2`), a Pico SDK project beside the fixed app's ([README](../../README.md#building-for-rp2040)).
From VS Code, the A2B menu's "RP2040 bench" section builds it, loads it through the debug
probe and runs it (Release) or debugs it (Debug).

- **Wiring:** the AB0310 board's, as the fixed app's: i2c0 on GPIO16 (SDA) and GPIO17 (SCL) at
  400 kHz, the main node's RESETn on GP2.
- **Console:** uart0 on GP0/GP1 at 230400 baud, the Debug Probe's UART port. Type at the
  `A2B>` prompt; backspace edits the line, and CR, LF or CR LF ends it.
- **At start:** the build stamp (`a2b_bench built ...`), the wiring, then `board AB0310` as if
  typed. Nothing else runs: `start` starts the bus. There are no command-line options; other
  start-up commands would be compiled in (`kStartup` in `examples/bench/main_rp2040.cpp`).
- **Not available**, as there is no file system: the `eeprom` commands, `i2cTrace`,
  `board FILE`, `module N file FILE` and `module N BOARD FILE`. Typed, each prints one line
  (`eeprom: not available on the RP2040 (no file system)`), and `help` doesn't list them.
  Everything else is the same as on the Pi and Windows. The console is built with
  `A2B_BENCH_FILES` 0; the host build compiles that variant too, so a change that breaks it
  shows in `run_tests.sh`.
- **`quit`** stops the bus (nothing more is sent) and the console keeps running: `start`
  starts it again.
- **`heap`**, the RP2040 console's own command (any case; `help` lists it last), prints the
  heap in use ([ram.md](ram.md#the-bench-console-on-the-rp2040)).
- **Typing ahead:** every character waiting is read on each pass, so a pasted block of
  commands isn't cut short by a long poll; ESC sequences (arrow keys, Home, End) are ignored.

One core does everything. A command runs between two polls (the stack is polled by one
thread), the bus is polled every millisecond by absolute time, and the output waits in a
buffer that goes to the UART as it has room, so a long listing (`help` is about 5 KB, some
200 ms at 230400 baud) never holds up a poll. The stack's own log lines go through the same
buffer. Nothing is lost while a command runs: there is no IRQ line, and the chips keep their
interrupts until the stack reads them.

### Command line

```
a2b_bench [--usbi | --i2c DEVICE] [--board NAME|FILE] [--ignore-eeprom] [--trace FILE] [--commands=LIST] [COMMAND]...
```

| Option | Same as |
|---|---|
| `--usbi` | The USBi (the default on Windows) |
| `--i2c DEVICE` | The host's I2C, e.g. `/dev/i2c-1` (the default on Linux) |
| `--board X` | `board X` |
| `--ignore-eeprom` | `ignoreEEPROM 1` |
| `--trace FILE` | `i2cTrace on FILE` |
| `--commands=LIST` | Each command in LIST, separated by `;` (spaces around them ignored; none if LIST is empty), e.g. `"--commands=ignoreEEPROM 1; link 2 custom"`. One argument for several commands, as VS Code passes the `a2b.windowsBenchArgs` setting |
| `COMMAND` | Any command; quote it if it has spaces, e.g. `"link 5 utp"` |

The options and commands run in order, then the console reads commands at the `A2B>`
prompt. The bus is polled every millisecond whatever is typed. For example:

```powershell
a2b_bench.exe --board ref --trace boot.txt start
```

After its build stamp the console prints its command line as it got it (`command line:
...`), and each option's command as it runs it (`A2B> board "ref"`). An option that takes
a value (`--i2c`, `--board`, `--trace`) refuses a missing one or another option in its
place: `--board --ignore-eeprom AB0020` stops with `--board needs a built-in board or an
export's C file, not --ignore-eeprom` instead of loading a file of that name. On Windows,
when the console has a window of its own that closes with it (VS Code's external console
for "Windows bench: Debug"), an error at start-up waits for Enter so the message can be
read. In VS Code, the A2B menu also writes each debug session's command line to the Debug
Console and the **A2B** output channel (`tools/vscode-a2b-menu/README.md`).

## Commands

Command names ignore case. `help` lists them.

| Command | What it does |
|---|---|
| `board` | Lists the built-in boards, and the one picked |
| `board NAME [CHAIN]` | Picks a built-in board; `AB0020` takes the export for the main node's chip at `start` (below) |
| `board FILE [CHAIN]` | Loads a SigmaStudio+ export's C file (below). Quote a path with spaces. CHAIN picks the main node in an export of several (default 0) |
| `start` | Imports the board into a fresh `BoardConfig` and starts the bus; a bus already started is started again |
| `stop` | Stops the bus: nothing more is sent (the chips keep running) |
| `status` | The board; the bus state (and its error), main node, sub-nodes with their chip and version, chip ID, module, power configuration, link and channel groups; the last rejection; the links and cable lengths set; the boards set at positions (`module N BOARD`); where module info comes from; the interrupts shown; the trace |
| `link` | Lists the links set with `link N` |
| `link N utp` | The link into sub-node N (0-15) is UTP: the node before it powers it over the A2B pair, B side straight |
| `link N rj45` | An RJ45 link: powered over the pair, B side inverted (XCVRBINV on the node before it), two-step from an AD2437: the node is found at 5 V (SWBYP on the node before it), its module info read, then the 24 V switch closed, the node found again and its VBUS checked (at least 4 V, else it is rejected: "no 24 V on its link"). The power-good of the board before it is checked first (a sub-node's GPIO7). From any other chip, single-step |
| `link N rj45 single` | The RJ45 link single-step, without the power-good and 24 V checks, as an export's inverted link |
| `link N xlr` | An XLR link: as `rj45`, B side straight |
| `link N utp two-step` | A UTP link run two-step, without the power-good and 24 V checks |
| `link N utp vmtr` | A UTP link, single-step, with the 24 V check (an AD243x node's VMTR) |
| `link N custom [inverted]` | The node has its own supply: the switch before it stays open, and SWBYP on that node turns its B side on without it. Needed for a node that draws no bias current (a galvanically isolated A side), which a closed switch never reaches; `inverted` for an RJ45 cable |
| `link N default`, `link all default` | Back to the link the node before it gives: its image's B port ([provisioning](provisioning.md)); into sub-node 0, the board's |
| `link all board` | Every link not set with `link N` is the board's, whatever the images' B ports say (on an RJ45 bench, every link `rj45 single`). Links set with `link N` stay. Kept until `link all default`, which clears those too |
| `cable` | Lists the cable lengths set with `cable N` |
| `cable N METRES` | The cable into sub-node N (0-15) is METRES long (from 0.1, one decimal, e.g. `12.5`), for the response cycles: the next probe's DISCVRY and, on a routed bus, a new plan at once if the length in use changes. Without it, the node before it's image gives the length (its B-side cable, in metres), else the board's 4 m, which the TRMs call adequate when the lengths are unknown. Give the real length, before `start`: a longer one can stop discovery ([hardware notes](hardware_notes.md#routing-and-audio)) |
| `cable N default`, `cable all default` | Back to the image's length, else the board's 4 m |
| `ignoreEEPROM 1` / `0` | Sub-nodes get the console's module info / read their EEPROMs |
| `module N BOARD [FILE]` | Sub-node N (0-15, or `all`) is a Clockworks BOARD (`AB0020`, `AB0312`): it gets the board's module info and codec program, if it has a codec (the board's built-in one, or FILE, SigmaStudio's XML), whether the EEPROMs are read or not. For nodes identified from then on (below) |
| `module N file FILE` | Sub-node N (0-15, or `all`) gets its module info from FILE, an image (`.dat`) as the image tool builds it or `eeprom read` reads it, whether the EEPROMs are read or not. Checked as `eeprom write` checks it. For nodes identified from then on: try an image before writing it (below) |
| `module N default`, `module all default` | Back to the EEPROM's or `ignoreEEPROM`'s module info |
| `module` | Lists the boards and image files set at sub-node positions, or the boards there are |
| `eeprom` | The EEPROM settings, and the job running |
| `eeprom config [addr A\|found] [size BYTES] [page BYTES] [cycle MS] [chunk BYTES]` | The sub-nodes' EEPROM. Defaults: the Clockworks boards' 24FC256 (32768 bytes, 64-byte pages), 6 ms after each write, 256 bytes per read. The address is the one the node's image was found at, else 0x50; `addr A` sets one, `addr found` goes back |
| `eeprom read N FILE [LEN\|all]` | Reads sub-node N's EEPROM into FILE: to the end of its image (the whole EEPROM if there is no image), LEN bytes, or all of it |
| `eeprom write N FILE` | Writes the image FILE to sub-node N's EEPROM: checked first, then only the pages that differ, read back, and the marker 0xAA last (below) |
| `eeprom verify N FILE` | Compares sub-node N's EEPROM with FILE |
| `eeprom info N` | Reads sub-node N's image and says what it holds, Clockworks' vendor section included |
| `eeprom erase N` | Clears sub-node N's marker (0xFF at address 0): the node then has no module info, as with a blank EEPROM |
| `eeprom abort` | Stops the job, at once, even while it runs; a write stopped leaves the node blank (below) |
| `i2cTrace on FILE` | Writes every I2C transfer to FILE (overwritten) |
| `i2cTrace off`, `i2cTrace status` | Stops and closes the trace / shows it |
| `interrupts quiet`, `interrupts all` | Hides (the default) or shows the last sub-node's open-line interrupts, types 12 and 13 (PWRERR: cable disconnected, open circuit or wrong port; AD242x TRM, Table 5-1). Each probe behind the last node closes its switch, and with nothing plugged in there an AD242x reports its open B side; `status` counts them. The first one from a last node prints a hint, once until the chain changes: nothing is plugged in after it, or the node there draws no bias current (its own supply behind an isolated A side, as an AB0020 with an AD2437 behind an AD242x), which needs `link N custom`. Every other interrupt is printed, with its type's name ("type 5 (SRF missed)") |
| `errors` | The error watch, on from the start (`a2b::Network::SetErrorWatch()`). Each node's error registers, read now: BECNT and the sum the stack keeps (read before each of its once-a-second clears; an upper bound, as a header-count error mostly counts a CRC error too), the latched INTPND0 bits by name (SRF-CRC, SRF-missed, BECOVF, PWRERR, parity, CRC, decoding, header-count; the main node's INTPND2 I2CERR and ICRCERR too), SWSTAT (FIN, a cable fault by name) and NODE (LAST: the node lost its downstream) |
| `errors on`, `errors off` | Turns the watch on or off. On: every node counts bit errors (BECCTL 0x7F: every type, BECOVF past 16 a second; counting header-count errors double-counts most line errors, which the TRMs advise against and the watch accepts so nothing is missed), raises the SRF-missed and BECOVF interrupts besides its own, and when a probe fails or a node fails the chain check the console prints an error report before the drop (below). Turning it on clears what the nodes had latched. Off: BECCTL and INTMSK0 as they were, and the stack sends nothing for it |
| `errors clear` | Zeroes the sums and clears every node's latched INTPND0 bits and BECNT |
| `errors report` | Prints the last error report again |
| `rdm REG [COUNT]` | Reads COUNT (1-256, default 1) main-node registers from REG, 16 to a line. REG 0x100-0x1FF is page 1, on the AD2433 and AD2437 only |
| `rds N REG [COUNT]` | The same for sub-node N |
| `wrm REG VALUE...` | Writes main-node registers from REG, one VALUE (0x00-0xFF) each |
| `wrs N REG VALUE...` | The same for sub-node N. See "Registers" below for the registers the stack owns |
| `periRead NODE ADDR COUNT [BYTE...]` | Reads COUNT bytes (1-256) from the I2C peripheral at 7-bit ADDR of NODE (`main` or `m`, or a sub-node 0-15). With BYTEs, writes them first and reads after a repeated start: a sub-address, then its data |
| `periWrite NODE ADDR BYTE...` | Writes the bytes to the peripheral, in one transfer |
| `addCon S:G D:G` | Connects RX group G of node S to TX group G of node D (S, D: `main`, `m` or `-1`, or a sub-node 0-15 in the bus; G: a group ID). Kept by chip ID; routed at `applyRouting` (as ADI's `addCon`) |
| `removeCon S:G D:G` | Removes that connection |
| `clearAllCon` | Removes every connection (as ADI's `clearAllCon`). What is routed stays until `applyRouting` |
| `listCon` | Lists the connections: each end by chip ID and where its node is now, and what the pair carries: the channel count (`2 channels (stereo), 0-1 -> 0-1`), and any source channels not sent or sink channels not played |
| `setRoute [append] S:G D:G...` | One audio stream (ADI's `addStream`, which called `a2b_pnp_SetAudioRt`): RX group G of node S to TX group G of each node D, all or nothing; replaces every connection unless `append`. Routed at `applyRouting` |
| `importCon S:G D:G [S:G D:G...]` | Replaces every connection with these pairs, all or nothing (with no pairs, an empty list: every connection goes). A node is `main` (or `m`), or a chip ID of 12 hex digits (`listCon` shows them), so modules that aren't in the bus yet can be named |
| `applyRouting` | Routes the connections, now and whenever the chain changes (as ADI's `applyRouting`); refused with the reason if they can't be routed |
| `mute [on\|off]` | Mutes or unmutes the whole bus (ADI's API `a2b_pnp_MuteNetwork`, which has no console command: DATCTL's slot enables and NEWSTRCT); without an argument, says which. Written when the bus is between steps (a probe under way delays it); it holds through plans and restarts |
| `muteMode [local\|global\|none]` | What the stack mutes while it writes a plan: the nodes that would play a wrong channel meanwhile (local, the default; AD243x nodes only, and the whole bus for a plan where an AD242x would), the whole bus (global; the AD242x too), or nothing. The setting lasts across `start`. `audio mute: ...` and `audio unmute` are printed around each plan |
| `bandwidth` | The busiest sub-node of the plan built last and the share of the superframe's 1024 bits it takes (ADI's API `a2b_pnp_GetNwBW`, which has no console command); `routing` shows it too |
| `routing` | The plan written last: each node's slots, masks, offsets, RXMASK, TXXBAR (marked not written on an AD242x, which has no crossbar) and RESPCYCS, and the response-cycle window |
| `groups` | The channel groups: the main node's, the console's module info's, and each sub-node's |
| `group main\|image rx\|tx ID CHANNELS` | Sets a channel group of the main node (from the next `start`) or of the console's module info (for nodes identified from then on). CHANNELS: `0-1`, `0,2,4-7`, or `none` to remove the group |
| `pause` | No new probes for sub-nodes; the nodes in the bus stay, and the chain is still checked for drops (as ADI's `pause`) |
| `resume` | Probes again at the end of the chain (as ADI's `resume`) |
| `reject N` | Takes sub-node N and every node after it out of the bus (their switch opened). The same node found there again is switched off again; another module is reviewed as usual. `status` shows "Last rejection: by the app" |
| `quit` | Stops the bus and leaves (on the RP2040 the console keeps running). Taken at once, even while an EEPROM job runs |

The names follow ADI's console where it has the command (`start`, `ignoreEEPROM`,
`i2cTrace`, `rdm`, `rds`, `wrm`, `wrs`, `addCon`, `clearAllCon`, `applyRouting`, `pause`,
`resume`, `quit`). Numbers are decimal,
or hexadecimal with `0x` (a leading `0` makes one octal, as in ADI's console). Every event prints as it happens: the bus's states, each sub-node
found (with its chip and module), rejected (with the reason) or removed, and the
interrupts the stack passes on. The stack's own log lines (`info: a2b: ...`) go to
stderr, also on the console.

### Boards

Three boards are built in, the repository's exports:

| Name | Export | Main node |
|---|---|---|
| `ref` | `adi/exports/rp2040/12_a2b_busconfig.c` (the Pi and RP2040 apps) | AD2437 at 0x68, links `rj45 single` (the export sets XCVRBINV), no host peripherals |
| `AB0020` | `tools/SS+reference/2x_AB0020_EVM_with_2437/adi_a2b_busconfig.c` (the AB0020 bench project, SigmaStudio+ 2.3.0), or for an AD2428 module `tools/SS+reference/2x_AB0020_EVM_with_2428/adi_a2b_busconfig.c` | AD2437 or AD2428 at 0x68, I2S TDM2 32-bit, links `utp`; the ADAU1761 codec at 0x39, programmed at `start` before the main node (it gives the main node its SYNC and BCLK) |
| `AB0310` | `tools/SS+reference/AB0310_AB0312_EVM_with_2437/adi_a2b_busconfig.c` (the AB0310 and AB0312 project, SigmaStudio+ 2.3.0) | AD2437 at 0x68, I2S TDM2 32-bit as the AB0020's, links `rj45 single` (the export sets XCVRBINV); SYNC (48 kHz) and BCLK from the board's own clock, so nothing is programmed on the host bus (the export's peripherals have no configuration) |

The AB0020 is UTP on both ports, so every `AB0020` link is `utp`. It takes an AD2437 or an
AD2428 module as its main node, with an export for each: at `start`, `AB0020` reads the
main node's VENDOR and PRODUCT (0x02, 0x03, at 0x68) and imports the export for that chip
(`Main node AD2428 found: board AB0020's export for it`). A chip it has no export for (an
AD2433) is refused before anything else is sent; load that module's export with `board
FILE`. If the main node doesn't answer, the AD2437 export is used and the stack reports at
`start`. A board from a file, or `ref` and `AB0310`, are used as they are: the stack checks
the chip, and the console then says which one it found (`the main node is an AD2428, the
board's export is for an AD2437`).

**The reference folder.** `tools/SS+reference` holds the SigmaStudio+ projects whose
exports the console compiles in, one folder per board and transceiver (an AB0020 with an
AD2433 module needs its own export), and, in `tools/SS+reference/adi`, the one copy of
ADI's generic headers they and the import are all compiled with (ADI's header merged for
SigmaStudio+ 2.3.0; [main-node configuration](main_node_config.md), "Building"). A project
folder holds only what SS+ and SigmaStudio write for it: a copy of an ADI header there would
be the one its export includes, so the build stops if it finds one.

Any other board is loaded from its export's C file (`adi_a2b_busconfig.c` as SigmaStudio+
writes it) at run time, with no rebuild: `board C:\exports\ad2428_main\adi_a2b_busconfig.c`.
The loader (`SsPlusFile`, `a2bcpp/import/ss_plus/ss_plus_file.h`) reads the C the
exports are written in: designated and positional initializers, the export's arrays and
pointers, `#if`/`#ifdef` blocks, ADI's constants. It fills the same structures the C
compiler would (a test checks the repository's exports byte for byte against their
compiled forms), and `start` imports them with `ImportSsPlus()` as for a built-in board
([main-node configuration](main_node_config.md)).

- A field the reference header doesn't have (an export from a SigmaStudio+ newer than
  2.3.0) is skipped with a warning, which `board` prints with its line. The first ten are
  printed, then the count.
- A constant the reference header doesn't have (an export from a SigmaStudio+ newer than
  2.3.0) is a warning, taken as 0, in what the import doesn't read: the
  sub-node descriptions and the streams. Where the import reads (the bus, the chain, the
  main node, the target's peripherals) it refuses the file, as do a pointer of the wrong
  type and a syntax error, with the line.
- Only the main node's settings and the target's peripherals are used: sub-nodes are
  configured from their EEPROMs (or, with `ignoreEEPROM 1`, the console's image).
- The main node's chip comes from the export's `sAuthSettings.nProductID` (0x28, 0x33 or
  0x37); `start` reports `the main node is not the board's chip` if the chip on the bench
  is another, and names the chip it found. The board's link is `rj45 single` if the export's main-node CONTROL sets
  XCVRBINV, `utp` if it doesn't. It is the main node's link, into sub-node 0. The link after
  each sub-node is the one its image's B port gives (UTP for the console's own image);
  `link N ...` changes it per link, and `link all board` makes every link the board's.
- Each host peripheral is programmed before the main node or once it is configured, as
  the export marks it (SigmaStudio+'s choice per peripheral, `bPostDiscCfg`); `start`
  says how many operations run before the main node.

### Module info with the EEPROMs ignored

With `ignoreEEPROM 1` every sub-node gets the minimal example's image (module 00AD:0037, a
UTP B port,
GPIO0-7 inputs, PINCFG 0, I2S on DRX0 and DTX0 as the Clockworks boards have it, I2SGCFG
0xE0 and I2SCFG 0x11, no peripheral scripts, the console's channel groups) with the
power configuration of its chip: CFG-4 on an AD2437, CFG-0 on an AD2433; AD242x have no power
configuration. (A node read from its EEPROM gets its chip's too, whatever the image says.) An
AD2427 gets the AB0204's image instead
(four PDM microphones, below): it has no I2S port for the generic image's groups.

### Clockworks boards as sub-nodes

`module N BOARD` gives sub-node N a Clockworks board's module info, in place of its
EEPROM's or `ignoreEEPROM`'s: for a board whose EEPROM is blank. Once the EEPROM is written
(next section), the board needs none of this. The boards are profiles in
`examples/clockworks/clockworks_boards.cpp`, which is also the worked example of supporting
boards without EEPROMs in an app. Built in:

- **`AB0020`**: the AB0020, with an AD2437, AD2433 or AD2428 module. Its transceiver's
  settings are the bench SigmaStudio+ project's sub-node
  (`tools/SS+reference/2x_AB0020_EVM_with_2437`), and `test_clockworks_boards` checks them
  against that export:
  - I2SGCFG 0xE0: TDM2 (I2S) 32-bit, SYNC inverted, early and 50% duty.
  - I2SCFG 0x11: DRX0 and DTX0, both on BCLK's rising edge.
  - GPIODAT and GPIOOEN 0x80: IO7 driven high, the board's LED.
  - PINCFG 0x01.
  - CLK2CFG 0x81: CLKOUT2 on the ADR2 pin, the codec's 12.288 MHz MCLK.
  - SPICFG 0x02: SPI off. SPI powers on enabled, and with it on CLKOUT2 never reaches
    ADR2 (AD2437 TRM, note to Table 5-28).
  - Channel groups: RX 0 and TX 0 on channels 0-1, the codec's stereo pair.
  - Codec program: the ADAU1761's, `AB0020-sub-node-ADAU1761.xml`, built in. The stack
    writes it as the node's peripheral script at every set-up, as it would from an
    EEPROM, so the codec is programmed again whenever the node is found (a re-plug, a
    restart). The main node's program is another one (its serial port is the master); it
    comes from the board's export.

- **`AB0312`**: the AB0312, the AB0310's sub-node board, with an AD2437, AD2433 or
  AD2428 module ([boards](boards.md)). No codec: its I2S is on a header. Its settings are
  its project's sub-node (`tools/SS+reference/AB0310_AB0312_EVM_with_2437`), the same
  values as the AB0020's (I2SGCFG 0xE0, I2SCFG 0x11, PINCFG 0x01, CLK2CFG 0x81, SPICFG
  0x02), with RX 0 and TX 0 on channels 0-1, but IO7 an input (GPIOIEN 0x80): the board's
  power-good. `test_clockworks_boards` checks them against the export. The
  project's RJ45 links come from the main board (`board AB0310`), not from the image.

Every sub-node the console makes is a `clockworks::BoardNode`: IO7 is made an input on
every node whose module info doesn't name the AB0020, whatever its image says.

- **`AB0204`**: the AB0204 microphone board, an AD2427 with four MEMS PDM microphones,
  two on DRX0 and two on DRX1 (the AD2427 has no I2S/TDM port, only PDM; AD2426/27/28
  data sheet, Table 1). Its settings:
  - PDMCTL 0x1F: both PDM pins on, two microphones each, and the high-pass filter (1 Hz).
  - PDMCTL2 0: the microphones' clock on the BCLK pin (not PDMCLK/IO7), the data to the
    bus only, rising-edge data first.
  - I2SGCFG left at its power-on TDM2 32-bit: with a PDM pin on, BCLK runs at 64 x SYNC,
    3.072 MHz at 48 kHz (AD242x TRM, "PDM Clocking Options").
  - Channel groups: RX 0 on channels 0-3 (all four), RX 1 on 0-1 (DRX0's pair: its
    rising- and falling-edge microphones) and RX 2 on 2-3 (DRX1's pair). No TX group.

  **An AD2427 with no EEPROM image is taken to be an AB0204**: if no
  EEPROM answers with an image (none there, or a blank one), or the EEPROMs are ignored,
  the node gets this image, and the console says so after its groups:
  `AB0204 (no EEPROM: the AD2427's default)`. An AD2427 whose EEPROM has an image is set
  up from it. An EEPROM that answers but can't be read still has the node rejected, and
  other chips with no EEPROM are rejected.

`module N AB0020 FILE` runs FILE, SigmaStudio's XML export of another codec program, in
place of the built-in one. The file is read when the command is given, and refused with
the line that is wrong. Both of SigmaStudio's XML layouts are read: with `i2caddr` and
`addr_width` on each write, or without them (the address is then the board's codec's). A
`read` action is skipped, since a peripheral script only writes and waits. Each write may
have up to 4094 bytes, and a wait up to 4095 ms.

A board at a position whose transceiver it isn't made with (an AD2427 for an `AB0020`)
isn't given: that node reads its EEPROM, and the console says so. For the bench's first
sub-node, with RJ45 links after sub-node 1:

```
a2b_bench.exe --board AB0020 --ignore-eeprom "link 2 rj45 single" "link 3 rj45 single" "link 4 rj45 single" "module 0 AB0020" start
```

An AB0310 main node with an AB0312 at sub-node 0 and AD2437 modules after it, every link
RJ45, single-step (the board's link, everywhere). Without `link all board`, the link after
the AB0312 is two-step (its image's RJ45 B port, with its AD2437), and the console's image
after it gives UTP:

```
a2b_bench.exe --board AB0310 --ignore-eeprom "link all board" "module 0 AB0312" start
```

The same bench with every link two-step (the link after the AB0312 from its image):

```
a2b_bench.exe --board AB0310 --ignore-eeprom "link 0 rj45" "link 2 rj45" "link 3 rj45" "module 0 AB0312" start
```

The same set-up runs on the RP2040 with nothing typed, its connections included: the
fixed app ([quick start](quick_start.md), RP2040).

### EEPROMs

A sub-node's module info normally comes from its EEPROM: a 24xx part with 16-bit addresses
on the node's I2C port, at 0x50-0x54. The stack reads it when it finds the node.

The image tool, `tools/a2b_eeprom` (its [README](../../tools/a2b_eeprom/README.md)), builds the
image (`.dat`) from a module's settings and its peripherals' SigmaStudio programs. The
`eeprom` commands move images to and from the EEPROMs. The format and the write procedure
are in [provisioning.md](provisioning.md).

**Provisioning blank boards.** On the Clockworks boards the EEPROM is on the base board
(AB0020, AB0310, AB0312); the plug-in A2B module has none. A node with a blank EEPROM has no
module info, so give every node the console's (`ignoreEEPROM 1`) and the right links
(`link N ...`, or `link all board`), write each EEPROM, then start the bus again from the
EEPROMs:

```
a2b_bench.exe --board AB0020 --ignore-eeprom start
A2B> eeprom read 0 node0_before.dat
A2B> eeprom write 0 tools/a2b_eeprom/examples/AB0020.dat
A2B> eeprom write 1 my_module.dat
A2B> ignoreEEPROM 0
A2B> start
```

Read every EEPROM to a file before the first write; `eeprom write` puts one back.

`eeprom write` refuses an image that names another transceiver than the node's (its target
chip, header 0x07): the stack would reject the node with it. `module N file` notes it.

**Trying an image first.** `module N file FILE` sets the node up from the file, without
writing anything. Use it to check an image's settings and codec program on the bench before
it goes into the EEPROM.

**What `eeprom write` does:** it checks the file (nothing is sent if it fails), reads the
EEPROM over the image's length, clears the marker if the image differs, writes each page that
differs, reads everything back, and writes the marker last. A write stopped after the marker
is cleared leaves the node blank, never a half image, and the console says so.
[provisioning.md](provisioning.md#6-writing-an-eeprom) has the steps. An EEPROM that already
holds the image is left as it is. The AB0020's 8.4 KB image takes about 1.5 s to write at a
sub-node's 100 kHz, and as long again to check.

**While a job runs.** The bus is polled between its transfers. `start`, `stop`, `board`,
`reject` and the other `eeprom` commands are refused until it ends; any other line typed
meanwhile waits for it, and the prompt comes back when it ends. `eeprom abort` and `quit` run
at once.

**After a write.** The stack reads a node's module info only when it finds the node: start
the bus again (with `ignoreEEPROM 0`) for the new image to take effect.

**Reads.** Reads go in 256-byte transfers, well inside the ADI USBi's 0.3 s for a transfer
([porting](porting.md)). `eeprom read` follows the image's blocks to find its end. A file
read with `all` (32 KB) can be written back as it is.

### Routing

Routing is the stack's ([cookbook](cookbook.md), "Routing"): AD2433 and AD2437 nodes through
their crossbars, AD242x nodes in TDM order. Each end of a
connection is a node and one of its channel groups:

- **The main node's** are the console's, `rx G0 0-1, tx G0 0-1` by default (the AB0020's
  ADAU1761: I2S, TDM channels 0-1 each way); an export has none. `group main ...` changes
  them for the next `start`.
- **A sub-node's** come from its module info: its EEPROM's group table (offset 0x100), or
  with `ignoreEEPROM 1` the console's, `rx G0 0-1, tx G0 0-1` by default (`group image
  ...`). An EEPROM without a table gives none, so with the EEPROMs read a module needs one
  for its node to be connected.

`addCon` takes nodes in the bus; the connection is kept by chip ID, so it follows the
module, waits while it is unplugged, and outlasts `start`. A sink group takes one source:
a second connection to it replaces the first. `applyRouting` builds a plan at once and
says why if it can't; the plan is written once the chain is up (`routing applied: ...`),
and again by itself whenever a node joins or leaves. The bench's first check, two AB0020
sub-nodes:

```
A2B> addCon m:0 0:0
Connected main node G0 -> sub-node 0 G0 (applyRouting routes it)
A2B> addCon 0:0 m:0
Connected sub-node 0 G0 -> main node G0 (applyRouting routes it)
A2B> applyRouting
Routing: written at the next poll, once the chain is up
routing applied: 2 connection(s) routed, main node RESPCYCS 0x78 (window 29-211)
```

The main node's I2S input (DRX0, channels 0-1) is then on sub-node 0's DTX0, and sub-node
0's DRX0 on the main node's DTX0. A sub-node needs its I2S port on and, to be heard, its
codec programmed: for an AB0020, `module N AB0020` (above) does both. The console's
plain module info (`ignoreEEPROM 1`) turns the port on (TDM2 on DRX0 and DTX0) but
programs no codec. A connection whose channels a node's port doesn't give is refused
("a connection uses a channel its node's I2S/TDM port doesn't give"). Routing it anyway would
leave the node with no SYNC.

**The AD2428 bench with microphones:** an AB0020 with an AD2428 as the main node (its
SigmaStudio+ export loaded from its file), an AB0020 with an AD2428 at sub-node 0, an
AB0204 at sub-node 1 (no EEPROM: the AB0204 default above):

```
A2B> board "AB0020_AD2428_main.c"
A2B> module 0 AB0020
A2B> start
  sub-node 1: AD2427 ...
    rx G0 0-3, rx G1 0-1, rx G2 2-3
    AB0204 (no EEPROM: the AD2427's default)
A2B> addCon m:0 0:0
A2B> addCon 1:1 m:0
A2B> applyRouting
```

The main node's stereo pair goes to sub-node 0's codec, and the AB0204's DRX0 pair to the
main AB0020's codec; `addCon 1:2 m:0` swaps in the DRX1 pair (a sink takes one source).
The AB0020's ports are I2S, two channels each way, so one pair at a time reaches a codec;
`addCon 1:0 m:0` (all four) pairs the lowest two. An AD2428 has no crossbar: the slots it
plays fill its TX channels from 0 in bus order, downstream before upstream, and upstream
the last sub-node's slots come first. So a microphone pair can go to sub-node 0's codec
only while that codec plays nothing from the main node (`removeCon m:0 0:0` first), and a
main node taking both the codec and the microphones must list the microphones' channels
first ([cookbook](cookbook.md), "Routing"). A scope on the AB0204's BCLK shows 3.072 MHz
once the node is set up.

**A mixed chain:** the same with the AB0020 at sub-node 0 on an AD2437
module, a crossbar node between the AD2428 main node and the AB0204:

```
A2B> board "AB0020_AD2428_main.c"
A2B> module 0 AB0020
A2B> start
A2B> addCon m:0 0:0
A2B> addCon 1:1 m:0
A2B> applyRouting
A2B> routing
```

The main node's pair is heard on sub-node 0's codec, and the microphones on the main
AB0020's codec, passed up through the AD2437. `routing` shows the AD2437's RXMASK and
TXXBAR and "no crossbar" for the main node and the AB0204. Turned round (`board AB0020`,
the AD2437 main node, with the AD2428 module on the AB0020 at sub-node 0), the same
commands route the pair to a TDM node under a crossbar main node, and the microphones
through the AD2428. Nothing is set for a mixed chain: each node is routed by its own chip
([cookbook](cookbook.md), "Routing", "Mixed chains"). Unplugging the AB0204 and plugging
it back in while routed re-routes each time (`routing applied`), and the AD2437 keeps
playing the main node's pair through both plans.

**Muting.** A plan is written under a mute so a node doesn't play the wrong
channels while the slots and crossbars change; `muteMode` picks it ([cookbook](cookbook.md),
"Routing"):

```
A2B> muteMode global
Routing mute: global (the whole bus) (from the next plan)
A2B> applyRouting
audio mute: the whole bus
audio unmute
routing applied: 2 connection(s) routed, main node RESPCYCS 0x78 (window 29-211)
```

`local` (the default) names the nodes it mutes instead (`audio mute: sub-node 0,
sub-node 1`): those whose crossbar changes, and those that play a slot whose channel the
new RXMASK at its source changes. An AD242x has no crossbar to mute that way; when one
would play a wrong channel (an AD242x playing an AD243x's slots in a mixed chain), the
whole bus is muted for that plan: `audio mute: the whole bus (local: a node that would play
a wrong channel can't be muted alone)`. `mute on` silences
the bus until `mute off`, whatever plans are written meanwhile. On the bench the check is a
scope or the ear on a sub-node's I2S while a second `applyRouting` changes a connection:
with `local`, only the nodes named drop out; with `global`, all do, for about a
millisecond a step.

Each link's cable length goes into the response cycles: 4 m unless `cable N` gives the
link its own (the stack doesn't measure cables). Give every link a length close to its
real one, short ones too, e.g. `cable 1 0.1` for modules on one board or `cable 2 15`:
short links left at 4 m dropped nodes now and then on the bench, though the TRMs call the
4 m calculation adequate ([hardware notes](hardware_notes.md#routing-and-audio)). `status`
lists them.

### Links

Each link takes, in order: the console's `link N`; then, with `link all board`, the board's;
then the B port of the node before it (its image's); and into sub-node 0, the board's (`rj45
single` if the export's main-node CONTROL sets XCVRBINV, else `utp`; every `AB0020` link is
`utp`).
The stack's order is in the [cookbook](cookbook.md#link-profiles). A link applies from the
next probe of that position, so it can be fixed while the bus runs: a node behind a link of
the wrong polarity is not found, and is found at the next probe once its link is right.

A two-step link (`rj45`, `xlr`, `utp two-step`, or an image's RJ45 or XLR B port on an
AD2437) turns the 24 V on only after the node is found at 5 V and kept by the review. The
log says when a board's power-good is low ("no power-good on the link to sub-node N"),
when a node isn't CFG-4 and stays at 5 V (not expected: only AD2437 nodes sit behind an
RJ45 link, [boards](boards.md)),
and when a link asked two-step runs single-step because the chip before it has no 24 V
switch. A node whose VBUS stays under 4 V is rejected ("no 24 V on its link") and probed
again every 2 s, from its first step. `status` names each link by its `link` form.

### Catching a drop

With the error watch on (the default), a node that fails the chain check prints, before
its `removed` lines:

```
  interrupt from sub-node 2: type 5 (SRF missed)
error report at 18235 ms: sub-node 3 failed the chain check (sub-node 3, 0x02 read, 3 tries, 1521 ms); no answer after that either
  main node: BECNT 0 (sum 0), INTPND0 0x00, INTPND2 0x00, SWSTAT 0x01 FIN
  sub-node 0: BECNT 0 (sum 0), INTPND0 0x00, SWSTAT 0x01 FIN, NODE 0x20
  ...
  sub-node 2: BECNT 0 (sum 3), INTPND0 0x40 SRF-missed, SWSTAT 0x01 FIN, NODE 0xA2 LAST
```

Read it from the node before the failing one: LAST with SRF-missed says the link or the node
behind it went away (the TRMs' drop); a cable fault in SWSTAT or PWRERR says power; neither,
with "it answered a read after that", says the node was dropped while it could still answer.
Bit-error sums that grow before a drop point at the link. The lines above are what the
console prints; the values are an example. The procedure is in
[testing](testing.md#catching-a-drop).

### Registers

`rdm`, `rds`, `wrm` and `wrs` use the stack's raw register access
(`Network::ReadRegisters()`/`WriteRegisters()`; [cookbook](cookbook.md), "Raw register
access"). They reach the main node at any time (at 0x68 before the first `start`, then at
the board's address) and a sub-node while it is in the bus. Each read or write is one
command's transfers, made at once between the stack's own. The stack selects the page,
so MMRPAGE can't be written. Registers the stack owns are written at your risk; the
console doesn't stop you:

- `wrs N 0x09 0` (SWCTL) opens sub-node N's switch: the nodes after it lose power and are
  removed as dropped at the next chain check, then probed and found again.
- `rdm 0x17` (INTTYPE) clears the interrupt it reports, which the stack then never
  sees. Read the interrupts from the events instead.
- `wrm 0x12 0x84` (a soft reset with MSTR kept) resets the main node's registers and
  loses the chain: run `start` again. CONTROL with MSTR clear starts the bus again by
  itself.
- A node's settings (its module info's, or the main node's export) last until the bus
  starts again or the node is found again; INTMSK0-2 until the stack writes them again
  (the main node's at `start`, a sub-node's before the first probe behind it).

```
A2B> rdm 0x02 3
main node 0x02: AD 37 21
A2B> rds 0 0x6A 6
sub-node 0 0x6A: 18 AC EE 04 23 16
A2B> wrs 0 0x4D 0x01
Wrote sub-node 0 0x4D: 01
```

### Peripherals

`periRead` and `periWrite` reach an I2C device on the host's bus beside the main node
(`main`) or on a sub-node's I2C port, as the stack's peripheral calls do. A 24C-series
EEPROM at 0x50 takes a two-byte address:

```
A2B> periRead 0 0x50 4 0x00 0x00
Peripheral 0x50 of sub-node 0:
  00: AA AD 00 37
```

### I2C trace

`i2cTrace on FILE` writes one `I2CT` line per transfer as it ends, the format of the
traces captured from ADI's stack and of `a2b_minimal --trace`, so `a2bcpp/tools/trace_decode.py` and
`trace_diff.py` read it ([testing guide](testing.md)). Times are microseconds from
`i2cTrace on`. Line ends are `\n` on every host.

## An example session

The AB0020 main board from Windows, with two sub-nodes and their EEPROMs ignored (the
sub-nodes' identities shortened to `...`):

```
> a2b_bench.exe --board AB0020 --trace ad2437.txt start --ignore-eeprom
ADI USBi open
A2B bench console: help lists the commands
A2B> board "AB0020"
Board AB0020, chain 0
A2B> i2cTrace on "ad2437.txt"
Tracing I2C to ad2437.txt
A2B> start
Main node AD2437 at 0x68 (sub-nodes 0x69), 40 host script operation(s) (40 before the main node), links utp by default
A2B> ignoreEEPROM 1
EEPROMs ignored: sub-nodes get the console's module info (CFG-4 on an AD2437, CFG-0 on an AD2433)
A2B> bus: starting the main node
info: a2b: main node running
bus: configuring the main node
info: a2b: main node configured
bus: bringing up the chain
info: a2b: sub-node 0 found and configured
  sub-node 0: AD2437 version ..., chip ID ..., module 00AD:0037, CFG-4, 0 peripheral block(s), link utp (board)
    rx G0 0-1, tx G0 0-1
    image for not stated; B port UTP, its cable not given
info: a2b: sub-node 1 found and configured
  sub-node 1: ..., link utp (sub-node 0's UTP port)
bus: running
  2 sub-node(s)
```

The codec's 40 operations ran first (no output unless one fails), then the AD2437 locked
on the codec's SYNC ("main node running"). If the USBi doesn't answer (every first
transfer NAKs, at the codec's 0x39 or the main node's 0x68), power-cycle the USBi and the
board; it has sometimes needed a few power cycles before the USBi worked again.

## Tests and tested configurations

`tests/example/test_example_bench.cpp` runs the console's commands against the simulator:
the built-in boards (the AB0020's links all UTP, its codec programmed before the main
node), an export loaded from a file and changed to an AD2428 main node with AD2428,
AD2427 and AD2437 sub-nodes (EEPROMs ignored), link profiles per position (an isolated
AD2437 found only with `link 1 custom`), the wrong main node, the host script, the
trace, register reads and writes (page 1 included), peripheral reads and writes on a
sub-node's EEPROM and the host bus, pause, resume and reject, the connection,
routing and group commands (the audio checked through the simulator's slot model), and
`module` (an AB0020's registers, groups and codec program write for write, again after a
re-plug; an AD2427 at a marked position reading its EEPROM). `test_sigma_xml` reads the
AB0020's two XML programs; `test_clockworks_boards` checks the board profiles against
their SigmaStudio+ exports. `test_eeprom_provisioning` (scenario 20) covers:
- blank EEPROMs written with the image tool's images, verified, read back and described,
  then the nodes found from them, the AB0020's codec programmed from its EEPROM;
- a write stopped half way (the node blank);
- a write-protected EEPROM, and a busy one (no write-cycle wait: its NAKs tried again);
- `module N file`, and the refusals.

`test_eeprom_provisioning` also checks that `eeprom abort` and `quit` are taken while a job
runs. The RP2040's variant is compiled on the host (`A2B_BENCH_FILES` 0), not run.

What has run on hardware, from Windows, the Pi and the RP2040, is in the
[roadmap](../../ROADMAP.md#tested-on-hardware), with what still needs a test.
