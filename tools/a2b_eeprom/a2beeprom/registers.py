"""The sub-node register settings a module-info image carries (0x7F-0x8F), bit by bit.

The fields are the TRMs': the AD2426/7/8 Technical Reference (docs/ADI_chips/AD242x/
ad242x-trm.pdf, chapter 7) for the AD242x family, the AD2437 Technical Reference (rev 0.2,
docs/ADI_chips/AD243x/ad2437-trm-0.2.pdf, chapter 11) for the AD243x family. They differ in
I2SCFG (pin enables on the AD242x, pin counts on the AD243x), I2SGCFG bit 3, PDMCTL2's
high-pass corner, PINCFG's GPIO mode, I2CCFG's upper bits, and the SPI registers (AD243x
only). The stack writes 0x80-0x8F in its own order and skips a value equal to the
register's power-on value (a2bcpp/src/module_info.cpp). Of I2CCFG (0x7F) it takes only
DATARATE: the node's I2C port moves to 400 kHz after the image's start is read at 100 kHz.

Copyright 2026 by ClockWorks Signal Processing LLC
Licensed under the terms found in CLOCKWORKS_SOFTWARE_LICENSE.txt and included here by reference.
SPDX-License-Identifier: BSD-3-Clause
"""

AD242X = "AD242x"
AD243X = "AD243x"
BOTH = (AD242X, AD243X)


class Chip:
    """A transceiver the stack supports, as a sub-node."""

    def __init__(self, name, product, family, has_i2s, has_spi, power_config):
        self.name = name
        self.product = product  # PRODUCT register.
        self.family = family
        self.has_i2s = has_i2s  # An I2S/TDM port; the AD2427 has only its PDM inputs.
        self.has_spi = has_spi
        # Its power configuration, the only one it has, which the stack uses in place of the
        # image's (a2bcpp/src/chip_caps.cpp): CFG4 on the AD2437 (its data sheet), CFG0 on the
        # AD2433 (AD2431W-35W data sheet, Table 1) and the AD242x, which have none.
        self.power_config = power_config


CHIPS = [
    Chip("AD2427", 0x27, AD242X, False, False, 0),
    Chip("AD2428", 0x28, AD242X, True, False, 0),
    Chip("AD2433", 0x33, AD243X, True, True, 0),
    Chip("AD2437", 0x37, AD243X, True, True, 4),
]


# The image's target chip (0x07) when it names none: a plug-in transceiver module, so the board
# with the EEPROM doesn't decide the chip (0x7FFF); or not stated (0x0000, as ADI's images).
EXTERNAL = "External"
NOT_STATED = "Not stated"
CHIP_EXTERNAL = 0x7FFF


def find_chip(name):
    """The Chip named `name` ("AD2437"), or None."""
    for chip in CHIPS:
        if chip.name.lower() == str(name).lower():
            return chip
    return None


def chip_code(name):
    """The target-chip field (0x07) for `name`: 0x2437 for "AD2437", 0x7FFF for External, 0
    for not stated (or a name that isn't a part number)."""
    if name == EXTERNAL:
        return CHIP_EXTERNAL
    text = str(name)
    if len(text) == 6 and text[:2].upper() == "AD":
        try:
            return int(text[2:], 16)
        except ValueError:
            pass
    return 0


def chip_name(code):
    """The target chip a field (0x07) gives: "AD2437", EXTERNAL or NOT_STATED."""
    if code in (0, 0xFFFF):
        return NOT_STATED
    if code == CHIP_EXTERNAL:
        return EXTERNAL
    return "AD{:04X}".format(code)


def guess_family(product_id):
    """A family from a module product ID that is a chip's (ADI's images: 0x0037), or None."""
    for chip in CHIPS:
        if product_id == chip.product:
            return chip.family
    return None


class Field:
    """Bits msb..lsb of a register, with their meaning per value (None: a number)."""

    def __init__(self, name, msb, lsb, label, values=None, families=BOTH):
        self.name = name
        self.msb = msb
        self.lsb = lsb
        self.label = label
        self.values = values
        self.families = families

    @property
    def mask(self):
        return ((1 << (self.msb - self.lsb + 1)) - 1) << self.lsb

    @property
    def width(self):
        return self.msb - self.lsb + 1

    def get(self, value):
        return (value & self.mask) >> self.lsb

    def put(self, value, field_value):
        return (value & ~self.mask & 0xFF) | ((field_value << self.lsb) & self.mask)

    def describe(self, value):
        """The field's value in words."""
        v = self.get(value)
        if self.values is not None:
            return self.values.get(v, "reserved ({})".format(v))
        if self.width == 1:
            return "on" if v else "off"
        return str(v)


