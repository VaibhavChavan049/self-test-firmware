# Board Self-Test Project Overview

## Problem

Boards come from the contract manufacturer (CM) with components stuffed only 
no functional testing on their side. Today, verification is done manually,
circuit-level, with no processor involvement. Slow and labor-intensive.

## Goal

An automated self-test system with two halves:

1. **Firmware** (runs on the board's own processor, `F28P650DK6PZP`)  exercises
   GPIOs, ADC channels, LEDs (and future parameters), and reports PASS/FAIL per
   parameter over UART.
2. **PC GUI** (Python) employee plugs in the board, hits "Start Test", watches
   results come in live, gets one overall GREEN (all pass) / RED (something
   failed) verdict, and every run is logged.

This is a brand-new, independent flow it does not touch or replace the
existing manual circuit-level test process.

## Production flow

1. Fresh board from CM arrives on the line.
2. Employee flashes the self-test firmware (via CCS / flash tool).
3. Employee connects board to PC over USB/UART and opens the GUI.
4. Employee clicks **Start Test**.
5. Firmware runs every configured test, streams results over UART as it goes.
6. GUI shows live PASS/FAIL per parameter, then one overall verdict.
7. Result is logged (timestamp, board ID if available, per-parameter outcome).
8. GREEN → board marked verified/production-ready. RED → board flagged, sent
   for rework/debug.

## Design principles

- **Scalable, not hardcoded.** Both sides use a *list of test descriptors*
  (name, type, pin, expected value/range) rather than one-off if/else code.
  Adding a new parameter later = adding one entry, not writing new logic.
  Firmware side: `firmware/src/test_config.h` (compile-time array, see notes
  in that file about a future flash/EEPROM-config variant). GUI/reference
  side: `docs/test_config_template.json`.
- **Fixed values for now, switch-box-ready later.** Right now every test
  checks against a fixed default expected value/range (no physical stimulus).
  The protocol and architecture leave room for a future physical switch box
  that the software can command (e.g., "assert this input now") see
  `docs/protocol.md` for how that slots in without a redesign.
- **Two independent, loosely-coupled halves.** Firmware only needs to speak
  the line protocol in `docs/protocol.md`; the GUI only needs to parse it.
  Either side can be reworked without touching the other, as long as the
  protocol contract holds.

## Folder map

- [`firmware/`](../firmware/) TI Code Composer Studio (CCS) project source
  for the `F28P650DK6PZP`. See `firmware/README.md` for CCS setup and TI-Rex
  reference examples.
- [`gui-app/`](../gui-app/) Python (Tkinter) GUI skeleton. See
  `gui-app/README.md` to run it.
- [`docs/`](.) this overview, the UART protocol contract
  (`protocol.md`), and the test-parameter config template
  (`test_config_template.json`).

## Status

Skeleton stage. Pin numbers, ADC channel assignments, and exact expected
values/ranges are placeholders until the schematic / final parameter list is
available search for `TODO` across `firmware/src/test_config.h` and
`docs/test_config_template.json`.
