/*
 * current_protect.c
 *
 * CONFIRMED against C2000Ware 26.01.00.00 installed locally
 * (driverlib/f28p65x/driverlib/cmpss.h, asysctl.h, inc/hw_memmap.h):
 * CMPSS1_BASE (Alexi's "myCMPSS0" was just a SysConfig instance name -
 * there is no CMPSS0 on this device, modules are CMPSS1-CMPSS11),
 * ASysCtl_selectCMPHPMux() is the real function behind SysConfig's
 * asysCMPHPMXSELValue field, and all the CMPSS_config-family,
 * CMPSS_set-family, and CMPSS_getStatus signatures.
 *
 * Calibration algorithm (CurrentProtect_Calibrate) is a direct port of
 * Alexi's CalibrateCMSSDAC() (Code/DSP/main.cpp) - full team-verified
 * logic: sweep DAC threshold down from 2200, and at each value, check if
 * the comparator trips; once it does, resample 100 times and accept that
 * threshold only if it trips in >=80 of those 100 samples (i.e. it's a
 * consistent trip point, not noise). Falls back to 2048 if the sweep
 * never finds one.
 *
 * ONE SUBSTITUTION vs. Alexi's version: his waits were counted against
 * g_timerCounter, a running tick counter from a timer peripheral this
 * project doesn't have set up. Swapped for DEVICE_DELAY_US() busy-waits
 * of the same duration (assuming his "timer counts" are 1ms ticks, the
 * most common convention - 50/100/20 counts below map to 50/100/20 ms).
 * If his ticks are actually a different rate, these delays need
 * adjusting to match, otherwise the calibration timing (and therefore
 * result) may not be equivalent.
 *
 * CurrentProtect_IsTripped() IS now polled automatically: coil_control.c's
 * CPU Timer0 ISR (1ms tick) checks it while COIL_STATE_HEATING and forces
 * COIL_STATE_ERROR (PWM outputs forced low, contactor opened) on trip -
 * this is a software poll-and-react loop, not a hardware trip-zone
 * connection. Alexi's own source arms this same comparator but never
 * acts on it in software either (EPWM_TZ_ACTION_DISABLE, the LOW action
 * is commented out in his source) - the software poll here is this
 * project's own addition on top of his code, not a difference from it.
 * A true hardware trip-zone path (CMPSS -> EPWM5 Trip Zone via
 * X-BAR/Digital Compare, which reacts within one PWM cycle instead of
 * within one 1ms tick) is still not wired - do not treat the current 1ms
 * software poll as equivalent to that for a real power test without
 * verifying the trip response time on a scope is fast enough for your
 * fault case.
 */

#include "driverlib.h"
#include "device.h"
#include "current_protect.h"

#define ISENS_CMPSS_BASE          CMPSS1_BASE
#define ISENS_CMPSS_ASYS_SELECT   ASYSCTL_CMPHPMUX_SELECT_1  /* CMPSS1's mux */
#define ISENS_CMPSS_ASYS_VALUE    2U                          /* -> A1, confirmed by team */

#define ISENS_DAC_UNCALIBRATED_FALLBACK  2048U  /* SysConfig default, used only if the sweep finds nothing */
#define ISENS_DAC_SWEEP_START            2200U
#define ISENS_DAC_SWEEP_FLOOR             1800U
#define ISENS_DAC_VALID_MIN               2000U
#define ISENS_DAC_VALID_MAX               2120U

#define ISENS_FILTER_PRESCALE     1U
#define ISENS_FILTER_WINDOW       32U    /* SysConfig sampleWindowHigh */
#define ISENS_FILTER_THRESHOLD    30U    /* SysConfig thresholdHigh */

/* Calibration-only filter config, matches Alexi's CalibrateCMSSDAC() exactly (different from the
 * steady-state filter numbers above - CMPSS_configFilterHigh/setHysteresis get called again with
 * these before the sweep, same as his code does). */
#define CAL_FILTER_PRESCALE       1U
#define CAL_FILTER_WINDOW         64U
#define CAL_FILTER_THRESHOLD      63U

#define CAL_VERIFY_SAMPLE_COUNT   100U
#define CAL_VERIFY_PASS_THRESHOLD 80U

static uint16_t s_calibratedDacValue = ISENS_DAC_UNCALIBRATED_FALLBACK;

static void runCalibrationSweep(void);

