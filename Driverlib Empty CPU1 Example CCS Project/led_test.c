/*
 * led_test.c
 *
 * Reuses the same GPIO write/read-back pattern as gpio_test.c - an LED is
 * electrically just a GPIO output. Kept as a separate module (rather than
 * folded into gpio_test.c) because it's a conceptually distinct test
 * category per docs/test_config_template.json, and because real
 * verification (does it visually light) will need different logic later
 * once a light sensor or the physical switch box exists.
 */

#include "driverlib.h"
#include "device.h"
#include "led_test.h"

uint8_t LEDTest_Run(const TestEntry_t *test, int32_t *outValue)
{
    uint32_t reading;

    GPIO_setDirectionMode(test->pin, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(test->pin, GPIO_PIN_TYPE_STD);
    GPIO_writePin(test->pin, (uint32_t)test->expectedValue);

    DEVICE_DELAY_US(10);

    reading = GPIO_readPin(test->pin);
    *outValue = (int32_t)reading;

    /*
     * PASS here only means "the write took" (read-back matches what we
     * drove) - not "the LED visibly lit". See header comment.
     */
    return (reading == (uint32_t)test->expectedValue) ? 1U : 0U;
}
