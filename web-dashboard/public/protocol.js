// JS port of gui-app/formatting.py + gui-app/serial_comm.py's parse_line().
// Used only by connect.html (the page that talks to the board directly over
// Web Serial, with no Python in between). index.html doesn't need this - it
// just displays whatever JSON relay.py/connect.html already formatted.
//
// IMPORTANT: this is a manual port, not a shared import (Python and browser
// JS can't share a module). If you change the calibration table, VBus scale,
// or panel-routing rules in formatting.py, mirror the change here too.

const ADC_VREF_VOLTS = 3.3;
const ADC_MAX_COUNT = 4095;

const _VBUS_ADC_SCALE = 3.3 / 4096;
const _VBUS_DIVIDER_TOP_R = 4 * 300.0;
const _VBUS_DIVIDER_BOT_R = 12.0;
const _VBUS_DIVIDER_GAIN = 1.68;
const VBUS_VOLTAGE_SCALE = _VBUS_ADC_SCALE * (_VBUS_DIVIDER_TOP_R + _VBUS_DIVIDER_BOT_R) /
  (_VBUS_DIVIDER_BOT_R * _VBUS_DIVIDER_GAIN);
const DC_BUS_PRESENT_THRESHOLD_V = 20.0;

const _TEMP_ADC_SCALE = 3.3 / 4096;
const _TEMP_CAL_C = [
  25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
  41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56,
  57, 58, 59, 60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72,
  73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88,
  89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100,
];
const _TEMP_CAL_VOUT = [
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
];

const TEMP_AD_NAMES = ["Temp1_AD", "Temp2_AD", "Temp_HS_AD"];
const PARAM_NAMES = ["VBus_AD", "Isens_AD", "Isens_Ref_AD"];
const BUTTON_NAMES = ["ButtonON", "ButtonLOW", "ButtonMEDIUM", "ButtonHIGH", "ButtonSILENT"];
const DISPLAY_NAME_OVERRIDES = {
  Temp1_AD: "Temp 1",
  Temp2_AD: "Temp 2",
  Temp_HS_AD: "HS Test",
  Cont_Enable: "Close Relay",
};

function quadraticThrough3Points(x0, y0, x1, y1, x2, y2, x) {
  const l0 = (x - x1) * (x - x2) / ((x0 - x1) * (x0 - x2));
  const l1 = (x - x0) * (x - x2) / ((x1 - x0) * (x1 - x2));
  const l2 = (x - x0) * (x - x1) / ((x2 - x0) * (x2 - x1));
  return y0 * l0 + y1 * l1 + y2 * l2;
}

function voltageToTempC(v) {
  const table = _TEMP_CAL_VOUT;
  const temps = _TEMP_CAL_C;
  const n = table.length;

  if (v <= table[0]) {
    return quadraticThrough3Points(table[0], temps[0], table[1], temps[1], table[2], temps[2], v);
  }
  if (v >= table[n - 1]) {
    return quadraticThrough3Points(
      table[n - 3], temps[n - 3], table[n - 2], temps[n - 2], table[n - 1], temps[n - 1], v);
  }

  let lo = 0, hi = n - 1;
  while (hi - lo > 1) {
    const mid = Math.floor((lo + hi) / 2);
    if (table[mid] <= v) lo = mid; else hi = mid;
  }
  const v0 = table[lo], v1 = table[lo + 1];
  const t0 = temps[lo], t1 = temps[lo + 1];
  const frac = (v - v0) / (v1 - v0);
  return t0 + frac * (t1 - t0);
}

function displayName(name) {
  return DISPLAY_NAME_OVERRIDES[name] || name;
}

function formatValue(name, testType, rawValue) {
  if (testType !== "ADC") return rawValue;
  const rawInt = parseInt(rawValue, 10);
  if (Number.isNaN(rawInt)) return rawValue;
  if (TEMP_AD_NAMES.includes(name)) {
    const volts = rawInt * _TEMP_ADC_SCALE;
    const tempC = voltageToTempC(volts);
    return `${tempC.toFixed(1)}°C`;
  }
  // VBus_AD deliberately falls through to here, same as every other plain
  // ADC channel - raw pin voltage (0-3.3V scale). The reverse-calculated
  // ~200V-scale bus voltage is shown separately via dcBusRow() below.
  const volts = rawInt / ADC_MAX_COUNT * ADC_VREF_VOLTS;
  return `${rawValue} (${volts.toFixed(2)}V)`;
}

function dcBusRow(vbusRawValue) {
  const rawInt = parseInt(vbusRawValue, 10);
  if (Number.isNaN(rawInt)) return null;
  const volts = rawInt * VBUS_VOLTAGE_SCALE;
  const present = volts >= DC_BUS_PRESENT_THRESHOLD_V ? 1 : 0;
  return { present, volts };
}

function panelFor(name) {
  if (BUTTON_NAMES.includes(name)) return "buttons";
  if (TEMP_AD_NAMES.includes(name)) return "temperature";
  if (PARAM_NAMES.includes(name)) return "param";
  return "io";
}

// Mirrors serial_comm.py's parse_line() exactly - same tags, same field counts.
function parseLine(line) {
  line = line.trim();
  if (!line) return null;

  if (line === "READY") return { type: "ready" };
  if (line === "PONG") return { type: "pong" };

  const parts = line.split(",");
  const tag = parts[0];

  if (tag === "BEGIN" && parts.length === 2) {
    const count = parseInt(parts[1], 10);
    return Number.isNaN(count) ? null : { type: "begin", count };
  }

  if (tag === "TEST" && parts.length === 5) {
    const [, name, testType, result, value] = parts;
    return { type: "test_result", name, test_type: testType, result, value };
  }

  if (tag === "RESULT" && parts.length === 3 && parts[1] === "OVERALL") {
    return { type: "overall_result", result: parts[2] };
  }

  if (tag === "FAN_ACK" && parts.length === 2) {
    const percent = parseInt(parts[1], 10);
    return Number.isNaN(percent) ? null : { type: "fan_ack", percent };
  }

  return { type: "raw", line };
}
