# Clockworks boards and modules

Which Clockworks boards and A2B modules go together, which link each pair runs, and what
that means for the stack and the examples. The board profiles are in
`examples/clockworks/clockworks_boards.cpp`, the EEPROM images in `tools/a2b_eeprom/examples`;
what the chips do is in the [hardware notes](hardware_notes.md).

## Base boards

| Board | Role | A2B connectors | IO7 |
|---|---|---|---|
| AB0020 | Main or sub-node; an ADAU1761 codec | UTP on both ports | An output: the board's LED |
| AB0310 | Main node; SYNC and BCLK from its own clock | RJ45, and headers for a plug-in module | An input: the board's power-good |
| AB0312 | Sub-node for the AB0310; I2S on a header, no codec | RJ45, and headers for a plug-in module | An input: the board's power-good |
| AB0204 | Sub-node; an AD2427 soldered down, four PDM microphones | UTP | Not used |

The AB0020, AB0310 and AB0312 carry their EEPROM on the base board and take the transceiver
on a plug-in module, so their images name no chip (target chip External) and the stack
reads the chip from the module.

## Modules

| Module | Transceiver | Power configuration | Link |
|---|---|---|---|
| AB0371 | AD2437 | CFG-4 | RJ45: the only module for RJ45 links. Two-step |
| AB0331 | AD2437 | CFG-4 | UTP, single-step |
| AB0001 | AD2428 | CFG-0 (none) | UTP, single-step |
| AB0333 | AD2433 | CFG-0 | UTP, single-step |

The UTP modules plug into the AB0310 and AB0312 too, for features the AB0020 lacks. On those
boards a UTP module uses its own UTP connector; the board's RJ45 connectors are not used for
A2B then.

## What follows for the stack

- **Two-step runs only behind an AB0371.** An RJ45 link always joins two AD2437 nodes on
  AB0371 modules, so the node behind it is always CFG-4. The stack keeps a node that isn't
  CFG-4 at 5 V ([decisions](decisions.md#interconnects)), but no Clockworks board puts one
  there: that path is checked on the simulator only.
- **The AB0312's image says RJ45** (its B port, `AB0312.dat` and the `AB0312` profile), as
  it is built for the AB0371. `LinkProfile::ForBPort()` gives UTP for it on an AD2428 or
  AD2433, so an AB0001 or AB0333 on an AB0312 runs UTP as it should. The AB0331 has the same
  chip as the AB0371, and the EEPROM on the base board can't tell the two apart: the stack
  runs the link after an AB0312 with an AB0331 two-step and inverted, which is wrong. Set
  that link (the bench console's `link N utp`), or write that board's EEPROM with an image
  whose B port is UTP.
- **IO7.** A two-step link reads the power-good of the board before it on GPIO7
  (`LinkProfile::powerGoodGpio`), the AB0312's IO7; the main node's comes from
  `Platform::PowerGood()`. IO7 is an input on every board but the AB0020, whose IO7 drives
  its LED: the `AB0312` profile and `AB0312.dat` leave it an input (GPIOIEN 0x80), and the
  examples' `clockworks::BoardNode` (the bench console, the fixed app) makes it an input on
  every node whose module info doesn't name the AB0020, whatever its image says.
- **The AB0310's main node** takes its settings from its SigmaStudio+ export
  (`tools/SS+reference/AB0310_AB0312_EVM_with_2437`), which has IO7 as an input on the main
  node and on its sub-node, as the `AB0312` profile has it.
