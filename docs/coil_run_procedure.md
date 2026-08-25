# Induction Coil — Run Procedure

This covers building, flashing, and safely running the induction coil control
(PWM + deadtime + button-driven heating + safety cutoffs). It's a companion
to `docs/README.md` (the general self-test flow) and `docs/protocol.md` (the
UART wire format) — this one is specifically about the coil/heating feature.

## 1. Build & Flash (CCS)

1. Open `Driverlib Empty CPU1 Example CCS Project` in Code Composer Studio.
2. Clean the project.
3. Build. Check the build **Output**/Console for `Finished building target:
   "...out"` with no compile/link errors — trust that over the Problems tab,
   which can show unrelated false positives.
4. Debug (flashes the board, opens a debug session — paused, not running yet).
5. Click **Resume** (▶ / F8). This step is easy to miss — without it the
   program is loaded but not actually executing.

## 2. Boot sequence — what to expect

- The board runs a **current-sensor calibration sweep** on boot, before
  anything else is armed. This can take up to **~1 minute**.
- **Do not press any button during this time** — nothing is listening yet.
- The board signals it's actually ready two ways, at the same moment:
  - A **long buzzer tick (~200ms)** — audible, no screen needed.
  - The text `READY` sent over UART (visible in the GUI's status bar as
    "Board ready", or in the Debug Console — see below).
- **Wait for one of these before touching any button.** This is the #1 cause
  of "I pressed the button and nothing happened."

## 3. Connecting the GUI

1. `cd gui-app && python3 app.py`
2. Select the board's serial port, click **Connect**.
3. Wait for the status bar to read **"Board ready."**

## 4. Button behavior

| Button | Effect |
|---|---|
| ButtonLOW / ButtonMEDIUM / ButtonHIGH | Starts heating at that mode — **only works if heating is currently OFF**. Also closes the DC bus contactor (relay) as part of the same action. |
| ButtonON | **Stops** heating (from HEATING or an ERROR state) and opens the contactor. Does **not** start heating — pressing it while OFF does nothing. |

Two audible signals confirm what happened on any press:

- **Short tick (10ms, buzzer)** — fires on *every* genuine press, whether or
  not it actually changed anything. This confirms firmware saw the press.
- **Click (relay)** — fires *only* when the contactor actually opens/closes,
  i.e. only on a real start/stop transition.

**Reading the combination:**
- Tick + click → button worked, heating state actually changed.
- Tick, no click → firmware saw the press but refused it (see Diagnostics
  below for why — most likely calibration or already-in-that-state).
- No tick at all → firmware never saw the press (board not ready yet, or a
  wiring/GPIO issue — not a logic problem).

## 5. Safety behavior

- **Deadtime**: 100ns gap enforced by the PWM hardware itself between the two
  switch outputs, on every switching edge, always active — not something
  that can be accidentally skipped or turned off in normal operation.
- **PWM**: fixed 45kHz, ~50% duty cycle. No frequency/resonance tracking
  ("angle finding") — deliberately not implemented, per team direction (see
  §6 below).
- **30-minute auto-shutoff**: heating force-stops on its own after 30 minutes
  even if nobody presses anything.
- **Over-current trip**: a hardware comparator (CMPSS) trip is polled every
  1ms while heating. On trip, heating force-stops, the contactor opens, and
  the board enters an **ERROR** state that only clears via ButtonON.

## 6. Diagnostics — Debug Console

The GUI has a **Debug Console** box at the bottom (text field + Send button,
with a log underneath). No separate terminal app needed.

- Type `COIL_STATUS`, press Enter → returns current state (OFF / HEATING /
  ERROR), heat mode, the calibrated current-sensor value, and whether that
  calibration is VALID or INVALID.
- Type `BUTTON_STATUS`, press Enter → returns how many genuine presses
  firmware has counted per button since boot. Press a button, query again,
  compare the count — confirms whether the press was actually detected.

## 7. What's intentionally NOT included (and why)

- **Angle-finding** (dynamic frequency/phase tracking to chase resonance) —
  skipped per team direction. Heat mode (LOW/MEDIUM/HIGH) is tracked for
  reporting but doesn't currently change the switching frequency, since that
  differentiation only ever came from the angle-finding logic being skipped.
- **Full RMS/peak current estimation** (the original reference design's
  DMA + ADC sine-capture pipeline) — not ported; this project's over-current
  protection is comparator-trip-based only (see §5), which is real
  protection but a different mechanism, not a drop-in equivalent.
- **HMI status messages / a separate debug UART frame protocol** — not
  applicable to this test fixture, which uses its own existing self-test
  UART protocol (`docs/protocol.md`) instead.

## Quick checklist

- [ ] Clean → Build → Debug → **Resume**
- [ ] Wait for the long tick / "Board ready" — not a fixed time estimate
- [ ] `python3 app.py`, Connect
- [ ] Press a heat button → listen for short tick + relay click
- [ ] If only a tick, no click → type `COIL_STATUS` in Debug Console, check why
- [ ] Press ButtonON to stop → listen for short tick + relay click (opens)
