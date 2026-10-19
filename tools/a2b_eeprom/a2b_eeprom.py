#!/usr/bin/env python3
"""A2B EEPROM image tool, command line: builds a module-info image (.dat) from a project
file, checks a project, or says what an image (a .dat, or one read back from an EEPROM)
holds. The GUI is a2b_eeprom_gui.py; both are described in tools/a2b_eeprom/README.md.

    python3 tools/a2b_eeprom/a2b_eeprom.py build PROJECT.json [-o IMAGE.dat]
    python3 tools/a2b_eeprom/a2b_eeprom.py check PROJECT.json
    python3 tools/a2b_eeprom/a2b_eeprom.py decode IMAGE.dat [--project OUT.json] [--all]

Exit status: 0, or 1 when there are errors (an image with errors isn't written).

Copyright 2026 by ClockWorks Signal Processing LLC
Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
SPDX-License-Identifier: BSD-3-Clause
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from a2beeprom import __version__, project, report, validate  # noqa: E402
from a2beeprom.decode import decode  # noqa: E402
from a2beeprom.encode import encode  # noqa: E402


def print_issues(issues, notes=True):
    for issue in issues:
        if issue.level != validate.NOTE or notes:
            print("  {}: {}: {}".format(issue.level, issue.where, issue.message))


def load(path):
    module, problems = project.load(path)
    for n, message in problems:
        print("  error: peripheral {} '{}': {}".format(n + 1, module.peripherals[n].name,
                                                      message))
    return module, bool(problems)


def cmd_check(args):
    try:
        module, failed = load(args.project)
    except project.ProjectError as error:
        print("error: {}".format(error))
        return 1
    issues = validate.check(module)
    print_issues(issues)
    errors = failed or any(i.level == validate.ERROR for i in issues)
    print("{}: {}".format(args.project, "errors" if errors else "ok"))
    return 1 if errors else 0


def cmd_build(args):
    try:
        module, failed = load(args.project)
    except project.ProjectError as error:
        print("error: {}".format(error))
        return 1
    issues = validate.check(module)
    print_issues(issues, notes=not args.quiet)
    if failed or any(i.level == validate.ERROR for i in issues):
        print("Not written: fix the errors first")
        return 1
    encoded = encode(module)
    out = args.output or os.path.splitext(args.project)[0] + ".dat"
    with open(out, "wb") as f:
        f.write(encoded.image)
    if not args.quiet:
        for start, end, name in encoded.regions:
            print("  0x{:04X}-0x{:04X}  {}".format(start, end - 1, name))
    print("Wrote {}: {} bytes of a {}-byte EEPROM".format(out, len(encoded.image),
                                                          module.eeprom_size))
    return 0


def cmd_decode(args):
    try:
        with open(args.image, "rb") as f:
            image = f.read()
    except OSError as error:
        print("error: {}".format(error))
        return 1
    decoded = decode(image)
    if decoded.module is None:
        for level, where, message in decoded.issues:
            print("{}: {}: {}".format(level, where, message))
        return 1
    if args.chip:
        decoded.module.target_chip = args.chip
    for line in report.describe(decoded.module, decoded, units_shown=10 ** 6 if args.all else 20):
        print(line)
    if len(image) > decoded.image_end and any(b != 0xFF for b in image[decoded.image_end:]):
        print("Note: bytes after the image (0x{:04X} on) aren't blank (0xFF)".format(
            decoded.image_end))
    if args.project:
        project.save(decoded.module, args.project)
        print("Wrote {} (the units are in it; the target chip is {})".format(
            args.project, decoded.module.target_chip))
    return 0 if decoded.ok else 1


def main(argv=None):
    parser = argparse.ArgumentParser(description="A2B EEPROM image tool " + __version__)
    sub = parser.add_subparsers(dest="command")
    p = sub.add_parser("build", help="build an image (.dat) from a project file")
    p.add_argument("project")
    p.add_argument("-o", "--output", help="the image (default: the project's name, .dat)")
    p.add_argument("-q", "--quiet", action="store_true", help="errors and warnings only")
    p = sub.add_parser("check", help="check a project file")
    p.add_argument("project")
    p = sub.add_parser("decode", help="say what an image holds")
    p.add_argument("image")
    p.add_argument("--project", help="also save it as a project file")
    p.add_argument("--chip", help="the transceiver it is for (AD2427, AD2428, AD2433, AD2437; "
                                  "default AD2437): how its register settings read")
    p.add_argument("--all", action="store_true", help="list every unit")
    args = parser.parse_args(argv)
    if args.command == "build":
        return cmd_build(args)
    if args.command == "check":
        return cmd_check(args)
    if args.command == "decode":
        return cmd_decode(args)
    parser.print_help()
    return 1


if __name__ == "__main__":
    sys.exit(main())
