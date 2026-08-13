/*
 * fan_control.c
 *
 * CONFIRMED against C2000Ware 26.01.00.00 installed locally:
 * GPIO0 -> EPWM1_A (pin_map.h), EPWM1_BASE (hw_memmap.h), and the
 * EPWM_set* call signatures below all checked against
 * driverlib/f28p65x/driverlib/epwm.h. Waveform generation pattern
 * (up-count, HIGH at TIMEBASE_ZERO, LOW at TIMEBASE_UP_CMPA) is the
 * standard driverlib PWM idiom - duty = CMPA / TBPRD.
 *
 * PWM frequency chosen at 20kHz (FAN_PWM_FREQ_HZ) - a common DC fan PWM
 * rate; change it here if the fan's datasheet specifies something else.
 */

#include "driverlib.h"
#include "device.h"
#include "fan_control.h"

#define FAN_PWM_FREQ_HZ   20000U
#define FAN_PWM_TBPRD     ((uint16_t)(DEVICE_SYSCLK_FREQ / FAN_PWM_FREQ_HZ))

void Fan_Init(void)
{
    GPIO_setPinConfig(GPIO_0_EPWM1_A);

    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_EPWM1);

    SysCtl_disablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);

    EPWM_setClockPrescaler(EPWM1_BASE, EPWM_CLOCK_DIVIDER_1, EPWM_HSCLOCK_DIVIDER_1);
    EPWM_setTimeBaseCounterMode(EPWM1_BASE, EPWM_COUNTER_MODE_UP);
    EPWM_setTimeBasePeriod(EPWM1_BASE, FAN_PWM_TBPRD);
    EPWM_disablePhaseShiftLoad(EPWM1_BASE);

    /* Start at 0% (fan off) until the GUI explicitly sets a speed. */
    EPWM_setCounterCompareValue(EPWM1_BASE, EPWM_COUNTER_COMPARE_A, 0U);

    EPWM_setActionQualifierAction(EPWM1_BASE, EPWM_AQ_OUTPUT_A,
                                   EPWM_AQ_OUTPUT_HIGH, EPWM_AQ_OUTPUT_ON_TIMEBASE_ZERO);
    EPWM_setActionQualifierAction(EPWM1_BASE, EPWM_AQ_OUTPUT_A,
                                   EPWM_AQ_OUTPUT_LOW, EPWM_AQ_OUTPUT_ON_TIMEBASE_UP_CMPA);

    SysCtl_enablePeripheral(SYSCTL_PERIPH_CLK_TBCLKSYNC);
}

void Fan_SetDutyPercent(uint16_t percent)
{
    uint16_t compareValue;

    if (percent > 100U)
    {
        percent = 100U;
    }

    compareValue = (uint16_t)(((uint32_t)FAN_PWM_TBPRD * percent) / 100U);
    EPWM_setCounterCompareValue(EPWM1_BASE, EPWM_COUNTER_COMPARE_A, compareValue);
}
