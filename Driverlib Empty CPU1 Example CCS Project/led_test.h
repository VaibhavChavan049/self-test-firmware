/*
 * led_test.h
 *
 * Runs one LED test entry from test_config.h.
 *
 * NOTE: there's no physical switch box yet, so this can only confirm the
 * GPIO write to the LED pin succeeded (and reads it back if the pin
 * supports read-after-write) - it cannot confirm the LED actually lit.
 * See docs/protocol.md "Future: physical switch box" for how a light
 * sensor / operator-confirm step would slot in later.
 */

#ifndef LED_TEST_H
#define LED_TEST_H

#include <stdint.h>
#include "test_config.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8_t LEDTest_Run(const TestEntry_t *test, int32_t *outValue);

#ifdef __cplusplus
}
#endif

#endif /* LED_TEST_H */
