/*
 * current_protect.h
 *
 * Over-current trip protection for the current-sensor signal (Isens_AD,
 * analog pin A1) using the CMPSS1 hardware comparator - ported from
 * Alexi's CMPSS/DAC setup (Code/DSP/main.cpp), which coil_control.c's
 * COIL_STATE_HEATING relies on but did not itself implement (see the
 * "SAFETY: NOT INCLUDED" note in coil_control.h - this file fills that
 * gap, partially - see the note in current_protect.c about what is
 * still missing).
 */

#ifndef CURRENT_PROTECT_H
#define CURRENT_PROTECT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One-time CMPSS bring-up + calibration sweep. Call once from main() before relying on trip status.
 * Takes up to ~1 minute (matches Alexi's algorithm) - CoilControl_Init() must run AFTER this so
 * the coil stays forced-off for that whole window regardless. */
void CurrentProtect_Init(void);

/* The DAC trip threshold this board calibrated to (2000-2120 = healthy; 2048 = uncalibrated fallback). */
uint16_t CurrentProtect_GetCalibratedValue(void);

/*
 * 1 if the calibrated value landed in Alexi's expected healthy range
 * [2000, 2120], else 0 (current-sensor reference has too much offset -
 * matches his post-calibration check). coil_control.c refuses to enter
 * COIL_STATE_HEATING when this is 0.
 */
uint8_t CurrentProtect_IsCalibrationValid(void);

/* 1 if the comparator has latched an over-threshold trip since the last clear, else 0. */
uint8_t CurrentProtect_IsTripped(void);

/* Clears the latched trip flag (e.g. after handling/reporting it). */
void CurrentProtect_ClearTrip(void);

#ifdef __cplusplus
}
#endif

#endif /* CURRENT_PROTECT_H */
