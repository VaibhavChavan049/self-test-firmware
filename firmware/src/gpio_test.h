/*
 * gpio_test.h
 *
 * Runs one GPIO test entry (input or output) from test_config.h.
 */

#ifndef GPIO_TEST_H
#define GPIO_TEST_H

#include <stdint.h>
#include "test_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Reads the pin and compares it to test->expectedValue.
 * *outValue receives the actual reading (for the UART TEST, line).
 * Returns 1 = PASS, 0 = FAIL.
 */
uint8_t GPIOTest_RunInput(const TestEntry_t *test, int32_t *outValue);

/*
 * Drives the pin to test->expectedValue, then reads it back (only
 * meaningful if the pin is looped back to an input somewhere on the
 * board - otherwise this just confirms the write didn't fault).
 * *outValue receives the read-back value. Returns 1 = PASS, 0 = FAIL.
 */
uint8_t GPIOTest_RunOutput(const TestEntry_t *test, int32_t *outValue);

#ifdef __cplusplus
}
#endif

#endif /* GPIO_TEST_H */
