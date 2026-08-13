/*
 * adc_test.c
 *
 * Software-triggered single-conversion ADC read, modeled on the
 * "adc_ex1_soc_software" style example that ships with C2000Ware for most
 * C2000 parts.
 *
 * Schematic (Tucena, sheet /MCU/) splits analog inputs across three
 * separate pin groups - A (Isens_AD, Temp1_AD, ...), B (Isens_Ref_AD,
 * Vsens_AD, ...), C (Temp2_AD, Temp_HS_AD, VBus_AD) - each its own ADC
 * SOC module. test_config.h's AdcModule_e records which one each entry
 * needs; g_adcModuleRegs below maps that to the actual module base
 * addresses.
 *
 * CONFIRMED against C2000Ware 26.01.00.00 installed locally
 * (~/ti/C2000Ware_26_01_00_00/driverlib/f28p65x/driverlib/adc.h +
 * .../inc/hw_memmap.h) and against adc_ex1_soc_software.c in that same
 * install: ADC_setupSOC/ADC_forceSOC/ADC_getInterruptStatus/
 * ADC_readResult signatures, ADCA_BASE/ADCB_BASE/ADCC_BASE +
 * ADCxRESULT_BASE macros, and ADC_CH_ADCINn = n (0-based, sequential) -
 * so test->pin cast directly to ADC_Channel is correct, no lookup table
 * needed.
 *
 * Still open: sample window cycles (line below) is a reasonable default,
 * not tuned to the actual source impedance of each sensor net - only
 * matters if readings look noisy/settle slowly once running on real
 * hardware.
 */

#include "driverlib.h"
#include "device.h"
#include "adc_test.h"

#define ADC_TEST_SOC_NUM   ADC_SOC_NUMBER0
/*
 * Raised from 15 -> 100 (driverlib valid range is 1-512, confirmed via
 * ADC_setupSOC()'s ASSERT). VBus_AD's source impedance (through R58/R60 on
 * the DCBUS_2 sheet) is higher than the thermistor circuits' dedicated
 * op-amp buffer outputs - 15 cycles may not have been enough acquisition
 * time for that specific net to settle before the SAR sampled it, especially
 * right after two other channel conversions on the same SOC/module. Real
 * hardware voltage at the pin (measured at TP18: 2.651V) doesn't match what
 * the ADC was reporting (pegged at 4095) - this is the one remaining
 * untested, legitimate explanation before concluding it's a board defect.
 */
#define ADC_TEST_SAMPLE_WINDOW_CYCLES 100U

/*
 * DIAGNOSTIC CHANGE: now using the chip's INTERNAL reference at 3.3V
 * (see ADC_setVREF call below) instead of the external "3V_REF" header,
 * since that header's components appear not stuffed on this board. This
 * constant now directly matters (unlike EXTERNAL mode, INTERNAL mode
 * does use the refVoltage argument) - used to convert raw ADC counts to
 * real volts: volts = rawCount / 4095.0f * ADC_VREF_VOLTS.
 */
#define ADC_VREF_VOLTS 3.3f

typedef struct
{
    uint32_t base;
    uint32_t resultBase;
} AdcModuleRegs_t;

/* Order MUST match AdcModule_e in test_config.h (A, B, C). */
static const AdcModuleRegs_t g_adcModuleRegs[] =
{
    { ADCA_BASE, ADCARESULT_BASE },
    { ADCB_BASE, ADCBRESULT_BASE },
    { ADCC_BASE, ADCCRESULT_BASE },
};

