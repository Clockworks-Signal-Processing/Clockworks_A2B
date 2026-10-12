#!/usr/bin/env python3
"""Measures the C++ stack's RAM on the RP2040 (Cortex-M0+) without running anything.

For each optimisation level the RP2040 build uses (-Og for Debug, -O3 for Release) it
compiles a2bcpp/src/*.cpp and a2bcpp/tools/ram_probe.cpp with the Arm GNU toolchain and
reports:
  - static RAM (.data + .bss) per object file;
  - the size of each object ram_probe.cpp lists (the per-bus state);
  - each function's worst-case stack depth, from GCC's call graph (-fcallgraph-info=su),
    for the stack and for the call paths ram_probe.cpp adds (a2b_PollTwoBuses: one thread
    polling two buses). Calls through a virtual function (the Platform methods) are not
    followed: add the platform's own stack use to the "+ Platform" rows. Library calls
    ("+ memcpy") are not followed either.

Usage, from the repository root:
  python a2bcpp/tools/ram_report.py                  print the report (Markdown)
  python a2bcpp/tools/ram_report.py --toolchain DIR  use DIR/arm-none-eabi-g++
The toolchain is taken from PATH, else from ~/.pico-sdk/toolchain/*/bin.
Objects go to build/a2bcpp-ram.

Copyright 2026 by ClockWorks Signal Processing LLC
Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
SPDX-License-Identifier: BSD-3-Clause
"""

import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
A2BCPP = os.path.join(ROOT, "a2bcpp")
OUT = os.path.join(ROOT, "build", "a2bcpp-ram")
PREFIX = "arm-none-eabi-"
LEVELS = [("-Og", "Debug build"), ("-O3", "Release build")]
FLAGS = ["-mcpu=cortex-m0plus", "-mthumb", "-std=c++17", "-fno-exceptions", "-fno-rtti",
         "-ffunction-sections", "-fdata-sections", "-fstack-usage", "-fcallgraph-info=su",
         "-I", os.path.join(A2BCPP, "include"), "-I", os.path.join(A2BCPP, "src")]
INDIRECT = "__indirect_call"


def find_toolchain(given):
    """Returns the directory holding arm-none-eabi-g++, or None."""
    exe = ".exe" if os.name == "nt" else ""
    if given:
        return given if os.path.isfile(os.path.join(given, PREFIX + "g++" + exe)) else None
    found = shutil.which(PREFIX + "g++")
    if found:
        return os.path.dirname(found)
    candidates = sorted(glob.glob(os.path.join(os.path.expanduser("~"), ".pico-sdk", "toolchain",
                                               "*", "bin", PREFIX + "g++" + exe)))
    return os.path.dirname(candidates[-1]) if candidates else None


def tool(toolchain, name):
    return os.path.join(toolchain, PREFIX + name)


def compile_all(toolchain, level, sources):
    """Compiles each source; returns the object paths."""
    out_dir = os.path.join(OUT, level.lstrip("-"))
    os.makedirs(out_dir, exist_ok=True)
    objects = []
    for source in sources:
        obj = os.path.join(out_dir, os.path.splitext(os.path.basename(source))[0] + ".o")
        subprocess.run([tool(toolchain, "g++"), level, *FLAGS, "-c", source, "-o", obj],
                       check=True, cwd=out_dir)
        objects.append(obj)
    return objects


def static_ram(toolchain, objects):
    """Returns [(object name, data, bss)] from `size` (Berkeley format)."""
    result = subprocess.run([tool(toolchain, "size"), *objects], check=True,
                            capture_output=True, text=True).stdout.splitlines()[1:]
    rows = []
    for line in result:
        fields = line.split()
        rows.append((os.path.basename(fields[5]), int(fields[1]), int(fields[2])))
    return rows


def probe_sizes(toolchain, probe_obj):
    """Returns [(name, size)] for the a2b_sizeof_<Name> symbols."""
    lines = subprocess.run([tool(toolchain, "nm"), "-S", probe_obj], check=True,
                           capture_output=True, text=True).stdout.splitlines()
    sizes = []
    for line in lines:
        fields = line.split()
        if len(fields) == 4 and fields[3].startswith("a2b_sizeof_"):
            sizes.append((fields[3][len("a2b_sizeof_"):], int(fields[1], 16)))
    return sorted(sizes)


NODE_RE = re.compile(r'node: \{ title: "([^"]*)" label: "((?:[^"\\]|\\.)*)"')
EDGE_RE = re.compile(r'edge: \{ sourcename: "([^"]*)" targetname: "([^"]*)"')
FRAME_RE = re.compile(r"(\d+) bytes \(([a-z,]+)\)")


def call_graph(objects):
    """Reads the .ci files; returns ({title: (name, frame, kind)}, {title: set(callees)})."""
    nodes, edges = {}, {}
    for obj in objects:
        with open(os.path.splitext(obj)[0] + ".ci", encoding="utf-8") as file:
            text = file.read()
        for title, label in NODE_RE.findall(text):
            parts = label.split("\\n")
            frame = FRAME_RE.search(label)
            if frame:
                nodes[title] = (parts[0], int(frame.group(1)), frame.group(2))
            else:
                nodes.setdefault(title, (parts[0], None, None))
        for source, target in EDGE_RE.findall(text):
            edges.setdefault(source, set()).add(target)
    return nodes, edges


