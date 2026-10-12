#!/usr/bin/env python3
"""Generates a2bcpp/import/ss_plus/ss_plus_tables.inc from ADI's adi_a2b_busconfig.h.

The SigmaStudio+ file loader (import/ss_plus/ss_plus_file.cpp) reads an export's C file at
run time. It needs, for every structure an export initialises, its fields in declaration
order, and the value of every constant an export may name (A2B_TDM8, ADI_A2B_AD2428, ...).
This script lists them as X-macros; the loader's templates take each field's offset, size
and kind from the compiler (offsetof, decltype), so the table can't disagree with the
header it was built with.

From the repository root:
    python3 a2bcpp/tools/gen_ss_plus_tables.py            write the table
    python3 a2bcpp/tools/gen_ss_plus_tables.py --check    exit 1 if it is out of date

Copyright 2026 by ClockWorks Signal Processing LLC
Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
SPDX-License-Identifier: BSD-3-Clause
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
HEADER = ROOT / "tools/SS+reference/adi/adi_a2b_busconfig.h"
# The header includes a2b/spi.h for a field's enum (A2B_SPI_MODE), so its enumerators
# can appear in an export too.
SPI_HEADER = ROOT / "adi/headers/stack/a2b/spi.h"
OUTPUT = ROOT / "a2bcpp/import/ss_plus/ss_plus_tables.inc"

# The structures an export defines variables of; everything they reach is included.
ROOTS = [
    "ADI_A2B_BCD",
    "ADI_A2B_MASTER_SLAVE_CONFIG",
    "ADI_A2B_MASTER_NCD",
    "ADI_A2B_SLAVE_NCD",
    "A2B_PERIPHERAL_DEVICE_CONFIG",
    "ADI_A2B_PERI_CONFIG_UNIT",
    "ADI_A2B_STREAM",
]

LICENCE = """\
// clang-format off
/*=============================================================================
 *
 * Project: a2bcpp
 *
 * Copyright (c) 2023 - Analog Devices Inc. All Rights Reserved.
 * See LICENSE_ADI_BSD.txt for additional licensing terms. You must include that file with all source you use.
 *
 *=============================================================================
 */
"""


def strip_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    return re.sub(r"//[^\n]*", " ", text)


def parse_structs(text):
    """Returns {typedef name: [(field, type text)]} in declaration order."""
    structs = {}
    pattern = r"typedef\s+struct\s*\w*\s*\{(.*?)\}\s*(\w+)[^;]*;"
    for match in re.finditer(pattern, text, re.S):
        fields = []
        for decl in match.group(1).split(";"):
            decl = " ".join(decl.split())
            if not decl:
                continue
            m = re.match(r"(.*?)(\w+)\s*((\[[^\]]*\]\s*)*)$", decl)
            if m is None:
                sys.exit(f"can't read field '{decl}' of {match.group(2)}")
            fields.append((m.group(2), m.group(1)))
        structs[match.group(2)] = fields
    return structs


def reachable(structs):
    """The roots and every structure their fields name, in header order."""
    seen = set()
    todo = list(ROOTS)
    while todo:
        name = todo.pop()
        if name in seen:
            continue
        if name not in structs:
            sys.exit(f"{name} is not a structure in {HEADER.name}")
        seen.add(name)
        for _, type_text in structs[name]:
            for word in re.findall(r"\w+", type_text):
                if word in structs and word not in seen:
                    todo.append(word)
    return [name for name in structs if name in seen]


def parse_symbols(text, defines=True):
    """Enumerators and (if @p defines) integer #defines, in header order."""
    names = []
    for match in re.finditer(r"typedef\s+enum\s*\w*\s*\{(.*?)\}", text, re.S):
        for item in match.group(1).split(","):
            m = re.match(r"\s*(\w+)", item)
            if m:
                names.append(m.group(1))
    value = r"[\s()0-9a-fA-FxXuUlL<>|&~+\-*]+"
    if not defines:
        return names
    for match in re.finditer(r"^\s*#\s*define\s+(\w+)\s+(.+?)\s*$", text, re.M):
        name, body = match.group(1), match.group(2)
        if re.fullmatch(value, body) and re.search(r"[0-9]", body):
            names.append(name)
    return names


def generate():
    text = strip_comments(HEADER.read_text(encoding="latin-1"))
    structs = parse_structs(text)
    lines = [LICENCE.rstrip("\n"), "// clang-format on", ""]
    lines += [
        "// Generated from tools/SS+reference/adi/adi_a2b_busconfig.h (and the enums of",
        "// adi/headers/stack/a2b/spi.h, which it includes) by",
        "// a2bcpp/tools/gen_ss_plus_tables.py: do not edit. Included by ss_plus_file.cpp,",
        "// which defines the macros.",
        "//",
        "// A2B_SS_STRUCT(type) ... A2B_SS_END(): a structure an export may initialise, with",
        "// A2B_SS_FIELD(name) for each field in declaration order.",
        "// A2B_SS_SYMBOL(name): a constant an export may name.",
        "",
        "#ifdef A2B_SS_STRUCT",
    ]
    for name in reachable(structs):
        lines.append(f"A2B_SS_STRUCT({name})")
        for field, _ in structs[name]:
            lines.append(f"A2B_SS_FIELD({field})")
        lines.append("A2B_SS_END()")
    lines += ["#endif", "", "#ifdef A2B_SS_SYMBOL"]
    names = parse_symbols(text)
    names += parse_symbols(strip_comments(SPI_HEADER.read_text(encoding="latin-1")), False)
    unique = []
    for name in names:
        if name not in unique:
            unique.append(name)
    lines += [f"A2B_SS_SYMBOL({name})" for name in unique]
    lines += ["#endif", ""]
    return "\n".join(lines)


def main():
    content = generate()
    if "--check" in sys.argv[1:]:
        if not OUTPUT.exists() or OUTPUT.read_text() != content:
            print(f"{OUTPUT} is out of date: run {pathlib.Path(__file__).name}")
            return 1
        return 0
    OUTPUT.write_text(content)
    print(f"wrote {OUTPUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
