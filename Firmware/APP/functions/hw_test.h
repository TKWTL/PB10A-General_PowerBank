#ifndef PB10A_HW_TEST_H
#define PB10A_HW_TEST_H
#include "ui_conf.h"
#include <stdint.h>

/* Temporary foreground FM auto-seek only. No repeated buzzer or
 * timed USB-A toggling. PA6/ADC sampling are permanent APP services. */
#define PB10A_HW_TEST_AUTORUN 1u
#define PB10A_HW_TEST_FM_MS 7000u

void HW_TestInit(void);
void HW_TestPoll(ui_t *ui);
uint8_t HW_TestActive(void);
#endif
