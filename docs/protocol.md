# UART Protocol — Firmware ↔ GUI

Plain-ASCII, line-based (`\n` terminated), one message per line. Chosen
because it's trivial to log raw, debug with any serial terminal (PuTTY /
Tera Term / CCS terminal), and parse from Python without a binary framing
layer.

Default port settings: **115200 baud, 8N1, no flow control** (change in
`firmware/src/uart_comm.h` and `gui-app/serial_comm.py` together if needed —
keep both sides in sync).

## PC → Board

| Command | Meaning |
|---|---|
| `PING\n` | Liveness check. Board replies `PONG\n`. |
| `START\n` | Run the full test sequence now. |
| `STATUS\n` | (reserved) Ask board what it's currently doing — not required for v1. |
| `FAN_SET,<0-100>\n` | Set the fan's PWM duty cycle live (0 = off). Independent of the test sequence - can be sent any time, not just during `START`. Board replies `FAN_ACK,<percent>`. |

## Board → PC

| Message | Meaning |
|---|---|
| `READY\n` | Sent once at boot, before any test runs. Tells GUI the board is alive and idle. |
| `PONG\n` | Reply to `PING`. |
| `BEGIN,<test_count>\n` | Test sequence starting; `<test_count>` = how many `TEST,` lines to expect, so the GUI can drive a progress bar. |
| `TEST,<name>,<type>,<result>,<value>\n` | One parameter's outcome. See fields below. |
| `RESULT,OVERALL,<PASS\|FAIL>\n` | Final verdict, sent once after all `TEST,` lines. |
| `FAN_ACK,<percent>\n` | Reply to `FAN_SET` - confirms the duty cycle now applied. |

### `TEST,` field definitions

- `<name>` — matches `name` in the test config (e.g. `GPIO_IN_3`, `ADC_CH2`, `LED_STATUS`). No commas/spaces.
- `<type>` — one of `GPIO`, `ADC`, `LED` (extend as new types are added — keep it a short enum-like token, not free text).
- `<result>` — `PASS` or `FAIL`.
- `<value>` — the measured value (raw or converted), for logging/debug. `NA` if not applicable.

Example run:

```
READY
> START
BEGIN,5
TEST,GPIO_IN_0,GPIO,PASS,1
TEST,GPIO_IN_1,GPIO,PASS,0
TEST,ADC_CH0,ADC,PASS,2048
TEST,ADC_CH1,ADC,FAIL,4095
TEST,LED_STATUS,LED,PASS,NA
RESULT,OVERALL,FAIL
```

## Error / malformed line handling

Any line that doesn't match a known prefix is ignored by the GUI but still
written to the raw log, so nothing observed over the wire is silently lost.

## Future: physical switch box

When the switch box exists, the flow gains one more command *before*
`START`:

```
PC → Board: SETPIN,<name>,<value>\n   (assert an input via the switch box)
Board → PC: ACK,<name>\n
```

The switch box is driven by the PC (relay board / USB-controlled
switches), not by this firmware — the board firmware only needs to accept
`SETPIN` acks so a test's actual (switch-driven) value can be compared
against expected, instead of assuming a fixed default. No change to the
`TEST,`/`RESULT,` reporting format is needed; only the *stimulus* changes
from "fixed default" to "PC just set this."
