"""Where everything is in a module-info image (ADI's PnP EEPROM format).

The format is ADI's "Plug and Play Content (Module Information) Specification"
(docs/ADI_PnP/A2B_PnP_EEPROM_ContentSpec.pdf) as ADI's own images (adi/module_info/*.dat)
and the stack (a2bcpp/src/module_info.cpp, module_script.cpp) have it. Where the PDF and the
images disagree, the images win: they are what SigmaStudio+ writes and EEPROMs in the field
hold. a2bcpp/docs/provisioning.md lists each such choice.

Every block and unit starts with a 3-byte header: byte 0 is the type (high nibble) and the
top 4 bits of a 12-bit length; byte 1 the length's low byte; byte 2 is reserved (0), except
in a write unit, where it is the peripheral's 7-bit I2C address. The length counts the
payload, its CRC-8 (its last byte) included, not the header. A block's CRC covers its header
and its payload up to the CRC. Multi-byte fields are low byte first, except the 12-bit
lengths and waits, a peripheral block's unit count, and the sub-address inside a write unit.

Copyright 2026 by ClockWorks Signal Processing LLC
Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
SPDX-License-Identifier: BSD-3-Clause
"""

MARKER = 0xAA
HEADER_CRC = 0x1F  # CRC-8 of bytes 0x00-0x1E.

# Header (0x00-0x1F).
VENDOR_ID = 0x01  # 16 bits.
PRODUCT_ID = 0x03  # 16 bits.
VERSION_ID = 0x05  # 16 bits (the stack reads the low byte as the silicon version).
TARGET_CHIP = 0x07  # 16 bits: the part number in hex (0x2437), 0x7FFF External, 0 not stated.
SW_VERSION = 0x09  # 4 bytes, most significant part first.
PRIMARY_FUNCTION = 0x0D  # 32 bits: 0x20.
SECONDARY_FUNCTION = 0x11  # 32 bits.

# Module-info block (type 3) at 0x20.
MODULE_INFO = 0x20
POWER_CONFIG = 0x23  # Bits 7:4 the configuration (4: CFG4), bit 0 local power.
MAX_POWER = 0x24  # 16 bits, mW.
MIN_POWER = 0x26  # 16 bits, mW.
CABLE_TYPE = 0x28  # The B port type: CABLE_TYPES.
MAX_CURRENT = 0x29  # 16 bits, mA.
DEVICE_NAME = 0x2B  # 60 bytes.
DEVICE_NAME_LEN = 60
DEVICE_TYPE = 0x67  # 32 bits, a capability per bit.
CABLE_LENGTH = 0x6C  # 16 bits: the B-side cable in metres; 0 not given.
PERIPHERAL_INFO_PTR = 0x6E  # 32 bits: 0x800.
DEVICE_FUNCTION_PTR = 0x76  # 32 bits: 0x300.
PERIPHERAL_COUNT = 0x7A
MIN_DOWN_SLOT = 0x7D  # 0-6: 8-32 bits in steps of 4; 7: no preference.
MIN_UP_SLOT = 0x7E
I2CCFG = 0x7F
REGS = 0x80  # 0x80-0x8F: the sub-node's register settings (registers.py).
SPI_TUNNEL = 0x90  # 0 no, 1 yes.
SPI_TUNNEL_DOWN = 0x91  # Minimum, maximum.
SPI_TUNNEL_UP = 0x93  # Minimum, maximum.
BRANCHES = 0x95  # 0-4.
LOCAL_MICRO = 0x96
LOCAL_MICRO_MODE = 0x97
LOCAL_MICRO_PERIPHERALS = 0x98
SUPPLIER_INFO_PTR = 0x99  # 32 bits: 0x400.
GROUP_TABLE = 0x100  # TX groups' count and records, then the RX groups'.
GROUP_RECORD_LEN = 37  # ID, 32-byte name, 32-bit channel bitmap.
GROUP_NAME_LEN = 32
MAX_GROUPS = 15  # Per direction, in the format.
STACK_MAX_GROUPS = 8  # Per direction, kept by the stack (ChannelGroups::kMaxGroups).

# The two 256-byte vendor blocks. ADI's images put their CRC at the block's last byte
# (base + 0xFF, over base..base + 0xFE) whatever their length says; this tool writes the
# length 0xFD, so both readings agree.
DEVICE_FUNCTION = 0x300  # Type 7.
SUPPLIER_INFO = 0x400  # Type 8.
FIXED_BLOCK_LEN = 0x100
FIXED_BLOCK_PAYLOAD = 0xFD
SUPPLIER_VENDOR_ID = 0x403  # 8 ASCII characters.
SUPPLIER_VENDOR_ID_LEN = 8
SUPPLIER_RESERVED = 0x40B  # 32 bytes.
SUPPLIER_DATA = 0x42B  # The vendor's own data, to the CRC at 0x4FF.
SUPPLIER_DATA_LEN = 0x4FF - 0x42B  # 212 bytes.

