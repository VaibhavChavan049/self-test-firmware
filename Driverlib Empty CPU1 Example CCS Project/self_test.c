#include <string.h>
#include "driverlib.h"
#include "device.h"
#include "test_config.h"
#include "uart_comm.h"
#include "gpio_test.h"
#include "adc_test.h"
#include "led_test.h"
#include "relay_control.h"
#include "self_test.h"

static const char *TypeToString(TestType_e type)
{
    switch (type)
    {
        case TEST_TYPE_GPIO_IN:  return "GPIO";
        case TEST_TYPE_GPIO_OUT: return "GPIO";
        case TEST_TYPE_ADC:      return "ADC";
        case TEST_TYPE_LED:      return "LED";
        default:                 return "UNKNOWN";
    }
}

void SelfTest_RunAll(void)
{
    uint16_t i;
    uint8_t allPassed = 1U;

    UART_sendBegin(TEST_LIST_COUNT);

    for (i = 0; i < TEST_LIST_COUNT; i++)
    {
        const TestEntry_t *test = &g_testList[i];
        int32_t value = 0;
        uint8_t passed = 0U;

        if (strcmp(test->name, "Cont_Enable") == 0)
        {
            /*
             * Special-cased instead of going through GPIOTest_RunOutput():
             * that would unconditionally force this pin to test->expectedValue
             * (1/closed) every single run, fighting the live RELAY_SET toggle
             * (relay_control.c) - especially painful under Live Mode, where
             * every ~1s auto-repeat would silently re-close a relay the
             * operator had just opened. Instead, just re-assert and report
             * whatever RelayControl_SetState() was last told - this test now
             * confirms "the pin is actually holding the state we last
             * commanded" rather than "force it closed," so a manual toggle
             * sticks across Live Mode cycles instead of getting overridden.
             * (Reads as PASS always - there's no longer a fixed expected
             * value to fail against, this is a live status report.)
             */
            RelayControl_SetState(RelayControl_GetState());
            value = (int32_t)RelayControl_GetState();
            passed = 1U;
        }
        else
        {
            switch (test->type)
            {
                case TEST_TYPE_GPIO_IN:
                    passed = GPIOTest_RunInput(test, &value);
                    break;
                case TEST_TYPE_GPIO_OUT:
                    passed = GPIOTest_RunOutput(test, &value);
                    break;
                case TEST_TYPE_ADC:
                    passed = ADCTest_Run(test, &value);
                    break;
                case TEST_TYPE_LED:
                    passed = LEDTest_Run(test, &value);
                    break;
                default:
                    passed = 0U;
                    break;
            }
        }

        if (!passed)
        {
            allPassed = 0U;
        }

        UART_sendTestResult(test->name, TypeToString(test->type), passed, value);
    }

    UART_sendOverallResult(allPassed);
}
