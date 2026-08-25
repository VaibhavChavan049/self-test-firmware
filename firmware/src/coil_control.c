/*
 * coil_control.c
 *
 * CONFIRMED against C2000Ware 26.01.00.00 installed locally
 * (driverlib/f28p65x/driverlib/epwm.h, gpio.h, cputimer.h, inc/hw_memmap.h,
 * inc/hw_ints.h): EPWM5_BASE, GPIO_59_EPWM5_A / GPIO_73_EPWM5_B, every
 * EPWM_setDeadBand-family/edge-delay-count/ActionQualifier call below,
 * EPWM_COUNTER_MODE_UP_DOWN, EPWM_PERIOD_DIRECT_LOAD,
 * EPWM_setTripZoneAction + EPWM_TZ_ACTION_EVENT_TZA/TZB/DISABLE,
 * EPWM_setEmulationMode + EPWM_EMULATION_STOP_AFTER_NEXT_TB,
 * EPWM_setCountModeAfterSync + EPWM_COUNT_MODE_UP_AFTER_SYNC, all
 * CPUTimer_-family calls + CPUTIMER0_BASE + INT_TIMER0, GPIO XINT1-4
 * setup calls + INT_XINT1-4 vector names. CPU Timer0 setup sequence is a
 * direct match of the CONFIRMED local reference example
 * driverlib/f28p65x/examples/c28x/timer/timer_ex1_cputimers.c (hand-
 * configured, non-SysConfig path - same style as the rest of this
 * project).
 *
 * Ported from Alexi's actual main.cpp (Code/DSP/main.cpp, Tucena-main
 * repo, read directly rather than worked from memory) - state machine,
 * button semantics, SetupOnePWM(), and the 30-minute auto-shutoff are
 * all a faithful port, re-expressed in plain C since his version is C++
 * with template classes (MovingAverage, Ring, ButtonDebouncer) this
 * project doesn't have. See coil_control.h for exactly what was
 * deliberately left out (angle-finding/resonance-tracking - per team
 * direction) vs added beyond his source (over-current trip auto-poll).
 *
 * Two things from his SetupOnePWM() are intentionally NOT ported here:
 * EPWM_setADCTriggerSource()/EPWM_setADCTriggerEventPrescale() (and the
 * EPWM_enableADCTrigger() he calls separately in main()). Those exist in
 * his code to feed his EPWM5-synchronized mains-voltage sampling
 * pipeline (DMA + ECAP + a dedicated sampling timer), which isn't ported
 * this round - adding a dangling EPWM->ADC SOC trigger with no consumer
 * risks silently colliding with adc_test.c's already-verified, working
 * self-test ADC channel configuration, which is exactly the kind of
 * unverified guess this project avoids. Everything else in his
 * SetupOnePWM() that's self-contained to the EPWM5 module itself is
 * ported as-is below.
 *
 * Debounce is simplified vs. Alexi's ButtonDebouncer class: same
 * resample-after-a-short-delay approach used throughout this project
 * (relay_control.c, this file's earlier version) instead of his
 * millisecond-timestamp debounce class - equivalent effect, simpler for
 * a project without his timer/millis() infrastructure.
 */

#include "driverlib.h"
#include "device.h"
#include "coil_control.h"
#include "current_protect.h"
#include "relay_control.h"
#include "buzzer_control.h"

/*
 * Audible "firmware saw this press" checkpoint, separate from the relay's
 * incidental click (which only happens on an actual OFF<->HEATING
 * transition). Kept short (10ms) because ButtonON/ButtonLOW's ISRs share
 * PIE group 1 with CPU Timer0's 1ms over-current poll - see
 * buzzer_control.c's file header for why that bounds this duration.
 */
#define BUTTON_TICK_PULSE_MS  10U

#define COIL_EPWM_BASE       EPWM5_BASE
#define COIL_DB_RED_COUNT    20U
#define COIL_DB_FED_COUNT    20U

