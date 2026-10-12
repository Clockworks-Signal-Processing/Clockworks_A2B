# Understanding main and sub-node programming

This page explains how the C++ stack gets the settings it writes to the main node and to
the sub-nodes, and why the two are done differently. It is a tutorial for someone who has
used SigmaStudio+ (SS+) and now wants to know what the stack does with its output.

## 1. The short version

| | Main node | Sub-node |
|---|---|---|
| What is described | the **board** | the **module** |
| Known when | the board is designed | only when the node is found on the bus |
| Authored in | an SS+ project | a board profile in code (`clockworks::MakeModuleImage()`: the bench console and the fixed app). An image builder that makes it from an SS+ project is not built yet |
| File the stack reads | the SS+ **export** (`adi_a2b_busconfig.c`) | a **module-info image** (a `.dat`, the layout of ADI's images in `adi/module_info/`) |
| Handed to the stack as | a `BoardConfig`, filled by `ImportSsPlus()` | a `ModuleInfoSource` |
| Where the bytes can come from | compiled in, or loaded from a file at run time | the module's EEPROM, or an image built at run time from a compiled-in board profile (`clockworks::BoardModuleSource`). Reading an image from a file, or from a compiled-in array, is not built yet |
| If it is wrong | `Start()` is refused | that one node is rejected; the bus keeps running |

The rest of this page walks through each row.

## 2. Why the two sides differ

The main node is part of the board. The app knows at build time which chip it is, at which
I2C address, and what the codec next to it needs. So the app configures it, from a file it
has in hand.

Sub-nodes are found one after another by **hot-plug discovery**, and the app doesn't know
which module will be at which position. Each module therefore describes itself: its
register settings, its power needs and the programs for the peripherals on it (a codec, for
example) are stored with the module, normally in its EEPROM. The stack reads that when the
node is found.

That is the one real difference. Everything else below is detail.

## 3. The main node

### 3.1 From the SS+ export

SS+ writes the bus configuration as a C file, `adi_a2b_busconfig.c`. It defines variables
of ADI's structures (`adi_a2b_busconfig.h`), the top one being `sBusDescription`.

`ImportSsPlus()` (`a2bcpp/import/ss_plus/ss_plus_import.cpp`) takes that structure and
fills a `BoardConfig`:

- the main node's **register image**: every register value, in the order ADI's stack
  wrote them;
- a **host script**: the programming of peripherals on the host's I2C bus next to the main
  node (for example the AB0020's ADAU1761). Peripherals SS+ marks "configure before the
  transceiver" run before the main node is started, because that codec may be what clocks
  it;
- the addresses, the chip, its power settings, two timings and the default link profile.

It **ignores** what the export says about sub-nodes. They are configured from their own
module info (section 4).

`Network::Start()` then writes the image and runs the host script. Details are in the
[main-node configuration guide](main_node_config.md).

### 3.2 Compiled in, or loaded at run time

There are two ways to get the export into `ImportSsPlus()`. Both end in the same call.

| | Compiled in | Loaded at run time |
|---|---|---|
| How | the export is part of the build; pass `sBusDescription` | `SsPlusFile::Load("adi_a2b_busconfig.c")`, then pass `*file.Bus()` |
| Where | every target; the **only** way on the RP2040 | Windows and Linux only (it reads a file) |
| Use it for | shipping builds | development and the bench console (`board FILE`) |
| Cost | a rebuild for every settings change | a hand-written C parser, to be kept in step with SS+ |

**Advice: ship compiled in, develop with the loader.** Shipping builds then take the same
path on every target (the RP2040 has no choice), and nothing has to be deployed beside the
program. A settings change during development is a file edit, not a rebuild.

The loader is trusted because a test (`tests/import/test_ss_plus_file.cpp`) checks that it
fills the structures byte for byte as the compiler would, for the repository's exports:
the Pi's and the AB0020's (`adi/exports`), and the AB0020 and AB0310/AB0312 projects' (`tools/SS+reference`).

Both ways use one ADI header, `tools/SS+reference/adi/adi_a2b_busconfig.h`. ADI has no
single header that compiles an SS+ 2.3.0 export, so it is ADI's 2023 header with the
missing fields added. A compiled export and the import must see the same structures, so
every export in `tools/SS+reference` and the import are compiled with it (the bench
console's guide, "The reference folder").

> **TODO:** run that test on every export that is added to the repository, so a board that
> works from a file is known to work compiled in. The AD2428-main AB0020 export
> (`tools/SS+reference/2x_AB0020_EVM_with_2428`) is compiled into the bench console but is not
> in that test yet.

### 3.3 What the loader skips

A field that the reference header doesn't have (an export from an SS+ newer than 2.3.0)
is skipped with a warning. An unknown constant is a warning, read as 0, in the parts the import doesn't read
(sub-node descriptions, streams), and an error where it does read (the bus, the chain, the
main node, the host peripherals). Read the warnings after loading a new export.

> **TODO:** make the bench console say clearly when a skipped field lies in a part the
> import reads. It prints only the first ten warnings today.

## 4. The sub-nodes

### 4.1 Module info

When a sub-node is found, the stack asks a **`ModuleInfoSource`** for its *module-info
image* (`a2b/module_info.h`). The image holds:

- the module's identity (vendor and product ID) and its power data;
- the register values to write to the sub-node (I2SCFG, PDMCTL, CLKOUT and so on);
- optionally, **peripheral scripts**: programs for the chips on the module, run over the
  node's I2C port.

The stack parses the image, checks its CRC, writes the settings, runs the scripts, and only
then counts the node as found. A node whose image is missing or corrupt is **rejected**
(`kNoModuleInfo`, `kBadModuleInfo`) and the rest of the bus carries on.

### 4.2 Where the image comes from

The `ModuleInfoSource` is an interface. The stack has one implementation, and an app can
supply others:

1. **The module's EEPROM** (`EepromModuleInfoSource`). This is the default and the shipping
   case: nothing for the app to do.
2. **An app source.** The app derives from `ModuleInfoSource` and installs it with
   `Network::SetModuleInfoSource()`. This is how "ignore EEPROM" works today: for a blank
   module the app supplies the image itself. The source sees the node's `SubNodeIdentity`
   (position, bus and the transceiver's chip ID).

The precedence is simple: if the app installed a source, it is used; otherwise the EEPROM.
`SetModuleInfoSource(nullptr)` goes back to the EEPROM.

Neither is a second path inside the stack. After the source returns an image, everything
else (parsing, `ModuleScript`, `NodeSetup`) is the same code.

### 4.3 Not built yet: making the image from the SS+ project

The peripheral program for a module, for example the AB0020's ADAU1761, is authored in SS+
like any other, but there is no tool yet that puts it into a module-info image. The bench
console's `module N BOARD [FILE]` builds an image at run time from a compiled-in board profile,
with the codec program read from SigmaStudio's XML if FILE is given; that is the only route today
besides an EEPROM that already holds an image.

The plan (see "Sub-node peripherals and EEPROM provisioning" in the
[roadmap](../../docs/cpp-stack/ROADMAP.md)) is a host tool, the **image builder**:

```
SS+ export (.c)  --image builder-->  module-info image (.dat)
```

The same image would then be used three ways, with no change to the stack:

| Case | How the image reaches the stack |
|---|---|
| **Shipping** | written to the module's EEPROM (the stack reads it as in 4.2, case 1) |
| **Development**, blank or no EEPROM | an app source that reads the `.dat` from a file |
| **A system with no EEPROM at all** | an app source that returns an array compiled into the program |

Because all three go through `ModuleScript`, what you test in development is, byte for byte,
what is burned into the module.

### 4.4 Why not read sub-node peripherals straight from the export?

It would be quicker to iterate, but it would be a second way to run a peripheral script
(ops built from the export, beside `ModuleScript` running an image). And the export lists
sub-nodes by position, while hot-plug discovery finds them in whatever order they are
plugged. The module image travels with the module. So the export is used to *make* images,
not to configure sub-nodes at run time.

> **Not built:** "SS+ sub-node types matched against the module type in the EEPROM" is listed in
> the roadmap as a later option. If it is built, revisit this section.

## 5. Putting it together

A shipping app on Windows or Linux:

1. Link the SS+ export in; `ImportSsPlus()` fills the `BoardConfig`.
2. `Network::Start()`. The main node is configured; sub-nodes are found and each is
   configured from its own EEPROM.

During development, or with blank modules:

1. `board FILE` loads the export at run time.
2. `ignoreEEPROM 1` (or an app source) gives the sub-nodes their images, and
   `module N BOARD [FILE]` a Clockworks board's image at sub-node N, codec program
   included.

## 6. Where people get confused

1. **One SS+ project, two file formats.** The `.c` export feeds the main node and the
   `.dat` image feeds sub-nodes. The image builder hides the second step.
2. **Two entry points.** `ImportSsPlus()` for the board, `ModuleInfoSource` for the modules.
   Think "board export" and "module image", not just "config".
3. **Different failures.** A bad export refuses `Start()`. A bad or blank module rejects
   only that node.
4. **The EEPROM is a default, not a rule.** It is where the bytes usually come from. The
   app can supply them from anywhere.
