/*
 * buzzer_control.c
 *
 * GPIO85 (GPIO_BUZZ) - same pin already verified physically working via
 * the self-test's GPIO_OUT entry (test_config.h: "will audibly sound
 * during this test"). Simple digital drive, no PWM/tone generation.
 *
 * BuzzerControl_Pulse() blocks (DEVICE_DELAY_US) - fine to call from
 * main.c with any duration. Callers using it from inside an interrupt
 * (coil_control.c's button ISRs, for audible press feedback) must keep
 * durationMs small: ButtonON/ButtonLOW's ISRs (XINT2/XINT1) share PIE
 * group 1 with CPU Timer0's 1ms over-current poll, so a long pulse in
 * one of those two delays that poll by the same amount. Kept to 10ms for
 * exactly this reason - see the callers in coil_control.c.
 *
 * Known interaction: if the self-test's GPIO_OUT entry for GPIO_BUZZ ran
 * (via "START"), it can leave this pin HIGH afterward (that generic test
 * just writes-and-verifies, doesn't restore prior state). A pulse called
 * right after still audibly toggles it, so this isn't a functional
 * problem - just noted here in case exact on/off timing is ever load-
 * bearing somewhere.
 */

#include "driverlib.h"
#include "device.h"
#include "buzzer_control.h"

#define BUZZER_GPIO   85U

void BuzzerControl_Init(void)
{
    GPIO_setDirectionMode(BUZZER_GPIO, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(BUZZER_GPIO, GPIO_PIN_TYPE_STD);
    GPIO_writePin(BUZZER_GPIO, 0U);
}

void BuzzerControl_Pulse(uint16_t durationMs)
{
    GPIO_writePin(BUZZER_GPIO, 1U);
    DEVICE_DELAY_US((uint32_t)durationMs * 1000UL);
    GPIO_writePin(BUZZER_GPIO, 0U);
}