void CurrentProtect_Init(void)
{
    /*
     * Route A1 into CMPSS1's high comparator input mux. Note: unlike the
     * ADC (adc_test.c), CMPSS reads the analog pin directly through the
     * analog subsystem mux - it doesn't go through GPIO_setPinConfig()/
     * ADC_setupSOC() the way a regular ADC channel read does.
     */
    ASysCtl_selectCMPHPMux(ISENS_CMPSS_ASYS_SELECT, ISENS_CMPSS_ASYS_VALUE);

    CMPSS_enableModule(ISENS_CMPSS_BASE);

    CMPSS_configHighComparator(ISENS_CMPSS_BASE, CMPSS_INSRC_DAC);

    CMPSS_configDAC(ISENS_CMPSS_BASE,
                     CMPSS_DACREF_VDDA | CMPSS_DACSRC_SHDW | CMPSS_DACVAL_SYSCLK);

    runCalibrationSweep(); /* sets s_calibratedDacValue and applies it via CMPSS_setDACValueHigh */

    /* Steady-state filter config (post-calibration), separate from the calibration-time filter above. */
    CMPSS_configFilterHigh(ISENS_CMPSS_BASE, ISENS_FILTER_PRESCALE,
                            ISENS_FILTER_WINDOW, ISENS_FILTER_THRESHOLD);
    CMPSS_setHysteresis(ISENS_CMPSS_BASE, 0U);

    CMPSS_clearFilterLatchHigh(ISENS_CMPSS_BASE);
}

uint16_t CurrentProtect_GetCalibratedValue(void)
{
    return s_calibratedDacValue;
}

uint8_t CurrentProtect_IsCalibrationValid(void)
{
    return ((s_calibratedDacValue >= ISENS_DAC_VALID_MIN) &&
            (s_calibratedDacValue <= ISENS_DAC_VALID_MAX)) ? 1U : 0U;
}

/*
 * Direct port of Alexi's CalibrateCMSSDAC(): sweep the DAC threshold down
 * from 2200, looking for the point where the comparator starts tripping
 * consistently (>=80/100 resample) rather than just on noise. Validates
 * the found value against [2000, 2120] same as his code; result is
 * always left in a defined state either way (calibrated value, or the
 * 2048 fallback), never silently unvalidated.
 */
static void runCalibrationSweep(void)
{
    uint16_t dacValue = ISENS_DAC_SWEEP_START;
    uint16_t status;
    uint16_t setTimes;
    uint16_t i;

    CMPSS_configFilterHigh(ISENS_CMPSS_BASE, CAL_FILTER_PRESCALE, CAL_FILTER_WINDOW, CAL_FILTER_THRESHOLD);
    CMPSS_setHysteresis(ISENS_CMPSS_BASE, 0U);

    while (dacValue > ISENS_DAC_SWEEP_FLOOR)
    {
        CMPSS_setDACValueHigh(ISENS_CMPSS_BASE, dacValue);
        DEVICE_DELAY_US(50000U); /* 50ms - see g_timerCounter substitution note in file header */

        CMPSS_clearFilterLatchHigh(ISENS_CMPSS_BASE);
        DEVICE_DELAY_US(100000U); /* 100ms */

        status = CMPSS_getStatus(ISENS_CMPSS_BASE);
        if ((status & CMPSS_STS_HI_LATCHFILTOUT) != 0U)
        {
            /* Found a candidate trip point - verify it's consistent, not noise. */
            setTimes = 0U;
            for (i = 0U; i < CAL_VERIFY_SAMPLE_COUNT; i++)
            {
                CMPSS_clearFilterLatchHigh(ISENS_CMPSS_BASE);
                DEVICE_DELAY_US(20000U); /* 20ms */
                status = CMPSS_getStatus(ISENS_CMPSS_BASE);
                if ((status & CMPSS_STS_HI_LATCHFILTOUT) != 0U)
                {
                    setTimes++;
                }
            }
            if (setTimes >= CAL_VERIFY_PASS_THRESHOLD)
            {
                s_calibratedDacValue = dacValue;
                CMPSS_setDACValueHigh(ISENS_CMPSS_BASE, s_calibratedDacValue);
                return;
            }
        }
        dacValue--;
    }

    /* Swept the whole range without finding a consistent trip point. */
    s_calibratedDacValue = ISENS_DAC_UNCALIBRATED_FALLBACK;
    CMPSS_setDACValueHigh(ISENS_CMPSS_BASE, s_calibratedDacValue);
}

uint8_t CurrentProtect_IsTripped(void)
{
    uint16_t status = CMPSS_getStatus(ISENS_CMPSS_BASE);
    return ((status & CMPSS_STS_HI_LATCHFILTOUT) != 0U) ? 1U : 0U;
}

void CurrentProtect_ClearTrip(void)
{
    CMPSS_clearFilterLatchHigh(ISENS_CMPSS_BASE);
}