class Register:
    """A register setting of the image."""

    def __init__(self, key, name, offset, power_on, label, fields, families=BOTH):
        self.key = key  # The project file's name for it.
        self.name = name
        self.offset = offset
        self.power_on = power_on
        self.label = label
        self.fields = fields
        self.families = families

    def fields_for(self, family):
        return [f for f in self.fields if family in f.families]

    def defined_mask(self, family):
        mask = 0
        for f in self.fields_for(family):
            mask |= f.mask
        return mask

    def describe(self, value, family):
        """One line per field; for the IO registers, one line naming the pins set."""
        fields = self.fields_for(family)
        if all(f.name.startswith("IO") and f.width == 1 for f in fields):
            pins = ["IO{}".format(f.lsb) for f in fields if f.get(value)]
            return ["pins: " + (", ".join(pins) if pins else "none")]
        return ["{} = {}".format(f.name, f.describe(value)) for f in fields]


_TDM_MODES = {0: "TDM2", 1: "TDM4", 2: "TDM8", 3: "TDM12 (no sub-node support)", 4: "TDM16",
              5: "TDM20 (no sub-node support)", 6: "TDM24 (no sub-node support)", 7: "TDM32"}
_PINS = {0: "none", 1: "1 pin", 2: "2 pins", 3: "3 pins", 4: "4 pins",
         7: "2 pins, interleaved"}
_BCLK_RATES = {0: "as I2SGCFG gives", 1: "SYNC x 2048", 2: "SYNC x 4096", 4: "SFF x 64",
               5: "SFF x 128", 6: "SFF x 256"}
_I2S_RATES = {0: "SFF x 1", 1: "SFF / 2 (reduced)", 2: "SFF / 4 (reduced)",
              3: "SFF / RRDIV (reduced)", 5: "SFF x 2", 6: "SFF x 4"}
_PDM_RATES = {0: "SFF", 1: "SFF / 2", 2: "SFF / 4"}
_MONO_STEREO = {0: "mono (1 mic)", 1: "stereo (2 mics)"}
_HPF_CORNERS = {0: "1 Hz", 1: "60 Hz", 2: "120 Hz", 3: "240 Hz"}
_PDM_DEST = {0: "A2B bus only", 1: "DTX/SIO pins only", 2: "A2B bus and DTX/SIO pins"}
_PRE_DIVIDE = {0: "pre-divide 2", 1: "pre-divide 32"}
_GPIO_MODES = {0: "GPIO on SPI pins", 2: "GPIO on I2C, IO0 on SIO0",
               3: "GPIO on I2C, IO0 on SIO4"}
_SPI_MODES = {0: "SPI slave", 1: "data tunnel target (SPI master)", 2: "SPI disabled"}
_SPI_FD_SS = {0: "SPISS0n (ADR1) with command bytes", 1: "SPISS1n (SIO2), no command bytes",
              2: "SPISS2n (ADR2), no command bytes"}


def _io_bits(suffix, label):
    return [Field("IO{}{}".format(i, suffix), i, i, "{} IO{}".format(label, i))
            for i in range(7, -1, -1)]


def _clock(n):
    return [
        Field("CLK{}EN".format(n), 7, 7, "CLKOUT{} enabled".format(n)),
        Field("CLK{}INV".format(n), 6, 6, "CLKOUT{} inverted".format(n)),
        Field("CLK{}PDIV".format(n), 5, 5, "Pre-divide", _PRE_DIVIDE),
        Field("CLK{}DIV".format(n), 3, 0, "Divide (2 x (n + 1))"),
    ]


