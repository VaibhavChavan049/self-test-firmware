/*
 * self_test.h
 *
 * Orchestrator: walks g_testList (test_config.h), dispatches each entry
 * to the right test module by TestType_e, and reports results over UART
 * per docs/protocol.md. This is the only file that needs to change if a
 * new TestType_e category is ever added.
 */

#ifndef SELF_TEST_H
#define SELF_TEST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Runs every entry in g_testList, streaming TEST,/RESULT,OVERALL, lines over UART as it goes. */
void SelfTest_RunAll(void);

#ifdef __cplusplus
}
#endif

#endif /* SELF_TEST_H */
