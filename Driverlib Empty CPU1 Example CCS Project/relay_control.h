/*
 * relay_control.h
 *
 * Live, on-demand control of the DC bus contactor relay (K1, Relay_Driver
 * sheet) via Cont_Enable (GPIO14) - separate from the one-shot GPIO_OUT
 * test entry of the same name in test_config.h/g_testList. That entry still
 * runs as part of SelfTest_RunAll() and forces the relay CLOSED (so VBus_AD
 * has something real to read during a full test pass) - this module exists
 * so the GUI can toggle it independently, between full test runs, without
 * needing to start a whole test sequence just to see the relay's effect on
 * VBus_AD/DC_BUS.
 *
 * Buttons no longer live here: in Alexi's actual code the contactor is
 * just an output of the coil heating state machine (ButtonLOW/MEDIUM/
 * HIGH start heating and close it as part of that; ButtonON stops
 * heating and opens it) - see coil_control.c/.h, which owns all four
 * button interrupts and calls RelayControl_SetState() directly. This
 * module is now just the GPIO14 driver, also reachable independently via
 * the GUI's RELAY_SET command for bring-up/verification without needing
 * the coil running.
 */

#ifndef RELAY_CONTROL_H
#define RELAY_CONTROL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One-time GPIO bring-up. Call once from main() before any RelayControl_SetState.
 * Leaves the relay OPEN (0) - matches the safe default the rest of the
 * firmware assumes at boot, same as CoilControl_Init() starting OFF. */
void RelayControl_Init(void);

/* 1 = close the relay (connects DC+_P1 to DC+_P2), 0 = open it. */
void RelayControl_SetState(uint8_t closed);

/* Last state actually written via RelayControl_SetState (0 or 1). */
uint8_t RelayControl_GetState(void);

#ifdef __cplusplus
}
#endif

#endif /* RELAY_CONTROL_H */
