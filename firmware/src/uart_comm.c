/*
 * uart_comm.c
 *
 * Uses DBG_UART_TX (GPIO70) / DBG_UART_RX (GPIO71) per the Tucena
 * schematic (sheet /MCU/) - these read as the debug/USB-UART bridge
 * pins, which is exactly the channel this GUI-facing protocol needs.
 * (Not HMI_UART_TX/RX on GPIO12/13 - that pair goes to a different
 * peripheral, the HMI module, not the PC.)
 *
 * CORRECTION: this device has two separate serial peripherals - the
 * older SCI blocks (sci.h, SCI_* calls) and a newer, separate UART block
 * (uart.h, UART_* calls). GPIO70/71 support pin-mux options for BOTH
 * (GPIO_70_SCIB_TX and GPIO_70_UARTB_TX both exist in pin_map.h), but the
 * board's own pinmux planning sheet marks GPIO70/71's selected mode as
 * "UARTB" (Used By: uartDBG) - so this file uses the UART peripheral
 * (UARTB_BASE, UART_* calls), not SCI-B. An earlier version of this file
 * used SCI-B, which was a valid pin-mux option but not the one this
 * board's design actually intends.
 *
 * All UART_* calls, macros, and base addresses below confirmed against
 * C2000Ware 26.01.00.00 installed locally: uart.h (API + UART_CLK_FREQ),
 * inc/hw_memmap.h (UARTB_BASE), pin_map.h (GPIO_70_UARTB_TX /
 * GPIO_71_UARTB_RX), and driverlib/examples/c28x/uart/uart_ex1_echoback.c
 * (call sequence / blocking read-write semantics).
 */

#include <string.h>
#include <stdio.h>
#include "driverlib.h"
#include "device.h"
#include "uart_comm.h"

#define UART_BASE   UARTB_BASE

/* DBG_UART_TX / DBG_UART_RX per schematic - see file header */
#define UART_TX_GPIO   70U
#define UART_RX_GPIO   71U

void UART_init(void)
{
    GPIO_setPinConfig(GPIO_70_UARTB_TX);
    GPIO_setPinConfig(GPIO_71_UARTB_RX);
    GPIO_setDirectionMode(UART_TX_GPIO, GPIO_DIR_MODE_OUT);
    GPIO_setDirectionMode(UART_RX_GPIO, GPIO_DIR_MODE_IN);
    GPIO_setPadConfig(UART_TX_GPIO, GPIO_PIN_TYPE_STD);
    GPIO_setPadConfig(UART_RX_GPIO, GPIO_PIN_TYPE_STD);
    GPIO_setQualificationMode(UART_RX_GPIO, GPIO_QUAL_ASYNC);

    UART_setConfig(UART_BASE, UART_CLK_FREQ, UART_BAUD_RATE,
                   (UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE));

    UART_enableFIFO(UART_BASE);
    UART_enableModule(UART_BASE);
}

void UART_sendString(const char *str)
{
    uint16_t i;
    uint16_t len = (uint16_t)strlen(str);

    for (i = 0; i < len; i++)
    {
        UART_writeChar(UART_BASE, (uint8_t)str[i]);
    }
}

void UART_sendReady(void)
{
    UART_sendString("READY\n");
}

void UART_sendPong(void)
{
    UART_sendString("PONG\n");
}

void UART_sendBegin(uint16_t testCount)
{
    char line[UART_LINE_MAXLEN];
    snprintf(line, sizeof(line), "BEGIN,%u\n", testCount);
    UART_sendString(line);
}

void UART_sendTestResult(const char *name, const char *typeStr, uint8_t passed, int32_t value)
{
    char line[UART_LINE_MAXLEN];
    snprintf(line, sizeof(line), "TEST,%s,%s,%s,%ld\n",
             name, typeStr, passed ? "PASS" : "FAIL", (long)value);
    UART_sendString(line);
}

void UART_sendOverallResult(uint8_t allPassed)
{
    char line[UART_LINE_MAXLEN];
    snprintf(line, sizeof(line), "RESULT,OVERALL,%s\n", allPassed ? "PASS" : "FAIL");
    UART_sendString(line);
}

void UART_sendFanAck(uint16_t percent)
{
    char line[UART_LINE_MAXLEN];
    snprintf(line, sizeof(line), "FAN_ACK,%u\n", percent);
    UART_sendString(line);
}

void UART_sendRelayAck(uint16_t state)
{
    char line[UART_LINE_MAXLEN];
    snprintf(line, sizeof(line), "RELAY_ACK,%u\n", state);
    UART_sendString(line);
}

void UART_sendCoilStatus(const char *stateStr, const char *heatModeStr, uint16_t calValue, uint8_t calValid)
{
    char line[UART_LINE_MAXLEN];
    snprintf(line, sizeof(line), "COIL_STATUS,%s,%s,%u,%s\n",
             stateStr, heatModeStr, calValue, calValid ? "VALID" : "INVALID");
    UART_sendString(line);
}

void UART_sendButtonStatus(uint32_t onCount, uint32_t lowCount, uint32_t mediumCount, uint32_t highCount)
{
    char line[UART_LINE_MAXLEN];
    snprintf(line, sizeof(line), "BUTTON_STATUS,%lu,%lu,%lu,%lu\n",
             (unsigned long)onCount, (unsigned long)lowCount, (unsigned long)mediumCount, (unsigned long)highCount);
    UART_sendString(line);
}

uint8_t UART_readLine(char *outBuf, uint16_t maxLen)
{
    uint16_t idx = 0;
    int32_t rxChar;

    while (idx < (maxLen - 1U))
    {
        rxChar = UART_readChar(UART_BASE); /* returns int32_t per uart.h */

        if (rxChar == (int32_t)'\n' || rxChar == (int32_t)'\r')
        {
            if (idx == 0U)
            {
                continue; /* ignore leading CR/LF */
            }
            outBuf[idx] = '\0';
            return 1U;
        }

        outBuf[idx] = (char)rxChar;
        idx++;
    }

    outBuf[maxLen - 1U] = '\0'; /* overflow - caller gets a truncated, null-terminated line */
    return 0U;
}
