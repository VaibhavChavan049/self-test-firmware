/*
 * coil_control.c
 *
 * CONFIRMED against C2000Ware 26.01.00.00 installed locally
 * (driverlib/f28p65x/driverlib/epwm.h, gpio.h, inc/hw_memmap.h,
 * inc/hw_ints.h): EPWM5_BASE, GPIO_59_EPWM5_A / GPIO_73_EPWM5_B (matches
 * schematic net names PWM_Top/PWM_Bot), all EPWM_setDeadBand-family,
 * edge-delay-count, and EPWM_setActionQualifierContSWForceAction
 * signatures, GPIO XINT2 setup calls, INT_XINT2 vector name.
 *
 * Deadtime values (RED/FED = 20 counts, EPWM_DB_INPUT_EPWMA source, RED
 * active-high / FED active-low polarity) and the button-driven
 * OFF<->HEATING toggle are a direct port of Alexi's SetupOnePWM() /
 * button-ON logic (Code/DSP/main.cpp) - see coil_control.h for the one
 * significant thing NOT ported (over-current protection).
 *
 * Debounce is simplified vs. Alexi's version: his used a millisecond
 * timestamp compared against a running system tick, which this project's
 * main loop doesn't have (it blocks on UART_readLine() between commands,
 * so a state toggle has to happen inside the ISR itself, not via
 * polling in the main loop). This resamples the pin after a short delay
 * instead - same effect (reject bounce/noise), simpler for this
 * architecture.
 */

#include "driverlib.h"
#include "device.h"
#include "coil_control.h"
#include "current_protect.h"

#define COIL_EPWM_BASE       EPWM5_BASE
#define COIL_DB_RED_COUNT    20U
#define COIL_DB_FED_COUNT    20U

#define BUTTON_ON_GPIO       2U    /* same physical pin as the self-test's ButtonON read */
#define GATEDRV_ENABLE_GPIO  30U   /* this board's contactor/gate-driver enable, per schematic */

#define BUTTON_DEBOUNCE_RESAMPLE_DELAY_US  2000U /* 2ms - see debounce note above */

static volatile CoilState_e s_coilState = COIL_STATE_OFF;

static void applyStateOutputs(void);
__interrupt void CoilControl_ButtonISR(void);

void CoilControl_Init(void)
{
    /*
     * --- Deadtime setup (PWM_Top/PWM_Bot, EPWM5A/B) ---
     * Mirrors Alexi's SetupOnePWM(): EPWMA drives both edge-delay inputs,
     * both RED and FED enabled, RED active-high / FED active-low so A/B
     * come out complementary with a dead gap between them.
     */
    GPIO_setPinConfig(GPIO_59_EPWM5_A);
    GPIO_setPinConfig(GPIO_73_EPWM5_B);

    EPWM_setRisingEdgeDeadBandDelayInput(COIL_EPWM_BASE, EPWM_DB_INPUT_EPWMA);
    EPWM_setFallingEdgeDeadBandDelayInput(COIL_EPWM_BASE, EPWM_DB_INPUT_EPWMA);

    EPWM_setDeadBandDelayMode(COIL_EPWM_BASE, EPWM_DB_RED, true);
    EPWM_setDeadBandDelayMode(COIL_EPWM_BASE, EPWM_DB_FED, true);

    EPWM_setDeadBandDelayPolarity(COIL_EPWM_BASE, EPWM_DB_RED, EPWM_DB_POLARITY_ACTIVE_HIGH);
    EPWM_setDeadBandDelayPolarity(COIL_EPWM_BASE, EPWM_DB_FED, EPWM_DB_POLARITY_ACTIVE_LOW);

    EPWM_setRisingEdgeDelayCount(COIL_EPWM_BASE, COIL_DB_RED_COUNT);
    EPWM_setFallingEdgeDelayCount(COIL_EPWM_BASE, COIL_DB_FED_COUNT);

    /* --- Gate driver / contactor enable pin, start disabled --- */
    GPIO_setDirectionMode(GATEDRV_ENABLE_GPIO, GPIO_DIR_MODE_OUT);
    GPIO_setPadConfig(GATEDRV_ENABLE_GPIO, GPIO_PIN_TYPE_STD);
    GPIO_writePin(GATEDRV_ENABLE_GPIO, 0U);

    /* Start forced OFF (safe state) before enabling the button interrupt. */
    s_coilState = COIL_STATE_OFF;
    applyStateOutputs();

    /* --- Button (GPIO2) interrupt setup, both edges, XINT2 --- */
    GPIO_setDirectionMode(BUTTON_ON_GPIO, GPIO_DIR_MODE_IN);
    GPIO_setPadConfig(BUTTON_ON_GPIO, GPIO_PIN_TYPE_STD);
    GPIO_setQualificationMode(BUTTON_ON_GPIO, GPIO_QUAL_6SAMPLE);

    GPIO_setInterruptPin(BUTTON_ON_GPIO, GPIO_INT_XINT2);
    GPIO_setInterruptType(GPIO_INT_XINT2, GPIO_INT_TYPE_BOTH_EDGES);
    Interrupt_register(INT_XINT2, &CoilControl_ButtonISR);
    Interrupt_enable(INT_XINT2);
    GPIO_enableInterrupt(GPIO_INT_XINT2);
}