/*
 * Base PWM frequency/period - direct port of Alexi's INV_PWM_TICKS() macro
 * and STARTING_HALF_PERIOD constant. His SYSTEM_FREQUENCY=200 (MHz, /1
 * clock div) matches this project's real DEVICE_SYSCLK_FREQ exactly (both
 * PLL config branches in device.h compute to 200MHz) - using
 * DEVICE_SYSCLK_FREQ here ties this to the real clock config instead of a
 * second magic number that could silently drift out of sync with it.
 *
 * UP_DOWN counter mode means TBPRD is only the HALF period (counter goes
 * 0 -> TBPRD -> 0 each full PWM cycle) - hence "half period" naming,
 * matching his. With PWM_FREQUENCY_HZ=45000:
 *   200000000 / 1 / 45000 = 4444 (integer division, matches his uint32_t
 *   cast truncating 4444.44) -> /2 = 2222 = TBPRD.
 * CMPA = TBPRD/2 = 1111, i.e. ~50% duty (his STARTING_HALF_PERIOD/2).
 */
#define COIL_PWM_FREQUENCY_HZ  45000U
#define COIL_PWM_CLK_DIV       1U
#define COIL_PWM_TBPRD         ((uint16_t)((DEVICE_SYSCLK_FREQ / COIL_PWM_CLK_DIV / COIL_PWM_FREQUENCY_HZ) / 2U))
#define COIL_PWM_CMPA          ((uint16_t)(COIL_PWM_TBPRD / 2U))

#define BUTTON_ON_GPIO       2U    /* ButtonON - stops heating only, does not start it (matches Alexi) */
#define BUTTON_LOW_GPIO      3U    /* ButtonLOW - starts heating at HEAT_LOW */
#define BUTTON_MEDIUM_GPIO   89U   /* ButtonMEDIUM - starts heating at HEAT_MEDIUM */
#define BUTTON_HIGH_GPIO     90U   /* ButtonHIGH - starts heating at HEAT_HIGH */
#define GATEDRV_ENABLE_GPIO  30U   /* this board's contactor/gate-driver enable, per schematic */

#define BUTTON_DEBOUNCE_RESAMPLE_DELAY_US  2000U /* 2ms - see debounce note in file header */

/* CPU Timer0, 1ms tick - drives the 30-min auto-shutoff and the over-current trip poll. */
#define COIL_TICK_PERIOD_US       1000UL
#define COIL_TICK_PERIOD_COUNT    ((uint32_t)((DEVICE_SYSCLK_FREQ / 1000000UL) * COIL_TICK_PERIOD_US) - 1UL)
/* 30 minutes, in 1ms ticks - matches Alexi's global_timeout.start_ms(30*60*1000ULL) exactly. */
#define COIL_HEATING_TIMEOUT_MS   (30UL * 60UL * 1000UL)

static volatile CoilState_e   s_coilState = COIL_STATE_OFF;
static volatile CoilHeatMode_e s_heatMode = COIL_HEAT_LOW;
static volatile uint32_t      s_heatingElapsedMs = 0UL;

/*
 * Increment on every genuine (debounced) press, regardless of whether
 * startHeating()/stopHeating() actually acted on it - lets COIL_STATUS
 * distinguish "firmware never saw the press" (these stay at 0 - check
 * wiring/GPIO/EMI) from "firmware saw it but refused" (these go up, but
 * state/calibration explain why nothing happened).
 */
static volatile uint32_t s_buttonOnPressCount = 0UL;
static volatile uint32_t s_buttonLowPressCount = 0UL;
static volatile uint32_t s_buttonMediumPressCount = 0UL;
static volatile uint32_t s_buttonHighPressCount = 0UL;

static void applyStateOutputs(void);
static void startHeating(CoilHeatMode_e mode);
static void stopHeating(void);

__interrupt void CoilControl_ButtonOnISR(void);
__interrupt void CoilControl_ButtonLowISR(void);
__interrupt void CoilControl_ButtonMediumISR(void);
__interrupt void CoilControl_ButtonHighISR(void);
__interrupt void CoilControl_TimerISR(void);

