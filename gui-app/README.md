# Self-Test GUI

Python/Tkinter desktop app. Connects to the board over UART, runs the test
sequence, shows live PASS/FAIL, and logs each run to `logs/`.

## Setup

```bash
cd gui-app
python -m venv venv
source venv/bin/activate        # Windows: venv\Scripts\activate
pip install -r requirements.txt
```

## Run

```bash
python app.py            # real board over serial
python app.py --mock     # no board attached - fakes results, for GUI dev
```

Tkinter ships with standard Python on macOS/Windows; on some Linux distros
install it separately (e.g. `sudo apt install python3-tk`).

## Files

- `app.py` — GUI: window, Start Test button, results table, overall
  GREEN/RED banner, CSV logging.
- `serial_comm.py` — serial transport + line-protocol parsing (background
  thread, queue handoff to the GUI thread) plus a `MockTestClient` for
  hardware-free development. Protocol is documented in
  [`../docs/protocol.md`](../docs/protocol.md).
- `logs/` — one CSV per test run (timestamp, overall result, per-parameter
  rows). Created automatically on first run; not checked in.

## Extending

Adding a new parameter needs zero changes here — the GUI just renders
whatever `TEST,` lines the firmware sends (see protocol doc). Only the
firmware's `test_config.h` and, for documentation, `docs/test_config_template.json`
need updating.

## Not yet implemented- ##push token = board2026xyz, https://self-test-dashboard.vercel.app/connect.html

- Board ID capture (protocol has no field for it yet — add a `BOARD_ID,`
  line if/when boards get a serial number to read).
- Physical switch-box control — see the "Future" section of
  `../docs/protocol.md`.