void ADCTest_Init(void)
{
    uint16_t i;

    /*
     * DIAGNOSTIC CHANGE: switched from EXTERNAL to INTERNAL reference.
     * Schematic shows VREFHI/VREFLO wired to an external reference header
     * (H1, "3V_REF"), but the very first schematic screenshot showed R10
     * and R4 near that header as 0-ohm AND crossed out (not stuffed) on
     * this physical board. With EXTERNAL selected but no real external
     * reference actually populated, the ADC reference pin floats -
     * matches exactly what we saw: nearly every channel pegged at 4095
     * regardless of real input (multimeter confirmed real voltage was
     * only tens/hundreds of mV, nowhere near full-scale). Trying INTERNAL
     * (chip's own bandgap, no external component needed) to see if raw
     * counts start reflecting real input. If board is later confirmed to
     * have H1 populated, switch back to EXTERNAL.
     */
    ADC_setVREF(ADCA_BASE, ADC_REFERENCE_INTERNAL, ADC_REFERENCE_3_3V);

    /*
     * CONFIRMED against the real MCU sheet (Tucena schematic, /MCU/):
     * Temp2_AD (ADC-C ch0) and Temp_HS_AD (ADC-C ch1) land on GPIO199 and
     * GPIO200 respectively - both dual-function AIO/GPIO pins, unlike
     * Isens_AD/Temp1_AD/Isens_Ref_AD/VBus_AD (A1/A2/B2/C2), which the same
     * sheet shows as dedicated analog-only pins with no GPIOxxx alternate
     * function at all. Dual-function pins reset into DIGITAL mode - the
     * ADC SAR isn't actually connected to the pin until GPIO_setAnalogMode()
     * explicitly switches it, which nothing was doing. That's why Temp1_AD
     * tracked real heat correctly while Temp2_AD/Temp_HS_AD read a
     * plausible-looking but disconnected-from-reality number that never
     * moved with it (confirmed via driverlib/f28p65x/driverlib/gpio.h -
     * GPIO_setAnalogMode(), "this setting should be thought of as another
     * level of muxing").
     */
    GPIO_setAnalogMode(199U, GPIO_ANALOG_ENABLED); /* Temp2_AD   (ADC-C ch0) */
    GPIO_setAnalogMode(200U, GPIO_ANALOG_ENABLED); /* Temp_HS_AD (ADC-C ch1) */

    for (i = 0; i < (sizeof(g_adcModuleRegs) / sizeof(g_adcModuleRegs[0])); i++)
    {
        uint32_t base = g_adcModuleRegs[i].base;

        ADC_setPrescaler(base, ADC_CLK_DIV_4_0);
        ADC_setMode(base, ADC_RESOLUTION_12BIT, ADC_MODE_SINGLE_ENDED);
        ADC_setInterruptPulseMode(base, ADC_PULSE_END_OF_CONV);
        ADC_enableConverter(base);

        /*
         * Without this, ADC_TEST_SOC_NUM's completion never sets the
         * ADCINT1 flag, so ADCTest_Run()'s wait loop
         * (ADC_getInterruptStatus(..., ADC_INT_NUMBER1)) spins forever -
         * this is what was hanging the firmware on the first ADC test.
         */
        ADC_setInterruptSource(base, ADC_INT_NUMBER1, ADC_TEST_SOC_NUM);
        ADC_enableInterrupt(base, ADC_INT_NUMBER1);
    }

    /* Power-up delay required after enabling the ADC - TODO confirm minimum for this device */
    DEVICE_DELAY_US(1000);
}

uint8_t ADCTest_Run(const TestEntry_t *test, int32_t *outValue)
{
    uint16_t rawResult;
    uint32_t base = g_adcModuleRegs[test->adcModule].base;
    uint32_t resultBase = g_adcModuleRegs[test->adcModule].resultBase;

    /*
     * test->pin here is treated as the ADC channel number within its
     * module (ADC_CH_ADCIN0, ADC_CH_ADCIN1, ...). TODO: confirm the
     * enum/macro matches test->pin's numeric value on this device.
     */
    ADC_setupSOC(base, ADC_TEST_SOC_NUM, ADC_TRIGGER_SW_ONLY,
                 (ADC_Channel)test->pin, ADC_TEST_SAMPLE_WINDOW_CYCLES);

    ADC_forceSOC(base, ADC_TEST_SOC_NUM);

    while (ADC_getInterruptStatus(base, ADC_INT_NUMBER1) == false)
    {
        /* wait for conversion complete - TODO: replace with ISR-driven flow if blocking is unacceptable */
    }
    ADC_clearInterruptStatus(base, ADC_INT_NUMBER1);

    rawResult = ADC_readResult(resultBase, ADC_TEST_SOC_NUM);
    *outValue = (int32_t)rawResult;

    return ((*outValue >= test->expectedMin) && (*outValue <= test->expectedMax)) ? 1U : 0U;
}
