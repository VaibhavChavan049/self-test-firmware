/*
 * uart_comm.h
 *
 * Thin wrapper around the SCI/UART peripheral that speaks the line
 * protocol in docs/protocol.md. Everything else in this project only
 * calls these functions - it never touches SCI_* driverlib calls directly.
 * That keeps the peripheral-specific code in exactly one file.
 *
 * TODO: verify SCI base (SCIA/SCIB/...), GPIO pin mux for TX/RX, and the
 * exact driverlib call names against the TI-Rex UART/SCI example you pull
 * for F28P650DK6PZP - C2000 driverlib is consistent across families but
 * pin numbers and the SCI instance are board/device specific.
 */

#ifndef UART_COMM_H
#define UART_COMM_H

#include <stdint.h>
#include "test_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UART_BAUD_RATE   115200U   /* must match gui-app/serial_comm.py DEFAULT_BAUD */
#define UART_LINE_MAXLEN 64U

/* Bring up the SCI peripheral + pin mux for the chosen baud rate. */
void UART_init(void);

/* Send a raw, already-newline-terminated or plain string. */
void UART_sendString(const char *str);

/* Protocol helpers - one call per message type in docs/protocol.md. */
void UART_sendReady(void);
void UART_sendPong(void);
void UART_sendBegin(uint16_t testCount);
void UART_sendTestResult(const char *name, const char *typeStr, uint8_t passed, int32_t value);
void UART_sendOverallResult(uint8_t allPassed);
void UART_sendFanAck(uint16_t percent);
void UART_sendRelayAck(uint16_t state);
void UART_sendCoilStatus(const char *stateStr, const char *heatModeStr, uint16_t calValue, uint8_t calValid);
void UART_sendButtonStatus(uint32_t onCount, uint32_t lowCount, uint32_t mediumCount, uint32_t highCount);

/*
 * Blocking read of one newline-terminated command line (e.g. "START", "PING")
 * into outBuf (outBuf must be at least maxLen bytes). Strips the trailing
 * newline. Returns 1 if a full line was read, 0 on overflow/error.
 */
uint8_t UART_readLine(char *outBuf, uint16_t maxLen);

#ifdef __cplusplus
}
#endif

#endif /* UART_COMM_H */
