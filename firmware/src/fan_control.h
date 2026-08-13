/*
 * fan_control.h
 *
 * Drives FAN_PWM (GPIO0 / EPWM1A, per Fan_Ckt schematic sheet) so the GUI
 * can turn the fan on/off and adjust its speed live, separate from the
 * one-shot pass/fail self-test sequence.
 */

#ifndef FAN_CONTROL_H
#define FAN_CONTROL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One-time ePWM bring-up. Call once from main() before any Fan_SetDutyPercent. */
void Fan_Init(void);

/*
 * Sets fan speed as a duty cycle percentage (0-100). 0 = fan off.
 * Values above 100 are clamped to 100.
 */
void Fan_SetDutyPercent(uint16_t percent);

#ifdef __cplusplus
}
#endif

#endif /* FAN_CONTROL_H */
