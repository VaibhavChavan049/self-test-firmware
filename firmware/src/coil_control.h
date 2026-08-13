/*
 * coil_control.h
 *
 * Deadtime-protected complementary PWM (PWM_Top/PWM_Bot = EPWM5A/EPWM5B,
 * confirmed against schematic + pin_map.h) with a button-driven
 * OFF/HEATING state machine, ported from Alexi's main.cpp deadtime +
 * button-ON architecture.
 *
 * *** SAFETY: NOT INCLUDED ***
 * Alexi's STATE_HEATING also arms CMPSS/DAC over-current protection
 * (CMPSS_setDACValueHigh(...)) before enabling the PWM outputs - that
 * setup wasn't part of what was shared, so it is NOT implemented here.
 * Do NOT connect this to real coil/power hardware until real
 * current-limit protection is added and verified - as written, entering
 * COIL_STATE_HEATING removes the forced-LOW safety state with no
 * over-current interlock behind it.
 */

#ifndef COIL_CONTROL_H
#define COIL_CONTROL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    COIL_STATE_OFF,
    COIL_STATE_HEATING
} CoilState_e;

/* One-time setup: deadtime config on EPWM5, button (GPIO2) interrupt. Starts in COIL_STATE_OFF. */
void CoilControl_Init(void);

/* Current state, e.g. for reporting over UART or in test output. */
CoilState_e CoilControl_GetState(void);

#ifdef __cplusplus
}
#endif

#endif /* COIL_CONTROL_H */
