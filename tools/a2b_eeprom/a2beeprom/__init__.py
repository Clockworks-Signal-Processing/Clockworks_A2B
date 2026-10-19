"""Builds and reads A2B module-info images: the ADI PnP EEPROM content of an A2B sub-node.

Modules: layout (where things are), registers (the register settings, bit by bit), model
(the fields), encode and decode (fields to bytes and back), validate (the checks),
sigma_xml (SigmaStudio XML programs), project (project files), report (an image in
words), vendor (vendor sections: Clockworks' as the example). The command line is
a2b_eeprom.py, the GUI a2b_eeprom_gui.py; see tools/a2b_eeprom/README.md.

Copyright 2026 by ClockWorks Signal Processing LLC
Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
SPDX-License-Identifier: BSD-3-Clause
"""

from .model import TOOL_VERSION

__version__ = ".".join(str(v) for v in TOOL_VERSION)
