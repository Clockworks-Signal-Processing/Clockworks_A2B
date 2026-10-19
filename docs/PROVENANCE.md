# Provenance

Where the code in this repository comes from, and how much of it is ADI's.

This repository is the C++ stack, `a2bcpp/`. It started from ADI's PnP release
`ADI_A2B_PnP_Software_SRC-Rel1.3.0` (the `Target/software` tree) by way of Clockworks' own
version of that release, which this document calls **the legacy stack**: ADI's 1.3.0 with
Clockworks' changes and platform ports (for example, audio connections by chip ID, which are
Clockworks' addition). The legacy stack is not part of this repository and is not described
here. Elsewhere in the documentation it is called "ADI's stack".

The numbers below compare `a2bcpp` with ADI's release as it came from ADI. They are a snapshot
made on 2026-10-08 and are not kept up to date; the [method](#method) is at the end.

## Summary

**The C++ stack shares almost nothing with ADI's source.** Of 22,811 code lines in `a2bcpp`
(comments excluded; SigmaStudio files, register definitions and docs left out), none is
verbatim ADI code, and 355 (1.6%) match ADI's code once identifier names are ignored. The
largest matches are generic idioms: method declarations, enumerator lists and runs of
`a.x = b.x;` assignments. Comment text is also clear of ADI's: 0.09% of the comment words
lie in a 10-word run shared with ADI's comments.

What the C++ stack does share with ADI is behavior: the order of register writes and the
timings follow the legacy stack on purpose, because the A2B chips and ADI's module-info
formats leave little room to differ. [How each part relates](#how-each-part-relates-to-the-legacy-code)
says which parts.

## How it was made

- **Started 2026-09-30** as a clone of A2B_PnP, the legacy repo, at its v0.1 (commit 5e3f744).
  The requirements were decided with the owner: a stack that is simpler to use, in C++ and
  without ADI's plugin machinery, with an API reasonably similar to `a2b_pnp_*`
  ([decisions.md](../a2bcpp/docs/decisions.md)).
- **Written mostly by Claude** (Sonnet and Opus), at the owner's direction, one planned step
  per session. The owner made some edits to the code and supplied the SigmaStudio+ reference
  projects in `tools/SS+reference/`.
- **The legacy stack was the reference for behavior, not a source of text.** The new stack's
  I2C traffic is checked against traces captured from the legacy stack on hardware
  (`a2bcpp/tests/golden/`). Comments in the library sources name the ADI function, file or TRM
  section a behavior follows.
- **A different architecture.** ADI's stack is C, with a message router, job executor,
  memory pools and master/slave plugins. The C++ stack is a single-threaded `Poll()`-driven
  set of state machines with fixed storage and no heap (`Network`, `Transport`, `Discovery`,
  `NodeSetup`, `RoutingPlanner`; see `a2bcpp/docs/architecture.md`).

## How each part relates to the legacy code

| Relation | What |
|---|---|
| **Follows the legacy behavior** (the order of register writes and the timings are matched to the legacy traces; the code is new) | Discovery (`src/discovery.cpp`, from ADI's `a2bplugin-master/src/discovery.c` and the PnP layer's `a2bpnp_local.c`), interrupt polling, node setup order, module-info and peripheral-script parsing (ADI's `a2bpnp_calc.c`), the peripheral write path, the CRC-8, muting, bus-drop detection and the audio-routing calls (`src/network*.cpp`, `src/routing.cpp`) |
| **Follows Clockworks' additions to the legacy stack** (not in ADI's release) | The connection calls by chip ID (`a2b_pnp_AddConByChipId`, `a2b_pnp_GetConnectionList`, `a2b_pnp_ImportConnections`) and the SWCTL handling around each discovery probe (`a2b_pnp_PrepareSwctlforBD`, `a2b_pnp_RestoreSwctlAftrBD`) |
| **Re-derived from the data sheets and TRMs, not ported** | `src/routing_planner.cpp` (replaces `crossbarCalc`, `AutoCalcSlot` and the pass-list pruning in ADI's `a2bpnp_calc.c`), the response-cycle formula, the AD242x and mixed-chain routing |
| **Fixed, not ported** | The module-info vendor check (the legacy code required vendor 0xAD), several routing bugs listed in [decisions.md](../a2bcpp/docs/decisions.md#bugs-fixed-not-ported) |
| **Trimmed copies and ports of ADI files** | `src/detail/regdefs.h` (a trimmed C++ copy of ADI's `regdefs.h`: 252 lines of the 4,021), `platforms/usbi/` (the USBi link, ported from the `pal_i2c_usbi.c` of ADI's win32 PAL), `import/ss_plus/ss_plus_tables.inc` (generated from ADI's `adi_a2b_busconfig.h` by `tools/gen_ss_plus_tables.py`) |
| **New, with no ADI counterpart** | The simulator (`sim/`), the test suites (`tests/`), the bench console (its RP2040 build and its EEPROM jobs included), the fixed and minimal examples, the Clockworks board profiles, the SigmaStudio+ and SigmaStudio XML loaders, the Linux and RP2040 platform layers, the EEPROM image tool (`tools/a2b_eeprom`) |

## SigmaStudio+ examples and ADI's headers

The stack includes examples and tests built from SigmaStudio+ (SS+) projects. Each project
produces a C export (`12_a2b_busconfig.c`, or `adi_a2b_busconfig.c` in the Clockworks board
projects) that describes a main node's configuration. The SS+ import (`import/ss_plus/`)
reads such an export to build a `BoardConfig`, and the bench console, the minimal example and
several tests use exports as their board data.

An export can only be compiled or read with three of ADI's headers:

- `adi_a2b_datatypes.h`
- `adi_a2b_graphdata.h`
- `adi_a2b_busconfig.h`

These are ADI's headers, needed by SS+ output (the same files also ship in the PnP
release). They are in this repository only because the examples use SS+ output, and they are
not Clockworks' work. They are not counted in the table below, and neither are the exports.
`import/ss_plus/ss_plus_tables.inc` is generated from `adi_a2b_busconfig.h`
(`tools/gen_ss_plus_tables.py`).

Where they are:

| What | Where |
|---|---|
| The Clockworks board projects and their exports | `tools/SS+reference/` |
| The headers used to build those exports | `tools/SS+reference/adi/`: `adi_a2b_graphdata.h` is the PnP 1.3.0 file unchanged; `adi_a2b_busconfig.h` differs from it by 41 lines (merged so that a 2.3.0 export compiles); `adi_a2b_datatypes.h` is the single-core version |
| Other copies of the headers, and the stack headers they include | `adi/headers/`: the PnP release's own `adi_a2b_busconfig.h`, `adi_a2b_graphdata.h` and `adi_a2b_datatypes.h` (`stack_export/`, used by the minimal example for the RP2040), and each platform's `a2b/conf.h`, `ctypes.h` and `features.h` (`linux/`, `rp2040/`) with the stack's `a2b/macros.h` and `spi.h` (`stack/`) |
| Two older exports, used as the bench console's "ref" board and as test data | `adi/exports/rp2040/12_a2b_busconfig.c`, `adi/exports/windows/12_a2b_busconfig.c` |
| ADI's module-info images, used as test data | `adi/module_info/` |

## Common lines, by directory

The comparison was made on the snapshot above, before the EEPROM provisioning, the RP2040
bench console and the split of `src/network.cpp`; that later code is Clockworks' own as well,
and is not counted here.

Counts are code lines: comments and `#include` lines removed, then non-blank lines. A line
is "common" when most of its tokens lie inside a 16-token run that also occurs in ADI's
source. **Verbatim** compares the tokens as written. **Identifiers ignored** first replaces
every variable, function and type name with a placeholder, so a copy with renamed identifiers
still matches. The last column repeats the identifiers-ignored test with 12-token runs, to
show how much the threshold matters: shorter runs pick up generic code.

| Directory | Code lines | Verbatim | Identifiers ignored | % | 12-token runs |
|---|---|---|---|---|---|
| `src` | 4,206 | 0 | 115 | 2.7% | 12.8% |
| `include` | 653 | 0 | 42 | 6.4% | 12.1% |
| `import` | 1,739 | 0 | 35 | 2.0% | 7.9% |
| `sim` | 1,557 | 0 | 23 | 1.5% | 7.3% |
| `platforms/linux` | 114 | 0 | 2 | 1.8% | 9.6% |
| `platforms/rp2040` | 76 | 0 | 5 | 6.6% | 22.4% |
| `platforms/usbi` | 279 | 0 | 12 | 4.3% | 16.1% |
| `examples/bench` | 2,413 | 0 | 22 | 0.9% | 5.9% |
| `examples/fixed` | 281 | 0 | 2 | 0.7% | 5.7% |
| `examples/minimal` | 256 | 0 | 2 | 0.8% | 7.4% |
| `tests/unit` | 2,185 | 0 | 23 | 1.1% | 5.3% |
| `tests/scenario` | 7,014 | 0 | 62 | 0.9% | 4.1% |
| `tests/example` | 1,412 | 0 | 3 | 0.2% | 3.2% |
| `tests/import` | 594 | 0 | 7 | 1.2% | 5.6% |
| `tools` (C++ only) | 32 | 0 | 0 | 0.0% | 0.0% |
| **Total** | **22,811** | **0** | **355** | **1.6%** | **7.0%** |

At 12 tokens, 11 lines are verbatim (8 in `import`, 3 in `tests/scenario`).

What the matches are: public method declarations (for example `Result RejectNode(NodeAddr
position);`), enumerator lists, `x.field = y.field;` runs, member lists and Pico SDK GPIO
setup calls. The largest file, `src/network.cpp`, has 39 matching lines out of 1,790, all of
those kinds. None is a copied block of logic. Comment text was checked too: 56 of 60,694
comment words (0.09%) lie inside a 10-word run shared with ADI's comments, nearly all in
`tests/import/test_ss_plus_import.cpp`.

**Left out of the table:**

- `docs/` and the golden traces (`tests/golden/`);
- SigmaStudio files and output (see the previous section): the eight wrapper stubs that
  `#include` an export (`examples/bench/board_*.c`, `tests/import/export_*.c`; the exports
  and ADI's headers are outside `a2bcpp`) and `import/ss_plus/ss_plus_tables.inc`;
- `src/detail/regdefs.h`, the hardware register map. It is a trimmed copy of ADI's by
  design: the addresses and bit masks are fixed by the chips;
- the Python, shell, PowerShell and CMake files, which ADI's release has no counterpart for.

The SigmaStudio loaders (`import/ss_plus/ss_plus_file.cpp`, `ss_plus_import.cpp` and
`examples/clockworks/sigma_xml.cpp`) are Clockworks code that reads those files, so they are
counted.

**What the table doesn't measure.** It finds shared text, including text with renamed
identifiers. It cannot find the same behavior written differently, such as the discovery
and node-setup register order that the C++ stack follows from the legacy stack on purpose.
The table above on how each part relates covers that.

## Method

- **Baseline:** every `.c`, `.cpp` and `.h` file in `ADI_A2B_PnP_Software_SRC-Rel1.3.0.zip`
  (204 files, including the SHARC target demos and the Windows library). cJSON is excluded.
  The baseline is ADI's release, not the legacy stack, so Clockworks' own additions to the
  legacy stack are not counted as ADI's.
- **`a2bcpp`:** comments and `#include` lines are removed and the rest is split into
  tokens. A token counts as shared when it lies in a window of 16 consecutive tokens that
  also occurs in the baseline. The identifiers-ignored test replaces every non-keyword
  identifier with a placeholder first. A line is common when at least half of its tokens are
  shared. Two controls confirmed the method: a file compared with itself scored 100%, and a
  copy of an ADI file with every identifier renamed and the formatting changed also scored
  100%.
- **Limits:** the comparison finds shared text, with or without renamed identifiers. It does
  not measure behavior or design. The scripts were run ad hoc and are not part of this
  repository.
