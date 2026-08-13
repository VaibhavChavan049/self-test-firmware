/*
 * adc_test.h
 *
 * Runs one ADC test entry from test_config.h.
 */

#ifndef ADC_TEST_H
#define ADC_TEST_H

#include <stdint.h>
#include "test_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/* One-time ADC module bring-up. Call once from main() before any ADCTest_Run. */
void ADCTest_Init(void);

/*
 * Triggers a conversion on test->pin (SOC/channel number) and compares
 * the raw 12-bit result against [test->expectedMin, test->expectedMax].
 * *outValue receives the raw reading. Returns 1 = PASS, 0 = FAIL.
 */
uint8_t ADCTest_Run(const TestEntry_t *test, int32_t *outValue);

#ifdef __cplusplus
}
#endif

#endif /* ADC_TEST_H */
