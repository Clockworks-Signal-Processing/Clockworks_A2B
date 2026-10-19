"""Clockworks' vendor section: an example of a vendor's own data in the EEPROM.

Vendor ID "CLKWORKS". In the Supplier Information block's vendor data (0x42B on):

| Offset | Bytes | Field |
|---|---|---|
| 0x42B | 16 | ID: text, up to 15 characters, NUL-terminated |
| 0x43B | 1 | Number of features, 0-6 |
| 0x43C | 6 x 32 | Features, each: UID (8, low byte first), name (8), value 0 (4, signed, low byte first), value 1 (4, signed), text (8) |
| 0x4FC | 3 | Spare (0) |

A feature's UID names the kind of feature, the same on every board that has it; no two
features of one image share one. Its name and text may use all 8 bytes (no NUL then).

To make your own section, copy this file, change VENDOR_ID, the title and the fields, and
add the new LAYOUT to LAYOUTS in vendor/__init__.py. The section must fit 212 bytes.

Copyright 2026 by ClockWorks Signal Processing LLC
Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
SPDX-License-Identifier: BSD-3-Clause
"""

from . import IntField, Repeat, TextField, UidField, VendorLayout

VENDOR_ID = "CLKWORKS"
MAX_FEATURES = 6

LAYOUT = VendorLayout(
    VENDOR_ID,
    "Clockworks",
    "An ID and up to six features, each with a 64-bit UID, a name, two values and a text.",
    [
        TextField("id", "ID", 16, terminated=True,
                  help_text="Up to 15 characters, for example a model or serial number."),
        Repeat("features", "Feature", MAX_FEATURES, [
            UidField("uid", "UID", help_text="64 bits: which feature this is (Generate makes "
                                             "a random one)."),
            TextField("name", "Name", 8),
            IntField("value0", "Value 0", 4, signed=True),
            IntField("value1", "Value 1", 4, signed=True),
            TextField("text", "Text", 8),
        ], unique="uid"),
    ],
)
