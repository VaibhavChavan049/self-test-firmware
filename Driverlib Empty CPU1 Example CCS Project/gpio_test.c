/*
 * gpio_test.c
 *
 * GPIO_setDirectionMode / GPIO_setPadConfig / GPIO_readPin / GPIO_writePin
 * signatures confirmed against the real driverlib header installed locally
 * (~/ti/C2000Ware_26_01_00_00/driverlib/f28p65x/driverlib/gpio.h).
 *
 * No GPIO_setPinConfig (function mux select) call before these - checked
 * Device_initGPIO() (device_support/f28p65x/common/source/device.c): it
 * only unlocks pin config locks, it doesn't force any pin to a specific
 * function. GPIO is each pin's reset-default function on this device
 * unless something else claims it (JTAG, crystal, etc.) - none of our
 * test pins are on that list, so no extra mux call is needed here.
 */

#include "driverlib.h"
#include "device.h"
#include "gpio_test.h"

uint8_t GPIOTest_RunInput(const TestEntry_t *test, int32_t *outValue)
{
    uint32_t reading;

    GPIO_setDirectionMode(test->pin, GPIO_DIR_MODE_IN);
    GPIO_setPadConfig(test->pin, GPIO_PIN_TYPE_STD);

    reading = GPIO_readPin(test->pin);
    *outValue = (int32_t)reading;

    return (reading == (uint32_t)test->expectedValue) ? 1U : 0U;
}

uint8_t GPIOTest_RunOutput(const TestEntry_t *test, int32_t *outValue)
{
    uint32_t reading;

    GPIO_setDirectionMode(test->pin, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(test->pin, GPIO_PIN_TYPE_STD);
    GPIO_writePin(test->pin, (uint32_t)test->expectedValue);

    /* Small settle delay before reading back - TODO tune / confirm needed */
    DEVICE_DELAY_US(10);

    reading = GPIO_readPin(test->pin);
    *outValue = (int32_t)reading;

    return (reading == (uint32_t)test->expectedValue) ? 1U : 0U;
}
