"""
Shared value-formatting logic - the pure, no-UI-framework half of the GUI.

Split out of app.py so relay.py (no Tkinter, runs headless to feed the
Vercel dashboard) can reuse the exact same temperature curve / VBus scale /
display-name rules instead of re-deriving them and risking drift between
the two front ends.
"""

# Must match firmware/src/adc_test.c's ADC_VREF_VOLTS and 12-bit ADC max count.
ADC_VREF_VOLTS = 3.3
ADC_MAX_COUNT = 4095

# VBus_AD-specific scale - confirmed against the real DCBUS_2 schematic sheet
# (1200K/12K divider, gain = R63/(R59+R61) = 1.68, matches this exactly).
_VBUS_ADC_SCALE = 3.3 / 4096
_VBUS_DIVIDER_TOP_R = 4 * 300.0
_VBUS_DIVIDER_BOT_R = 12.0
_VBUS_DIVIDER_GAIN = 1.68
VBUS_VOLTAGE_SCALE = _VBUS_ADC_SCALE * (_VBUS_DIVIDER_TOP_R + _VBUS_DIVIDER_BOT_R) / (
    _VBUS_DIVIDER_BOT_R * _VBUS_DIVIDER_GAIN
)
# Threshold to call the bus "energized" (1) vs not (0) - well above the few-volt
# noise floor seen with nothing connected, well below a real ~200-400V bus.
DC_BUS_PRESENT_THRESHOLD_V = 20.0

# Real calibration data (NTC.txt, from circuit simulation of the actual thermistor +
# op-amp divider circuit). V(vout) is the op-amp OUTPUT - the same node as the
# Temp*_AD net that goes to the MCU ADC pin - so V(vout) is what we interpolate
# against. 76 points, 25C-100C in 1C steps.
_TEMP_ADC_SCALE = 3.3 / 4096
_TEMP_CAL_C = [
    25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
    41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56,
    57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72,
    73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88,
    89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100,
]
_TEMP_CAL_VOUT = [
    0.4464374, 0.4784622, 0.5110626, 0.5442221, 0.5779223, 0.6121444,
    0.6468677, 0.6820708, 0.7177311, 0.7538250, 0.7903280, 0.8272147,
    0.8644590, 0.9020339, 0.9399120, 0.9780652, 1.0164650, 1.0550820,
    1.0938880, 1.1328530, 1.1719480, 1.2111430, 1.2504080, 1.2897150,
    1.3290350, 1.3683380, 1.4075960, 1.4467820, 1.4858690, 1.5248290,
    1.5636370, 1.6022670, 1.6406950, 1.6788970, 1.7168510, 1.7545350,
    1.7919270, 1.8290080, 1.8657600, 1.9021630, 1.9382010, 1.9738580,
    2.0091200, 2.0439720, 2.0784020, 2.1123980, 2.1460760, 2.1792610,
    2.2119820, 2.2442300, 2.2759980, 2.3072820, 2.3380750, 2.3683730,
    2.3981730, 2.4274730, 2.4562700, 2.4845620, 2.5122740, 2.5394850,
    2.5661940, 2.5924030, 2.6181130, 2.6433260, 2.6680460, 2.6922760,
    2.7160190, 2.7392800, 2.7620640, 2.7843760, 2.8062200, 2.8276010,
    2.8485270, 2.8690030, 2.8890340, 2.9086280,
]
TEMP_AD_NAMES = ("Temp1_AD", "Temp2_AD", "Temp_HS_AD")
PARAM_NAMES = ("VBus_AD", "Isens_AD", "Isens_Ref_AD")
# Momentary pushbuttons - shown separately at the top of I/O Test as
# "press to confirm" rows, since a button that's simply broken/disconnected
# would still read its idle level and false-PASS a plain compare-to-expected
# check. These only go PASS once an actual press (0, active-low) is observed.
BUTTON_NAMES = ("ButtonON", "ButtonLOW", "ButtonMEDIUM", "ButtonHIGH", "ButtonSILENT")

