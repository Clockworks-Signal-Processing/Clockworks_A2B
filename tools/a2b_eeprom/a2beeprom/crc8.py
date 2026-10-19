"""ADI's CRC-8, as the stack computes it (a2bcpp/src/crc8.cpp).

Polynomial 0x07 (x^8 + x^2 + x + 1), most significant bit first, starting value 0xFF, no
final XOR. ADI's PnP specification doesn't name the parameters; these are the ones ADI's
stack used, and they check against ADI's images in adi/module_info/.

Copyright 2026 by ClockWorks Signal Processing LLC
Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
SPDX-License-Identifier: BSD-3-Clause
"""

INIT = 0xFF
_POLYNOMIAL = 0x07


def _table():
    table = []
    for byte in range(256):
        crc = byte
        for _ in range(8):
            crc = ((crc << 1) ^ _POLYNOMIAL) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
        table.append(crc)
    return table


_TABLE = _table()


def crc8(data, crc=INIT):
    """ADI's CRC-8 of `data` (bytes or a list of ints); `crc` continues an earlier one."""
    for byte in data:
        crc = _TABLE[crc ^ byte]
    return crc