static void setupButtonInput(uint32_t gpio, GPIO_ExternalIntNum intPin, uint32_t intVector, void (*isr)(void))
{
    GPIO_setDirectionMode(gpio, GPIO_DIR_MODE_IN);
    GPIO_setPadConfig(gpio, GPIO_PIN_TYPE_STD);
    GPIO_setQualificationMode(gpio, GPIO_QUAL_6SAMPLE);

    GPIO_setInterruptPin(gpio, intPin);
    GPIO_setInterruptType(intPin, GPIO_INT_TYPE_BOTH_EDGES);
    Interrupt_register(intVector, isr);
    Interrupt_enable(intVector);
    GPIO_enableInterrupt(intPin);
}

void CoilControl_Init(void)
{
    GPIO_setPinConfig(GPIO_59_EPWM5_A);
    GPIO_setPinConfig(GPIO_73_EPWM5_B);

    /*
     * --- Base PWM generation + deadtime (PWM_Top/PWM_Bot, EPWM5) ---
     * Direct port of Alexi's SetupOnePWM(): center-aligned (UP_DOWN)
     * counter mode, fixed 45kHz / ~50% duty, RED/FED=20 counts on
     * EPWMA source, RED active-high / FED active-low. No angle-finding -
     * fixed frequency only, per team direction.
     */
    EPWM_setPeriodLoadMode(COIL_EPWM_BASE, EPWM_PERIOD_DIRECT_LOAD);
    EPWM_setTimeBasePeriod(COIL_EPWM_BASE, COIL_PWM_TBPRD);
    EPWM_setPhaseShift(COIL_EPWM_BASE, 0U);
    EPWM_setTimeBaseCounter(COIL_EPWM_BASE, 0U);
    EPWM_setTimeBaseCounterMode(COIL_EPWM_BASE, EPWM_COUNTER_MODE_UP_DOWN);
    EPWM_setClockPrescaler(COIL_EPWM_BASE, EPWM_CLOCK_DIVIDER_1, EPWM_HSCLOCK_DIVIDER_1);
    EPWM_setCounterCompareValue(COIL_EPWM_BASE, EPWM_COUNTER_COMPARE_A, COIL_PWM_CMPA);
    EPWM_setActionQualifierActionComplete(COIL_EPWM_BASE, EPWM_AQ_OUTPUT_A,
        (EPWM_ActionQualifierEventAction)(EPWM_AQ_OUTPUT_HIGH_UP_CMPA | EPWM_AQ_OUTPUT_LOW_DOWN_CMPA));

    EPWM_setRisingEdgeDeadBandDelayInput(COIL_EPWM_BASE, EPWM_DB_INPUT_EPWMA);
    EPWM_setFallingEdgeDeadBandDelayInput(COIL_EPWM_BASE, EPWM_DB_INPUT_EPWMA);
    EPWM_setDeadBandDelayMode(COIL_EPWM_BASE, EPWM_DB_RED, true);
    EPWM_setDeadBandDelayMode(COIL_EPWM_BASE, EPWM_DB_FED, true);
    EPWM_setDeadBandDelayPolarity(COIL_EPWM_BASE, EPWM_DB_RED, EPWM_DB_POLARITY_ACTIVE_HIGH);
    EPWM_setDeadBandDelayPolarity(COIL_EPWM_BASE, EPWM_DB_FED, EPWM_DB_POLARITY_ACTIVE_LOW);
    EPWM_setRisingEdgeDelayCount(COIL_EPWM_BASE, COIL_DB_RED_COUNT);
    EPWM_setFallingEdgeDelayCount(COIL_EPWM_BASE, COIL_DB_FED_COUNT);

    /* Remaining SetupOnePWM() calls that are self-contained to EPWM5 (see file header for what's skipped and why). */
    EPWM_enablePhaseShiftLoad(COIL_EPWM_BASE);
    EPWM_setCountModeAfterSync(COIL_EPWM_BASE, EPWM_COUNT_MODE_UP_AFTER_SYNC);
    EPWM_disableCounterCompareShadowLoadMode(COIL_EPWM_BASE, EPWM_COUNTER_COMPARE_A);
    EPWM_setEmulationMode(COIL_EPWM_BASE, EPWM_EMULATION_STOP_AFTER_NEXT_TB);
    EPWM_setTripZoneAction(COIL_EPWM_BASE, EPWM_TZ_ACTION_EVENT_TZB, EPWM_TZ_ACTION_DISABLE);
    EPWM_setTripZoneAction(COIL_EPWM_BASE, EPWM_TZ_ACTION_EVENT_TZA, EPWM_TZ_ACTION_DISABLE);

    /* --- Gate driver / contactor-enable pin, start disabled --- */
    GPIO_setDirectionMode(GATEDRV_ENABLE_GPIO, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(GATEDRV_ENABLE_GPIO, GPIO_PIN_TYPE_STD);
    GPIO_writePin(GATEDRV_ENABLE_GPIO, 0U);

    /* Start forced OFF (safe state) before enabling any interrupt. */
    s_coilState = COIL_STATE_OFF;
    s_heatingElapsedMs = 0UL;
    applyStateOutputs();

    /* --- Buttons: ON (stop-only), LOW/MEDIUM/HIGH (start-only) --- */
    setupButtonInput(BUTTON_ON_GPIO, GPIO_INT_XINT2, INT_XINT2, &CoilControl_ButtonOnISR);
    setupButtonInput(BUTTON_LOW_GPIO, GPIO_INT_XINT1, INT_XINT1, &CoilControl_ButtonLowISR);
    setupButtonInput(BUTTON_MEDIUM_GPIO, GPIO_INT_XINT3, INT_XINT3, &CoilControl_ButtonMediumISR);
    setupButtonInput(BUTTON_HIGH_GPIO, GPIO_INT_XINT4, INT_XINT4, &CoilControl_ButtonHighISR);

    /*
     * --- CPU Timer0, 1ms tick --- sequence CONFIRMED against
     * timer_ex1_cputimers.c (configCPUTimer()/initCPUTimers()).
     */
    CPUTimer_setPeriod(CPUTIMER0_BASE, COIL_TICK_PERIOD_COUNT);
    CPUTimer_setPreScaler(CPUTIMER0_BASE, 0U);
    CPUTimer_stopTimer(CPUTIMER0_BASE);
    CPUTimer_reloadTimerCounter(CPUTIMER0_BASE);
    CPUTimer_setEmulationMode(CPUTIMER0_BASE, CPUTIMER_EMULATIONMODE_STOPAFTERNEXTDECREMENT);
    CPUTimer_enableInterrupt(CPUTIMER0_BASE);
    Interrupt_register(INT_TIMER0, &CoilControl_TimerISR);
    Interrupt_enable(INT_TIMER0);
    CPUTimer_startTimer(CPUTIMER0_BASE);
}

CoilState_e CoilControl_GetState(void)
{
    return s_coilState;
}

CoilHeatMode_e CoilControl_GetHeatMode(void)
{
    return s_heatMode;
}

void CoilControl_GetButtonPressCounts(uint32_t *onCount, uint32_t *lowCount, uint32_t *mediumCount, uint32_t *highCount)
{
    *onCount = s_buttonOnPressCount;
    *lowCount = s_buttonLowPressCount;
    *mediumCount = s_buttonMediumPressCount;
    *highCount = s_buttonHighPressCount;
}

/*
 * Applies the electrical consequence of the current state - COIL_STATE_OFF
 * and COIL_STATE_ERROR are electrically identical (outputs forced low,
 * contactor open); ERROR is kept as a distinct state only so it's
 * reported/latched separately and only clears via ButtonON, matching
 * Alexi's STATE_HEATING_ERROR.
 */
static void applyStateOutputs(void)
{
    if (s_coilState == COIL_STATE_HEATING)
    {
        EPWM_setActionQualifierContSWForceAction(COIL_EPWM_BASE, EPWM_AQ_OUTPUT_A, EPWM_AQ_SW_DISABLED);
        EPWM_setActionQualifierContSWForceAction(COIL_EPWM_BASE, EPWM_AQ_OUTPUT_B, EPWM_AQ_SW_DISABLED);
        GPIO_writePin(GATEDRV_ENABLE_GPIO, 1U);
        RelayControl_SetState(1U);
    }
    else /* COIL_STATE_OFF or COIL_STATE_ERROR */
    {
        EPWM_setActionQualifierContSWForceAction(COIL_EPWM_BASE, EPWM_AQ_OUTPUT_A, EPWM_AQ_SW_OUTPUT_LOW);
        EPWM_setActionQualifierContSWForceAction(COIL_EPWM_BASE, EPWM_AQ_OUTPUT_B, EPWM_AQ_SW_OUTPUT_LOW);
        GPIO_writePin(GATEDRV_ENABLE_GPIO, 0U);
        RelayControl_SetState(0U);
    }
}

static void startHeating(CoilHeatMode_e mode)
{
    /* Refuse to heat if this board's current-sensor calibration never landed in a healthy range. */
    if ((s_coilState == COIL_STATE_OFF) && CurrentProtect_IsCalibrationValid())
    {
        s_heatMode = mode;
        s_coilState = COIL_STATE_HEATING;
        s_heatingElapsedMs = 0UL;
        applyStateOutputs();
    }
}

static void stopHeating(void)
{
    if (s_coilState == COIL_STATE_ERROR)
    {
        CurrentProtect_ClearTrip();
    }
    s_coilState = COIL_STATE_OFF;
    s_heatingElapsedMs = 0UL;
    applyStateOutputs();
}

__interrupt void CoilControl_ButtonOnISR(void)
{
    DEVICE_DELAY_US(BUTTON_DEBOUNCE_RESAMPLE_DELAY_US);
    if (GPIO_readPin(BUTTON_ON_GPIO) == 0U)
    {
        s_buttonOnPressCount++;
        BuzzerControl_Pulse(BUTTON_TICK_PULSE_MS);
        if ((s_coilState == COIL_STATE_HEATING) || (s_coilState == COIL_STATE_ERROR))
        {
            stopHeating();
        }
    }
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

__interrupt void CoilControl_ButtonLowISR(void)
{
    DEVICE_DELAY_US(BUTTON_DEBOUNCE_RESAMPLE_DELAY_US);
    if (GPIO_readPin(BUTTON_LOW_GPIO) == 0U)
    {
        s_buttonLowPressCount++;
        BuzzerControl_Pulse(BUTTON_TICK_PULSE_MS);
        startHeating(COIL_HEAT_LOW);
    }
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}

__interrupt void CoilControl_ButtonMediumISR(void)
{
    DEVICE_DELAY_US(BUTTON_DEBOUNCE_RESAMPLE_DELAY_US);
    if (GPIO_readPin(BUTTON_MEDIUM_GPIO) == 0U)
    {
        s_buttonMediumPressCount++;
        BuzzerControl_Pulse(BUTTON_TICK_PULSE_MS);
        startHeating(COIL_HEAT_MEDIUM);
    }
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP12);
}

__interrupt void CoilControl_ButtonHighISR(void)
{
    DEVICE_DELAY_US(BUTTON_DEBOUNCE_RESAMPLE_DELAY_US);
    if (GPIO_readPin(BUTTON_HIGH_GPIO) == 0U)
    {
        s_buttonHighPressCount++;
        BuzzerControl_Pulse(BUTTON_TICK_PULSE_MS);
        startHeating(COIL_HEAT_HIGH);
    }
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP12);
}

/*
 * 1ms tick: 30-minute auto-shutoff while heating (matches Alexi's
 * global_timeout exactly), plus an over-current trip poll that his own
 * code arms but never actually acts on (see file header) - this project
 * closes that loop.
 */
__interrupt void CoilControl_TimerISR(void)
{
    if (s_coilState == COIL_STATE_HEATING)
    {
        if (CurrentProtect_IsTripped())
        {
            s_coilState = COIL_STATE_ERROR;
            applyStateOutputs();
        }
        else
        {
            s_heatingElapsedMs++;
            if (s_heatingElapsedMs >= COIL_HEATING_TIMEOUT_MS)
            {
                s_coilState = COIL_STATE_OFF;
                s_heatingElapsedMs = 0UL;
                applyStateOutputs();
            }
        }
    }
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}
