#include "driverlib.h"
#include "device.h"
#include "test_config.h"
#include "uart_comm.h"
#include "gpio_test.h"
#include "adc_test.h"
#include "led_test.h"
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

        if (!passed)
        {
            allPassed = 0U;
        }

        UART_sendTestResult(test->name, TypeToString(test->type), passed, value);
    }

    UART_sendOverallResult(allPassed);
}