def worst_paths(nodes, edges):
    """Returns {title: (worst depth, calls through a virtual, notes)} for defined functions."""
    memo = {}

    def visit(title, stack):
        if title in memo:
            return memo[title]
        if title in stack:
            return (0, False, {"recursion"})
        name, frame, kind = nodes.get(title, (title, None, None))
        if title == INDIRECT:
            return (0, True, set())
        if frame is None:
            return (0, False, {"+ " + short(name).replace("__builtin_", "")})
        depth, indirect, notes = 0, False, set()
        if kind != "static":
            notes.add(kind)
        for callee in edges.get(title, ()):
            child = visit(callee, stack | {title})
            depth = max(depth, child[0])
            indirect = indirect or child[1]
            notes |= child[2]
        memo[title] = (frame + depth, indirect, notes)
        return memo[title]

    return {t: visit(t, set()) for t, (_, frame, _) in nodes.items() if frame is not None}


# Paths through a virtual call, which the call graph can't follow: (from, the function that
# makes the virtual call, what it calls). The depth is the deepest path from the first to
# the second, frames included, plus the third's worst path. The callees stand in for the
# app's: the EEPROM source as the module-info source, a2b_HandlerWritesOtherBus
# (ram_probe.cpp) as a listener's OnEvent() that writes to a peripheral on another bus.
VIRTUAL_PATHS = [
    (root, caller, callee)
    for root in ("a2b::Network::Poll", "a2b_PollTwoBuses")
    for caller, callee in (
        ("a2b::ModuleScript::RunUnit", "a2b::EepromModuleInfoSource::ReadModuleInfo"),
        ("a2b::ModuleScript::ReadInfo", "a2b::EepromModuleInfoSource::ReadModuleInfo"),
        ("a2b::Discovery::Identify", "a2b::EepromModuleInfoSource::GetModuleInfo"),
        ("a2b::Network::Impl::Dispatch", "a2b_HandlerWritesOtherBus"),
        ("a2b::Network::Impl::Review", None),
    )
]


def deepest_to(nodes, edges, source, target):
    """Returns the deepest stack from entering `source` to inside `target`, or None."""
    memo = {}

    def visit(title, stack):
        if title == target:
            return nodes[title][1]
        if title in memo:
            return memo[title]
        best = None
        for callee in edges.get(title, ()):
            if callee in stack or nodes.get(callee, (None, None, None))[1] is None:
                continue
            depth = visit(callee, stack | {title})
            if depth is not None and (best is None or depth > best):
                best = depth
        memo[title] = None if best is None else best + nodes[title][1]
        return memo[title]

    return visit(source, set())


def short(name):
    """Drops the return type and parameter list from a demangled name."""
    return name.split("(")[0].split(" ")[-1]


def report(toolchain):
    version = subprocess.run([tool(toolchain, "g++"), "--version"], check=True,
                             capture_output=True, text=True).stdout.splitlines()[0]
    sources = sorted(glob.glob(os.path.join(A2BCPP, "src", "*.cpp")))
    probe = os.path.join(A2BCPP, "tools", "ram_probe.cpp")
    print("# a2bcpp RAM report (RP2040, Cortex-M0+)\n")
    print(f"Toolchain: `{version}`. Sources: `a2bcpp/src/*.cpp`.\n")
    for level, what in LEVELS:
        objects = compile_all(toolchain, level, sources + [probe])
        stack_objects, probe_obj = objects[:-1], objects[-1]
        print(f"## `{level}` ({what})\n")
        print("Static RAM:\n\n| Object file | .data | .bss |\n|---|---:|---:|")
        total = 0
        for name, data, bss in static_ram(toolchain, stack_objects):
            print(f"| {name} | {data} | {bss} |")
            total += data + bss
        print(f"| **Total** | | **{total}** |\n")
        print("Object sizes (from `ram_probe.cpp`):\n\n| Object | Bytes |\n|---|---:|")
        for name, size in probe_sizes(toolchain, probe_obj):
            print(f"| `{name}` | {size} |")
        nodes, edges = call_graph(objects)
        paths = worst_paths(nodes, edges)
        print("\nStack, worst path from each function (bytes):\n")
        print("| Function | Own frame | Worst path | Notes |\n|---|---:|---:|---|")
        rows = sorted(paths.items(), key=lambda item: (-item[1][0], nodes[item[0]][0]))
        for title, (depth, indirect, notes) in rows:
            name, frame, _ = nodes[title]
            extra = sorted(notes) + (["+ Platform"] if indirect else [])
            print(f"| `{short(name)}` | {frame} | {depth} | {', '.join(extra)} |")
        by_name = {short(name): title for title, (name, frame, _) in nodes.items()
                   if frame is not None}
        print("\nPaths through a virtual call (bytes; the callee's worst path added):\n")
        print("| From | To the virtual call in | Depth there | Callee | Total |")
        print("|---|---|---:|---|---:|")
        for root, caller, callee in VIRTUAL_PATHS:
            if root not in by_name or caller not in by_name:
                continue
            depth = deepest_to(nodes, edges, by_name[root], by_name[caller])
            if depth is None:
                continue
            if callee is None:
                print(f"| `{root}` | `{caller}` | {depth} | the app's handler | {depth} + handler |")
            else:
                extra = paths[by_name[callee]][0]
                print(f"| `{root}` | `{caller}` | {depth} | `{callee}` ({extra}) | {depth + extra} |")
        print()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--toolchain", help="directory holding arm-none-eabi-g++")
    args = parser.parse_args()
    toolchain = find_toolchain(args.toolchain)
    if toolchain is None:
        sys.exit("arm-none-eabi-g++ not found: pass --toolchain, or install the Pico SDK")
    report(toolchain)


if __name__ == "__main__":
    main()
