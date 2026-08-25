/*
 * relay_control.c
 *
 * GPIO14 (Cont_Enable) direct drive - same pin, same direction/pad config
 * pattern as GPIOTest_RunOutput() in gpio_test.c, just callable on demand
 * instead of only once per full self-test pass. Confirmed against
 * driverlib/f28p65x/driverlib/gpio.h (same calls already verified for the
 * GPIO_OUT test path).
 *
 * Purely a low-level GPIO14 driver now - ButtonLOW/MEDIUM/HIGH no longer
 * touch this pin directly. In Alexi's actual code (Code/DSP/main.cpp,
 * read directly), the contactor is just an output of the coil heating
 * state machine, not something separately toggled by those buttons - so
 * that ownership moved to coil_control.c, which now calls
 * RelayControl_SetState() as part of starting/stopping heating. This
 * module still exists so the GUI's RELAY_SET command can drive the pin
 * directly for bring-up/verification independent of the coil.
 */

#include "driverlib.h"
#include "device.h"
#include "relay_control.h"

#define RELAY_GPIO   14U

static uint8_t s_relayState = 0U;

void RelayControl_Init(void)
{
    GPIO_setDirectionMode(RELAY_GPIO, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(RELAY_GPIO, GPIO_PIN_TYPE_STD);

    s_relayState = 0U;
    GPIO_writePin(RELAY_GPIO, s_relayState);
}

void RelayControl_SetState(uint8_t closed)
{
    s_relayState = (closed != 0U) ? 1U : 0U;
    GPIO_writePin(RELAY_GPIO, s_relayState);
}

uint8_t RelayControl_GetState(void)
{
    return s_relayState;
}
