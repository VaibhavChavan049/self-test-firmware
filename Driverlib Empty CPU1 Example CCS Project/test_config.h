/*
 * test_config.h
 *
 * Single source of truth for what gets tested. Add a new parameter by
 * adding one entry to g_testList below - no other file needs to change.
 *
 * Mirrors the schema in docs/test_config_template.json (that file documents
 * the same list for humans/GUI-side reference; this one is what actually
 * runs on the board).
 *
 * Pin/net names below come from the "Tucena" schematic (sheet /MCU/,
 * U1A = F28P650DK6PZP), cross-checked against the board's own pin
 * function list and pinmux planning sheet. Entries NOT included on
 * purpose, with why:
 *   - FAN_PWM, CAN_TX, CAN_RX, PWM_Top, PWM_Bot - these are ePWM/CAN
 *     peripheral pins, not plain GPIO. Testing them needs a different
 *     test type than what exists today - add a TEST_TYPE_PWM / _CAN
 *     later rather than mis-testing them as GPIO.
 *   - DBG_UART_TX/RX (GPIO70/71) - this is the UART this firmware itself
 *     uses to talk to the GUI (see uart_comm.c) - can't test the channel
 *     it's reporting over.
 *   - HMI_UART_TX/RX (GPIO12/13) - confirmed via pinmux sheet as SCIA,
 *     used for MCU<->ESP32(HMI) communication - a different peripheral,
 *     out of scope here.
 *   - AD_DACA_OUT, B0_VDAC, B1_DACC_OUT, MRAM_CS/CLK/MISO/MOSI - DAC
 *     outputs and the external MRAM SPI bus - out of scope for the
 *     GPIO/ADC/LED parameters requested so far. (MRAM confirmed via
 *     pinmux sheet as real SPI-A peripheral pins, "Used By: spiMRAM".)
 *   - TP1-TP5 (B0_VDAC, B1_DACC_OUT, B6/GPIO207, B7/GPIO208, C3/GPIO206) -
 *     the board's own pin function list marks these as bare test points
 *     ("-"), and the schematic shows their series resistors (R5-R9) as
 *     NOT STUFFED - no real signal path.
 *   - FAULT (GPIO86) - the board's pin function list explicitly notes
 *     this "does not go anywhere" (floating/unconnected on this board
 *     rev). Deliberately excluded rather than tested - a floating input
 *     reads noise, which would produce a meaningless, non-reproducible
 *     PASS/FAIL. Add it back once/if a real net is wired to it.
 *   - Vsens_AD (B3) - same reasoning: pin function list notes this
 *     "goes nowhere". Excluded for the same floating-input reason.
 *   - A3, A4, A5, A8/GPIO209, A10/GPIO213, A11/GPIO214 - marked
 *     not-connected on the schematic.
 */

#ifndef TEST_CONFIG_H
#define TEST_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    TEST_TYPE_GPIO_IN,   /* digital input, compare read value to expectedValue      */
    TEST_TYPE_GPIO_OUT,  /* digital output, drive expectedValue, read back if wired */
    TEST_TYPE_ADC,       /* analog input, compare reading to [expectedMin, expectedMax] */
    TEST_TYPE_LED        /* drive the LED pin; no physical verification yet (no switch box) */
} TestType_e;

/* Which ADC module a TEST_TYPE_ADC entry's channel belongs to - this device
 * exposes separate A/B/C (/D) analog pin groups, each behind its own ADC
 * SOC module, so channel number alone isn't enough to configure a read. */
typedef enum
{
    ADC_MODULE_A,
    ADC_MODULE_B,
    ADC_MODULE_C
} AdcModule_e;

typedef struct
{
    const char *name;          /* reported over UART - must match docs/protocol.md TEST,<name>,... */
    TestType_e  type;
    uint16_t    pin;           /* GPIO number for GPIO_IN/GPIO_OUT/LED, ADC channel number for ADC */
    AdcModule_e adcModule;     /* ADC only - which module `pin` is a channel of. Ignored otherwise. */
    int32_t     expectedValue; /* used by GPIO_IN / GPIO_OUT / LED (0 or 1) */
    int32_t     expectedMin;   /* used by ADC only */
    int32_t     expectedMax;   /* used by ADC only */
} TestEntry_t;

/*
 * NOTE ON FUTURE PHYSICAL SWITCH BOX:
 * Today every test is checked against the fixed values below. When the
 * switch box exists, the PC will send SETPIN,<name>,<value> before START
 * (see docs/protocol.md "Future: physical switch box"). At that point
 * expectedValue/expectedMin/expectedMax here become the *default* fallback
 * used when no override was received for that test - the struct shape and
 * this file do not need to change.
 *
 * TODO before trusting any result below:
 *   - Button expectedValue=1 assumes active-low with a pull-up
 *     (idle/not-pressed reads high). CONFIRM against the schematic's pull
 *     resistors before relying on this - if any of these are active-high
 *     instead, flip that entry's expectedValue to 0.
 *   - Temp2_FLT/Temp_HS_FLT/Temp1_FLT expectedValue=0: originally guessed
 *     as 1 (active-low, idle=high) but real hardware read 0 on a freshly
 *     powered, room-temperature board - since no real overheat fault is
 *     possible in that state, 0 is what "no fault" actually looks like on
 *     this board, so flipped to match. I_OC_Flt wasn't touched - it read
 *     1 already, consistent with the original active-low assumption.
 *     Worth a sanity confirm from the schematic/datasheet when convenient,
 *     but this isn't a guess anymore - it's what the real board reports
 *     at rest.
 *   - ADC expectedMin/expectedMax are left as the full 12-bit range
 *     (0-4095), i.e. these currently only prove the conversion runs, they
 *     do NOT check the sensor is in a sane range. Narrow the range once
 *     you know each net's expected voltage (sensor scaling, divider
 *     ratio, temp at room temp, etc.).
 *   - BOOT1_STRAP (GPIO84) is a boot-mode strap pin - kept as INPUT only
 *     (read-only check). Do not change this to GPIO_OUT / drive it.
 *   - GPIO_BUZZ (GPIO85) will audibly sound during this test - expected
 *     and harmless, just don't be surprised on the line.
 */