# Peripherals.
PERIPHERAL_INFO = 0x800  # Type 4: a 32-bit address per peripheral block, then a CRC.
PERIPHERAL_BLOCK_PAYLOAD = 80  # Type 5.
PERIPHERAL_NAME_LEN = 64
STACK_MAX_PERIPHERALS = 8  # ModuleScript::kMaxBlocks.

# Block and unit types.
TYPE_WRITE = 1  # Write unit, with a CRC (type 0: without; read, never written).
TYPE_WRITE_NO_CRC = 0
TYPE_DELAY = 2
TYPE_MODULE_INFO = 3
TYPE_PERIPHERAL_INFO = 4
TYPE_PERIPHERAL = 5
TYPE_DEVICE_FUNCTION = 7
TYPE_SUPPLIER_INFO = 8

MAX_LENGTH = 0xFFF  # 12 bits.
MAX_WRITE = MAX_LENGTH - 1  # Bytes a write unit writes (sub-address and data), its CRC aside.
MAX_DELAY_MS = 0xFFF

# Interfaces of a peripheral block.
INTERFACE_I2C = 0
INTERFACE_SPI = 1
INTERFACE_NONE = 0xFF  # Not programmable.

# The format's ceiling (16-bit EEPROM addresses) and Clockworks' EEPROM (24FC256).
MAX_IMAGE = 0x10000
DEFAULT_EEPROM_SIZE = 0x8000

# The B port types (0x28): the specification's 0-2, then Clockworks' 3-5; 6 and up reserved.
# The stack's link for each: a2bcpp/src/link_profile.cpp (LinkProfile::ForBPort()).
CABLE_TYPES = {0: "XLR", 1: "RJ45", 2: "UTP", 3: "RJ45-CUSTOM", 4: "RJ45-CUSTOM-INV", 5: "CUSTOM"}
CABLE_TYPE_HINTS = {
    0: "ADI's XLR: two-step on an AD2437, else UTP",
    1: "ADI's RJ45: two-step, B side inverted, on an AD2437; else UTP",
    2: "Single-step, power on the pair, B side straight",
    3: "An RJ45 not ADI's, B side straight (the stack can't run it yet)",
    4: "An RJ45 not ADI's, B side inverted (the stack can't run it yet)",
    5: "Something else (the stack can't run it yet)",
}
TWO_STEP_CABLES = (0, 1)  # Two-step on an AD2437.
CUSTOM_CABLES = (3, 4, 5)  # No handler in the stack yet: "Unsupported B port type".
MAX_CABLE_M = 30  # The AD2437 data sheet's longest link (a warning only); the AD242x's is 15 m.
MAX_CABLE_M_AD242X = 15

# Clockworks' module vendor ID (0x01): "CSP". Its product IDs are board numbers (AB0020: 0x0020).
CLOCKWORKS_MODULE_VENDOR = 0x0C54
DEVICE_TYPE_BITS = {
    0: "PDM microphone (digital)",
    1: "Microphone (analog)",
    2: "Audio out",
    3: "Audio in",
    4: "MIDI device",
    5: "Sensor",
}
SLOT_SIZES = {0: "8 bits", 1: "12 bits", 2: "16 bits", 3: "20 bits", 4: "24 bits",
              5: "28 bits", 6: "32 bits", 7: "no preference"}


def get16(image, at):
    """A 16-bit field, low byte first."""
    return image[at] | (image[at + 1] << 8)


def get32(image, at):
    """A 32-bit field, low byte first."""
    return image[at] | (image[at + 1] << 8) | (image[at + 2] << 16) | (image[at + 3] << 24)


def put16(image, at, value):
    """Sets a 16-bit field, low byte first."""
    image[at] = value & 0xFF
    image[at + 1] = (value >> 8) & 0xFF


def put32(image, at, value):
    """Sets a 32-bit field, low byte first."""
    for i in range(4):
        image[at + i] = (value >> (8 * i)) & 0xFF


def block_header(block_type, length, third=0):
    """A block's or unit's 3-byte header."""
    return bytes([((block_type & 0x0F) << 4) | ((length >> 8) & 0x0F), length & 0xFF, third])


def header_type(header):
    """The type in a block's or unit's header."""
    return header[0] >> 4


def header_length(header):
    """The 12-bit length in a block's or unit's header."""
    return ((header[0] & 0x0F) << 8) | header[1]
