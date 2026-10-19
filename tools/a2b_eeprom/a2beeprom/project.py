"""Project files (.json): a module's settings, saved and loaded, with each peripheral's
SigmaStudio XML program named by a path relative to the project file.

Copyright 2026 by ClockWorks Signal Processing LLC
Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
SPDX-License-Identifier: BSD-3-Clause
"""

import json
import os

from . import sigma_xml
from .model import Module


class ProjectError(Exception):
    """A project file that can't be read."""


def stored_path(path, project_dir):
    """`path` as a project stores it: relative to the project's folder with forward
    slashes, or absolute when it is on another drive."""
    if not path:
        return ""
    try:
        relative = os.path.relpath(os.path.abspath(path), os.path.abspath(project_dir))
    except ValueError:  # Another drive (Windows).
        return os.path.abspath(path)
    return relative.replace(os.sep, "/")


def full_path(path, project_dir):
    """A stored path made absolute against the project's folder."""
    if not path or os.path.isabs(path):
        return path
    return os.path.normpath(os.path.join(project_dir, path))


def read_programs(module, project_dir):
    """Reads each peripheral's XML program into its units. Returns (peripheral index,
    message) for each that couldn't be read; those keep no units."""
    problems = []
    for n, p in enumerate(module.peripherals):
        p.xml_report = None
        if not p.xml:
            continue
        try:
            program = sigma_xml.read_file(full_path(p.xml, project_dir), p.address)
        except (OSError, sigma_xml.XmlError) as error:
            p.units = []
            problems.append((n, str(error)))
            continue
        p.units = program.units
        p.xml_report = program
    return problems


def load(path):
    """Reads a project file and its XML programs: (module, problems with the programs)."""
    try:
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        module = Module.from_json(data)
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise ProjectError("{}: {}".format(path, error))
    problems = read_programs(module, os.path.dirname(os.path.abspath(path)))
    return module, problems


def save(module, path):
    """Writes a project file. XML paths are kept as they are (stored_path() makes them)."""
    text = json.dumps(module.to_json(), indent=2)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text + "\n")
