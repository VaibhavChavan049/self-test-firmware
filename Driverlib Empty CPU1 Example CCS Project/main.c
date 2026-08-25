/*
 * main.c
 *
 * Entry point. Bring-up sequence + command loop:
 *   boot -> init device/UART/ADC/fan/coil/relay -> send READY -> wait for PC command
 *   "START" -> SelfTest_RunAll()   "PING" -> reply PONG
 *   "FAN_SET,<0-100>" -> Fan_SetDutyPercent(), reply FAN_ACK,<percent>
 *   "RELAY_SET,<0|1>" -> RelayControl_SetState(), reply RELAY_ACK,<0|1>
 *     (live/independent toggle of Cont_Enable - see relay_control.h. Note
 *     SelfTest_RunAll() still force-closes this same pin via the Cont_Enable
 *     GPIO_OUT test entry every time a full test runs, so it'll snap back
 *     to closed the next time "START" runs, regardless of this toggle)
 *
 * CoilControl_Init() (coil_control.c) sets up the PWM_Top/PWM_Bot deadtime
 * and the ButtonON (GPIO2)-driven OFF/HEATING toggle - that state change
 * happens inside its own interrupt handler, not from anything in this
 * file's command loop, so it keeps working even while this loop is
 * blocked inside UART_readLine() waiting for the next PC command.
 *
 * Bring-up sequence below is CONFIRMED against the real TI-Rex reference
 * examples installed locally (C2000Ware 26.01.00.00,
 * driverlib/f28p65x/examples/c28x/{adc,sci}/) - both adc_ex1_soc_software.c
 * and sci_ex3_echoback.c use exactly this hand-written driverlib sequence
 * (Device_init -> Device_initGPIO -> Interrupt_initModule ->
 * Interrupt_initVectorTable -> peripheral setup -> EINT/ERTM), not
 * SysConfig/Board_init() - only the gpio_ex2_toggle.c example used
 * SysConfig, and that's not needed for our hand-configured pins.
 */

#include <string.h>
#include <stdlib.h>
#include "driverlib.h"
#include "device.h"
#include "uart_comm.h"
#include "adc_test.h"
#include "self_test.h"
#include "fan_control.h"
#include "coil_control.h"
#include "current_protect.h"
#include "relay_control.h"
#include "buzzer_control.h"

/* Boot-ready tick duration - long enough to be clearly noticed, safe to block this long since nothing is heating yet. */
#define BUZZER_READY_PULSE_MS  200U

#define FAN_SET_PREFIX        "FAN_SET,"
#define FAN_SET_PREFIX_LEN    8U
#define RELAY_SET_PREFIX      "RELAY_SET,"
#define RELAY_SET_PREFIX_LEN  10U

#define CMD_MAXLEN 32U

static const char *coilStateToString(CoilState_e state)
{
    switch (state)
    {
        case COIL_STATE_OFF:     return "OFF";
        case COIL_STATE_HEATING: return "HEATING";
        case COIL_STATE_ERROR:   return "ERROR";
        default:                 return "UNKNOWN";
    }
}

static const char *coilHeatModeToString(CoilHeatMode_e mode)
{
    switch (mode)
    {
        case COIL_HEAT_LOW:    return "LOW";
        case COIL_HEAT_MEDIUM: return "MEDIUM";
        case COIL_HEAT_HIGH:   return "HIGH";
        default:               return "UNKNOWN";
    }
}

int main(void)
{
    char cmdBuf[CMD_MAXLEN];

    Device_init();
    Device_initGPIO();

    Interrupt_initModule();
    Interrupt_initVectorTable();

    EINT;
    ERTM;

    UART_init();
    ADCTest_Init();
    Fan_Init();
    RelayControl_Init();
    BuzzerControl_Init();
    CurrentProtect_Init(); /* must arm before CoilControl_Init() can ever reach HEATING */
    CoilControl_Init();

    UART_sendReady();
    /*
     * Audible "board is actually ready" checkpoint - fires only after
     * calibration AND button interrupts are armed, matching UART_sendReady()
     * exactly. Bench-friendly alternative to watching the serial terminal:
     * wait for this tick, THEN press buttons, not before.
     */
    BuzzerControl_Pulse(BUZZER_READY_PULSE_MS);

    for (;;)
    {
        if (UART_readLine(cmdBuf, CMD_MAXLEN))
        {
            if (strcmp(cmdBuf, "START") == 0)
            {
                SelfTest_RunAll();
            }
            else if (strcmp(cmdBuf, "PING") == 0)
            {
                UART_sendPong();
            }
            else if (strncmp(cmdBuf, FAN_SET_PREFIX, FAN_SET_PREFIX_LEN) == 0)
            {
                uint16_t percent = (uint16_t)atoi(cmdBuf + FAN_SET_PREFIX_LEN);
                Fan_SetDutyPercent(percent);
                UART_sendFanAck(percent);
            }
            else if (strncmp(cmdBuf, RELAY_SET_PREFIX, RELAY_SET_PREFIX_LEN) == 0)
            {
                uint16_t state = (uint16_t)atoi(cmdBuf + RELAY_SET_PREFIX_LEN);
                RelayControl_SetState((uint8_t)state);
                UART_sendRelayAck(RelayControl_GetState());
            }
            else if (strcmp(cmdBuf, "COIL_STATUS") == 0)
            {
                UART_sendCoilStatus(coilStateToString(CoilControl_GetState()),
                                     coilHeatModeToString(CoilControl_GetHeatMode()),
                                     CurrentProtect_GetCalibratedValue(),
                                     CurrentProtect_IsCalibrationValid());
            }
            else if (strcmp(cmdBuf, "BUTTON_STATUS") == 0)
            {
                uint32_t onCount, lowCount, mediumCount, highCount;
                CoilControl_GetButtonPressCounts(&onCount, &lowCount, &mediumCount, &highCount);
                UART_sendButtonStatus(onCount, lowCount, mediumCount, highCount);
            }
            /* Unrecognized commands are silently ignored - PC side logs raw traffic regardless. */
        }
    }
}
