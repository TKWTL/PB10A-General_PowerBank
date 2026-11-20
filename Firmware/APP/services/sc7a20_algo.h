#ifndef PB10A_SC7A20_ALGO_H
#define PB10A_SC7A20_ALGO_H
#include "ui_conf.h"
void SC7A20Algo_Init(void);
void SC7A20Algo_Poll(void);
enum {SC7A20_ORIENT_UNKNOWN=0,SC7A20_ORIENT_NORMAL,SC7A20_ORIENT_FLIPPED};
/* Returns 0 for an unknown/sideways attitude; otherwise writes flip flag. */
uint8_t SC7A20Algo_GetRotation(uint8_t *flipped);
/* EXTI5 ISR may use this without I2C to gate a wake notification. */
uint8_t SC7A20Algo_WakeIRQ(void);
void SC7A20Algo_AddWakeSettingsItems(ui_page_t *page);
void SC7A20Algo_AddStatisticsItems(ui_page_t *page);
#endif