REGISTERS = [
    Register("i2ccfg", "I2CCFG", 0x7F, 0x00, "I2C configuration (only DATARATE is used)", [
        Field("DISI2C", 7, 7, "I2C pins used as GPIO", families=(AD243X,)),
        Field("FMPLUS", 3, 3, "Fast-mode Plus (1 MHz)", families=(AD243X,)),
        Field("FRAMERATE", 2, 2, "Frame rate", {0: "48 kHz", 1: "44.1 kHz"}),
        Field("EACK", 1, 1, "Early acknowledge (main node only)"),
        Field("DATARATE", 0, 0, "I2C data rate after the image is read",
              {0: "100 kHz", 1: "400 kHz"}),
    ]),
    Register("spicfg", "SPICFG", 0x80, 0x00, "SPI configuration", [
        Field("SPIFDSS", 7, 6, "Full-duplex slave select", _SPI_FD_SS),
        Field("ENFDCS", 5, 5, "Full-duplex clock stretching"),
        Field("SPI_CPOL", 4, 4, "Clock polarity"),
        Field("SPI_CPHA", 3, 3, "Clock phase"),
        Field("TNLOWNER", 2, 2, "Data tunnel owner"),
        Field("SPIMODE", 1, 0, "SPI mode", _SPI_MODES),
    ], families=(AD243X,)),
    Register("spickdiv", "SPICKDIV", 0x81, 0x00, "SPI clock divide", [
        Field("CKDIV", 5, 0, "SCK period = bit time x (n + 1)"),
    ], families=(AD243X,)),
    Register("spipincfg", "SPIPINCFG", 0x82, 0x00, "SPI pin configuration", [
        Field("SPIMSS2EN", 6, 6, "Master slave select 2"),
        Field("SPIMSS1EN", 5, 5, "Master slave select 1"),
        Field("SPIMSS0EN", 4, 4, "Master slave select 0"),
        Field("SPIGPIOEN", 3, 3, "SPIBUSY on a GPIO"),
        Field("SPIGPIOSEL", 2, 0, "SPIBUSY GPIO"),
    ], families=(AD243X,)),
    Register("i2scfg", "I2SCFG", 0x83, 0x00, "I2S/TDM pins", [
        Field("RXBCLKINV", 7, 7, "RX sampled on BCLK's falling edge"),
        Field("RX2PINTL", 6, 6, "RX two-pin interleave", families=(AD242X,)),
        Field("RX1EN", 5, 5, "DRX1 enabled", families=(AD242X,)),
        Field("RX0EN", 4, 4, "DRX0 enabled", families=(AD242X,)),
        Field("RXPINS", 6, 4, "RX pins", _PINS, families=(AD243X,)),
        Field("TXBCLKINV", 3, 3, "TX driven on BCLK's falling edge"),
        Field("TX2PINTL", 2, 2, "TX two-pin interleave", families=(AD242X,)),
        Field("TX1EN", 1, 1, "DTX1 enabled", families=(AD242X,)),
        Field("TX0EN", 0, 0, "DTX0 enabled", families=(AD242X,)),
        Field("TXPINS", 2, 0, "TX pins", _PINS, families=(AD243X,)),
    ]),
    Register("i2sgcfg", "I2SGCFG", 0x84, 0x00, "I2S/TDM format", [
        Field("INV", 7, 7, "SYNC falling edge starts the frame"),
        Field("EARLY", 6, 6, "SYNC one cycle early"),
        Field("ALT", 5, 5, "SYNC 50% duty (I2S)"),
        Field("TDMSS", 4, 4, "Channel size", {0: "32-bit", 1: "16-bit"}),
        Field("RXONDTX1", 3, 3, "RX on the DTX1 pin", families=(AD242X,)),
        Field("SYNCDIS", 3, 3, "SYNC pin disabled", families=(AD243X,)),
        Field("TDMMODE", 2, 0, "TDM mode", _TDM_MODES),
    ]),
    Register("i2srate", "I2SRATE", 0x85, 0x00, "I2S/TDM rate", [
        Field("SHARE", 7, 7, "Share bus slots (reduced rate)"),
        Field("REDUCE", 6, 6, "Reduce and duplicate"),
        Field("BCLKRATE", 5, 3, "BCLK", _BCLK_RATES),
        Field("I2SRATE", 2, 0, "Rate", _I2S_RATES),
    ]),
    Register("pdmctl", "PDMCTL", 0x86, 0x00, "PDM microphones", [
        Field("PDMRATE", 6, 5, "PDM sample rate", _PDM_RATES),
        Field("HPFEN", 4, 4, "High-pass filter"),
        Field("PDM1SLOTS", 3, 3, "PDM1 channels", _MONO_STEREO),
        Field("PDM1EN", 2, 2, "PDM1 enabled"),
        Field("PDM0SLOTS", 1, 1, "PDM0 channels", _MONO_STEREO),
        Field("PDM0EN", 0, 0, "PDM0 enabled"),
    ]),
    Register("pdmctl2", "PDMCTL2", 0x87, 0x00, "PDM clock and data", [
        Field("HPFCORNER", 7, 6, "High-pass corner", _HPF_CORNERS, families=(AD243X,)),
        Field("PDMINVCLK", 5, 5, "Inverted alternate PDM clock"),
        Field("PDMALTCLK", 4, 4, "PDM clock on IO7"),
        Field("PDM1FFRST", 3, 3, "PDM1 falling edge first"),
        Field("PDM0FFRST", 2, 2, "PDM0 falling edge first"),
        Field("PDMDEST", 1, 0, "PDM destination", _PDM_DEST),
    ]),
    Register("clk1cfg", "CLK1CFG", 0x88, 0x00, "CLKOUT1 (ADR1)", _clock(1)),
    Register("clk2cfg", "CLK2CFG", 0x89, 0x00, "CLKOUT2 (ADR2)", _clock(2)),
    Register("gpiooen", "GPIOOEN", 0x8A, 0x00, "GPIO outputs", _io_bits("OEN", "Output")),
    Register("gpioien", "GPIOIEN", 0x8B, 0x00, "GPIO inputs", _io_bits("IEN", "Input")),
    Register("pinten", "PINTEN", 0x8C, 0x00, "Pin interrupts", _io_bits("IE", "Interrupt")),
    Register("pintinv", "PINTINV", 0x8D, 0x00, "Pin interrupt inversion",
             _io_bits("INV", "Inverted")),
    Register("pincfg", "PINCFG", 0x8E, 0x01, "Pin configuration", [
        Field("GPIOSEL", 7, 6, "GPIO mode", _GPIO_MODES, families=(AD243X,)),
        Field("IRQTS", 5, 5, "IRQ three-state when inactive"),
        Field("IRQINV", 4, 4, "IRQ active low"),
        Field("DRVSTR", 0, 0, "Drive strength", {0: "low", 1: "high"}),
    ]),
    Register("gpiodat", "GPIODAT", 0x8F, 0x00, "GPIO output levels", _io_bits("DAT", "High")),
]


