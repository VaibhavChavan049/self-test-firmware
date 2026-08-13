/*
 * main.c
 *
 * Entry point. Bring-up sequence + command loop:
 *   boot -> init device/UART/ADC/fan/coil -> send READY -> wait for PC command
 *   "START" -> SelfTest_RunAll()   "PING" -> reply PONG
 *   "FAN_SET,<0-100>" -> Fan_SetDutyPercent(), reply FAN_ACK,<percent>
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

#define FAN_SET_PREFIX      "FAN_SET,"
#define FAN_SET_PREFIX_LEN  8U

#define CMD_MAXLEN 32U

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
    CurrentProtect_Init(); /* must arm before CoilControl_Init() can ever reach HEATING */
    CoilControl_Init();

    UART_sendReady();

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
            /* Unrecognized commands are silently ignored - PC side logs raw traffic regardless. */
        }
    }
}
