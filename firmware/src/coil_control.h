/*
 * coil_control.h
 *
 * Induction coil control, ported from Alexi's actual main.cpp state
 * machine (Code/DSP/main.cpp, Tucena-main repo) - re-expressed in plain C
 * to match this project's style (his version is C++ with template
 * classes this project doesn't have). Behavior matches his real button
 * semantics, confirmed by reading his source directly rather than going
 * from memory:
 *   - ButtonLOW / ButtonMEDIUM / ButtonHIGH (only from COIL_STATE_OFF)
 *     START heating at the corresponding heat mode, AND close the DC bus
 *     contactor (RelayControl_SetState(1)) as part of that transition -
 *     in his code the contactor is just an output of the heating state,
 *     not a separately-toggled thing.
 *   - ButtonON only STOPS heating (from HEATING or ERROR back to OFF) and
 *     opens the contactor - it does NOT start heating. This is a
 *     deliberate correction from this project's earlier version, which
 *     had ButtonON toggle both ways - Alexi's actual code never does
 *     that.
 *   - relay_control.c no longer owns ButtonLOW/MEDIUM/HIGH's GPIO
 *     interrupts - that ownership moved here since starting/stopping
 *     heating and opening/closing the contactor are now one coordinated
 *     action, exactly like his code.
 *
 * *** NOT PORTED, PER TEAM DIRECTION ***
 * "Angle finding" - his closed-loop resonance/frequency tracking (the
 * m_phase_correction-seeking block driven by ECAP zero-cross phase
 * capture) - is deliberately excluded. This runs a fixed 45kHz, ~50%
 * duty PWM only; LOW/MEDIUM/HIGH are tracked for reporting but don't yet
 * change the switching frequency, since that differentiation in his code
 * only ever came from the angle-finding loop we're skipping.
 *
 * *** SAFETY STATUS ***
 * Over-current protection is now real: current_protect.c's CMPSS
 * hardware comparator trip latch is polled every 1ms (see the CPU Timer0
 * ISR in coil_control.c) and forces COIL_STATE_ERROR (outputs forced low,
 * contactor opened) on trip - notably this auto-poll-and-stop wiring is
 * MORE than Alexi's own code does (his CMPSS trip is armed but nothing
 * in his source automatically acts on it either - confirmed by reading
 * his main.cpp). What's still NOT ported: his DMA+ADC sine-capture-based
 * RMS/peak current estimation and the ECAP-based power-factor safety
 * check - both need his mains-voltage sampling pipeline (DMA, ECAP,
 * a dedicated sampling timer) that this project doesn't have set up yet.
 * A global 30-minute auto-shutoff (his exact number) is also ported.
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
    COIL_STATE_HEATING,
    COIL_STATE_ERROR
} CoilState_e;

typedef enum
{
    COIL_HEAT_LOW,
    COIL_HEAT_MEDIUM,
    COIL_HEAT_HIGH
} CoilHeatMode_e;

/*
 * One-time setup: base PWM + deadtime on EPWM5, GATEDRV enable pin,
 * CPU Timer0 (1ms tick, drives the 30-min timeout + over-current poll),
 * and the four button interrupts (ButtonON/LOW/MEDIUM/HIGH). Starts in
 * COIL_STATE_OFF. Call AFTER CurrentProtect_Init() and RelayControl_Init()
 * (main.c already sequences it this way).
 */
void CoilControl_Init(void);

/* Current state, e.g. for reporting over UART or in test output. */
CoilState_e CoilControl_GetState(void);

/* Last-selected heat mode (meaningful once state has been COIL_STATE_HEATING at least once). */
CoilHeatMode_e CoilControl_GetHeatMode(void);

/*
 * Number of genuine (debounced) presses seen by each button's interrupt,
 * since boot - counts up regardless of whether the press actually changed
 * state (e.g. LOW pressed while already HEATING still counts here, even
 * though startHeating() ignores it). Useful for telling "firmware never
 * saw the press" (count stuck at 0) apart from "firmware saw it but
 * didn't act" (count increments, check CoilControl_GetState() /
 * CurrentProtect_IsCalibrationValid() for why).
 */
void CoilControl_GetButtonPressCounts(uint32_t *onCount, uint32_t *lowCount, uint32_t *mediumCount, uint32_t *highCount);

#ifdef __cplusplus
}
#endif

#endif /* COIL_CONTROL_H */