def find_register(key):
    """The Register with project key `key` ("i2scfg"), or None."""
    for reg in REGISTERS:
        if reg.key == key:
            return reg
    return None


def default_registers():
    """Every register at its power-on value, as a project holds them."""
    return {reg.key: reg.power_on for reg in REGISTERS}


TDM_CHANNELS = {0: 2, 1: 4, 2: 8, 4: 16, 7: 32}


def port_summary(regs, family):
    """The I2S/TDM and PDM settings in a few words, for the GUI and the decoder."""
    i2sgcfg = regs["i2sgcfg"]
    i2scfg = regs["i2scfg"]
    pdmctl = regs["pdmctl"]
    mode = i2sgcfg & 0x07
    size = 16 if i2sgcfg & 0x10 else 32
    channels = TDM_CHANNELS.get(mode)
    if family == AD242X:
        tx = (i2scfg & 0x01) + ((i2scfg >> 1) & 0x01)
        rx = ((i2scfg >> 4) & 0x01) + ((i2scfg >> 5) & 0x01)
    else:
        tx_field = i2scfg & 0x07
        rx_field = (i2scfg >> 4) & 0x07
        tx = 2 if tx_field == 7 else tx_field
        rx = 2 if rx_field == 7 else rx_field
    parts = []
    if tx or rx:
        parts.append("{} {}-bit, {} TX pin(s), {} RX pin(s)".format(
            _TDM_MODES[mode].split(" ")[0], size, tx, rx))
        if channels is not None:
            parts.append("{} channels per pin".format(channels))
    else:
        parts.append("I2S/TDM off")
    mics = 0
    if pdmctl & 0x01:
        mics += 2 if pdmctl & 0x02 else 1
    if pdmctl & 0x04:
        mics += 2 if pdmctl & 0x08 else 1
    if mics:
        parts.append("PDM: {} microphone(s)".format(mics))
    return ", ".join(parts)


