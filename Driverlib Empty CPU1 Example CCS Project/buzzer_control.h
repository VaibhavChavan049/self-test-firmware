/*
 * buzzer_control.h
 *
 * GPIO_BUZZ (GPIO85) - simple digital-drive buzzer, HIGH = buzzing,
 * LOW = silent. Used for two audible checkpoints so bench testing doesn't
 * require watching a screen: a boot-ready tick (main.c, once calibration
 * + all init is done and buttons are actually armed) and a button-press
 * tick (coil_control.c, on every genuine debounced press, whether or not
 * it actually changed state) - see buzzer_control.c for why button ticks
 * are kept short.
 */

#ifndef BUZZER_CONTROL_H
#define BUZZER_CONTROL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One-time GPIO bring-up. Call once from main() before any BuzzerControl_Pulse. */
void BuzzerControl_Init(void);

/* Blocking: drives the buzzer HIGH for durationMs, then LOW. See file header for ISR-context caveats. */
void BuzzerControl_Pulse(uint16_t durationMs);

#ifdef __cplusplus
}
#endif

#endif /* BUZZER_CONTROL_H */