CoilState_e CoilControl_GetState(void)
{
    return s_coilState;
}

/*
 * Applies the electrical consequence of the current state - this is the
 * safety-relevant half of this file: COIL_STATE_OFF unconditionally
 * forces both outputs low and drops the gate-driver enable, regardless
 * of anything else going on.
 */
static void applyStateOutputs(void)
{
    if (s_coilState == COIL_STATE_OFF)
    {
        EPWM_setActionQualifierContSWForceAction(COIL_EPWM_BASE, EPWM_AQ_OUTPUT_A, EPWM_AQ_SW_OUTPUT_LOW);
        EPWM_setActionQualifierContSWForceAction(COIL_EPWM_BASE, EPWM_AQ_OUTPUT_B, EPWM_AQ_SW_OUTPUT_LOW);
        GPIO_writePin(GATEDRV_ENABLE_GPIO, 0U);
    }
    else /* COIL_STATE_HEATING */
    {
        EPWM_setActionQualifierContSWForceAction(COIL_EPWM_BASE, EPWM_AQ_OUTPUT_A, EPWM_AQ_SW_DISABLED);
        EPWM_setActionQualifierContSWForceAction(COIL_EPWM_BASE, EPWM_AQ_OUTPUT_B, EPWM_AQ_SW_DISABLED);
        GPIO_writePin(GATEDRV_ENABLE_GPIO, 1U);
    }
}

__interrupt void CoilControl_ButtonISR(void)
{
    /*
     * Button is active-low (matches this project's ButtonON polarity
     * assumption elsewhere) - a press pulls the pin low. Resample after a
     * short delay to reject bounce/noise before accepting the edge as a
     * real press.
     */
    DEVICE_DELAY_US(BUTTON_DEBOUNCE_RESAMPLE_DELAY_US);

    if (GPIO_readPin(BUTTON_ON_GPIO) == 0U)
    {
        if (s_coilState == COIL_STATE_OFF)
        {
            /* Refuse to enter HEATING if this board's current-sensor calibration
             * never landed in a healthy range - see current_protect.h. */
            if (CurrentProtect_IsCalibrationValid())
            {
                s_coilState = COIL_STATE_HEATING;
            }
        }
        else
        {
            s_coilState = COIL_STATE_OFF;
        }
        applyStateOutputs();
    }

    /*
     * CONFIRMED against gpio_ex3_interrupt.c (same C2000Ware install) -
     * that example's XINT ISR only calls Interrupt_clearACKGroup(), no
     * separate GPIO-side flag clear exists/is needed.
     */
    Interrupt_clearACKGroup(INTERRUPT_ACK_GROUP1);
}