# A board whose transceiver is a plug-in module (target chip External) takes one set of
# settings for either family. Most fields read alike; these don't:
# - I2SCFG: pin enables on the AD242x, pin counts on the AD243x. Off, or one pin each way
#   (DTX0, DRX0), reads alike: I2SCFG_SHARED below.
# - I2SGCFG bit 3: RXONDTX1 on the AD242x, SYNCDIS on the AD243x (CONFLICT_BITS).
# - Bits the AD242x hasn't got (AD242X_UNUSED_BITS): the stack writes them clear on an AD242x
#   (a2bcpp/src/node_setup.cpp), and leaves out the SPI registers, which it hasn't got either.
I2SCFG_SHARED = [
    Field("RXBCLKINV", 7, 7, "RX sampled on BCLK's falling edge"),
    Field("RX", 6, 4, "RX pin", {0: "off", 1: "DRX0"}),
    Field("TXBCLKINV", 3, 3, "TX driven on BCLK's falling edge"),
    Field("TX", 2, 0, "TX pin", {0: "off", 1: "DTX0"}),
]
CONFLICT_BITS = {"i2sgcfg": 0x08}
AD242X_UNUSED_BITS = {"pdmctl2": 0xC0, "pincfg": 0xC0}


def shared_fields(reg):
    """The fields of `reg` that read alike on both families (none for an AD243x register)."""
    if reg.key == "i2scfg":
        return I2SCFG_SHARED
    if reg.families != BOTH:
        return []
    return [f for f in reg.fields if f.families == BOTH]


def ad243x_only_fields(reg):
    """The fields of `reg` only the AD243x has, on bits the AD242x leaves unused (or a whole
    AD243x register): an AD242x module doesn't get them."""
    if reg.families == (AD243X,):
        return list(reg.fields)
    if reg.key in ("i2scfg",) or reg.key in CONFLICT_BITS:
        return []
    return [f for f in reg.fields if f.families == (AD243X,)]


def i2scfg_shared(value):
    """True if I2SCFG `value` reads alike on both families: off or one pin each way."""
    return (value & 0x77) in (0x00, 0x01, 0x10, 0x11)


# The format names a peripheral's A2B pins by the AD243x's SIO pins (its RX and TX interface
# fields: bits 7:5 the pin count, 4:0 SIO0-SIO4). Which stream each SIO pin carries depends on
# the PDM pins (AD2437 TRM, Table 5-5, "Configuration Modes"); the AD242x's DRXn and DTXn are
# taken as the AD243x's of the same name.
_SIO_TX = {"DTX0": 4, "DTX1": 3, "DTX2": 2, "DTX3": 1}
_SIO_RX = {  # (PDM0EN, PDM1EN): the SIO pin of DRX0, DRX1, ...
    (0, 0): (0, 1, 2, 3),
    (1, 0): (1, 2, 3),
    (0, 1): (0, 2, 3),
    (1, 1): (2, 3),
}


def sio_streams(regs):
    """The stream each SIO pin carries for these settings: {"rx": {sio: "DRX0"}, "tx": ...}."""
    pdm = regs["pdmctl"]
    rx_pins = _SIO_RX[(pdm & 0x01, (pdm >> 2) & 0x01)]
    return {
        "rx": {sio: "DRX{}".format(n) for n, sio in enumerate(rx_pins)},
        "tx": {sio: name for name, sio in _SIO_TX.items()},
    }


def enabled_streams(regs, family, external=False):
    """The data streams the settings turn on: ({"DRX0", ...}, {"DTX0", ...}). With a plug-in
    module (`external`), only those both families read alike."""
    i2scfg = regs["i2scfg"]
    if external:
        if not i2scfg_shared(i2scfg):
            return set(), set()
        rx = 1 if i2scfg & 0x10 else 0
        tx = 1 if i2scfg & 0x01 else 0
        return {"DRX{}".format(n) for n in range(rx)}, {"DTX{}".format(n) for n in range(tx)}
    if family == AD242X:
        rx = {"DRX{}".format(n) for n in range(2) if i2scfg >> (4 + n) & 1}
        tx = {"DTX{}".format(n) for n in range(2) if i2scfg >> n & 1}
        return rx, tx
    rx_count = (i2scfg >> 4) & 0x07
    tx_count = i2scfg & 0x07
    rx_count = 2 if rx_count == 7 else rx_count
    tx_count = 2 if tx_count == 7 else tx_count
    return ({"DRX{}".format(n) for n in range(rx_count)},
            {"DTX{}".format(n) for n in range(tx_count)})