# Friendlier labels for the I/O Test panel, matching the whiteboard sketch.
# Anything not listed here just shows its real parameter name.
DISPLAY_NAME_OVERRIDES = {
    "Temp1_AD": "Temp 1",
    "Temp2_AD": "Temp 2",
    "Temp_HS_AD": "HS Test",
    "Cont_Enable": "Close Relay",
}


def _quadratic_through_3_points(x0, y0, x1, y1, x2, y2, x):
    """Lagrange quadratic through 3 known points, evaluated at x - used to
    extrapolate beyond the calibrated table using the curve's local shape
    (better than a flat clamp, still not real calibration data)."""
    l0 = (x - x1) * (x - x2) / ((x0 - x1) * (x0 - x2))
    l1 = (x - x0) * (x - x2) / ((x1 - x0) * (x1 - x2))
    l2 = (x - x0) * (x - x1) / ((x2 - x0) * (x2 - x1))
    return y0 * l0 + y1 * l1 + y2 * l2


def voltage_to_temp_c(v):
    """
    Linear interpolation against the real NTC.txt calibration data
    (25C-100C). Outside that range, EXTRAPOLATES using a quadratic fit
    through the nearest 3 calibrated points instead of clamping flat -
    per team request, so readings below 25C or above 100C still move
    live instead of sticking at a fixed number. This is an ESTIMATE
    outside [25C, 100C], not real calibration data - the further outside
    the range, the less trustworthy it gets.
    """
    table = _TEMP_CAL_VOUT
    temps = _TEMP_CAL_C
    n = len(table)

    if v <= table[0]:
        return _quadratic_through_3_points(
            table[0], temps[0], table[1], temps[1], table[2], temps[2], v)
    if v >= table[-1]:
        return _quadratic_through_3_points(
            table[-3], temps[-3], table[-2], temps[-2], table[-1], temps[-1], v)

    lo, hi = 0, n - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if table[mid] <= v:
            lo = mid
        else:
            hi = mid
    v0, v1 = table[lo], table[lo + 1]
    t0, t1 = temps[lo], temps[lo + 1]
    frac = (v - v0) / (v1 - v0)
    return t0 + frac * (t1 - t0)


def display_name(name):
    return DISPLAY_NAME_OVERRIDES.get(name, name)


def format_value(name, test_type, raw_value):
    """Raw counts for ADC rows get a computed-voltage (or temperature) suffix."""
    if test_type != "ADC":
        return raw_value
    try:
        raw_int = int(raw_value)
    except (ValueError, TypeError):
        return raw_value
    if name in TEMP_AD_NAMES:
        volts = raw_int * _TEMP_ADC_SCALE
        temp_c = voltage_to_temp_c(volts)
        return f"{temp_c:.1f}°C"
    # VBus_AD deliberately falls through to here, same as every other plain
    # ADC channel (Isens_AD etc) - it's the raw pin voltage (0-3.3V scale).
    # The reverse-calculated ~200V-scale bus voltage is a DIFFERENT number,
    # shown separately in the DC_BUS row (see dc_bus_row() below) - showing
    # that large-scale number here instead was the original source of
    # confusion this split was built to fix. Don't special-case VBus_AD
    # back to VBUS_VOLTAGE_SCALE here again.
    volts = raw_int / ADC_MAX_COUNT * ADC_VREF_VOLTS
    return f"{raw_value} ({volts:.2f}V)"


def dc_bus_row(vbus_raw_value):
    """Returns (present, volts) for the GUI-derived DC_BUS row, or None if
    vbus_raw_value isn't a parseable int. Shared by app.py and relay.py so
    both front ends compute this identically."""
    try:
        raw_int = int(vbus_raw_value)
    except (ValueError, TypeError):
        return None
    volts = raw_int * VBUS_VOLTAGE_SCALE
    present = 1 if volts >= DC_BUS_PRESENT_THRESHOLD_V else 0
    return present, volts
