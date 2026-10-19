#!/usr/bin/env python3
"""A2B EEPROM image tool, GUI: every field of a sub-node's module-info image in a window,
saved as a project (.json), exported as the image (.dat) the bench console writes to an
EEPROM, and imported from an image read back. The command line is a2b_eeprom.py; both are
described in tools/a2b_eeprom/README.md.

    python3 tools/a2b_eeprom/a2b_eeprom_gui.py [PROJECT.json]

Needs Tkinter: part of Python on Windows; on Debian or Raspberry Pi OS,
`sudo apt install python3-tk`.

Copyright 2026 by ClockWorks Signal Processing LLC
Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
SPDX-License-Identifier: BSD-3-Clause
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

try:
    from a2beeprom import gui  # noqa: E402
except ImportError as error:
    if "tkinter" in str(error):
        sys.exit("The GUI needs Tkinter: on Debian or Raspberry Pi OS, sudo apt install "
                 "python3-tk (the command line, a2b_eeprom.py, doesn't)")
    raise

if __name__ == "__main__":
    sys.exit(gui.main(sys.argv))