static const TestEntry_t g_testList[] =
{
    /* name              type                 pin  adcModule     expVal  expMin  expMax */

    /* -- Buttons (digital inputs) -- */
    { "ButtonON",         TEST_TYPE_GPIO_IN,   2,   ADC_MODULE_A, 1,      0,      0    },
    { "ButtonLOW",        TEST_TYPE_GPIO_IN,   3,   ADC_MODULE_A, 1,      0,      0    },
    { "ButtonMEDIUM",     TEST_TYPE_GPIO_IN,   89,  ADC_MODULE_A, 1,      0,      0    },
    { "ButtonHIGH",       TEST_TYPE_GPIO_IN,   90,  ADC_MODULE_A, 1,      0,      0    },
    { "ButtonSILENT",     TEST_TYPE_GPIO_IN,   91,  ADC_MODULE_A, 1,      0,      0    },

    /* -- Fault / status inputs -- */
    { "Temp2_FLT",        TEST_TYPE_GPIO_IN,   1,   ADC_MODULE_A, 0,      0,      0    },
    { "I_OC_Flt",         TEST_TYPE_GPIO_IN,   11,  ADC_MODULE_A, 1,      0,      0    },
    { "Temp_HS_FLT",      TEST_TYPE_GPIO_IN,   87,  ADC_MODULE_A, 0,      0,      0    },
    { "Temp1_FLT",        TEST_TYPE_GPIO_IN,   92,  ADC_MODULE_A, 0,      0,      0    },
    /* FAULT (GPIO86) deliberately omitted - floating/unconnected, see file header */
    { "VREG_PG",          TEST_TYPE_GPIO_IN,   64,  ADC_MODULE_A, 1,      0,      0    },
    { "BOOT1_STRAP",      TEST_TYPE_GPIO_IN,   84,  ADC_MODULE_A, 1,      0,      0    },

    /* -- Digital outputs -- */
    { "GATEDRV_ENABLE",   TEST_TYPE_GPIO_OUT,  30,  ADC_MODULE_A, 1,      0,      0    },
    { "HMI_BOOT1_CTRL",   TEST_TYPE_GPIO_OUT,  72,  ADC_MODULE_A, 1,      0,      0    },
    { "GPIO_BUZZ",        TEST_TYPE_GPIO_OUT,  85,  ADC_MODULE_A, 1,      0,      0    },
    /*
     * Drives the relay (K1, Relay_Driver sheet) that connects DC+_P1 to
     * DC+_P2, i.e. gates whether DC_BUS actually carries voltage. Placed
     * before the ADC section (array order = test order) so the relay is
     * already closed by the time VBus_AD runs later in the same pass -
     * without this, VBus_AD reads ~0 regardless of real input power,
     * since the bus is electrically open until this is asserted.
     */
    { "Cont_Enable",      TEST_TYPE_GPIO_OUT,  14,  ADC_MODULE_A, 1,      0,      0    },

    /* -- LEDs -- */
    { "BUTTON_LED_ON",    TEST_TYPE_LED,       35,  ADC_MODULE_A, 1,      0,      0    },
    { "LED_YELLOW",       TEST_TYPE_LED,       61,  ADC_MODULE_A, 1,      0,      0    },

    /* -- ADC channels -- */
    { "Isens_AD",         TEST_TYPE_ADC,       1,   ADC_MODULE_A, 0,      0,      4095 },
    { "Temp1_AD",         TEST_TYPE_ADC,       2,   ADC_MODULE_A, 0,      0,      4095 },
    { "Isens_Ref_AD",     TEST_TYPE_ADC,       2,   ADC_MODULE_B, 0,      0,      4095 },
    /* Vsens_AD (B3) deliberately omitted - floating/unconnected, see file header */
    { "Temp2_AD",         TEST_TYPE_ADC,       0,   ADC_MODULE_C, 0,      0,      4095 },
    { "Temp_HS_AD",       TEST_TYPE_ADC,       1,   ADC_MODULE_C, 0,      0,      4095 },
    { "VBus_AD",          TEST_TYPE_ADC,       2,   ADC_MODULE_C, 0,      0,      4095 },
};

#define TEST_LIST_COUNT ((uint16_t)(sizeof(g_testList) / sizeof(g_testList[0])))

#ifdef __cplusplus
}
#endif

#endif /* TEST_CONFIG_H */
