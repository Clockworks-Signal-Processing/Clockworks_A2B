# Golden I2C traces

I2C traces of ADI's stack, the reference the C++ stack was checked against ([testing
guide](../../docs/testing.md), "Golden traces", for the list G1-G13). How to read and compare
them: [`a2bcpp/tools/README.md`](../../tools/README.md). Each scenario has two runs, and the two
runs must compare equal with `trace_diff.py`.

Names are `<scenario>_<platform>_run<n>.txt`.

| Files | Scenario | Platform | Result |
|---|---|---|---|
| `boot4_pi_run1.txt`, `boot4_pi_run2.txt` | Boot with a chain of 4 sub-nodes, all with EEPROM module info (vendor 0xAD, product 0x37); startup table defaults (`configType 0`, EEPROM used); idle probing for a fifth node afterwards | Pi (Linux arm64), ADI's stack with its SUSTAIN fix | EQUAL (both about 35 s) |
| `G13_pi_run1.txt`, `G13_pi_run2.txt` | **G13:** boot with `ignoreEEPROM 1` and a chain of 4 sub-nodes: discovered with default settings and no EEPROM reads; idle probing afterwards | Pi (Linux arm64), ADI's stack with its SUSTAIN fix, `ignoreEEPROM 1` in the startup table | EQUAL (both about 20 s) |

Both pairs were captured after ADI's stack was fixed to wait out SUSTAIN after the main node's
soft reset on the first discovery. An earlier capture, from before the fix, differed:

- **boot4:** the same writes, but INV and MSTR came 0.1 ms after the soft reset instead of
  about 250 ms. The default `mingap` rules catch this: with the old run as the trace to check,
  `trace_diff.py` reports both writes as too early ([timing.md](../../docs/timing.md)).
- **G13:** the earlier runs were restarts without a power cycle, so the main node was still in
  SUSTAIN when it was configured. Those runs found only the first sub-node; these find all four.

Each run shows three failed reads of register 0x0F at the bus address, one after each sub-node
except the last. They are part of ADI's stack's behaviour.

Only `boot4_pi_run1.txt` is read by a test (`test_sim_module_info`); the expected values in
`test_sim_g13_image` and `test_ss_plus_import` come from G13. The `run2` files are the second run
of each pair, for `trace_diff.py`.

Check a pair from the repository root:

```
python a2bcpp/tools/trace_diff.py a2bcpp/tests/golden/boot4_pi_run1.txt a2bcpp/tests/golden/boot4_pi_run2.txt
```
